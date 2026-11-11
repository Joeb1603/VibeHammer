#include "normal/defaults.hpp"

#include "unit_style.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "action_text.hpp"
#include "casualty_order.hpp"
#include "casualty_plan.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/units.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/state/queries.hpp"
#include "decision_view.hpp"
#include "fight_move_skip.hpp"
#include "melee_plan.hpp"
#include "analysis/threat.hpp"
#include "normal/fight.hpp"
#include "normal/value.hpp"
#include "shot_plan.hpp"
#include "simulator.hpp"
#include "state_hash.hpp"

namespace wh40k::ai::normal {

namespace {

using core::GameAction;
using core::GameState;

constexpr float kFarInches = 8.0f;          // the M12 policy's "far away"
constexpr float kStepInches = 2.0f;         // how far a non-charge move goes
constexpr float kChargeShortInches = 1.0f;  // a charge move stops this far short of the enemy
// D-066 (M56): beyond 12" + its Move from the closest enemy unit, the default walks its full Move (after it, the enemy is
// still more than 12" away: outside any charge); nearer, the old 2" steps. V1's units start 12" apart and the default only
// ever closes, so the V1 board doesn't reach it (checked over the arena's 1v1 games).
constexpr float kWalkBeyondInches = 12.0f;

// D-066 (M56): the enemy unit closest to `u` (D-010, edge to edge; a tie goes to the first in unit order, the
// controlling player's choice). With one enemy unit it is the first one, so V1 play is unchanged.
const core::Unit* enemy_of(const GameState& gs, const core::Unit& u) { return closest_enemy(gs, u); }

struct Vec {
    float x = 0.0f;
    float y = 0.0f;
};

Vec centre(const core::Unit& u) {
    Vec c;
    for (const core::Model& m : u.models) {
        c.x += m.position.x;
        c.y += m.position.y;
    }
    const float n = static_cast<float>(std::max<std::size_t>(1, u.models.size()));
    return Vec{c.x / n, c.y / n};
}

// The moving unit as it stood when its move started (every model at its start position), so the plan is the
// same from any state along the move (D-054: a pure function of the state when planning started).
core::Unit unit_at_starts(const GameState& gs) {
    const core::MoveInProgress& mv = *gs.flow.move;
    core::Unit u = *gs.find_unit(mv.unit);
    for (core::Model& m : u.models) {
        for (const core::ModelStart& s : mv.starts) {
            if (s.model == m.id) m.position = s.position;
        }
    }
    return u;
}

// What the move in progress aims at: for a charge, the closest of its charge targets (D-066); otherwise the closest
// enemy unit.
const core::Unit* aim_of(const GameState& gs, const core::Unit& u) {
    if (gs.flow.move->type == core::MoveType::Charge) {
        const core::Unit* best = nullptr;
        float best_gap = 0.0f;
        for (const core::UnitId id : gs.flow.move->targets) {
            const core::Unit* t = gs.find_unit(id);
            if (t == nullptr || t->models.empty()) continue;
            const std::optional<float> gap = core::unit_distance(u, *t);
            if (!gap) continue;
            if (best == nullptr || *gap < best_gap) {
                best = t;
                best_gap = *gap;
            }
        }
        return best;
    }
    return enemy_of(gs, u);
}

// The unit's full Move for the move in progress (its slowest model's maximum).
float full_move_mm(const GameState& gs, const core::Unit& u) {
    float m = -1.0f;
    for (const core::Model& model : u.models) {
        const float max = core::max_move_distance_mm(gs, model.id).value_or(0.0f);
        m = m < 0.0f ? max : std::min(m, max);
    }
    return std::max(m, 0.0f);
}

// D-066: the closest enemy is far enough away to walk the full Move (more than 12" + M).
bool walks_full_move(const GameState& gs, const core::Unit& u, const core::Unit& e) {
    if (gs.flow.move->type != core::MoveType::Normal) return false;
    const float gap = core::unit_distance(u, e).value_or(0.0f);
    return core::more_than(gap, core::inches_to_mm(kWalkBeyondInches) + full_move_mm(gs, u));
}

// The M12 policy's "Move unit" handle for the move in progress: towards the aimed-at enemy unit's centre, 2" (D-066:
// or its full Move while that unit is far away, `full`), or for a charge the gap to the target less 1".
std::optional<Vec> easy_delta(const GameState& gs, bool full) {
    const core::Unit u = unit_at_starts(gs);
    const core::Unit* e = aim_of(gs, u);
    if (e == nullptr || u.models.empty()) return std::nullopt;
    const Vec a = centre(u);
    const Vec b = centre(*e);
    const float len = std::hypot(b.x - a.x, b.y - a.y);
    if (!(len > 0.0f)) return std::nullopt;
    float dist = core::inches_to_mm(kStepInches);
    if (gs.flow.move->type == core::MoveType::Charge) {
        dist = std::max(0.0f, core::unit_distance(u, *e).value_or(0.0f) - core::inches_to_mm(kChargeShortInches));
    } else if (full) {
        dist = full_move_mm(gs, u);
    }
    return Vec{(b.x - a.x) / len * dist, (b.y - a.y) / len * dist};
}

// Every model not yet moved, furthest along the move first (D-027: as translate_unit, so no model ends on one
// still to move), each one straight line from its start.
std::vector<core::action::ProposeModelMove> remaining_proposals(const GameState& gs, Vec d) {
    const core::Unit u = unit_at_starts(gs);
    std::vector<const core::Model*> order;
    for (const core::Model& m : u.models) order.push_back(&m);
    const auto ahead = [&](const core::Model* m) { return m->position.x * d.x + m->position.y * d.y; };
    std::stable_sort(order.begin(), order.end(),
                     [&](const core::Model* a, const core::Model* b) { return ahead(a) > ahead(b); });
    std::vector<core::action::ProposeModelMove> out;
    const auto& paths = gs.flow.move->paths;
    for (const core::Model* m : order) {
        if (std::any_of(paths.begin(), paths.end(), [&](const core::ModelPath& p) { return p.model == m->id; })) {
            continue;
        }
        out.push_back(core::action::ProposeModelMove{
            m->id, {core::Position{m->position.x + d.x, m->position.y + d.y, m->position.z}}});
    }
    return out;
}

// The proposals of `d` from `sim`'s state, each one the engine accepts, and the commit; nullopt if the move
// wouldn't stand (move_end_problems, a refused step or a revert).
bool add_proposals_and_commit(Simulator& sim, core::Side side, const std::optional<Vec>& d, const std::string& reason,
                              std::vector<PlannedStep>& steps) {
    if (d) {
        for (const auto& p : remaining_proposals(sim.state(), *d)) {
            if (!core::validate_action(sim.state(), side, p).ok()) continue;  // that model stays where it is
            steps.push_back(PlannedStep{state_hash(sim.state()), p, reason});
            if (!sim.step(side, p).ok()) return false;
        }
    }
    if (!core::move_end_problems(sim.state()).empty()) return false;
    steps.push_back(PlannedStep{state_hash(sim.state()), core::action::CommitUnitMove{}, reason});
    std::vector<core::GameEvent> events;
    if (!sim.step(side, core::action::CommitUnitMove{}, &events).ok()) return false;
    for (const core::GameEvent& e : events) {
        if (std::holds_alternative<core::event::UnitMoveReverted>(e)) return false;
    }
    return true;
}

// D-066: walk the full Move? Only while the aimed-at unit is far away and the whole full move, made from the move's
// start (a pure function of it, so a replan from any state along the move decides the same), stands.
bool use_full_move(const GameState& in_move, core::Side side, std::uint64_t seed) {
    const core::Unit u = unit_at_starts(in_move);
    const core::Unit* e = aim_of(in_move, u);
    if (e == nullptr || !walks_full_move(in_move, u, *e)) return false;
    Simulator sim(move_origin(in_move), seed);
    std::vector<PlannedStep> scratch;
    return add_proposals_and_commit(sim, side, easy_delta(sim.state(), true), {}, scratch);
}

// D-054: the whole move simulated before any of it is made. From `state` (with `start` first, unless the move
// is already in progress): the start, each model's proposal (one the engine refuses leaves that model where
// it is), and the commit, which must pass move_end_problems and not be reverted. Returns the steps, each with
// the hash of the state it expects; nullopt if the move wouldn't stand. `full` (if given) says whether the move
// is the full Move (D-066) rather than the 2" step.
std::optional<std::vector<PlannedStep>> plan_move(const GameState& state, core::Side side,
                                                  const std::optional<GameAction>& start, std::uint64_t seed,
                                                  const std::string& reason, bool* full = nullptr) {
    Simulator sim(state, seed);
    std::vector<PlannedStep> steps;
    if (start) {
        steps.push_back(PlannedStep{state_hash(sim.state()), *start, reason});
        if (!sim.step(side, *start).ok()) return std::nullopt;
    }
    if (!sim.state().flow.move || !sim.state().pending ||
        !std::holds_alternative<core::decision::MoveModels>(sim.state().pending->kind)) {
        return std::nullopt;
    }
    const bool whole = use_full_move(sim.state(), side, seed);
    if (full != nullptr) *full = whole;
    if (!add_proposals_and_commit(sim, side, easy_delta(sim.state(), whole), reason, steps)) return std::nullopt;
    return steps;
}

PolicyResult from_plan(std::vector<PlannedStep> steps) {
    PolicyResult r{steps.front().action, steps.front().reason, {}};
    r.rest.assign(std::make_move_iterator(steps.begin() + 1), std::make_move_iterator(steps.end()));
    return r;
}

PolicyResult single(const GameState& gs, const GameAction& a, const std::string& why = {}) {
    std::string reason = client::action_label(gs, a);
    if (!why.empty()) reason += " (" + why + ")";
    return PolicyResult{a, std::move(reason), {}};
}

std::string unit_name(const GameState& gs, core::UnitId id) {
    return client::unit_display_name(gs, id);  // D-065
}

// 09.02: a Normal move towards the enemy while it is more than 8" away, else Remain Stationary. D-066 (M56): towards
// the closest enemy unit, and its full Move while that unit is more than 12" + M away.
PolicyResult move_units(const GameState& gs, core::Side side, const client::DecisionView& view, std::uint64_t seed) {
    for (const GameAction& b : view.buttons) {
        const auto* m = std::get_if<core::action::StartMove>(&b);
        if (m == nullptr || m->move_type != core::MoveType::Normal) continue;
        const core::Unit& u = *gs.find_unit(m->unit);
        const core::Unit* e = enemy_of(gs, u);
        if (e == nullptr) break;
        const float gap = core::mm_to_inches(core::unit_distance(u, *e).value_or(0.0f));
        const core::action::StartMove stay{m->unit, core::MoveType::RemainStationary, std::nullopt};
        if (gap <= kFarInches) return single(gs, stay, client::unit_display_name(gs, e->id) + " within 8\"");
        const std::string reason = client::unit_display_name(gs, u.id) + ": Normal move 2\" towards " + client::unit_display_name(gs, e->id) + " (more than 8\" away)";
        bool full = false;
        if (auto plan = plan_move(gs, side, b, seed, reason, &full)) {
            if (full) {
                // D-066: the full Move; the reason says so (a 2" step keeps the M12 text).
                char buf[96];
                std::snprintf(buf, sizeof buf, ": Normal move %.0f\" towards ",
                              static_cast<double>(core::mm_to_inches(analysis::threat_ranges(u).move_mm)));
                const std::string why = client::unit_display_name(gs, u.id) + buf + client::unit_display_name(gs, e->id) +
                                        " (more than 12\" + its Move away)";
                for (PlannedStep& step : *plan) step.reason = why;
            }
            return from_plan(std::move(*plan));
        }
        return single(gs, stay, "a 2\" move towards " + client::unit_display_name(gs, e->id) + " would not stand");
    }
    return single(gs, view.buttons.front());
}

// D-066 (M56): the offered charge with the closest eligible target alone (D-010; a tie goes to the first in unit
// order). nullopt if none is offered.
std::optional<GameAction> closest_target_charge(const GameState& gs, core::UnitId unit,
                                                const client::DecisionView& view) {
    const core::Unit* u = gs.find_unit(unit);
    if (u == nullptr) return std::nullopt;
    std::optional<GameAction> best;
    float best_gap = 0.0f;
    for (const GameAction& b : view.buttons) {
        const auto* c = std::get_if<core::action::StartChargeMove>(&b);
        if (c == nullptr || c->unit != unit || c->targets.size() != 1) continue;
        const core::Unit* t = gs.find_unit(c->targets.front());
        if (t == nullptr) continue;
        const std::optional<float> gap = core::unit_distance(*u, *t);
        if (!gap) continue;
        if (!best || *gap < best_gap) {
            best = b;
            best_gap = *gap;
        }
    }
    return best;
}

}  // namespace

std::optional<PolicyResult> reroll_failed_wound_rolls(const GameState& gs) {
    if (!gs.pending || !std::holds_alternative<core::decision::RerollWoundRolls>(gs.pending->kind) ||
        !gs.flow.attack || !gs.flow.attack->wound_rerolls) {
        return std::nullopt;
    }
    const std::vector<std::size_t> failed = core::failed_wound_rolls(gs.flow.attack->wound_rerolls->rolls);
    return PolicyResult{core::action::RerollWoundRolls{failed},
                        "Re-roll the " + std::to_string(failed.size()) + " failed wound roll" +
                            (failed.size() == 1 ? "" : "s") + " ([TWIN-LINKED], 24.38)",
                        {}};
}

PolicyResult default_answer(const GameState& gs, core::Side side, const std::vector<GameAction>& legal,
                            std::uint64_t seed) {
    using namespace core;
    const client::DecisionView view = client::make_decision_view(legal);
    const DecisionKind& kind = gs.pending->kind;
    if (std::holds_alternative<decision::RerollWoundRolls>(kind)) {
        // 24.38 (M61, D-072): every failed wound roll is re-rolled and no other (no critical-wound effect is in the
        // game, so a re-rolled success can only fail).
        if (auto r = reroll_failed_wound_rolls(gs)) return std::move(*r);
    }
    if (std::holds_alternative<decision::MoveUnits>(kind) && !view.buttons.empty()) {
        return move_units(gs, side, view, seed);
    }
    if (std::holds_alternative<decision::MoveModels>(kind) && gs.flow.move &&
        (gs.flow.move->type == MoveType::PileIn || gs.flow.move->type == MoveType::Consolidation)) {
        // D-057 (M46): pile-ins and consolidations are Auto move (D-050), replanned from the move's start.
        if (auto r = continue_fight_move(gs, side, seed)) return std::move(*r);
    }
    if (std::holds_alternative<decision::MoveModels>(kind) && gs.flow.move) {
        // Normally answered from the plan memo. From a state the plan didn't come from (a load, an undo), the
        // rest of the move is planned again from the move's start positions, so it comes out the same.
        const std::string reason = unit_name(gs, gs.flow.move->unit) + ": moving";
        if (auto plan = plan_move(gs, side, std::nullopt, seed, reason)) return from_plan(std::move(*plan));
        return PolicyResult{action::ResetUnitMove{}, "The move would not stand: reset it", {}};
    }
    if (const auto* d = std::get_if<decision::SelectChargeTargets>(&kind)) {
        // The first set of targets (D-066, M56: the closest eligible target alone, when it is offered), if the
        // translated charge move stands; otherwise no charge move.
        std::optional<GameAction> charge;
        if (!view.buttons.empty() && std::holds_alternative<action::StartChargeMove>(view.buttons.front())) {
            charge = view.buttons.front();
        }
        if (charge) {
            if (const std::optional<GameAction> c = closest_target_charge(gs, d->unit, view)) charge = c;
        }
        if (charge) {
            const std::string reason = unit_name(gs, d->unit) + ": charge move";
            if (auto plan = plan_move(gs, side, *charge, seed, reason)) {
                return from_plan(std::move(*plan));
            }
        }
        return single(gs, action::SkipChargeMove{d->unit}, "no charge move that ends engaged");
    }
    if (const auto* d = std::get_if<decision::OverrunPileIn>(&kind)) {
        // D-057 (M46): the first offered pile-in, by Auto move (D-050), if it stands.
        for (const GameAction& a : legal) {
            if (!std::holds_alternative<action::StartPileIn>(a)) continue;
            const std::string reason = unit_name(gs, d->unit) + ": Overrun pile-in (auto move)";
            if (auto plan = simulate_fight_move(gs, side, a, seed, reason)) return from_plan(std::move(plan->steps));
        }
        return single(gs, action::SkipPileIn{d->unit}, "no pile-in that stands");
    }
    if (std::holds_alternative<decision::PileIn>(kind) || std::holds_alternative<decision::Consolidate>(kind)) {
        // D-057 (M46): the first offered move worth making (D-047, D-049), by Auto move (D-050), if it stands;
        // no weighing. Otherwise the step's end button.
        const bool pile_in = std::holds_alternative<decision::PileIn>(kind);
        std::optional<GameAction> end;
        for (const GameAction& a : legal) {
            if (std::holds_alternative<action::EndPileIns>(a) || std::holds_alternative<action::EndConsolidations>(a)) {
                end = a;
            }
        }
        for (const GameAction& a : legal) {
            std::optional<core::UnitId> unit;
            if (const auto* p = std::get_if<action::StartPileIn>(&a)) unit = p->unit;
            if (const auto* c = std::get_if<action::StartConsolidation>(&a)) unit = c->unit;
            if (!unit || !client::fight_move_worthwhile(gs, side, a)) continue;
            const std::string reason =
                unit_name(gs, *unit) + (pile_in ? ": pile in (auto move)" : ": consolidate (auto move)");
            if (auto plan = simulate_fight_move(gs, side, a, seed, reason)) return from_plan(std::move(plan->steps));
        }
        if (end) return single(gs, *end, pile_in ? "no pile-in worth making" : "no consolidation worth making");
    }
    if (const auto* d = std::get_if<decision::RestoreCoherency>(&kind)) {
        // D-058 (M47): the model the default casualty order would lose first (03.03 Regaining Coherency).
        if (const std::optional<core::ModelId> m = client::default_coherency_removal(gs, d->unit)) {
            const GameAction remove = action::RemoveModelForCoherency{d->unit, *m};
            if (std::find(legal.begin(), legal.end(), remove) != legal.end()) {
                return single(gs, remove, "default order");
            }
        }
    }
    for (const GameAction& t : view.templates) {
        switch (*client::editor_kind(t)) {
            case client::EditorKind::Shot: {
                const auto& sh = std::get<action::Shoot>(t);
                const action::Shoot a = client::ShotPlan(gs, sh.unit, sh.type).expand();
                if (!a.shots.empty()) return single(gs, a);
                break;
            }
            case client::EditorKind::Melee:
                return single(gs, client::MeleePlan(gs, std::get<action::Fight>(t).unit).expand());
            case client::EditorKind::Casualties:
                return single(gs, client::CasualtyPlan(gs, std::get<action::OrderCasualties>(t).target).action(),
                              "default order");
            case client::EditorKind::Path: break;
            case client::EditorKind::Deploy: break;  // D-081: Bot::decide answers DeployUnits (deploy_policy)
        }
    }
    // Prefer charging and fighting over passing when offered.
    for (const GameAction& b : view.buttons) {
        if (std::holds_alternative<action::DeclareCharge>(b) || std::holds_alternative<action::SelectToFight>(b)) {
            return single(gs, b);
        }
    }
    if (!view.buttons.empty()) return single(gs, view.buttons.front());
    // Only templates the policy doesn't fill (a Shoot with nothing to fire): Bot::decide falls back.
    return PolicyResult{legal.front(), "No choice", {}};
}

}  // namespace wh40k::ai::normal
