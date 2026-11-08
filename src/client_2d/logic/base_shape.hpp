#pragma once

// D-074 part 3 (M66): oval bases in the client logic, as geometry on core's BasePose (no rules, D-024).
// - The outline of a base at its facing and the ring a distance out from its edge (01.04 edge to edge): for an oval
//   that ring is the ellipse's offset curve, not a scaled ellipse. For drawing, hit-testing and the ring outlines.
// - The swept area of a turn about the centre (03.01 Rotating a Model), for the turn preview while dragging.
// - D-075: contact spots round a base (the spot where a moving base is a given gap from it along a direction) and
//   how many directions to sample round a big base, for the auto moves (D-050, D-056).
// Round bases keep today's arithmetic wherever a result feeds a decision.

#include <optional>
#include <string>
#include <vector>

#include "core/geometry/pose.hpp"
#include "core/geometry/primitives.hpp"
#include "directions.hpp"

namespace wh40k::client {

// D-074: the major axis of an oval at `facing_deg` is (-sin f, cos f) (at 0 it lies along y, south to north).
struct BaseAxes {
    double ux = 0.0;  // major axis (unit)
    double uy = 1.0;
    double vx = 1.0;  // minor axis (unit), the major axis turned -90 degrees
    double vy = 0.0;
};
[[nodiscard]] BaseAxes base_axes(float facing_deg);

// The base's edge as `segments` points counter-clockwise (world, +y north), starting at the +major end.
[[nodiscard]] std::vector<core::Position> base_outline(const core::BasePose& pose, int segments = 64);

// The line `offset_mm` out from the base's edge: every point at that distance from it (01.04). For an oval it is the
// ellipse's offset curve e(t) + offset * n(t), n the outward normal. `segments` points counter-clockwise.
[[nodiscard]] std::vector<core::Position> offset_outline(const core::BasePose& pose, float offset_mm,
                                                         int segments = 96);

// Hit-testing: `p` is on the base at its facing (inside it or on its edge). A round base is the old test,
// dx * dx + dy * dy <= r * r.
[[nodiscard]] bool base_contains(const core::BasePose& pose, const core::Position& p);

// `p` is no more than `offset_mm` from the base's edge (inside the offset outline, or on the base).
[[nodiscard]] bool offset_contains(const core::BasePose& pose, float offset_mm, const core::Position& p);

// 03.01 Rotating a Model: the area the base sweeps turning about its centre from `from_deg` to `to_deg` the shorter
// way (rotation_delta_deg), as the farthest the base reaches in each of `segments` directions (exact per
// direction: the union of ellipses with a common centre is star-shaped round it). A round base gives its circle.
[[nodiscard]] std::vector<core::Position> turn_swept_outline(const core::Base& base, const core::Position& centre,
                                                             float from_deg, float to_deg, int segments = 144);

// D-075: the base's perimeter (mm): pi * d for a round base, Ramanujan's second formula for an oval.
[[nodiscard]] double base_perimeter_mm(const core::Base& base);

// D-075 finer contact sampling: the number of directions to sample round `target` so that the spacing along its
// perimeter is no worse than 72 directions round a 32 mm base: 72 * ceil(perimeter / (pi * 32 mm)), so 72 for every
// base up to 32 mm (as before M66).
inline constexpr int kContactDirections = 72;
inline constexpr float kContactReferenceDiameterMm = 32.0f;
[[nodiscard]] int contact_direction_count(const core::Base& target);

// D-075: `count` unit vectors from (1, 0) counter-clockwise, evenly spaced: for 72 the kDirections72 table itself,
// otherwise from core's platform-independent sine and cosine (oval_detail::sin_cos_deg).
[[nodiscard]] std::vector<UnitVector> direction_table(int count);

// D-074 part 3: the spot along `dir` from `target`'s centre where a moving base (`mover`, facing `mover_facing_deg`)
// is `gap_mm` from it (01.04 edge to edge; negative overlaps). The bases within that gap of each other form a convex
// area round the target's centre, so the ray crosses its edge once; the spot is the outside end of a bisection on
// core's signed_base_distance, within 1e-4 mm, so it is never closer than `gap_mm`. nullopt when no spot along the ray
// is that close (`gap_mm` below minus the inscribed radii). Round pairs use the old closed form,
// centre + (r1 + r2 + gap) * dir.
[[nodiscard]] std::optional<core::Position> ring_spot(const core::BasePose& target, const core::Base& mover,
                                                      float mover_facing_deg, UnitVector dir, float gap_mm);

// D-074 part 3 / D-075: `count` spots round `target` where a moving base (`mover`, facing `mover_facing_deg`) is
// `gap_mm` from it, evenly round the target. A round pair: the old closed form along kDirections72 or direction_table
// (centre + (r1 + r2 + gap) * dir). A round base and an oval: exact and closed form, evenly in the ellipse's parameter:
// by central symmetry the moving centre is r + gap from the oval centred on the target (whichever of the two is the
// oval), so it lies on that oval's offset curve (offset_outline at r + gap; never closer than the gap but for float
// rounding, far below EPS_MM). Two ovals: ring_spot along each direction.
[[nodiscard]] std::vector<std::optional<core::Position>> contact_ring(const core::BasePose& target, const core::Base& mover,
                                                                      float mover_facing_deg, int count, float gap_mm);

// D-074 part 3: how far the moving base (`mover` at `from`, facing `mover_facing_deg`) can go straight towards
// `target`'s centre before it is `gap_mm` from the target: the outside end of a bisection on the line (the line's
// start is outside, its end at the target's centre inside, and the set is convex). 0 when it starts within the gap.
// Round pairs use the old closed form, centre_distance - r1 - r2 - gap.
[[nodiscard]] float distance_until_gap(const core::BasePose& mover_at_from, const core::BasePose& target, float gap_mm);

// D-075 (M66): how a damaged model shows its wounds on the board. Up to W 6 one pip per wound, filled for the wounds
// it has left (D-028, as before); past W 6 the pips can't be told apart, so a bar filled to the wounds left with
// "7/12" on it. A model at full wounds shows nothing (the tooltip gives the numbers).
inline constexpr int kMaxWoundPips = 6;
struct WoundDisplay {
    enum class Kind { None, Pips, Bar };
    Kind kind = Kind::None;
    int left = 0;      // wounds remaining (never below 0)
    int total = 0;     // the W characteristic
    std::string text;  // "7/12" for a bar
    [[nodiscard]] float fraction() const noexcept {
        return total > 0 ? static_cast<float>(left) / static_cast<float>(total) : 0.0f;
    }
};
[[nodiscard]] WoundDisplay wound_display(int wounds_remaining, int wounds);

}  // namespace wh40k::client
