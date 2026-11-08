#pragma once

// 06.01 Visibility: line of sight between two models (D-017).

#include <span>

#include "core/geometry/coherency.hpp"  // PlacedModel
#include "core/geometry/pose.hpp"       // PosedModel

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

// D-074 (M64): the same for models of any base shape with a facing (visibility_oval.cpp). With round bases only it
// is the function above (bit-identical). With an oval anywhere:
// - a line crosses an oval base along a closed-form chord, and an oval blocker blocks a line that comes within
//   0.5 mm - EPS_MM of it (it overlaps the 1 mm line by more than EPS_MM);
// - the candidate lines are D-017's: centre to centre, every pair of sample points on the two bases (64 on an oval,
//   32 on a round base, plus the centre), and every common tangent of two of the shapes (closed form for two circles,
//   the support-line tangents found by root bracketing otherwise).
[[nodiscard]] bool has_line_of_sight(const PosedModel& observer, const PosedModel& observed,
                                     std::span<const PosedModel> blockers);

}  // namespace wh40k::core
