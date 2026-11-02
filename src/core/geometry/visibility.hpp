#pragma once

// 06.01 Visibility: line of sight between two models (D-017).

#include <span>

#include "core/geometry/coherency.hpp"  // PlacedModel

namespace wh40k::core {

// 06.01: the line of sight is an imaginary straight line 1 mm wide.
inline constexpr float kLineOfSightWidthMm = 1.0f;

// 06.01: can a 1 mm wide straight line be drawn from any part of `observer`'s base to any part of
// `observed`'s base without passing through any of `blockers`? The caller passes only the models
// that can block: 06.01 ignores other models in the observer's unit and in the observed model's
// unit. V1 has no terrain (13.07). A blocker overlapping the line by no more than EPS_MM does not
// block it (tolerance policy). D-017 describes the candidate lines tested.
[[nodiscard]] bool has_line_of_sight(const PlacedModel& observer, const PlacedModel& observed,
                                     std::span<const PlacedModel> blockers);

}  // namespace wh40k::core
