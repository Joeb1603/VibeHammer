// D-081 (M74): the deployment step. The sides alternate setting up one unit at a time (DeployUnits), starting with the
// side that takes the second turn; a unit with no legal spot left counts as destroyed (CannotSetUp). The rules for a
// set-up and the engine's proposals are the read-only queries in core/state/deployment.hpp.

#include <algorithm>
#include <utility>

#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"
#include "core/geometry/pose.hpp"
#include "core/state/deployment.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core::detail {

namespace {

ValidationResult from_problems(std::vector<std::string> problems) {
    ValidationResult r;
    r.reasons = std::move(problems);
    return r;
}

// Takes unit `id` out of `undeployed` and puts it into `units` at its id position (`units` stays in id order).
Unit& move_into_units(GameState& state, UnitId id) {
    const auto from = std::find_if(state.undeployed.begin(), state.undeployed.end(),
                                   [id](const Unit& u) { return u.id == id; });
    Unit unit = std::move(*from);
    state.undeployed.erase(from);
    const auto at = std::upper_bound(state.units.begin(), state.units.end(), id,
                                     [](UnitId value, const Unit& u) { return value < u.id; });
    return *state.units.insert(at, std::move(unit));
}

}  // namespace

void start_first_battle_round(GameState& state, DiceSource& dice, Events& events) {
    state.flow.battle_round = 1;
    state.flow.turn_in_round = 0;
    enter_stage(state, Stage::StartOfBattleRound, dice, events);
}

std::optional<PendingDecision> deploy_decision(const GameState& state) {
    if (const std::optional<Side> side = side_to_deploy(state)) return PendingDecision{*side, decision::DeployUnits{}};
    return std::nullopt;
}

// D-004: for the deploying side's units waiting to be set up, in id order, one fully specified action each (its
// proposal, or CannotSetUp when the engine finds no legal spot), then one template each (every model's place open).
std::vector<GameAction> legal_for(const GameState& state, const decision::DeployUnits&) {
    std::vector<GameAction> out;
    if (!state.pending) return out;
    const std::vector<SetUpProposal> proposals = deployment_proposals(state, state.pending->side);
    out.reserve(proposals.size() * 2);
    for (const SetUpProposal& p : proposals) {
        if (p.action) {
            out.push_back(*p.action);
        } else {
            out.push_back(action::CannotSetUp{p.unit});
        }
    }
    for (const SetUpProposal& p : proposals) out.push_back(action::DeployUnit{p.unit, {}});
    return out;
}

ValidationResult validate_details(const GameState& state, Side side, const action::DeployUnit& a) {
    return from_problems(set_up_problems(state, side, a));
}

ValidationResult validate_details(const GameState& state, Side side, const action::CannotSetUp& a) {
    return from_problems(cannot_set_up_problems(state, side, a.unit));
}

// 03.02: the unit's models are placed; it is on the battlefield from now on.
void apply_details(GameState& state, Side side, const action::DeployUnit& a, DiceSource&, Events& events) {
    Unit& unit = move_into_units(state, a.unit);
    for (const ModelPlacement& p : a.models) {
        Model* m = find_model(unit, p.model);
        m->position = p.position;
        // D-074: a round base has no facing; an oval's is stored modulo 180.
        m->facing_deg = m->base.shape == BaseShape::Round ? 0.0f : footprint_facing_deg(p.facing_deg);
    }
    events.push_back(event::UnitSetUp{a.unit, side});
}

// D-081: no legal spot was left, so the unit is not set up and counts as destroyed (Rules Appendix: Destroyed): it goes
// into `units` with no models. Its points, starting wounds and starting strength stay as built, so the opponent scores
// its full points (D-076).
void apply_details(GameState& state, Side side, const action::CannotSetUp& a, DiceSource&, Events& events) {
    Unit& unit = move_into_units(state, a.unit);
    unit.models.clear();
    events.push_back(event::UnitNotSetUp{a.unit, side});
    events.push_back(event::UnitDestroyed{a.unit});
}

}  // namespace wh40k::core::detail
