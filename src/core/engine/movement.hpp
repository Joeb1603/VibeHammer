#pragma once

// Movement queries for clients and tests (09.05-09.07, 03.01). The move itself runs through the
// engine actions StartMove, ProposeModelMove, CommitUnitMove and ResetUnitMove.

#include <optional>
#include <string>
#include <vector>

#include "core/geometry/primitives.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::core {

// The maximum distance `model` may move in the move in progress (its M, plus the advance roll for an
// Advance move; Rules Appendix: Different Move Characteristics). nullopt if the model isn't moving.
[[nodiscard]] std::optional<float> max_move_distance_mm(const GameState& state, ModelId model);

// The position `model` started the move in progress from. nullopt if the model isn't moving.
[[nodiscard]] std::optional<Position> move_start_position(const GameState& state, ModelId model);

// D-040: where `model`'s base centre can end the move in progress, for clients to shade. A circle round the
// start (03.01: measured from the same point on the base) of the move's maximum distance, inside the
// battlefield (03.01: the base can't cross its edge), outside every `forbidden` circle: other models' bases
// (03.01 Ending a Move: not on another model) and, for moves that must end unengaged (Normal, Advance,
// Fall Back: 09.05-09.07 After Moving), each enemy model's engagement range (03.04). Ending inside the area
// does not make the path legal (enemy bases can't be crossed) or the unit coherent (03.03); the engine
// still checks both. nullopt if the model isn't moving, or for a charge move (it has its charge ring).
struct ReachCircle {
    Position centre;
    float radius_mm = 0.0f;  // for the moving model's centre: base radii (+ engagement range) added
    bool engagement = false; // an enemy model's engagement range, not just its base
    friend bool operator==(const ReachCircle&, const ReachCircle&) = default;
};
struct MoveReach {
    Position start;
    float max_mm = 0.0f;
    Rect centre_bounds;  // the battlefield shrunk by the model's base radius
    std::vector<ReachCircle> forbidden;
    bool must_end_unengaged = false;
    friend bool operator==(const MoveReach&, const MoveReach&) = default;
};
[[nodiscard]] std::optional<MoveReach> move_reach(const GameState& state, ModelId model);

// What would make CommitUnitMove return the models to their start positions right now (03.01 Ending a
// Move, 03.03 coherency, the move type's After Moving conditions). Rule-cited; empty means the move
// can end. Empty if no move is in progress.
[[nodiscard]] std::vector<std::string> move_end_problems(const GameState& state);

}  // namespace wh40k::core
