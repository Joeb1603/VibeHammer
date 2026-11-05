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

// D-040 / D-041: where `model`'s base centre can end the move in progress, for clients to shade.
// - The next straight line starts at `from` (its position now) and can be up to `remaining_mm` long (the
//   move's maximum distance less what its path has used, 03.01). The whole move is measured from `start`.
// - Inside the battlefield (03.01: the base can't cross its edge): `centre_bounds`.
// - Outside every `forbidden` circle: other models' bases (03.01 Ending a Move: not on another model) and,
//   for moves that must end unengaged (Normal, Advance, Fall Back: 09.05-09.07 After Moving), each enemy
//   model's engagement range (03.04).
// - Inside at least one circle of every `required_any` group: for pile-in and consolidation moves (12.03,
//   12.08 Ongoing / Engaging), closer to the closest target (D-022: any unit tied for closest counts) and
//   still engaged with each enemy unit the model started engaged with.
// - `immobile`: it can't be moved (12.03 / 12.08 Ongoing: in base contact with an enemy model).
// - D-044: the next straight line can't pass through a `blocking` circle (03.01: its base cannot be moved
//   through enemy models; as check_model_path, base radii less EPS). None when the move may pass through
//   enemy models (09.07 Desperate Escape).
// Ending inside the area does not make the path legal (enemy bases can't be crossed), the unit coherent
// (03.03) or meet unit-wide conditions (e.g. 12.03 "your unit must be engaged"); the engine still checks
// them. D-046: a charge move (11.04) is shaded too: up to the charge roll, closer to one or more charge
// targets (`required_any`), and not within engagement range of an enemy unit that isn't a target
// (`forbidden`). nullopt if the model isn't moving.
struct ReachCircle {
    Position centre;
    float radius_mm = 0.0f;   // for the moving model's centre: base radii (+ a distance) added
    bool engagement = false;  // an enemy model's engagement range, not just its base
    ModelId model;            // the model this circle is round
    friend bool operator==(const ReachCircle&, const ReachCircle&) = default;
};
struct MoveReach {
    Position start;
    float max_mm = 0.0f;
    Position from;
    float remaining_mm = 0.0f;
    Rect centre_bounds;  // the battlefield shrunk by the model's base radius
    std::vector<ReachCircle> forbidden;
    std::vector<std::vector<ReachCircle>> required_any;
    std::vector<ReachCircle> blocking;
    bool must_end_unengaged = false;
    bool immobile = false;
    friend bool operator==(const MoveReach&, const MoveReach&) = default;
};
[[nodiscard]] std::optional<MoveReach> move_reach(const GameState& state, ModelId model);

// What would make CommitUnitMove return the models to their start positions right now (03.01 Ending a
// Move, 03.03 coherency, the move type's After Moving conditions). Rule-cited; empty means the move
// can end. Empty if no move is in progress.
[[nodiscard]] std::vector<std::string> move_end_problems(const GameState& state);

}  // namespace wh40k::core
