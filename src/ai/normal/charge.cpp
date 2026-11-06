#include "normal/charge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <utility>
#include <variant>

#include "analysis/charge_odds.hpp"
#include "analysis/threat.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/rules/charge.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "core/state/queries.hpp"
#include "normal/fight.hpp"
#include "normal/value.hpp"
#include "simulator.hpp"
#include "state_hash.hpp"

namespace wh40k::ai::normal {

namespace {

constexpr double kBetter = 1e-9;          // a choice must beat the alternative by more than this
constexpr std::size_t kMaxTargetUnits = 3;  // target sets are drawn from the closest few (D-054: a fixed budget)
constexpr int kMaxAdvanceSteps = 64;      // steps to reach the Charge step from earlier in the turn
// A model that can pile in (12.03: up to 3") and then be engaged fights in this Fight phase.
constexpr float kFightReachMm = core::inches_to_mm(core::kPileInDistanceInches) + core::kEngagementRangeMm;

std::string name_of(const core::GameState& state, core::UnitId id) {
    const core::Unit* u = state.find_unit(id);
    return u != nullptr ? u->name : "unit " + std::to_string(id.v);
}

std::string names_of(const core::GameState& state, std::span<const core::UnitId> ids) {
    std::string out;
    for (std::size_t i = 0; i < ids.size(); ++i) out += (i > 0 ? ", " : "") + name_of(state, ids[i]);
    return out;
}

std::string signed_value(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%+.2f", v);
    return buf;
}

std::string percent(double p) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f%%", 100.0 * p);
    return buf;
}

PolicyResult single(core::GameAction a, std::string reason) {
    return PolicyResult{std::move(a), std::move(reason), {}};
}

PolicyResult from_steps(std::vector<PlannedStep> steps, const std::string& reason) {
    PolicyResult r{steps.front().action, reason, {}};
    r.rest.assign(std::make_move_iterator(steps.begin() + 1), std::make_move_iterator(steps.end()));
    return r;
}

bool offered(const std::vector<core::GameAction>& legal, const core::GameAction& a) {
    return std::find(legal.begin(), legal.end(), a) != legal.end();
}

// ---- the value model (see the header) ----

// What is left of each unit, as shares of its starting strength (value.hpp's currency), as volleys land.
class Exchange {
public:
    Exchange(double us, double them) : us0_(us), them0_(them), us_(us), them_(them) {}
    // A volley worth `rate` (shares of the target) at the attacker's strength when the exchange started.
    void we_hit(double rate) {
        if (us0_ > 0.0) them_ -= std::min(them_, rate * us_ / us0_);
    }
    void they_hit(double rate) {
        if (them0_ > 0.0) us_ -= std::min(us_, rate * them_ / them0_);
    }
    [[nodiscard]] double us() const noexcept { return us_; }
    [[nodiscard]] double them() const noexcept { return them_; }
    // Their losses minus ours.
    [[nodiscard]] double value() const noexcept { return (them0_ - them_) - (us0_ - us_); }

private:
    double us0_;
    double them0_;
    double us_;
    double them_;
};

double alive(const core::Unit& u) { return unit_share(u, static_cast<double>(u.models.size())); }

struct Rates {
    double melee = 0.0;   // every model fighting with its best melee weapon
    double cq = 0.0;      // close-quarters shooting while engaged (10.06)
    double ranged = 0.0;  // shooting across the gap
};

Rates rates_of(analysis::OddsCache& cache, const core::Unit& attacker, const core::Unit& target,
               std::optional<float> gap) {
    Rates r;
    r.melee = melee_share(cache, attacker, target);
    r.cq = ranged_share(cache, attacker, target, 0.0f, true);
    if (gap) r.ranged = ranged_share(cache, attacker, target, *gap);
    return r;
}

// The models of `u` that can fight `other` this phase: engaged with it, or within pile-in reach of engaging it.
core::Unit fighters(const core::Unit& u, const core::Unit& other) {
    core::Unit out = u;
    out.models.clear();
    for (const core::Model& m : u.models) {
        const std::optional<float> gap = core::model_distance_to_unit(m, m.position, other);
        if (gap && core::within(*gap, kFightReachMm)) out.models.push_back(m);
    }
    return out;
}

struct ChargedValue {
    double total = 0.0;
    double now = 0.0;  // this Fight phase alone
};

ChargedValue charged_parts(analysis::OddsCache& cache, const core::GameState& after, core::UnitId unit,
                           std::span<const core::UnitId> targets, int turns) {
    ChargedValue out;
    const core::Unit* u = after.find_unit(unit);
    if (u == nullptr || u->models.empty()) return out;
    for (const core::UnitId id : targets) {
        const core::Unit* e = after.find_unit(id);
        if (e == nullptr || e->models.empty()) continue;
        const Rates ours = rates_of(cache, *u, *e, std::nullopt);
        const Rates theirs = rates_of(cache, *e, *u, std::nullopt);
        Exchange x(alive(*u), alive(*e));
        // This Fight phase: Fights First (11.04 After Moving, 24.13), so ours strike first; theirs reply.
        x.we_hit(melee_share(cache, fighters(*u, *e), *e));
        x.they_hit(melee_share(cache, fighters(*e, *u), *u));
        out.now += x.value();
        if (turns >= 1) {  // their turn, engaged: close-quarters shooting, then they fight first
            x.they_hit(theirs.cq);
            x.they_hit(theirs.melee);
            x.we_hit(ours.melee);
        }
        if (turns >= 2) {  // our turn, engaged
            x.we_hit(ours.cq);
            x.we_hit(ours.melee);
            x.they_hit(theirs.melee);
        }
        out.total += x.value();
    }
    return out;
}

// ---- reaching the Charge step for a prospect ----

std::optional<core::GameState> at_charge_step(const core::GameState& state, core::Side side, std::uint64_t seed) {
    Simulator sim(state, seed);
    for (int i = 0; i < kMaxAdvanceSteps; ++i) {
        const core::GameState& s = sim.state();
        if (!s.pending || s.pending->side != side) return std::nullopt;
        const core::DecisionKind& kind = s.pending->kind;
        if (std::holds_alternative<core::decision::Charge>(kind)) return s;
        std::optional<core::GameAction> next;
        if (std::holds_alternative<core::decision::Shoot>(kind)) {
            next = core::action::EndShootStep{};
        } else if (std::holds_alternative<core::decision::MoveUnits>(kind)) {
            for (const core::GameAction& a : core::legal_actions(s, side)) {
                const auto* m = std::get_if<core::action::StartMove>(&a);
                if (m != nullptr && m->move_type == core::MoveType::RemainStationary) {
                    next = a;
                    break;
                }
            }
        } else if (std::holds_alternative<core::decision::MoveModels>(kind) && core::move_end_problems(s).empty()) {
            next = core::action::CommitUnitMove{};
        }
        if (!next || !sim.step(side, *next).ok()) return std::nullopt;
    }
    return std::nullopt;
}

// Our shooting's expected kills taken from the enemy unit closest to `unit`, its models closest to us first.
void remove_front_models(core::GameState& state, core::UnitId unit, double losses) {
    const core::Unit* u = state.find_unit(unit);
    if (u == nullptr || !(losses >= 1.0)) return;
    const core::Unit* closest = closest_enemy(state, *u);
    if (closest == nullptr) return;
    core::Unit& e = *state.find_unit(closest->id);
    std::vector<std::pair<float, core::ModelId>> by_gap;
    for (const core::Model& m : e.models) {
        const core::Unit* us = state.find_unit(unit);
        by_gap.emplace_back(core::model_distance_to_unit(m, m.position, *us).value_or(0.0f), m.id);
    }
    std::stable_sort(by_gap.begin(), by_gap.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second.v < b.second.v;
    });
    const std::size_t k = std::min(by_gap.size(), static_cast<std::size_t>(std::floor(losses)));
    for (std::size_t i = 0; i < k; ++i) {
        const core::ModelId id = by_gap[i].second;
        std::erase_if(e.models, [&](const core::Model& m) { return m.id == id; });
    }
}

}  // namespace

std::optional<ChargePlan> plan_charge_move(const core::GameState& after_roll, core::Side side, core::UnitId unit,
                                           const std::vector<core::UnitId>& targets, std::uint64_t seed,
                                           const std::string& reason, std::vector<std::string>* problems) {
    const auto fail = [&](std::vector<std::string> why) -> std::optional<ChargePlan> {
        if (problems != nullptr) *problems = std::move(why);
        return std::nullopt;
    };
    Simulator sim(after_roll, seed);
    ChargePlan plan;
    plan.targets = targets;
    const core::GameAction start = core::action::StartChargeMove{unit, targets};
    plan.steps.push_back(PlannedStep{state_hash(sim.state()), start, reason});
    if (const core::ValidationResult v = sim.step(side, start); !v.ok()) return fail(v.reasons);
    const client::AutoChargeMove am = client::auto_charge_move(sim.state());
    if (!am.ok()) return fail(am.problems);
    for (const core::GameAction& move : am.moves) {
        plan.steps.push_back(PlannedStep{state_hash(sim.state()), move, reason});
        if (const core::ValidationResult v = sim.step(side, move); !v.ok()) return fail(v.reasons);
    }
    if (std::vector<std::string> p = core::move_end_problems(sim.state()); !p.empty()) return fail(std::move(p));
    plan.steps.push_back(PlannedStep{state_hash(sim.state()), core::action::CommitUnitMove{}, reason});
    std::vector<core::GameEvent> events;
    if (const core::ValidationResult v = sim.step(side, core::action::CommitUnitMove{}, &events); !v.ok()) {
        return fail(v.reasons);
    }
    for (const core::GameEvent& e : events) {
        if (std::holds_alternative<core::event::UnitMoveReverted>(e)) {
            return fail({"D-054: the charge move was reverted"});
        }
    }
    plan.after = sim.state();
    plan.score = am.score;
    return plan;
}

int turns_left(const core::GameState& state) {
    if (state.flow.battle_round < state.battle_rounds) return 2;
    return state.flow.turn_in_round == 0 ? 1 : 0;
}

double hold_value(analysis::OddsCache& cache, const core::GameState& state, core::UnitId unit) {
    const core::Unit* u = state.find_unit(unit);
    if (u == nullptr || u->models.empty()) return 0.0;
    const core::Unit* e = closest_enemy(state, *u);
    const int turns = turns_left(state);
    if (e == nullptr || turns == 0) return 0.0;
    const float gap = core::unit_distance(*u, *e).value_or(0.0f);
    Rates ours = rates_of(cache, *u, *e, gap);
    const Rates theirs = rates_of(cache, *e, *u, gap);
    // An enemy engaged with another unit can neither shoot us (10.06: only units it is engaged with) nor charge
    // (11.02) next turn; and whether we may shoot it is the engine's answer (shot_options, with our shooting this
    // phase forgotten).
    const bool enemy_engaged = core::is_engaged(state, *e);
    if (enemy_engaged) {
        core::GameState probe = state;
        probe.find_unit(unit)->phase_flags.selected_to_shoot = false;
        const std::vector<core::ShotDeclaration> shots =
            core::shot_options(probe, *probe.find_unit(unit), core::ShootingType::Normal);
        const bool targetable = std::any_of(shots.begin(), shots.end(),
                                            [&](const core::ShotDeclaration& s) { return s.target == e->id; });
        if (!targetable) ours.ranged = 0.0;
    }
    Exchange x(alive(*u), alive(*e));
    if (!enemy_engaged) x.they_hit(theirs.ranged);  // their turn: they shoot us
    double p_charged = 0.0;
    if (!enemy_engaged) {
        // Would they charge us? Their own view: they strike first and we reply, then our turn engaged; against our
        // shooting in our turn if they don't.
        Exchange charge(x.them(), x.us());
        charge.we_hit(theirs.melee);
        charge.they_hit(ours.melee);
        if (turns >= 2) {
            charge.they_hit(ours.cq);
            charge.they_hit(ours.melee);
            charge.we_hit(theirs.melee);
        }
        Exchange stay(x.them(), x.us());
        if (turns >= 2) stay.they_hit(ours.ranged);
        if (charge.value() > stay.value() + kBetter) p_charged = analysis::threat_ranges(*e).p_charge_after_move(gap);
    }
    Exchange charged = x;  // they charge us: they strike first, we reply; then our turn engaged
    charged.they_hit(theirs.melee);
    charged.we_hit(ours.melee);
    if (turns >= 2) {
        charged.we_hit(ours.cq);
        charged.we_hit(ours.melee);
        charged.they_hit(theirs.melee);
    }
    Exchange free = x;  // they don't: our turn, we shoot them
    if (turns >= 2) free.we_hit(ours.ranged);
    return p_charged * charged.value() + (1.0 - p_charged) * free.value();
}

double charged_value(analysis::OddsCache& cache, const core::GameState& after_commit, core::UnitId unit,
                     std::span<const core::UnitId> targets, int turns) {
    return charged_parts(cache, after_commit, unit, targets, turns).total;
}

std::optional<ChargeProspect> charge_prospect(analysis::OddsCache& cache, const core::GameState& state,
                                              core::UnitId unit, std::uint64_t seed, double enemy_losses) {
    const core::Unit* u0 = state.find_unit(unit);
    if (u0 == nullptr || u0->models.empty()) return std::nullopt;
    const core::Side side = u0->side;
    std::optional<core::GameState> at = at_charge_step(state, side, seed);
    if (!at) return std::nullopt;
    core::GameState& gs = *at;
    remove_front_models(gs, unit, enemy_losses);
    if (!core::validate_action(gs, side, core::action::DeclareCharge{unit}).ok()) return std::nullopt;
    const core::Unit& u = *gs.find_unit(unit);
    const int turns = turns_left(gs);

    ChargeProspect out;
    out.unit = unit;
    out.hold = hold_value(cache, gs, unit);
    // Target sets: the enemy units a charge could reach at all (within 12", 11.04 Before Moving), closest first.
    std::vector<core::UnitId> enemies =
        core::eligible_charge_targets(gs, u, static_cast<int>(core::kChargeRangeInches));
    std::stable_sort(enemies.begin(), enemies.end(), [&](core::UnitId a, core::UnitId b) {
        const float da = core::unit_distance(u, *gs.find_unit(a)).value_or(0.0f);
        const float db = core::unit_distance(u, *gs.find_unit(b)).value_or(0.0f);
        return da < db;
    });
    if (enemies.size() > kMaxTargetUnits) enemies.resize(kMaxTargetUnits);
    std::sort(enemies.begin(), enemies.end(), [](core::UnitId a, core::UnitId b) { return a.v < b.v; });

    std::array<bool, 13> any{};
    std::array<double, 13> best{};
    std::array<int, 13> engaged{};
    double best_gain = -1.0;
    const auto& pmf = analysis::two_d6_pmf();
    for (std::uint32_t mask = 1; mask < (1u << enemies.size()); ++mask) {
        std::vector<core::UnitId> set;
        for (std::size_t i = 0; i < enemies.size(); ++i) {
            if ((mask >> i) & 1u) set.push_back(enemies[i]);
        }
        std::array<double, 13> value{};
        std::array<int, 13> eng{};
        std::optional<int> full_roll;  // a roll whose move engaged every model: a larger one does no better
        const auto check = [&](const core::GameState& after_roll, core::UnitId id, std::span<const core::UnitId> t) {
            const std::optional<int> roll = after_roll.find_unit(id)->phase_flags.charge_roll;
            if (!roll || *roll < 0 || *roll > 12) return false;
            const auto r = static_cast<std::size_t>(*roll);
            if (full_roll) {
                value[r] = value[static_cast<std::size_t>(*full_roll)];
                eng[r] = eng[static_cast<std::size_t>(*full_roll)];
                return true;
            }
            const std::optional<ChargePlan> plan =
                plan_charge_move(after_roll, side, id, std::vector<core::UnitId>(t.begin(), t.end()), seed, {});
            if (!plan) return false;
            value[r] = charged_value(cache, plan->after, id, t, turns);
            eng[r] = plan->score.engaged;
            if (plan->score.engaged == static_cast<int>(u.models.size())) full_roll = *roll;
            return true;
        };
        const std::optional<analysis::ChargeOdds> odds = analysis::charge_odds(gs, unit, set, check);
        if (!odds) continue;
        double gain = 0.0;
        for (std::size_t r = 0; r <= 12; ++r) {
            if (!odds->success_by_total[r]) continue;
            gain += pmf[r] * std::max(0.0, value[r] - out.hold);
            if (!any[r] || value[r] > best[r] + kBetter) {
                any[r] = true;
                best[r] = value[r];
                engaged[r] = eng[r];
            }
        }
        if (gain > best_gain + kBetter) {
            best_gain = gain;
            out.targets = set;
        }
    }
    double success_weight = 0.0;
    for (std::size_t r = 0; r <= 12; ++r) {
        out.success_by_roll[r] = any[r];
        out.value_by_roll[r] = any[r] ? best[r] : out.hold;
        out.engaged_by_roll[r] = any[r] ? engaged[r] : 0;
        out.expected += pmf[r] * (any[r] ? std::max(best[r], out.hold) : out.hold);
        if (any[r]) {
            out.p_success += pmf[r];
            out.success_mean += pmf[r] * best[r];
            success_weight += pmf[r];
        }
    }
    if (success_weight > 0.0) out.success_mean /= success_weight;
    out.gain = std::max(0.0, out.expected - out.hold);
    return out;
}

std::optional<PolicyResult> decide_charge(analysis::OddsCache& cache, const core::GameState& state, core::Side side,
                                          const std::vector<core::GameAction>& legal, std::uint64_t seed) {
    std::optional<ChargeProspect> best;  // the largest expected gain; ties keep the engine's order
    for (const core::GameAction& a : legal) {
        const auto* d = std::get_if<core::action::DeclareCharge>(&a);
        const core::Unit* u = d != nullptr ? state.find_unit(d->unit) : nullptr;
        if (u == nullptr || u->side != side) continue;
        std::optional<ChargeProspect> p = charge_prospect(cache, state, d->unit, seed);
        if (!p) continue;
        if (!best || p->gain > best->gain + kBetter) best = std::move(p);
    }
    if (best && best->gain > kBetter) {
        return single(core::action::DeclareCharge{best->unit},
                      name_of(state, best->unit) + ": declare a charge (" + percent(best->p_success) +
                          " charge odds against " + names_of(state, best->targets) + "; a charge move " +
                          signed_value(best->success_mean) + " on average, holding " + signed_value(best->hold) + ")");
    }
    const core::GameAction end = core::action::EndChargeStep{};
    if (!offered(legal, end)) return std::nullopt;
    if (!best) return single(end, "End the Charge step: no unit can declare a charge");
    std::string why;
    if (!(best->p_success > 0.0)) {
        why = "no charge roll gives a charge move that stands";
    } else if (const core::Unit* e = closest_enemy(state, *state.find_unit(best->unit)); e != nullptr &&
                                                                                         best->success_mean < 0.0) {
        why = percent(best->p_success) + " charge odds; the " + e->name + " would win the fight: a charge move " +
              signed_value(best->success_mean) + ", holding " + signed_value(best->hold);
    } else {
        why = percent(best->p_success) + " charge odds; holding is worth more: a charge move " +
              signed_value(best->success_mean) + ", holding " + signed_value(best->hold);
    }
    return single(end, name_of(state, best->unit) + ": no charge (" + why + ")");
}

std::optional<PolicyResult> decide_charge_targets(analysis::OddsCache& cache, const core::GameState& state,
                                                  core::Side side, const std::vector<core::GameAction>& legal,
                                                  std::uint64_t seed) {
    const auto& d = std::get<core::decision::SelectChargeTargets>(state.pending->kind);
    const core::Unit* u = state.find_unit(d.unit);
    if (u == nullptr) return std::nullopt;
    const int roll = u->phase_flags.charge_roll.value_or(0);
    const int turns = turns_left(state);
    const double hold = hold_value(cache, state, d.unit);
    std::optional<ChargePlan> best;
    double best_value = 0.0;
    std::string best_reason;
    std::vector<std::string> first_problems;
    for (const core::GameAction& a : legal) {
        const auto* s = std::get_if<core::action::StartChargeMove>(&a);
        if (s == nullptr || s->unit != d.unit) continue;
        std::vector<std::string> problems;
        std::optional<ChargePlan> plan = plan_charge_move(state, side, d.unit, s->targets, seed, {}, &problems);
        if (!plan) {
            if (first_problems.empty()) first_problems = std::move(problems);
            continue;
        }
        const double v = charged_value(cache, plan->after, d.unit, s->targets, turns);
        if (!best || v > best_value + kBetter) {
            best_value = v;
            best_reason = u->name + ": charge move into " + names_of(state, s->targets) + " (roll " +
                          std::to_string(roll) + "): " + std::to_string(plan->score.engaged) + " of " +
                          std::to_string(u->models.size()) + " models engaged, " +
                          std::to_string(plan->score.contacts) + " in base contact (" + signed_value(v) +
                          " against " + signed_value(hold) + " for holding)";
            best = std::move(plan);
        }
    }
    if (best && best_value > hold + kBetter) {
        for (PlannedStep& step : best->steps) step.reason = best_reason;
        return from_steps(std::move(best->steps), best_reason);
    }
    const core::GameAction skip = core::action::SkipChargeMove{d.unit};
    if (!offered(legal, skip)) return std::nullopt;
    if (!best) {
        return single(skip, u->name + ": no charge move stands for a roll of " + std::to_string(roll) +
                                (first_problems.empty() ? std::string() : " (" + first_problems.front() + ")"));
    }
    return single(skip, u->name + ": no charge move, it is worth less than holding (" + signed_value(best_value) +
                            " against " + signed_value(hold) + ")");
}

std::optional<PolicyResult> continue_charge_move(const core::GameState& state, core::Side side, std::uint64_t seed) {
    if (!state.flow.move || state.flow.move->type != core::MoveType::Charge) return std::nullopt;
    const std::string reason = name_of(state, state.flow.move->unit) + ": charge move (auto move, D-056)";
    const client::AutoChargeMove am = client::auto_charge_move(move_origin(state));
    const std::vector<core::ModelPath>& done = state.flow.move->paths;
    bool matches = am.ok() && done.size() <= am.moves.size();
    for (std::size_t i = 0; matches && i < done.size(); ++i) {
        const auto* p = std::get_if<core::action::ProposeModelMove>(&am.moves[i]);
        matches = p != nullptr && p->model == done[i].model && done[i].points.size() == p->path.size() + 1 &&
                  std::equal(p->path.begin(), p->path.end(), done[i].points.begin() + 1);
    }
    if (matches) {
        Simulator sim(state, seed);
        std::vector<PlannedStep> steps;
        bool ok = true;
        for (std::size_t i = done.size(); ok && i < am.moves.size(); ++i) {
            steps.push_back(PlannedStep{state_hash(sim.state()), am.moves[i], reason});
            ok = sim.step(side, am.moves[i]).ok();
        }
        if (ok && core::move_end_problems(sim.state()).empty()) {
            steps.push_back(PlannedStep{state_hash(sim.state()), core::action::CommitUnitMove{}, reason});
            std::vector<core::GameEvent> events;
            ok = sim.step(side, core::action::CommitUnitMove{}, &events).ok();
            for (const core::GameEvent& e : events) {
                if (std::holds_alternative<core::event::UnitMoveReverted>(e)) ok = false;
            }
            if (ok) return from_steps(std::move(steps), reason);
        }
    }
    if (core::move_end_problems(state).empty() && !done.empty()) {
        return single(core::action::CommitUnitMove{}, reason + ": commit the move so far");
    }
    return single(core::action::ResetUnitMove{}, reason + ": the move would not stand, reset it (D-018)");
}

}  // namespace wh40k::ai::normal
