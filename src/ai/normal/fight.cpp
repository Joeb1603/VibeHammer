#include "unit_style.hpp"
#include "normal/fight.hpp"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <utility>
#include <variant>

#include "auto_fight_move.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/fight.hpp"
#include "core/state/queries.hpp"
#include "fight_move_skip.hpp"
#include "normal/shooting.hpp"
#include "normal/value.hpp"
#include "simulator.hpp"
#include "state_hash.hpp"

namespace wh40k::ai::normal {

namespace {

constexpr double kBetter = 1e-9;     // a choice must beat the current one by more than this (ties keep it)
constexpr double kFightMoveTie = 1e-6;  // D-057: fight-move gains this close are equal (the tidier move wins)
constexpr std::size_t kMaxSplits = 16;  // split-attack candidates per model (D-054: a fixed budget)
constexpr int kDescentPasses = 2;

// D-057 (M46): a pile-in or consolidation candidate, or not moving (no plan, no gain, no change).
struct FightMoveCand {
    std::optional<FightMovePlan> plan;
    double gain = 0.0;
    client::FightMoveShape change;  // the moving unit's shape after the move minus before it
    std::string reason;
};

// D-057: more gain wins; gains within kFightMoveTie are a tie, which goes to the tidier move. Not moving has a
// zero change, so a tied move wins over it only if it is tidier than standing still.
bool beats(const FightMoveCand& a, const FightMoveCand& b) {
    if (a.gain > b.gain + kFightMoveTie) return true;
    if (a.gain < b.gain - kFightMoveTie) return false;
    return client::tidier(a.change, b.change);
}

client::FightMoveShape move_change(const core::GameState& before, const core::GameState& after, core::UnitId unit,
                                   const std::vector<core::UnitId>& targets) {
    return client::shape_change(client::fight_move_shape(before, unit, targets),
                                client::fight_move_shape(after, unit, targets));
}

std::string name_of(const core::GameState& state, core::UnitId id) {
    return client::unit_display_name(state, id);  // D-065
}

std::string names_of(const core::GameState& state, const std::vector<core::UnitId>& ids) {
    std::string out;
    for (std::size_t i = 0; i < ids.size(); ++i) out += (i > 0 ? ", " : "") + name_of(state, ids[i]);
    return out;
}

std::string signed_value(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%+.3f", v);
    return buf;
}

PolicyResult single(core::GameAction a, std::string reason) { return PolicyResult{std::move(a), std::move(reason), {}}; }

PolicyResult from_steps(std::vector<PlannedStep> steps, const std::string& reason) {
    PolicyResult r{steps.front().action, reason, {}};
    r.rest.assign(std::make_move_iterator(steps.begin() + 1), std::make_move_iterator(steps.end()));
    return r;
}

// "<head> - 3 more in base contact, 1.2\" closer in total (+0.000)": what the move does to the unit's shape, then
// the gain (D-057). The combat log's short form keeps the head and the first clause after " - " (M45).
std::string fight_move_reason(const std::string& head, const FightMoveCand& c) {
    return head + " - " + client::describe_shape_change(c.change) + " (" + signed_value(c.gain) + ")";
}

std::optional<double> melee_value(analysis::OddsCache& cache, const core::GameState& state, core::UnitId unit,
                                  const std::vector<core::MeleeDeclaration>& decls,
                                  std::vector<analysis::TargetOutcome>* outcomes = nullptr) {
    if (decls.empty()) return 0.0;
    const std::vector<core::AttackBatch> batches = analysis::melee_batches(state, unit, decls);
    if (batches.empty()) return std::nullopt;
    std::vector<analysis::TargetOutcome> outs = evaluate_in_best_order(cache, state, batches);
    const double v = share_value(state, outs);
    if (outcomes != nullptr) *outcomes = std::move(outs);
    return v;
}

// A model's candidate declarations: each single-target option, and (04.02 Splitting Melee Attacks) its attacks
// split between two of the units it is engaged with.
std::vector<core::MeleeDeclaration> melee_candidates(const core::Model& model,
                                                     const std::vector<core::MeleeDeclaration>& options) {
    std::vector<core::MeleeDeclaration> out;
    for (const core::MeleeDeclaration& o : options) {
        if (o.model == model.id) out.push_back(o);
    }
    const std::size_t singles = out.size();
    std::size_t splits = 0;
    for (std::size_t i = 0; i < singles && splits < kMaxSplits; ++i) {
        for (std::size_t j = i + 1; j < singles && splits < kMaxSplits; ++j) {
            // Copies: the push_back below may reallocate `out` (M60: references here were left dangling).
            const core::MeleeDeclaration a = out[i];
            const core::MeleeDeclaration b = out[j];
            if (a.weapon_index != b.weapon_index || a.profile_index != b.profile_index) continue;
            if (a.targets.size() != 1 || b.targets.size() != 1 || a.targets[0].unit == b.targets[0].unit) continue;
            const int attacks = a.targets[0].attacks;
            for (int k = 1; k < attacks && splits < kMaxSplits; ++k) {
                core::MeleeDeclaration s = a;
                s.targets = {core::MeleeTarget{a.targets[0].unit, k}, core::MeleeTarget{b.targets[0].unit, attacks - k}};
                out.push_back(std::move(s));
                ++splits;
            }
        }
    }
    return out;
}

// D-071 (24.11): a model's candidate declaration sets. Without [EXTRA ATTACKS] weapons, one set per candidate
// above. With them, each candidate of its main weapon (one of its other melee weapons, or one of its [EXTRA
// ATTACKS] weapons if it is offered no other) with its other [EXTRA ATTACKS] weapons, each at the main
// candidate's first target if it reaches it, else at its first target.
std::vector<std::vector<core::MeleeDeclaration>> melee_candidate_sets(const core::Model& model,
                                                                      const std::vector<core::MeleeDeclaration>& options) {
    const auto extra = [&](const core::MeleeDeclaration& o) {
        return core::is_extra_attacks_weapon(model.weapons[o.weapon_index]);
    };
    std::vector<core::MeleeDeclaration> mine;
    for (const core::MeleeDeclaration& o : options) {
        if (o.model == model.id) mine.push_back(o);
    }
    const bool any_extra = std::any_of(mine.begin(), mine.end(), extra);
    std::vector<std::vector<core::MeleeDeclaration>> out;
    if (!any_extra) {
        for (core::MeleeDeclaration& d : melee_candidates(model, options)) out.push_back({std::move(d)});
        return out;
    }
    const bool has_other = std::any_of(mine.begin(), mine.end(), [&](const auto& o) { return !extra(o); });
    std::vector<core::MeleeDeclaration> main_options;
    for (const core::MeleeDeclaration& o : mine) {
        if (!has_other || !extra(o)) main_options.push_back(o);
    }
    for (const core::MeleeDeclaration& main : melee_candidates(model, main_options)) {
        std::vector<core::MeleeDeclaration> set{main};
        for (const core::MeleeDeclaration& o : mine) {
            if (!extra(o) || o.weapon_index == main.weapon_index) continue;
            if (std::any_of(set.begin(), set.end(), [&](const auto& d) { return d.weapon_index == o.weapon_index; })) {
                continue;
            }
            const core::MeleeDeclaration* pick = &o;  // the weapon's first option
            for (const core::MeleeDeclaration& x : mine) {
                if (x.weapon_index == o.weapon_index && x.targets.front().unit == main.targets.front().unit) {
                    pick = &x;
                    break;
                }
            }
            set.push_back(*pick);
        }
        out.push_back(std::move(set));
    }
    return out;
}

std::vector<core::MeleeDeclaration> flatten(const std::vector<std::vector<core::MeleeDeclaration>>& sets) {
    std::vector<core::MeleeDeclaration> out;
    for (const auto& set : sets) out.insert(out.end(), set.begin(), set.end());
    return out;
}

struct SideMelee {
    double ours = 0.0;
    double theirs = 0.0;
};

SideMelee side_melee(analysis::OddsCache& cache, const core::GameState& state, core::Side side) {
    SideMelee out;
    for (const core::Unit& u : state.units) {
        if (u.models.empty() || !will_fight(state, u)) continue;
        if (u.side == side) {
            if (auto p = plan_melee(cache, state, u.id)) out.ours += p->value;
            continue;
        }
        bool engages_us = false;
        for (const core::UnitId e : core::engaged_enemy_units(state, u)) {
            const core::Unit* t = state.find_unit(e);
            if (t != nullptr && t->side == side) engages_us = true;
        }
        if (!engages_us) continue;
        if (auto p = plan_melee(cache, state, u.id)) out.theirs += p->value;
    }
    return out;
}

double own_fight_value(analysis::OddsCache& cache, const core::GameState& state, core::UnitId unit) {
    const auto p = plan_melee(cache, state, unit);
    return p ? p->value : 0.0;
}

// From a pile-in or consolidation in progress: the auto_fight_move arrangement planned from the move's start,
// the placements not yet made and the commit, each simulated and checked.
std::optional<FightMovePlan> plan_rest(const core::GameState& state, core::Side side, std::uint64_t seed,
                                       const std::string& reason) {
    if (!state.flow.move || !state.pending ||
        !std::holds_alternative<core::decision::MoveModels>(state.pending->kind)) {
        return std::nullopt;
    }
    const client::AutoFightMove am = client::auto_fight_move(move_origin(state));
    if (!am.ok()) return std::nullopt;
    const std::vector<core::ModelPath>& done = state.flow.move->paths;
    if (done.size() > am.moves.size()) return std::nullopt;
    for (std::size_t i = 0; i < done.size(); ++i) {
        const auto* p = std::get_if<core::action::ProposeModelMove>(&am.moves[i]);
        if (p == nullptr || p->model != done[i].model || done[i].points.size() != p->path.size() + 1 ||
            !std::equal(p->path.begin(), p->path.end(), done[i].points.begin() + 1)) {
            return std::nullopt;  // the move so far isn't this plan's
        }
    }
    Simulator sim(state, seed);
    FightMovePlan plan;
    for (std::size_t i = done.size(); i < am.moves.size(); ++i) {
        plan.steps.push_back(PlannedStep{state_hash(sim.state()), am.moves[i], reason});
        if (!sim.step(side, am.moves[i]).ok()) return std::nullopt;
    }
    if (!core::move_end_problems(sim.state()).empty()) return std::nullopt;  // D-054: no silent reverts
    plan.steps.push_back(PlannedStep{state_hash(sim.state()), core::action::CommitUnitMove{}, reason});
    std::vector<core::GameEvent> events;
    if (!sim.step(side, core::action::CommitUnitMove{}, &events).ok()) return std::nullopt;
    for (const core::GameEvent& e : events) {
        if (std::holds_alternative<core::event::UnitMoveReverted>(e)) return std::nullopt;
    }
    plan.after = sim.state();
    return plan;
}

}  // namespace

core::GameState move_origin(const core::GameState& state) {
    core::GameState origin = state;
    core::MoveInProgress& mv = *origin.flow.move;
    if (core::Unit* u = origin.find_unit(mv.unit)) {
        for (core::Model& m : u->models) {
            for (const core::ModelStart& s : mv.starts) {
                if (s.model == m.id) m.position = s.position;
            }
        }
    }
    mv.paths.clear();
    return origin;
}

std::optional<MeleeChoice> plan_melee(analysis::OddsCache& cache, const core::GameState& state, core::UnitId unit) {
    const core::Unit* u = state.find_unit(unit);
    if (u == nullptr || u->models.empty()) return std::nullopt;
    const std::vector<core::MeleeDeclaration> options = core::melee_options(state, *u);
    if (options.empty()) return std::nullopt;
    // Per model, its candidate declaration sets (D-071: one declaration each, or a model's [EXTRA ATTACKS]
    // weapons with its main one, 24.11) and the set it takes.
    std::vector<std::vector<std::vector<core::MeleeDeclaration>>> cands;
    std::vector<std::vector<core::MeleeDeclaration>> current;
    for (const core::Model& m : u->models) {
        std::vector<std::vector<core::MeleeDeclaration>> mine = melee_candidate_sets(m, options);
        if (mine.empty()) continue;  // D-021: a model with no possible target declares nothing
        std::size_t best = 0;
        double best_value = -1.0;
        for (std::size_t i = 0; i < mine.size(); ++i) {
            const std::optional<double> v = melee_value(cache, state, unit, mine[i]);
            if (v && *v > best_value + kBetter) {
                best_value = *v;
                best = i;
            }
        }
        current.push_back(mine[best]);
        cands.push_back(std::move(mine));
    }
    std::optional<double> value = melee_value(cache, state, unit, flatten(current));
    if (!value) return std::nullopt;
    for (int pass = 0; pass < kDescentPasses; ++pass) {
        bool improved = false;
        for (std::size_t i = 0; i < current.size(); ++i) {
            if (cands[i].size() < 2) continue;
            for (const std::vector<core::MeleeDeclaration>& d : cands[i]) {
                if (d == current[i]) continue;
                std::vector<std::vector<core::MeleeDeclaration>> trial = current;
                trial[i] = d;
                const std::optional<double> v = melee_value(cache, state, unit, flatten(trial));
                if (v && *v > *value + kBetter) {
                    value = v;
                    current = std::move(trial);
                    improved = true;
                }
            }
        }
        if (!improved) break;
    }
    MeleeChoice out;
    out.unit = unit;
    out.declarations = flatten(current);
    out.value = melee_value(cache, state, unit, out.declarations, &out.outcomes).value_or(0.0);
    return out;
}

bool will_fight(const core::GameState& state, const core::Unit& unit) {
    if (state.flow.fight && state.flow.fight->unit == unit.id) return !state.flow.fight->attacks_started;
    if (unit.phase_flags.selected_to_fight) return false;
    return core::eligible_to_fight(state, unit) || core::is_engaged(state, unit);
}

double melee_exchange(analysis::OddsCache& cache, const core::GameState& state, core::Side side) {
    const SideMelee m = side_melee(cache, state, side);
    return m.ours - m.theirs;
}

double consolidation_score(analysis::OddsCache& cache, const core::GameState& state, core::Side side,
                           Generation gen) {
    double score = 0.0;
    // 12.08 Engaging Consolidation: each enemy unit engaged that hasn't fought must now fight us.
    for (const core::UnitId id : state.flow.forced_fights) {
        const core::Unit* e = state.find_unit(id);
        if (e == nullptr || e->side == side || e->models.empty()) continue;
        score -= own_fight_value(cache, state, id);
    }
    for (const core::Unit& u : state.units) {
        if (u.side != side || u.models.empty()) continue;
        const std::vector<core::UnitId> engaged = core::engaged_enemy_units(state, u);
        if (!engaged.empty()) {
            // The next fight, and close-quarters shooting (10.06) while engaged.
            score += own_fight_value(cache, state, u.id);
            for (const core::UnitId id : engaged) {
                const core::Unit* e = state.find_unit(id);
                if (e == nullptr || e->models.empty()) continue;
                score += ranged_share(cache, u, *e, 0.0f, true);
                score -= own_fight_value(cache, state, id);
                score -= ranged_share(cache, *e, u, 0.0f, true);
            }
            continue;
        }
        if (gen == Generation::M55 || enemy_units(state, side).size() <= 1) {
            if (const core::Unit* e = closest_enemy(state, u)) {
                const float gap = core::unit_distance(u, *e).value_or(0.0f);
                score += ranged_share(cache, u, *e, gap) - ranged_share(cache, *e, u, gap);
            }
            continue;
        }
        // D-066 (M56): our best shot at any enemy unit that may be shot (unengaged, 04.02) minus the shooting of
        // every enemy unit that is free to shoot us (an engaged one can't, 10.04). One enemy unit: as above.
        double ours = 0.0;
        double theirs = 0.0;
        for (const core::Unit* e : enemy_units(state, side)) {
            if (core::is_engaged(state, *e)) continue;
            const float gap = core::unit_distance(u, *e).value_or(0.0f);
            ours = std::max(ours, ranged_share(cache, u, *e, gap));
            theirs += ranged_share(cache, *e, u, gap);
        }
        score += ours - theirs;
    }
    return score;
}

std::optional<FightMovePlan> simulate_fight_move(const core::GameState& state, core::Side side,
                                                 const core::GameAction& start, std::uint64_t seed,
                                                 const std::string& reason) {
    Simulator sim(state, seed);
    const PlannedStep first{state_hash(sim.state()), start, reason};
    if (!sim.step(side, start).ok()) return std::nullopt;
    std::optional<FightMovePlan> rest = plan_rest(sim.state(), side, seed, reason);
    if (!rest) return std::nullopt;
    rest->steps.insert(rest->steps.begin(), first);
    return rest;
}

std::optional<PolicyResult> decide_fight_selection(analysis::OddsCache& cache, const core::GameState& state,
                                                   core::Side side, const std::vector<core::GameAction>& legal,
                                                   std::uint64_t seed) {
    const double base_theirs = side_melee(cache, state, side).theirs;
    std::optional<core::GameAction> best;
    double best_score = 0.0;
    double best_own = 0.0;
    std::string best_reason;
    for (const core::GameAction& a : legal) {
        const auto* sel = std::get_if<core::action::SelectToFight>(&a);
        if (sel == nullptr) continue;
        Simulator sim(state, seed);
        if (!sim.step(side, a).ok()) continue;
        const core::GameState& s1 = sim.state();
        double own = own_fight_value(cache, s1, sel->unit);
        double theirs = side_melee(cache, s1, side).theirs;
        std::string how = sel->type == core::FightType::Overrun ? "Overrun fight" : "Normal fight";
        if (s1.pending && std::holds_alternative<core::decision::OverrunPileIn>(s1.pending->kind)) {
            // 12.06: the additional pile-in, planned as decide_overrun_pile_in will plan it.
            double best_here = own - (theirs - base_theirs);
            for (const core::GameAction& p : core::legal_actions(s1, side)) {
                if (!std::holds_alternative<core::action::StartPileIn>(p)) continue;
                const auto plan = simulate_fight_move(s1, side, p, seed, {});
                if (!plan) continue;
                const double o = own_fight_value(cache, plan->after, sel->unit);
                const double t = side_melee(cache, plan->after, side).theirs;
                if (o - (t - base_theirs) > best_here + kBetter) {
                    best_here = o - (t - base_theirs);
                    own = o;
                    theirs = t;
                    how = "Overrun fight with a pile-in";
                }
            }
        }
        const double score = own - (theirs - base_theirs);
        if (!best || score > best_score + kBetter) {
            best = a;
            best_score = score;
            best_own = own;
            best_reason = name_of(state, sel->unit) + ": " + how + " (" + signed_value(score) + ")";
        }
    }
    const bool may_pass = std::any_of(legal.begin(), legal.end(), [](const core::GameAction& a) {
        return std::holds_alternative<core::action::PassFight>(a);
    });
    if (may_pass && (!best || !(best_own > kBetter))) {
        return single(core::action::PassFight{}, "Pass: no unit can make an attack yet (Appendix)");
    }
    if (!best) return std::nullopt;
    return single(*best, best_reason);
}

std::optional<PolicyResult> decide_melee(analysis::OddsCache& cache, const core::GameState& state, core::UnitId unit) {
    const std::optional<MeleeChoice> plan = plan_melee(cache, state, unit);
    if (!plan) return std::nullopt;
    double models = 0.0;
    for (const analysis::TargetOutcome& o : plan->outcomes) models += o.outcome.model_equivalents;
    char buf[64];
    std::snprintf(buf, sizeof buf, " (expected %.2f models)", models);
    std::vector<core::UnitId> targets;
    for (const core::MeleeDeclaration& d : plan->declarations) {
        for (const core::MeleeTarget& t : d.targets) {
            if (std::find(targets.begin(), targets.end(), t.unit) == targets.end()) targets.push_back(t.unit);
        }
    }
    return single(core::action::Fight{unit, plan->declarations},
                  name_of(state, unit) + ": melee attacks against " + names_of(state, targets) + buf);
}

std::optional<PolicyResult> decide_pile_in(analysis::OddsCache& cache, const core::GameState& state, core::Side side,
                                           const std::vector<core::GameAction>& legal, std::uint64_t seed) {
    const double base = melee_exchange(cache, state, side);
    FightMoveCand best;  // not moving
    for (const core::GameAction& a : legal) {
        const auto* p = std::get_if<core::action::StartPileIn>(&a);
        if (p == nullptr || !client::fight_move_worthwhile(state, side, a)) continue;
        const std::string head = name_of(state, p->unit) + ": pile in towards " + names_of(state, p->targets);
        FightMoveCand c;
        c.plan = simulate_fight_move(state, side, a, seed, head);
        if (!c.plan) continue;
        c.gain = melee_exchange(cache, c.plan->after, side) - base;
        c.change = move_change(state, c.plan->after, p->unit, p->targets);
        c.reason = fight_move_reason(head, c);
        if (beats(c, best)) best = std::move(c);
    }
    if (best.plan) return from_steps(std::move(best.plan->steps), best.reason);
    return single(core::action::EndPileIns{},
                  "End pile-ins: no pile-in improves the melee exchange or closes in without a cost");
}

std::optional<PolicyResult> decide_overrun_pile_in(analysis::OddsCache& cache, const core::GameState& state,
                                                   core::Side side, const std::vector<core::GameAction>& legal,
                                                   std::uint64_t seed) {
    const auto& d = std::get<core::decision::OverrunPileIn>(state.pending->kind);
    const double base_theirs = side_melee(cache, state, side).theirs;
    const double skip_score = own_fight_value(cache, state, d.unit);
    FightMoveCand best;  // skipping
    for (const core::GameAction& a : legal) {
        const auto* p = std::get_if<core::action::StartPileIn>(&a);
        if (p == nullptr) continue;
        const std::string head = name_of(state, d.unit) + ": Overrun pile-in towards " + names_of(state, p->targets);
        FightMoveCand c;
        c.plan = simulate_fight_move(state, side, a, seed, head);
        if (!c.plan) continue;
        const double score = own_fight_value(cache, c.plan->after, d.unit) -
                             (side_melee(cache, c.plan->after, side).theirs - base_theirs);
        c.gain = score - skip_score;
        c.change = move_change(state, c.plan->after, d.unit, p->targets);
        c.reason = fight_move_reason(head, c);
        if (beats(c, best)) best = std::move(c);
    }
    if (best.plan) return from_steps(std::move(best.plan->steps), best.reason);
    return single(core::action::SkipPileIn{d.unit},
                  name_of(state, d.unit) + ": skip the Overrun pile-in (12.06): it would not gain anything");
}

std::optional<PolicyResult> decide_consolidation(analysis::OddsCache& cache, const core::GameState& state,
                                                 core::Side side, const std::vector<core::GameAction>& legal,
                                                 std::uint64_t seed, Generation gen) {
    const double base = consolidation_score(cache, state, side, gen);
    FightMoveCand best;  // not moving
    for (const core::GameAction& a : legal) {
        const auto* s = std::get_if<core::action::StartConsolidation>(&a);
        if (s == nullptr || !client::fight_move_worthwhile(state, side, a)) continue;
        const std::string head = name_of(state, s->unit) + ": consolidate towards " + names_of(state, s->targets);
        FightMoveCand c;
        c.plan = simulate_fight_move(state, side, a, seed, head);
        if (!c.plan) continue;
        c.gain = consolidation_score(cache, c.plan->after, side, gen) - base;
        c.change = move_change(state, c.plan->after, s->unit, s->targets);
        c.reason = fight_move_reason(head, c);
        if (beats(c, best)) best = std::move(c);
    }
    if (best.plan) return from_steps(std::move(best.plan->steps), best.reason);
    return single(core::action::EndConsolidations{},
                  "End consolidations: no consolidation improves the position or closes in without a cost");
}

std::optional<PolicyResult> continue_fight_move(const core::GameState& state, core::Side side, std::uint64_t seed) {
    if (!state.flow.move) return std::nullopt;
    const std::string reason = name_of(state, state.flow.move->unit) + ": " +
                               (state.flow.move->type == core::MoveType::PileIn ? "pile-in" : "consolidation") +
                               " (auto move, D-050)";
    if (std::optional<FightMovePlan> plan = plan_rest(state, side, seed, reason)) {
        return from_steps(std::move(plan->steps), reason);
    }
    if (core::move_end_problems(state).empty() && !state.flow.move->paths.empty()) {
        return single(core::action::CommitUnitMove{}, reason + ": commit the move so far");
    }
    return single(core::action::ResetUnitMove{}, reason + ": the move would not stand, reset it");
}

}  // namespace wh40k::ai::normal
