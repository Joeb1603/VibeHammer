#pragma once

// 06.01 Visibility: line of sight between two models (D-017).

#include <span>

#include "core/geometry/coherency.hpp"  // PlacedModel
#include "core/geometry/pose.hpp"       // PosedModel
#include "core/geometry/terrain_shape.hpp"

namespace wh40k::core {

// 06.01: the line of sight is an imaginary straight line 1 mm wide.
inline constexpr float kLineOfSightWidthMm = 1.0f;

// 06.01: can a 1 mm wide straight line be drawn from any part of `observer`'s base to any part of
// `observed`'s base without passing through any of `blockers`? The caller passes only the models
// that can block: 06.01 ignores other models in the observer's unit and in the observed model's
// unit. Terrain (13.07) is the M88 overload below. A blocker overlapping the line by no more than EPS_MM does not
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

// M88 (D-088): the same with terrain shapes that block lines of sight too (13.10 obscuring areas, 13.11 Dense
// features: the caller picks them). A shape blocks a line that comes within 0.5 mm - EPS_MM of it, as a model base
// does. When no shape is near the hull of the two bases this is the overload above, unchanged. Otherwise:
//   1. the centre-to-centre line;
//   2. an exact early "no": a shape that touches neither base's bounding circle but crosses both outer common tangents
//      of those circles cuts every line between the bases;
//   3. D-017's candidate lines (every pair of sample points, every common tangent of two of the shapes) plus the
//      tangents that involve the near shapes: each polygon vertex as a 0.5 mm circle (a corner of the widened
//      polygon) and each terrain circle widened by 0.5 mm.
// Like D-017, not a proof of exactness.
[[nodiscard]] bool has_line_of_sight(const PosedModel& observer, const PosedModel& observed,
                                     std::span<const PosedModel> blockers,
                                     std::span<const TerrainShape* const> terrain);

// M88 (D-088), 06.01 Model Fully Visible, for 13.08: every part of `observed` facing `observer` can be seen past the
// `terrain` shapes (models don't count: 13.08 asks whether terrain stops it being fully visible). The part facing the
// observer is the boundary points whose outward normal is within 90 degrees of the direction to the observer's
// centre, sampled at 9 points (every 20 degrees from -80 to 80). A point is seen when a 1 mm line reaches it from the
// observer: the lines through it and each of the observer's sample points, and its tangents through each polygon
// vertex (as a 0.5 mm circle) and each widened terrain circle. With no shape near the two bases it is true. Not a
// proof of exactness.
[[nodiscard]] bool fully_visible_past(const PosedModel& observer, const PosedModel& observed,
                                      std::span<const TerrainShape* const> terrain);

namespace visibility_detail {
// M65: D-017's code run on posed models that all have round bases (the posed overload's round case, without copying
// the blockers into PlacedModels). Precondition: every base is round.
[[nodiscard]] bool round_line_of_sight(const PosedModel& observer, const PosedModel& observed,
                                       std::span<const PosedModel> blockers);
}  // namespace visibility_detail

}  // namespace wh40k::core
