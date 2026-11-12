#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/geometry/coherency.hpp"  // PlacedModel
#include "core/geometry/pose.hpp"       // PosedModel
#include "core/geometry/primitives.hpp"
#include "core/geometry/terrain_shape.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

// 03.01: a model moves in straight lines (rotation is free). A path is its base-centre positions:
// points[0] is where the model starts, each later point ends one straight-line segment.
// Measuring the same point on the base at start and end of each line = measuring the centre.

// 03.01 Moving a Model in a Straight Line: total distance moved is the sum of the segment lengths.
[[nodiscard]] float path_length(std::span<const Position> points);

enum class PathViolationKind : std::uint8_t {
    CrossesEnemyModel,  // 03.01: its base cannot be moved through enemy models
    LeavesBattlefield,  // 03.01: its base cannot cross the edge of the battlefield
    EndsOnModel,        // 03.01 Ending a Move: no model may be on another model
    // D-074 (M64): an oval's turn in place (03.01 Rotating a Model) hits an enemy model or crosses the edge.
    TurnCrossesEnemyModel,
    TurnLeavesBattlefield,
    // M88 (D-087): 13.06 a model that can't move through a Dense feature over 2" tall crosses one (a straight line or,
    // for an oval, a turn), or 03.01 Ending a Move: it ends partway through a Light or Dense feature.
    CrossesTerrain,
    TurnCrossesTerrain,
    EndsOnTerrain,
};

struct PathViolation {
    PathViolationKind kind = PathViolationKind::CrossesEnemyModel;
    std::optional<ModelId> other;        // the model crossed or ended on, if any
    std::optional<std::size_t> segment;  // 0-based segment index, where relevant
    std::string reason;                  // rule-cited, e.g. "03.01: model 3's base ..."
};

struct PathContext {
    Rect battlefield;
    // Other models of the moving model's army (including its own unit, at their current/final
    // positions). The path may pass through them but must not end on them.
    std::span<const PlacedModel> friendly;
    // Enemy models the path may not pass through (unless allowed below) or end on.
    std::span<const PlacedModel> enemy;
    // Some move types let a model move through enemy models (e.g. Desperate Escape, 09.07).
    bool may_move_through_enemy_models = false;
    // 17.01 (D-073): enemy models this model may be moved through but not end on (a MONSTER/VEHICLE model's
    // Normal or Advance move passes enemy models that are not MONSTER/VEHICLE models). Empty for every other move.
    std::span<const PlacedModel> passable_enemy{};
};

// Checks one model's path against 03.01 (movement restrictions and "on another model"). Does not
// check move distance, coherency or engagement; those depend on the move type and the whole unit.
// `points` must not be empty (std::logic_error otherwise). Models with the mover's id in the
// context spans are ignored. Returns every violation found; empty means the path is legal.
[[nodiscard]] std::vector<PathViolation> check_model_path(ModelId mover, const Base& base,
                                                          std::span<const Position> points,
                                                          const PathContext& context);

// M88 (D-087): a terrain feature a move must respect, with its name for the reasons (e.g. "'Ruin wall' (dense, 5\"
// tall)"). The shape is the caller's.
struct PathFeature {
    const TerrainShape* shape = nullptr;
    std::string label;
};

// D-074 (M64): PathContext for posed models.
struct PosedPathContext {
    Rect battlefield;
    std::span<const PosedModel> friendly;
    std::span<const PosedModel> enemy;
    bool may_move_through_enemy_models = false;
    std::span<const PosedModel> passable_enemy{};
    // M88 (D-087): 13.06 the features the model can't move through (Dense, over 2" tall, for a model that isn't
    // INFANTRY/BEASTS/SWARM/MOBILE; a Desperate Escape too), and 03.01 the features it can't end on (Light and Dense).
    // Empty without terrain: the result is as before.
    std::span<const PathFeature> uncrossable{};
    std::span<const PathFeature> no_end{};
};

// D-074 (M64): check_model_path for a base of any shape with a facing at each path point.
// - `facings[i]` is the facing at `points[i]` (empty: 0 everywhere; otherwise it must be as long as `points`,
//   std::logic_error if not).
// - Between points i and i+1 the model first turns in place at points[i] from facings[i] to facings[i+1] (the
//   shorter way, 03.01 Rotating a Model), then moves in a straight line to points[i+1] keeping facings[i+1].
// - Each turn and each straight line is checked against the enemy models (rotation_against_base,
//   translation_crosses) and the battlefield edge (rotation_against_edge, wholly_within); the end against every model
//   (bases_overlap, D-027). A turn that hits reports TurnCrossesEnemyModel / TurnLeavesBattlefield for segment i.
// - A round base ignores facings: without terrain its result is exactly check_model_path's above (the same checks,
//   violations and reasons, in the same order).
// - M88 (D-087): after those, each turn and straight line against every `uncrossable` feature (rotation_against_shape,
//   translation_crosses_shape; CrossesTerrain / TurnCrossesTerrain, one report per feature) and the end against every
//   `no_end` feature (overlaps_shape; EndsOnTerrain).
[[nodiscard]] std::vector<PathViolation> check_model_path(ModelId mover, const Base& base,
                                                          std::span<const Position> points,
                                                          std::span<const float> facings,
                                                          const PosedPathContext& context);

}  // namespace wh40k::core
