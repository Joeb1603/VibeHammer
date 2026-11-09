#pragma once

// D-081 (M74): deployment by hand. The units not yet set up are in GameState::undeployed (not on the battlefield, so
// not in `units`); during Stage::Deploy the sides alternate setting one up (DeployUnits, answered by DeployUnit or, when
// the engine finds no legal spot, CannotSetUp). Read-only queries for the engine, the client logic (the placement
// editor, M76) and the bots. Deterministic: functions of the state only.

#include <optional>
#include <string>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

// The unit with this id waiting to be set up, or nullptr.
[[nodiscard]] const Unit* find_undeployed(const GameState& state, UnitId id);

// D-081: the side that sets up its next unit. The side taking the second turn first; the sides alternate while both
// have units left (the one that has handled no more units than the other goes, S before F on a tie); then the side with
// units left sets up the rest. A unit handled is one in `units` (set up, or not set up and destroyed), so a unit that
// can't be set up uses its side's turn. nullopt when no unit is waiting to be set up.
[[nodiscard]] std::optional<Side> side_to_deploy(const GameState& state);

// D-081 / 03.02: why `side` can't set up `a.unit` with `a.models`; empty when it can. Every reason is rule-cited, and
// every problem found is listed. In order: the unit is `side`'s and waiting to be set up; each of its models is placed
// exactly once, at a finite position with z 0, with a finite facing (0 for a round base, D-074); each base is wholly on
// the battlefield (03.02) and wholly within its side's deployment zone (D-009); no model is on another, in the unit or
// already on the battlefield (03.01 Ending a Move, D-064); the unit is in coherency (03.03); it is unengaged (03.02,
// 03.04). Units not yet set up are not on the battlefield and are ignored.
[[nodiscard]] std::vector<std::string> set_up_problems(const GameState& state, Side side, const action::DeployUnit& a);

// D-081: why `side` can't declare `unit` as not set up (CannotSetUp); empty when it can: the unit is `side`'s, waiting
// to be set up, and propose_set_up finds no legal spot for it.
[[nodiscard]] std::vector<std::string> cannot_set_up_problems(const GameState& state, Side side, UnitId unit);

// D-081: the engine's placement for a unit waiting to be set up, or nullopt when it finds no legal spot ("no legal spot
// left": then the unit may be declared not set up). Always a legal DeployUnit (set_up_problems is empty for it).
//   1. The unit's slot in the auto-layout (D-079 part 2: layout_zone over the bases of its side's units in id order, set
//      up or not, units destroyed left out), if that is legal now. For a custom battle these are exactly the positions
//      Auto-place gives.
//   2. Otherwise the first legal spot of a scan: ranks from the fewest that make the formation coherent and fit the
//      zone upward (gap 0, facing 0); the depth behind the zone's front line from 0 in 1/4" steps; the formation's
//      centre from the battlefield's middle outward in 1/4" steps, west before east.
// Ovals are proposed at facing 0 only. North and south zones only (no proposal otherwise).
[[nodiscard]] std::optional<action::DeployUnit> propose_set_up(const GameState& state, UnitId unit);

// D-081: propose_set_up for every unit of `side` waiting to be set up, in id order, sharing one auto-layout and one
// view of the battlefield (what legal_actions lists).
struct SetUpProposal {
    UnitId unit;
    std::optional<action::DeployUnit> action;  // nullopt: no legal spot (CannotSetUp)
    friend bool operator==(const SetUpProposal&, const SetUpProposal&) = default;
};
[[nodiscard]] std::vector<SetUpProposal> deployment_proposals(const GameState& state, Side side);

}  // namespace wh40k::core
