#pragma once

// 14 Objectives and the D-084 mission (M86): read-only queries on GameState::mission. The engine's end-of-phase and
// end-of-turn control checks and the Command phase scoring are in core/engine/objectives.cpp.

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "core/geometry/pose.hpp"
#include "core/model/unit.hpp"
#include "core/state/game_state.hpp"
#include "core/state/mission.hpp"

namespace wh40k::core {

// D-084: bounds on the mission's numbers (the loader and the state JSON refuse anything else as DataError), far
// above any real mission, so no VP total can overflow an int.
inline constexpr std::size_t kMaxObjectives = 32;
inline constexpr int kMaxObjectiveVp = 1000;   // per objective and side, per scoring
inline constexpr int kMaxKillVp = 1000;        // kill VP per kill_points
inline constexpr int kMaxKillPoints = 100000;  // the points per kill VP step

// 14.01 / 14.02 (D-009 "within"): a model is within range of a terrain objective while it is within that terrain area,
// i.e. while any part of its base is within the area. The area is a circle, so this is the base's edge-to-edge
// distance to a round "base" the size of the area, at most 0 (EPS_MM tolerance). Works for round and oval bases.
[[nodiscard]] bool model_within_objective(const Model& model, const ObjectiveArea& area);
// The same for a base at any pose (M87: the bot tests a planned move's end positions), and the gap behind it: the
// edge-to-edge distance between the base and the area (0 when they overlap).
[[nodiscard]] bool within_objective(const BasePose& pose, const ObjectiveArea& area);
[[nodiscard]] float objective_gap_mm(const BasePose& pose, const ObjectiveArea& area);

// 14.02: each player's level of control over the area: the sum of the OC characteristics of their models within range
// of it. A model with OC '-' (including every model of a battle-shocked unit, 01.07) adds nothing.
[[nodiscard]] std::array<int, 2> level_of_control(const GameState& state, const ObjectiveArea& area);

// 14.02 / 14.03: who controls the area after a check at the end of a phase (`end_of_phase`) or turn, given both levels
// of control, and whether it stays secured.
//   - Not secured: the player with the higher level controls it; equal levels: neither.
//   - Secured by a player: it stays under their control (even with no units in range, and on equal levels) until
//     their opponent's level is greater than theirs at the end of a phase; then the opponent controls it and it is no
//     longer secured. A check at the end of a turn alone does not end it (14.03 says "at the end of a phase").
struct ControlOutcome {
    std::optional<Side> controller;
    std::optional<Side> secured_by;
    friend bool operator==(const ControlOutcome&, const ControlOutcome&) = default;
};
[[nodiscard]] ControlOutcome determine_control(const ObjectiveArea& area, const std::array<int, 2>& levels,
                                               bool end_of_phase);

// 14.02: the units that are "controlling" objective `index`: within range of it, from the army of the player that
// controls it, with one or more models with an OC of 1 or more. Empty when no player controls it. In UnitId order.
[[nodiscard]] std::vector<UnitId> controlling_units(const GameState& state, std::size_t index);

// D-084: kill VP per side: kill_vp for each full kill_points points the side has destroyed (D-076's exact tally,
// floor(destroyed / (denominator x kill_points)), computed as two integer divisions so nothing overflows). Zero
// without a mission.
[[nodiscard]] std::array<int, 2> kill_vp(const GameState& state);

// D-084: both sides' objective VP so far, their kill VP now, and the sums. Precondition: state.mission.
[[nodiscard]] VpTally vp_tally(const GameState& state);

// D-084: refuses (DataError, naming `where`) a mission state that breaks the format: "result" mission without a
// mission or a mission without it, a first scoring round outside 1..battle_rounds, kill numbers out of range, no
// objectives or more than kMaxObjectives, an objective with an empty name, a diameter of 0 or less or not wholly on
// the battlefield, or VP out of 0..kMaxObjectiveVp, and negative VP scored.
void check_mission_data(const GameState& state, std::string_view where);

}  // namespace wh40k::core
