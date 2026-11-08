#include "unit_style.hpp"
#include "hard/hard_bot.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <numeric>
#include <set>
#include <string>
#include <utility>
#include <variant>

#include "analysis/charge_odds.hpp"
#include "auto_fight_move.hpp"
#include "casualty_order.hpp"
#include "fight_move_skip.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/casualties.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
#include "core/rules/charge.hpp"
#include "core/rules/mortal_wounds.hpp"
#include "core/state/queries.hpp"
#include "normal/charge.hpp"
#include "normal/defense.hpp"
#include "normal/fight.hpp"
#include "normal/normal_bot.hpp"
#include "normal/value.hpp"
#include "simulator.hpp"
#include "state_hash.hpp"

namespace wh40k::ai::hard {

namespace {

using core::GameAction;
using core::GameState;

constexpr double kBetter = 1e-9;
constexpr std::size_t kStandingAttempts = 12;  // simulations per move plan (as M43's kMaxSimulatedMoves)

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

std::string inches(float mm) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f\"", static_cast<double>(mm / core::MM_PER_INCH));
    return buf;
}

std::string odds_text(const Evaluation& e) {
    return "win " + percent(e.p_win) + ", draw " + percent(e.p_draw) + ", loss " + percent(e.p_loss);
}

PolicyResult single(GameAction a, std::string reason) { return PolicyResult{std::move(a), std::move(reason), {}}; }

PolicyResult from_steps(std::vector<PlannedStep> steps, std::size_t first, const std::string& reason) {
    PolicyResult r{steps[first].action, reason, {}};
    for (std::size_t i = first + 1; i < steps.size(); ++i) {
        steps[i].reason = reason;
        r.rest.push_back(std::move(steps[i]));
    }
    return r;
}

bool offered(const std::vector<GameAction>& legal, const GameAction& a) {
    return std::find(legal.begin(), legal.end(), a) != legal.end();
}

std::string name_of(const GameState& gs, core::UnitId id) {
    return client::unit_display_name(gs, id);  // D-065
}

double blend(const LookaheadWeights& w, const Evaluation& chain, const std::optional<MonteCarloResult>& mc) {
    if (!mc || mc->rollouts == 0) return chain.score;
    return (1.0 - w.share) * chain.score + w.share * mc->mean;
}

bool use_lookahead(const LookaheadWeights& w) { return w.share > 0.0 && w.rollouts > 0 && w.plans > 0; }

std::string lookahead_text(const std::optional<MonteCarloResult>& mc) {
    if (!mc || mc->rollouts == 0) return "";
    return "; lookahead " + signed_value(mc->mean) + " over " + std::to_string(mc->rollouts) + " rollouts";
}

// "13.2\" from Termagants" after a move.
std::string gap_text(const GameState& after, core::UnitId unit) {
    const core::Unit* u = after.find_unit(unit);
    const core::Unit* e = u != nullptr ? normal::closest_enemy(after, *u) : nullptr;
    if (u == nullptr || e == nullptr) return "no enemy left";
    return inches(core::unit_distance(*u, *e).value_or(0.0f)) + " from " + client::unit_display_name(after, e->id);
}

std::string move_type_text(core::MoveType t, std::optional<core::FallBackMode> mode) {
    switch (t) {
        case core::MoveType::RemainStationary: return "Remain Stationary";
        case core::MoveType::Normal: return "Normal move";
        case core::MoveType::Advance: return "Advance";
        case core::MoveType::FallBack:
            return mode == core::FallBackMode::DesperateEscape ? "Fall Back (Desperate Escape)"
                                                               : "Fall Back (Ordered Retreat)";
        default: return "move";
    }
}

// Removes `models` from the copy (wounds lost first), as a hypothetical position.
void apply_results(GameState& gs, core::UnitId unit, const std::vector<core::DamageResult>& results) {
    core::Unit* u = gs.find_unit(unit);
    if (u == nullptr) return;
    for (const core::DamageResult& r : results) {
        if (core::Model* m = core::find_model(*u, r.model)) m->wounds_remaining -= r.wounds_lost;
    }
    std::erase_if(u->models, [](const core::Model& m) { return m.wounds_remaining <= 0; });
}

// D-074 part 3 (M66): each model at its facing (round bases: check_coherency's round result).
int coherency_problem_count(const std::vector<core::PosedModel>& models) {
    const core::CoherencyResult c = core::check_coherency(std::span<const core::PosedModel>(models));
    return static_cast<int>(c.isolated.size() + c.too_far.size());
}

// 03.03 Regaining Coherency, estimated on a copy: models removed one at a time (each the one whose removal leaves
// the fewest coherency problems, ties to the first) until the unit is in coherency.
void restore_coherency(GameState& gs, core::UnitId unit) {
    core::Unit* u = gs.find_unit(unit);
    if (u == nullptr) return;
    while (u->models.size() > 1) {
        const std::vector<core::PosedModel> placed = core::posed_models(*u);
        if (coherency_problem_count(placed) == 0) return;
        std::size_t best = 0;
        int best_problems = -1;
        for (std::size_t i = 0; i < placed.size(); ++i) {
            std::vector<core::PosedModel> rest = placed;
            rest.erase(rest.begin() + static_cast<std::ptrdiff_t>(i));
            const int p = coherency_problem_count(rest);
            if (best_problems < 0 || p < best_problems) {
                best = i;
                best_problems = p;
            }
        }
        u->models.erase(u->models.begin() + static_cast<std::ptrdiff_t>(best));
    }
}

// ---- move plans ----

struct Option {
    GameAction start;
    std::vector<PlannedStep> steps;  // the whole plan when no dice are rolled (StartMove first)
    Evaluation chain;
    std::optional<MonteCarloResult> mc;
    double value = 0.0;
    std::string text;
    std::string where;  // the position after it
};

double hazard_wounds_per_model(const core::Unit& unit) {
    double total = 0.0;
    for (int face = 1; face <= 6; ++face) {
        core::ScriptedDice dice{face};
        total += analysis::d6_pmf()[static_cast<std::size_t>(face)] *
                 core::make_hazard_rolls(dice, unit, 1).mortal_wounds;
    }
    return total;
}

std::optional<PolicyResult> decide_casualties(HardContext& hc, analysis::OddsCache& cache, const GameState& gs,
                                              core::Side side, std::vector<HardCasualtyCandidate>* out = nullptr);

// D-054 (no reverted moves): a Desperate Escape is only started when every likely outcome of its hazard rolls
// (06.03: one to three failures, our casualties chosen as Hard chooses them) still leaves a fall-back move that
// stands; otherwise the losses could break the unit's coherency (03.03) with no move that restores it, and the
// move would have to be reset. Failing and passing faces come from make_hazard_rolls itself.
bool escape_stands(HardContext& hc, analysis::OddsCache& cache, const GameState& gs, core::Side side,
                   const GameAction& escape, const core::Unit& unit) {
    std::optional<int> fail;
    std::optional<int> pass;
    for (int face = 1; face <= 6; ++face) {
        core::ScriptedDice dice{face};
        if (core::make_hazard_rolls(dice, unit, 1).mortal_wounds > 0) {
            if (!fail) fail = face;
        } else if (!pass) {
            pass = face;
        }
    }
    if (!pass) return false;
    if (!fail) return true;  // a hazard roll never fails
    const int n = static_cast<int>(unit.models.size());
    for (int k = 1; k <= std::min(3, n - 1); ++k) {
        std::vector<int> faces(static_cast<std::size_t>(n), *pass);
        for (int i = 0; i < k; ++i) faces[static_cast<std::size_t>(i)] = *fail;
        core::ScriptedDice dice(faces);
        Simulator sim(gs, 0);
        if (!sim.step(side, escape, dice).ok()) return false;
        for (int guard = 0; guard < 8 && sim.state().pending &&
                            std::holds_alternative<core::decision::OrderCasualties>(sim.state().pending->kind);
             ++guard) {
            const std::optional<PolicyResult> c = decide_casualties(hc, cache, sim.state(), side);
            if (!c || !sim.step(side, c->action).ok()) return false;
        }
        const GameState& after = sim.state();
        if (!after.pending || !std::holds_alternative<core::decision::MoveModels>(after.pending->kind)) continue;
        if (normal::standing_moves(cache, after, hc.weights.move, 1, kStandingAttempts).empty()) return false;
    }
    return true;
}

}  // namespace

std::optional<HardMovePlan> hard_plan_unit_move(HardContext& hc, analysis::OddsCache& cache, const GameState& origin,
                                                bool lookahead, bool normal_default) {
    if (!origin.flow.move || !origin.flow.move->paths.empty()) return std::nullopt;
    const core::Unit* u = origin.find_unit(origin.flow.move->unit);
    if (u == nullptr || u->models.empty()) return std::nullopt;
    // Normal's plan (M43) is the default; the best few candidates that stand compete with it.
    std::vector<normal::PlannedMove> plans;
    const bool with_default = normal_default && hc.weights.margin >= 0.0;
    if (with_default) {
        if (std::optional<normal::PlannedMove> np = normal::plan_unit_move(cache, origin)) plans.push_back(std::move(*np));
    }
    const bool have_default = !plans.empty();
    for (normal::PlannedMove& pm : normal::standing_moves(cache, origin, hc.weights.move,
                                                          std::max<std::size_t>(1, hc.weights.static_plans),
                                                          kStandingAttempts)) {
        if (have_default && pm.candidate.ends == plans.front().candidate.ends) continue;
        plans.push_back(std::move(pm));
    }
    if (plans.empty() && duel_play(origin)) return std::nullopt;  // M44's order (no table looked up)
    const Evaluator eval(cache, origin, u->side, hc.weights.chain, &hc.chains);
    if (!eval.ok()) return std::nullopt;
    if (eval.army() && origin.result_rule != core::ResultRule::PointsDestroyed) {
        // D-067: also the move that keeps furthest from every enemy model (playing for the draw, D-007: with several
        // enemy units M43's cheap score rarely ranks it among the best few). D-078: not under the points tiebreak
        // (D-076), where there is no draw to play for (measured on the rosters, D-078 part 11: Hard's Tyranids won more
        // often without it, its Chaos Space Marines as often).
        if (std::optional<normal::PlannedMove> far = normal::furthest_standing_move(origin)) {
            const bool known = std::any_of(plans.begin(), plans.end(), [&](const normal::PlannedMove& pm) {
                return pm.candidate.ends == far->candidate.ends;
            });
            if (!known) plans.push_back(std::move(*far));
        }
    }
    if (plans.empty()) return std::nullopt;
    std::vector<Evaluation> values;
    values.reserve(plans.size());
    for (const normal::PlannedMove& pm : plans) values.push_back(eval.evaluate(pm.after));
    // The default, then the others by the chain's value.
    std::vector<std::size_t> order(plans.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin() + (have_default ? 1 : 0), order.end(),
                     [&](std::size_t a, std::size_t b) { return values[a].score > values[b].score + kBetter; });
    const LookaheadWeights& lw = hc.weights.lookahead;
    const bool mc = lookahead && use_lookahead(lw);
    const std::size_t checked = mc ? std::min(lw.plans + (have_default ? 1 : 0), order.size()) : order.size();
    const std::uint64_t seed = rollout_seed(origin);
    std::optional<HardMovePlan> best;
    std::optional<double> default_value;
    for (std::size_t k = 0; k < checked; ++k) {
        const std::size_t i = order[k];
        std::optional<MonteCarloResult> r;
        if (mc) r = monte_carlo(eval, cache, plans[i].after, u->side, seed, lw);
        const double v = blend(lw, values[i], r);
        if (have_default && i == 0) default_value = v;
        // Something other than Normal's plan only when it is worth more than the margin more (BotWeights::margin).
        const double bar = default_value && i != 0 ? std::max(best ? best->value : *default_value,
                                                              *default_value + hc.weights.margin)
                                                   : (best ? best->value : v - 1.0);
        if (!best || v > bar + kBetter) {
            best = HardMovePlan{plans[i], values[i], r, v, plans.size(), have_default && i == 0};
        }
    }
    return best;
}

std::optional<PolicyResult> hard_move_units(HardContext& hc, analysis::OddsCache& cache, const GameState& gs,
                                            core::Side side, const std::vector<GameAction>& legal, std::uint64_t seed,
                                            HardTypeValues* values) {
    std::optional<core::UnitId> unit;
    if (duel_play(gs)) {
        for (const GameAction& a : legal) {
            if (const auto* m = std::get_if<core::action::StartMove>(&a)) {
                unit = m->unit;
                break;
            }
        }
    } else {
        // D-067: Normal's activation order (D-066: the unit closest to an enemy unit first), so Normal's own answer
        // (the default) is for the same unit.
        unit = normal::unit_to_move(gs, legal);
    }
    if (!unit) return std::nullopt;
    const core::Unit& u = *gs.find_unit(*unit);
    const Evaluator eval(cache, gs, side, hc.weights.chain, &hc.chains);
    if (!eval.ok()) return std::nullopt;
    const LookaheadWeights& lw = hc.weights.lookahead;
    const std::uint64_t mc_seed = rollout_seed(gs);
    std::vector<Option> options;
    // Normal's move type (M43) is the default (BotWeights::margin).
    std::optional<GameAction> normal_start;
    if (hc.weights.margin >= 0.0) {
        if (const std::optional<PolicyResult> r = normal::decide_move_units(cache, gs, side, legal, seed)) {
            normal_start = r->action;
        }
    }

    // Remain Stationary (09.04).
    const GameAction stay = core::action::StartMove{*unit, core::MoveType::RemainStationary, std::nullopt};
    if (offered(legal, stay)) {
        Simulator sim(gs, seed);
        if (sim.step(side, stay).ok()) {
            Option o;
            o.start = stay;
            o.chain = eval.evaluate(sim.state());
            if (use_lookahead(lw)) o.mc = monte_carlo(eval, cache, sim.state(), side, mc_seed, lw);
            o.value = blend(lw, o.chain, o.mc);
            o.text = "Remain Stationary";
            o.where = gap_text(sim.state(), *unit);
            if (values != nullptr) values->remain_stationary = o.value;
            options.push_back(std::move(o));
        }
    }
    // A move with no dice (Normal, Ordered Retreat): the whole plan.
    const auto planned = [&](const GameAction& start) -> std::optional<Option> {
        Simulator sim(gs, seed);
        if (!sim.step(side, start).ok()) return std::nullopt;
        std::optional<HardMovePlan> plan = hard_plan_unit_move(hc, cache, sim.state(), true);
        if (!plan) return std::nullopt;
        const auto& m = std::get<core::action::StartMove>(start);
        if (plan->move.candidate.proposals.empty() && m.move_type == core::MoveType::Normal) return std::nullopt;
        Option o;
        o.start = start;
        o.chain = plan->chain;
        o.mc = plan->mc;
        o.value = plan->value;
        o.text = move_type_text(m.move_type, m.fall_back_mode) + " " + plan->move.candidate.label +
                 (plan->normal_plan ? " (Normal's plan)" : "");
        o.where = gap_text(plan->move.after, *unit);
        o.steps.push_back(PlannedStep{state_hash(gs), start, {}});
        for (PlannedStep& s : plan->move.steps) o.steps.push_back(std::move(s));
        return o;
    };
    const GameAction normal_move = core::action::StartMove{*unit, core::MoveType::Normal, std::nullopt};
    if (offered(legal, normal_move)) {
        if (std::optional<Option> o = planned(normal_move)) {
            if (values != nullptr) values->normal = o->value;
            options.push_back(std::move(*o));
        }
    }
    // An Advance before its roll (D-054: the real roll is never looked at first): each result's best move by the
    // chain on a scratch state with that roll scripted; the lookahead rolls it with the bot's dice.
    const GameAction advance = core::action::StartMove{*unit, core::MoveType::Advance, std::nullopt};
    if (offered(legal, advance)) {
        Opening opening;
        opening.actions.push_back(advance);
        Evaluation mean;
        bool all = true;
        for (int face = 1; face <= 6 && all; ++face) {
            Simulator sim(gs, 0);
            core::ScriptedDice dice{face};  // not consumed when the roll is reused (D-005)
            if (!sim.step(side, advance, dice).ok()) {
                all = false;
                break;
            }
            // An estimate before the roll: the chain's best plans only (the move after the real roll is planned
            // with Normal's plan as the default).
            const std::optional<HardMovePlan> plan = hard_plan_unit_move(hc, cache, sim.state(), false, false);
            if (!plan) {
                all = false;
                break;
            }
            const double w = analysis::d6_pmf()[static_cast<std::size_t>(face)];
            mean.p_win += w * plan->chain.p_win;
            mean.p_draw += w * plan->chain.p_draw;
            mean.p_loss += w * plan->chain.p_loss;
            mean.v += w * plan->chain.v;
            mean.score += w * plan->chain.score;
            for (const PlannedStep& s : plan->move.steps) {
                opening.after_advance_roll[static_cast<std::size_t>(face)].push_back(s.action);
            }
        }
        if (all) {
            Option o;
            o.start = advance;
            o.chain = mean;
            if (use_lookahead(lw)) o.mc = monte_carlo(eval, cache, gs, side, mc_seed, lw, opening);
            o.value = blend(lw, o.chain, o.mc);
            o.text = "Advance (" + signed_value(o.chain.score) + " on average over the advance roll)";
            if (values != nullptr) values->advance = o.value;
            options.push_back(std::move(o));
        }
    }
    // Fall Back (09.07).
    const GameAction retreat =
        core::action::StartMove{*unit, core::MoveType::FallBack, core::FallBackMode::OrderedRetreat};
    const GameAction escape =
        core::action::StartMove{*unit, core::MoveType::FallBack, core::FallBackMode::DesperateEscape};
    if (offered(legal, retreat)) {
        if (std::optional<Option> o = planned(retreat)) {
            if (values != nullptr) values->fall_back = o->value;
            options.push_back(std::move(*o));
        }
    } else if (offered(legal, escape) && escape_stands(hc, cache, gs, side, escape, u)) {
        // Decided before the hazard rolls (D-054): planned with no failed roll, valued with the expected losses
        // taken from the unit (its last models) after the move.
        Simulator sim(gs, seed);
        core::ScriptedDice sixes(std::vector<int>(u.models.size(), 6));
        if (sim.step(side, escape, sixes).ok()) {
            if (std::optional<HardMovePlan> plan = hard_plan_unit_move(hc, cache, sim.state(), false)) {
                const int w = std::max(1, u.models.front().characteristics.wounds);
                const double lost = static_cast<double>(u.models.size()) * hazard_wounds_per_model(u) / w;
                GameState after = plan->move.after;
                if (core::Unit* mu = after.find_unit(*unit)) {
                    const auto k = static_cast<std::size_t>(std::lround(lost));
                    for (std::size_t i = 0; i < k && mu->models.size() > 1; ++i) mu->models.pop_back();
                }
                Option o;
                o.start = escape;
                o.chain = eval.evaluate(after);
                o.value = o.chain.score;
                o.text = "Fall Back (Desperate Escape) " + plan->move.candidate.label + ", expecting " +
                         std::to_string(std::lround(lost)) + " models lost to hazard rolls";
                o.where = gap_text(plan->move.after, *unit);
                if (values != nullptr) values->fall_back = o.value;
                options.push_back(std::move(o));
            }
        }
    }
    if (options.empty()) return std::nullopt;
    std::optional<std::size_t> normal_index;
    for (std::size_t i = 0; i < options.size() && normal_start; ++i) {
        if (options[i].start == *normal_start) normal_index = i;
    }
    std::size_t best = normal_index.value_or(0);
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i == best) continue;
        const double bar = normal_index ? std::max(options[best].value, options[*normal_index].value + hc.weights.margin)
                                        : options[best].value;
        if (options[i].value > bar + kBetter) best = i;
    }
    Option& chosen = options[best];
    std::string others;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i == best) continue;
        const auto& m = std::get<core::action::StartMove>(options[i].start);
        others += (others.empty() ? "" : ", ") + move_type_text(m.move_type, m.fall_back_mode) + " " +
                  signed_value(options[i].value);
    }
    std::string reason = client::unit_display_name(gs, u.id) + ": " + chosen.text;
    if (!chosen.where.empty()) reason += " - " + chosen.where;
    reason += " (Hard: " + odds_text(chosen.chain) + ", value " + signed_value(chosen.value) +
              lookahead_text(chosen.mc) + (normal_index && best == *normal_index ? ", as Normal" : "") +
              (others.empty() ? "" : "; " + others) + ")";
    if (!chosen.steps.empty()) return from_steps(std::move(chosen.steps), 0, reason);
    return single(chosen.start, reason);
}

namespace {

// MoveModels of a Normal, Advance or Fall-back move: the rest of hard_plan_unit_move from the move's start.
std::optional<PolicyResult> continue_unit_move(HardContext& hc, analysis::OddsCache& cache, const GameState& gs) {
    if (!gs.flow.move) return std::nullopt;
    const core::MoveInProgress& mv = *gs.flow.move;
    if (mv.type != core::MoveType::Normal && mv.type != core::MoveType::Advance &&
        mv.type != core::MoveType::FallBack) {
        return std::nullopt;
    }
    const std::string who = name_of(gs, mv.unit);
    const GameState origin = normal::move_origin(gs);
    if (std::optional<HardMovePlan> plan = hard_plan_unit_move(hc, cache, origin, true)) {
        std::string text = move_type_text(mv.type, mv.mode);
        if (mv.type == core::MoveType::Advance) {
            const core::Unit* u = gs.find_unit(mv.unit);
            if (u != nullptr && u->phase_flags.advance_roll) {
                text += " (roll " + std::to_string(*u->phase_flags.advance_roll) + ")";
            }
        }
        const std::string reason = who + ": " + text + " " + plan->move.candidate.label + " - " +
                                   gap_text(plan->move.after, mv.unit) + " (Hard: " + odds_text(plan->chain) +
                                   ", value " + signed_value(plan->value) + lookahead_text(plan->mc) +
                                   (plan->normal_plan ? ", Normal's plan" : "") + ")";
        const std::uint64_t h = state_hash(gs);
        for (std::size_t i = 0; i < plan->move.steps.size(); ++i) {
            if (plan->move.steps[i].expect_hash == h) return from_steps(std::move(plan->move.steps), i, reason);
        }
    }
    if (core::move_end_problems(gs).empty()) {
        return single(core::action::CommitUnitMove{}, who + ": the move so far stands; commit it");
    }
    return single(core::action::ResetUnitMove{}, who + ": the move so far would not stand; reset it (D-018)");
}

// ---- charges ----

// Hard keeps Normal's answer unless its own is worth more than the margin more (BotWeights::margin).
bool deviates(const HardContext& hc, double hard_value, double normal_value) {
    return hc.weights.margin < 0.0 || hard_value > normal_value + hc.weights.margin + kBetter;
}

// D-067: the Charge decision with several units. Each unit offered a DeclareCharge is valued at its closest eligible
// target (P = the 2D6 roll covering the gap, 11.02): P x the army value with the two engaged and the charger
// striking first (11.04) + (1 - P) x the army value with it holding; ending the step, by the army value after
// EndChargeStep. Normal's answer (M42, D-066) is the default, kept unless another is worth more than the margin
// more.
std::optional<PolicyResult> decide_charge_army(HardContext& hc, analysis::OddsCache& cache, const GameState& gs,
                                               core::Side side, const std::vector<GameAction>& legal,
                                               std::uint64_t seed, const Evaluator& eval) {
    const GameAction end = core::action::EndChargeStep{};
    if (!offered(legal, end)) return std::nullopt;
    struct Option {
        GameAction action;
        Evaluation value;
        std::string text;
    };
    std::vector<Option> options;
    {
        Simulator held(gs, seed);
        if (!held.step(side, end).ok()) return std::nullopt;
        const Evaluation v = eval.evaluate(held.state());
        options.push_back(Option{end, v, "no charge " + signed_value(v.score) + " (" + odds_text(v) + ")"});
    }
    for (const GameAction& a : legal) {
        const auto* d = std::get_if<core::action::DeclareCharge>(&a);
        const core::Unit* u = d != nullptr ? gs.find_unit(d->unit) : nullptr;
        if (u == nullptr || u->side != side) continue;
        const core::Unit* t = closest_charge_target(gs, *u);
        if (t == nullptr) continue;
        const double p = analysis::p_charge_roll_covers(core::unit_distance(*u, *t).value_or(0.0f));
        const Evaluator::ChargeValues cv = eval.charge_values(gs, u->id, t->id, p);
        options.push_back(Option{a, cv.charge,
                                 name_of(gs, u->id) + " at " + name_of(gs, t->id) + " " + signed_value(cv.charge.score) +
                                     " (" + percent(p) + " charge odds; the fight " + signed_value(cv.fight.score) +
                                     ", holding " + signed_value(cv.hold.score) + ")"});
    }
    // Normal's answer (M42, D-066) is the default.
    std::optional<std::size_t> normal_index;
    if (hc.weights.margin >= 0.0) {
        if (const std::optional<PolicyResult> n = normal::decide_charge(cache, gs, side, legal, seed)) {
            for (std::size_t i = 0; i < options.size(); ++i) {
                if (options[i].action == n->action) normal_index = i;
            }
        }
    }
    std::size_t best = normal_index.value_or(0);
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i == best) continue;
        const double bar = normal_index ? std::max(options[best].value.score,
                                                   options[*normal_index].value.score + hc.weights.margin)
                                        : options[best].value.score;
        if (options[i].value.score > bar + kBetter) best = i;
    }
    std::string others;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i != best) others += (others.empty() ? "" : "; ") + options[i].text;
    }
    const Option& chosen = options[best];
    const bool as_normal = normal_index && best == *normal_index;
    const std::string head = std::holds_alternative<core::action::DeclareCharge>(chosen.action)
                                 ? name_of(gs, std::get<core::action::DeclareCharge>(chosen.action).unit) +
                                       ": declare a charge"
                                 : "End the Charge step";
    return single(chosen.action, head + " (Hard: " + odds_text(chosen.value) + ", value " +
                                     signed_value(chosen.value.score) + (as_normal ? ", as Normal" : "") + "; " +
                                     chosen.text + (others.empty() ? "" : "; " + others) + ")");
}

std::optional<PolicyResult> decide_charge(HardContext& hc, analysis::OddsCache& cache, const GameState& gs,
                                          core::Side side, const std::vector<GameAction>& legal, std::uint64_t seed) {
    const Evaluator eval(cache, gs, side, hc.weights.chain, &hc.chains);
    if (!eval.ok()) return std::nullopt;
    if (eval.army()) return decide_charge_army(hc, cache, gs, side, legal, seed, eval);
    for (const GameAction& a : legal) {
        const auto* d = std::get_if<core::action::DeclareCharge>(&a);
        const core::Unit* u = d != nullptr ? gs.find_unit(d->unit) : nullptr;
        if (u == nullptr || u->side != side) continue;
        const core::Unit* e = normal::closest_enemy(gs, *u);
        if (e == nullptr) continue;
        const GameAction end = core::action::EndChargeStep{};
        if (!offered(legal, end)) return std::nullopt;
        const double p = analysis::p_charge_roll_covers(core::unit_distance(*u, *e).value_or(0.0f));
        const Evaluator::ChargeValues cv = eval.charge_values(gs, p);
        // Normal's answer (M42) is the default.
        bool declare = cv.wanted;
        if (hc.weights.margin >= 0.0) {
            const std::optional<PolicyResult> n = normal::decide_charge(cache, gs, side, legal, seed);
            const bool normal_declares = n && std::holds_alternative<core::action::DeclareCharge>(n->action);
            declare = normal_declares ? !deviates(hc, cv.hold.score, cv.charge.score)
                                      : deviates(hc, cv.charge.score, cv.hold.score);
        }
        // The chosen option's odds come first (M45: the combat log shows the first odds of a Hard reason).
        const std::string fight = "the fight " + signed_value(cv.fight.score) + " (" + odds_text(cv.fight) + ")";
        const std::string hold = "holding " + signed_value(cv.hold.score) + " (" + odds_text(cv.hold) + ")";
        const std::string odds = percent(p) + " charge odds against " + client::unit_display_name(gs, e->id) + "; ";
        if (declare) return single(a, client::unit_display_name(gs, u->id) + ": declare a charge (Hard: " + odds + fight + ", " + hold + ")");
        return single(end, client::unit_display_name(gs, u->id) + ": no charge (Hard: " + odds + hold + ", " + fight + ")");
    }
    return std::nullopt;
}

std::optional<PolicyResult> decide_charge_targets(HardContext& hc, analysis::OddsCache& cache, const GameState& gs,
                                                  core::Side side, const std::vector<GameAction>& legal,
                                                  std::uint64_t seed) {
    const auto& d = std::get<core::decision::SelectChargeTargets>(gs.pending->kind);
    const core::Unit* u = gs.find_unit(d.unit);
    if (u == nullptr) return std::nullopt;
    const Evaluator eval(cache, gs, side, hc.weights.chain, &hc.chains);
    if (!eval.ok()) return std::nullopt;
    const GameAction skip = core::action::SkipChargeMove{d.unit};
    if (!offered(legal, skip)) return std::nullopt;
    Simulator held(gs, seed);
    if (!held.step(side, skip).ok()) return std::nullopt;
    const Evaluation hold = eval.evaluate(held.state());
    const int roll = u->phase_flags.charge_roll.value_or(0);
    std::optional<normal::ChargePlan> best;
    Evaluation best_value;
    for (const GameAction& a : legal) {
        const auto* s = std::get_if<core::action::StartChargeMove>(&a);
        if (s == nullptr || s->unit != d.unit) continue;
        std::optional<normal::ChargePlan> plan = normal::plan_charge_move(gs, side, d.unit, s->targets, seed, {});
        if (!plan) continue;
        const Evaluation v = eval.evaluate(plan->after);
        if (!best || v.score > best_value.score + kBetter) {
            best_value = v;
            best = std::move(plan);
        }
    }
    if (!best) return single(skip, client::unit_display_name(gs, u->id) + ": no charge move stands for a roll of " + std::to_string(roll));
    // Normal's answer (M42: the planned move if it beats holding) is the default.
    bool charge = best_value.score > hold.score + kBetter;
    if (hc.weights.margin >= 0.0) {
        const std::optional<PolicyResult> n = normal::decide_charge_targets(cache, gs, side, legal, seed);
        const bool normal_charges = n && std::holds_alternative<core::action::StartChargeMove>(n->action);
        charge = normal_charges ? !deviates(hc, hold.score, best_value.score) : deviates(hc, best_value.score, hold.score);
    }
    if (charge) {
        const std::string reason = client::unit_display_name(gs, u->id) + ": charge move (roll " + std::to_string(roll) + "): " +
                                   std::to_string(best->score.engaged) + " of " + std::to_string(u->models.size()) +
                                   " models engaged (Hard: " + signed_value(best_value.score) + ", " +
                                   odds_text(best_value) + "; holding " + signed_value(hold.score) + ")";
        for (PlannedStep& step : best->steps) step.reason = reason;
        return from_steps(std::move(best->steps), 0, reason);
    }
    return single(skip, client::unit_display_name(gs, u->id) + ": no charge move (Hard: holding " + signed_value(hold.score) + " against " +
                            signed_value(best_value.score) + " for the charge)");
}

// ---- casualties, coherency, consolidation ----

// The models that die (and the wounds lost) if `priority` answers the pending OrderCasualties, from the engine's
// preview (05.04, 06.02, D-013).
std::optional<std::vector<core::DamageResult>> preview(const GameState& gs, const std::vector<core::ModelId>& priority) {
    std::vector<core::ModelId> picks;
    for (std::size_t guard = 0; guard <= priority.size() * 8 + 8; ++guard) {
        const std::optional<core::CasualtyPreview> pv = core::preview_casualties(gs, picks);
        if (!pv) return std::nullopt;
        if (pv->done()) return pv->results;
        std::optional<core::ModelId> next;
        for (const core::ModelId id : priority) {
            if (std::find(pv->options.begin(), pv->options.end(), id) != pv->options.end()) {
                next = id;
                break;
            }
        }
        picks.push_back(next ? *next : pv->options.front());
    }
    return std::nullopt;
}

std::optional<PolicyResult> decide_casualties(HardContext& hc, analysis::OddsCache& cache, const GameState& gs,
                                              core::Side side, std::vector<HardCasualtyCandidate>* out) {
    const auto* d = std::get_if<core::decision::OrderCasualties>(&gs.pending->kind);
    if (d == nullptr) return std::nullopt;
    const core::Unit* unit = gs.find_unit(d->target);
    if (unit == nullptr || unit->models.empty()) return std::nullopt;
    const Evaluator eval(cache, gs, side, hc.weights.chain, &hc.chains);
    if (!eval.ok()) return std::nullopt;
    struct Cand {
        std::vector<core::ModelId> priority;
        std::string text;
    };
    std::vector<Cand> cands;
    std::string normal_reason;
    if (std::optional<normal::CasualtyOrder> o = normal::casualty_order(cache, gs)) {
        cands.push_back(Cand{o->priority, "Normal's order"});
        normal_reason = o->reason;
    }
    const bool have_normal = !cands.empty();
    // The front models first (a charge against us gets longer) and the rear ones first (ours gets shorter). D-067:
    // with several units, measured against the attacker (D-066's attacking_enemy; M44: the closest enemy unit, the
    // duel's only one).
    const core::Unit* enemy = eval.army() ? normal::attacking_enemy(gs, *unit) : normal::closest_enemy(gs, *unit);
    if (enemy != nullptr) {
        std::vector<std::pair<float, core::ModelId>> by_gap;
        for (const core::Model& m : unit->models) {
            by_gap.emplace_back(core::model_distance_to_unit(m, m.position, *enemy).value_or(0.0f), m.id);
        }
        std::stable_sort(by_gap.begin(), by_gap.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first < b.first;
            return a.second.v < b.second.v;
        });
        Cand front{{}, "the front models first"};
        for (const auto& [gap, id] : by_gap) front.priority.push_back(id);
        Cand rear{std::vector<core::ModelId>(front.priority.rbegin(), front.priority.rend()), "the rear models first"};
        cands.push_back(std::move(front));
        cands.push_back(std::move(rear));
    }
    // D-058 (M47): the default order (the players' default and Easy's), last so that a tie changes nothing.
    cands.push_back(
        Cand{client::default_casualty_order(gs, d->target), "the default order, furthest from the enemy first"});
    std::optional<std::size_t> best;
    Evaluation best_value;
    std::optional<double> normal_value;
    // Each candidate's preview (who dies, wounds lost) and value; a candidate with the same results as an earlier
    // one leaves the same position, so it takes that value without evaluating it again (never better, so never
    // chosen over it).
    std::vector<std::optional<std::vector<core::DamageResult>>> seen;
    std::vector<std::optional<Evaluation>> values;
    for (std::size_t i = 0; i < cands.size(); ++i) {
        std::optional<std::vector<core::DamageResult>> results = preview(gs, cands[i].priority);
        std::optional<Evaluation> value;
        if (results) {
            for (std::size_t j = 0; j < i && !value; ++j) {
                if (seen[j] && *seen[j] == *results) value = values[j];
            }
            if (!value) {
                GameState after = gs;
                apply_results(after, d->target, *results);
                restore_coherency(after, d->target);
                value = eval.evaluate(after);
            }
        }
        seen.push_back(std::move(results));
        values.push_back(value);
        if (out != nullptr) {
            out->push_back(HardCasualtyCandidate{cands[i].priority, cands[i].text,
                                                 value ? std::optional<double>(value->score) : std::nullopt});
        }
        if (!value) continue;
        const Evaluation& v = *value;
        if (have_normal && i == 0) normal_value = v.score;
        const bool better = !best || v.score > best_value.score + kBetter;
        if (better && (!normal_value || i == 0 || deviates(hc, v.score, *normal_value))) {
            best = i;
            best_value = v;
        }
    }
    if (!best) return std::nullopt;
    const std::string reason =
        (*best == 0 && have_normal ? normal_reason : client::unit_display_name(gs, unit->id) + ": casualties, " + cands[*best].text) +
        " (Hard: " + signed_value(best_value.score) + ", " + odds_text(best_value) + ")";
    return single(core::action::OrderCasualties{d->target, cands[*best].priority}, reason);
}

std::optional<PolicyResult> decide_coherency(HardContext& hc, analysis::OddsCache& cache, const GameState& gs,
                                             core::Side side, core::UnitId unit_id) {
    const core::Unit* unit = gs.find_unit(unit_id);
    if (unit == nullptr || unit->models.empty()) return std::nullopt;
    const Evaluator eval(cache, gs, side, hc.weights.chain, &hc.chains);
    if (!eval.ok()) return std::nullopt;
    // Normal's pick (M41) first: the default.
    std::vector<core::ModelId> order;
    if (const std::optional<core::ModelId> n = normal::coherency_removal(cache, gs, unit_id)) order.push_back(*n);
    const bool have_normal = !order.empty();
    for (const core::Model& m : unit->models) {
        if (std::find(order.begin(), order.end(), m.id) == order.end()) order.push_back(m.id);
    }
    std::optional<core::ModelId> best;
    Evaluation best_value;
    std::optional<double> normal_value;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const core::ModelId id = order[i];
        const GameAction a = core::action::RemoveModelForCoherency{unit_id, id};
        if (!core::validate_action(gs, side, a).ok()) continue;
        GameState after = gs;
        core::Unit* u = after.find_unit(unit_id);
        std::erase_if(u->models, [&](const core::Model& m) { return m.id == id; });
        restore_coherency(after, unit_id);
        const Evaluation v = eval.evaluate(after);
        if (have_normal && i == 0) normal_value = v.score;
        const bool better = !best || v.score > best_value.score + kBetter;
        if (better && (!normal_value || i == 0 || deviates(hc, v.score, *normal_value))) {
            best = id;
            best_value = v;
        }
    }
    if (!best) return std::nullopt;
    return single(core::action::RemoveModelForCoherency{unit_id, *best},
                  client::unit_display_name(gs, unit->id) + ": remove model " + std::to_string(best->v) + " to regain coherency (03.03; Hard: " +
                      signed_value(best_value.score) + ", " + odds_text(best_value) + ")");
}

std::optional<PolicyResult> decide_consolidation(HardContext& hc, analysis::OddsCache& cache, const GameState& gs,
                                                 core::Side side, const std::vector<GameAction>& legal,
                                                 std::uint64_t seed) {
    const GameAction end = core::action::EndConsolidations{};
    if (!offered(legal, end)) return std::nullopt;
    const Evaluator eval(cache, gs, side, hc.weights.chain, &hc.chains);
    if (!eval.ok()) return std::nullopt;
    const Evaluation base = eval.evaluate(gs);
    // Normal's answer (M41) is the default: not moving, or the consolidation it starts.
    std::optional<GameAction> normal_start;
    if (hc.weights.margin >= 0.0) {
        if (const std::optional<PolicyResult> n = normal::decide_consolidation(cache, gs, side, legal, seed)) {
            normal_start = n->action;
        }
    }
    struct Cand {
        GameAction start;
        std::optional<normal::FightMovePlan> plan;  // nullopt: end consolidations
        Evaluation value;
        std::string reason;
        client::FightMoveShape change;  // D-057: the unit's shape after minus before (zero for not moving)
    };
    std::vector<Cand> cands;
    cands.push_back(Cand{end, std::nullopt, base, "End consolidations", {}});
    for (const GameAction& a : legal) {
        const auto* c = std::get_if<core::action::StartConsolidation>(&a);
        if (c == nullptr || !client::fight_move_worthwhile(gs, side, a)) continue;
        const std::string reason = name_of(gs, c->unit) + ": consolidate";
        std::optional<normal::FightMovePlan> plan = normal::simulate_fight_move(gs, side, a, seed, reason);
        if (!plan) continue;
        const Evaluation v = eval.evaluate(plan->after);
        const client::FightMoveShape change =
            client::shape_change(client::fight_move_shape(gs, c->unit, c->targets),
                                 client::fight_move_shape(plan->after, c->unit, c->targets));
        cands.push_back(Cand{a, std::move(plan), v, reason + " - " + client::describe_shape_change(change), change});
    }
    std::size_t best = 0;
    for (std::size_t i = 0; i < cands.size(); ++i) {
        if (normal_start && cands[i].start == *normal_start) best = i;
    }
    const std::size_t normal_index = best;
    const bool have_normal = normal_start.has_value();
    for (std::size_t i = 0; i < cands.size(); ++i) {
        if (i == best) continue;
        const bool better = cands[i].value.score > cands[best].value.score + kBetter;
        if (better && (!have_normal || deviates(hc, cands[i].value.score, cands[normal_index].value.score))) best = i;
        // D-057 (M46): without Normal's default (tuning), an equal score goes to the tidier move, as in Normal.
        const bool tie = std::fabs(cands[i].value.score - cands[best].value.score) <= kBetter;
        if (!have_normal && tie && client::tidier(cands[i].change, cands[best].change)) best = i;
    }
    Cand& chosen = cands[best];
    const std::string why = " (Hard: " + signed_value(chosen.value.score) + " against " + signed_value(base.score) +
                            " for not moving, " + odds_text(chosen.value) + ")";
    if (!chosen.plan) return single(end, "End consolidations" + why);
    for (PlannedStep& step : chosen.plan->steps) step.reason = chosen.reason + why;
    return from_steps(std::move(chosen.plan->steps), 0, chosen.reason + why);
}

bool is_unit_move(const GameState& gs) {
    return gs.flow.move && (gs.flow.move->type == core::MoveType::Normal ||
                            gs.flow.move->type == core::MoveType::Advance ||
                            gs.flow.move->type == core::MoveType::FallBack);
}

// D-067: Hard's own answers apply to a duel (M44) and to several units a side (the army), while both sides have a
// unit with models and the game isn't over.
bool hard_applies(const GameState& gs) {
    if (gs.result) return false;
    if (duel_play(gs)) return true;
    std::array<bool, 2> alive{};
    for (const core::Unit& u : gs.units) {
        if (!u.models.empty()) alive[core::side_index(u.side)] = true;
    }
    return alive[0] && alive[1];
}

// D-078 (M69): a side with more units with models than BotWeights::move_search_units moves as Normal does. Measured on
// the rosters (60 seeds against Normal): Hard's Tyranids (9 units) won 35 games with their own move search and 44 with
// Normal's moves; its Chaos Space Marines (5 units) are not affected.
bool moves_as_normal(const GameState& gs, core::Side side, const BotWeights& w) {
    if (w.move_search_units == 0) return false;
    std::size_t units = 0;
    for (const core::Unit& u : gs.units) {
        if (u.side == side && !u.models.empty()) ++units;
    }
    return units > w.move_search_units;
}

std::optional<PolicyResult> hard_answer(const GameState& gs, core::Side side, const std::vector<GameAction>& legal,
                                        std::uint64_t seed, HardContext& hc) {
    // D-078: the cache's build workers, and its tables drop the half-turns already played (never a value).
    hc.chains.set_workers(hc.weights.lookahead.table_workers);
    hc.chains.trim_before(std::max(0, (gs.flow.battle_round - 1) * 2 + gs.flow.turn_in_round));
    if (!hard_applies(gs)) return std::nullopt;
    analysis::OddsCache cache;  // per decision (D-054)
    const core::DecisionKind& kind = gs.pending->kind;
    const bool unit_move = std::holds_alternative<core::decision::MoveUnits>(kind) ||
                           (std::holds_alternative<core::decision::MoveModels>(kind) && is_unit_move(gs));
    if (unit_move && moves_as_normal(gs, side, hc.weights)) return std::nullopt;
    if (std::holds_alternative<core::decision::MoveUnits>(kind)) {
        return hard_move_units(hc, cache, gs, side, legal, seed);
    }
    if (std::holds_alternative<core::decision::MoveModels>(kind) && is_unit_move(gs)) {
        return continue_unit_move(hc, cache, gs);
    }
    if (std::holds_alternative<core::decision::Charge>(kind)) return decide_charge(hc, cache, gs, side, legal, seed);
    if (std::holds_alternative<core::decision::SelectChargeTargets>(kind)) {
        return decide_charge_targets(hc, cache, gs, side, legal, seed);
    }
    if (std::holds_alternative<core::decision::OrderCasualties>(kind)) return decide_casualties(hc, cache, gs, side);
    if (const auto* d = std::get_if<core::decision::RestoreCoherency>(&kind)) {
        return decide_coherency(hc, cache, gs, side, d->unit);
    }
    if (std::holds_alternative<core::decision::Consolidate>(kind)) {
        return decide_consolidation(hc, cache, gs, side, legal, seed);
    }
    return std::nullopt;
}

}  // namespace

PolicyResult hard_policy(const GameState& gs, core::Side side, const std::vector<GameAction>& legal,
                         std::uint64_t seed, HardContext& context) {
    if (std::optional<PolicyResult> r = hard_answer(gs, side, legal, seed, context)) return std::move(*r);
    return normal_policy(gs, side, legal, seed);
}

std::optional<PolicyResult> hard_casualties(HardContext& context, analysis::OddsCache& cache, const GameState& gs,
                                            core::Side side, std::vector<HardCasualtyCandidate>* candidates) {
    if (!gs.pending) return std::nullopt;
    return decide_casualties(context, cache, gs, side, candidates);
}

}  // namespace wh40k::ai::hard
