#pragma once

#include <span>
#include <vector>

#include "core/geometry/coherency.hpp"  // PlacedModel
#include "core/geometry/pose.hpp"       // PosedModel
#include "core/geometry/units.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

// 03.04: a model's engagement range is the area within 2" horizontally (and 5" vertically, trivially
// satisfied in V1) of it.
constexpr float kEngagementRangeMm = 2.0f * MM_PER_INCH;

// 03.04: two opposing models are engaged while one is within the other's engagement range.
// Callers pass models of opposing sides; friend/enemy is not a geometry concern.
[[nodiscard]] bool models_engaged(const PlacedModel& a, const PlacedModel& b);

// 03.04: two opposing units are engaged while any of their models are engaged with each other.
[[nodiscard]] bool units_engaged(std::span<const PlacedModel> unit, std::span<const PlacedModel> enemy);

// 03.04: the models of `unit` that are engaged with at least one model in `enemies`.
// `unit` is unengaged when this is empty.
[[nodiscard]] std::vector<ModelId> engaged_models(std::span<const PlacedModel> unit,
                                                  std::span<const PlacedModel> enemies);

// D-074 (M64): the same for models of any base shape with a facing, measured with base_distance(BasePose, BasePose).
// Round bases give the same answers as the functions above.
[[nodiscard]] bool models_engaged(const PosedModel& a, const PosedModel& b);
[[nodiscard]] bool units_engaged(std::span<const PosedModel> unit, std::span<const PosedModel> enemy);
[[nodiscard]] std::vector<ModelId> engaged_models(std::span<const PosedModel> unit,
                                                  std::span<const PosedModel> enemies);

}  // namespace wh40k::core
