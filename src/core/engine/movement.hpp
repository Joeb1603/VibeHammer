#pragma once

// Movement queries for clients and tests (09.05-09.07, 03.01). The move itself runs through the
// engine actions StartMove, ProposeModelMove, CommitUnitMove and ResetUnitMove.

#include <optional>
#include <string>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/geometry/pose.hpp"
#include "core/geometry/primitives.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::core {

// The maximum distance `model` may move in the move in progress (its M, plus the advance roll for an
// Advance move; Rules Appendix: Different Move Characteristics). nullopt if the model isn't moving.
[[nodiscard]] std::optional<float> max_move_distance_mm(const GameState& state, ModelId model);

// The position `model` started the move in progress from. nullopt if the model isn't moving.
[[nodiscard]] std::optional<Position> move_start_position(const GameState& state, ModelId model);

// D-074 part 2 (M65): the facing `model` started the move in progress with (0 for a round base). nullopt if the model
// isn't moving.
[[nodiscard]] std::optional<float> move_start_facing_deg(const GameState& state, ModelId model);

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
//   enemy models (09.07 Desperate Escape). 17.01 (D-073): in a Normal or Advance move a MONSTER/VEHICLE model
//   has blocking circles only round enemy MONSTER/VEHICLE models (it can't end on the others: `forbidden`).
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
// D-074 part 2 (M65): one of MoveReach's areas for a pair of bases that isn't round to round (an oval mover, or an
// oval other model). The moving model's centre c is in it when (reach_shape_contains)
//     signed_base_distance(BasePose{c, mover_base, mover_facing_deg}, obstacle) < threshold_mm.
// A round pair's ReachCircle is exactly that set, with radius both base radii + threshold_mm. The thresholds: 0 (not on
// the model, 03.01), 2" + EPS (an engagement range, 03.04), `before - EPS` (closer, 11.04 / 12.03 / 12.08), -EPS
// (blocking: the line's swept base would overlap the model by more than EPS, translation_crosses).
// - At a fixed facing the set is convex (the signed distance to a convex set, the Minkowski difference of the two
//   bases, is a convex function of c), so on any ray it is one interval, possibly empty: bisection on
//   reach_shape_contains finds its ends.
// - It contains the disc round obstacle.position of radius reach_shape_inner_radius_mm and lies inside the disc of
//   radius reach_shape_outer_radius_mm (for culling and bisection brackets).
// - A blocking shape's shadow (D-044) is, on each ray from MoveReach::from, everything beyond the ray's first point in
//   the shape.
struct ReachShape {
    Base mover_base;
    float mover_facing_deg = 0.0f;
    BasePose obstacle;
    float threshold_mm = 0.0f;
    bool engagement = false;  // an enemy model's engagement range, not just its base
    ModelId model;            // the model this shape is round
    friend bool operator==(const ReachShape&, const ReachShape&) = default;
};
[[nodiscard]] bool reach_shape_contains(const ReachShape& shape, const Position& centre);
// inscribed_radius(mover) + inscribed_radius(obstacle) + threshold: the shape holds this disc (when positive).
[[nodiscard]] float reach_shape_inner_radius_mm(const ReachShape& shape);
// circumscribed_radius(mover) + circumscribed_radius(obstacle) + threshold: the shape lies inside this disc.
[[nodiscard]] float reach_shape_outer_radius_mm(const ReachShape& shape);

struct MoveReach {
    Position start;
    float max_mm = 0.0f;
    Position from;
    float remaining_mm = 0.0f;
    Rect centre_bounds;  // the battlefield shrunk by the model's base (D-074: its half extents at `facing_deg`)
    std::vector<ReachCircle> forbidden;
    std::vector<std::vector<ReachCircle>> required_any;
    std::vector<ReachCircle> blocking;
    bool must_end_unengaged = false;
    bool immobile = false;
    // D-074 part 2 (M65). With only round bases every field below keeps its default, so the reach is as before.
    // - facing_deg: the facing the reach is for: the model turns at `from` to it, then moves (0 for a round base).
    // - turn_blocked: that turn would pass through an enemy model the line may not pass, or cross the battlefield edge
    //   (03.01), so nothing is reachable at this facing.
    // - the *_shapes: the same areas as ReachShapes for each pair that isn't round to round. required_any_shapes is
    //   empty (no group has a shape) or holds one group per required_any group: group i is required_any[i] and
    //   required_any_shapes[i] together.
    float facing_deg = 0.0f;
    bool turn_blocked = false;
    std::vector<ReachShape> forbidden_shapes;
    std::vector<std::vector<ReachShape>> required_any_shapes;
    std::vector<ReachShape> blocking_shapes;
    friend bool operator==(const MoveReach&, const MoveReach&) = default;
};
// At the model's current facing.
[[nodiscard]] std::optional<MoveReach> move_reach(const GameState& state, ModelId model);
// D-074 part 2: at `facing_deg` (the facing being dragged; any value, taken modulo 180; a round base ignores it).
[[nodiscard]] std::optional<MoveReach> move_reach(const GameState& state, ModelId model, float facing_deg);

// 17.01 (D-073): "Each time you make a normal or advance move with a unit, MONSTER/VEHICLE models in that unit can
// be moved through friendly and enemy models (excluding other MONSTER/VEHICLE models)." True when a move of `type`
// lets `model` of `unit` pass through enemy models that are not MONSTER/VEHICLE models: a Normal (09.05) or Advance
// (09.06) move of a MONSTER/VEHICLE model (is_monster_or_vehicle, a unit keyword). Friendly models it may pass in any
// move (03.01), friendly MONSTER/VEHICLE models included (D-073).
[[nodiscard]] bool moves_through_non_monster_enemies(MoveType type, const Unit& unit, const Model& model);

// D-047: the state just after `side` starts the pile-in (12.03) or consolidation (12.08) `start`, with the
// move in progress, so move_reach can be asked about a move before it is made. nullopt if `start` is not a
// StartPileIn or StartConsolidation, or is not valid now. Starting these moves rolls no dice. Read-only:
// `state` is not changed.
[[nodiscard]] std::optional<GameState> preview_fight_move(const GameState& state, Side side,
                                                          const GameAction& start);

// What would make CommitUnitMove return the models to their start positions right now (03.01 Ending a
// Move, 03.03 coherency, the move type's After Moving conditions). Rule-cited; empty means the move
// can end. Empty if no move is in progress.
[[nodiscard]] std::vector<std::string> move_end_problems(const GameState& state);

}  // namespace wh40k::core
