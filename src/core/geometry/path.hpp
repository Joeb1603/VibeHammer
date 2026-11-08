#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/geometry/coherency.hpp"  // PlacedModel
#include "core/geometry/primitives.hpp"
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

}  // namespace wh40k::core
