#pragma once

// D-074 (M64): the geometry kernel for bases of any shape (round or oval) with a facing. Pure functions; until M65
// nothing in the engine calls them (callers still use the round-only functions in distance.hpp and path.hpp, and
// base_radius() still throws for ovals).
//
// - Every function takes the round-round case first, with the old float code, so its results are bit-identical
//   to the round-only functions (D-074 tests compare them over random poses).
// - The oval code works in double and returns float like the rest. Only + - * / and sqrt are used on the decision
//   paths (sine and cosine are polynomials here), with capped iterations, so the same inputs give the same output on
//   every platform, and valid inputs never give NaN or throw.
// - Error bounds (D-074): round-oval distances are exact to double rounding (Eberly's bisection); oval-oval, swept and
//   rectangle distances come from GJK (separated) or a support-polygon bracket (overlapping), certified and refined
//   to 1e-9 mm.

#include <cmath>

#include "core/geometry/pose.hpp"
#include "core/geometry/primitives.hpp"

namespace wh40k::core {

// ---- 01.04 Measuring Distances ----

// 01.04: the distance between the closest parts of two bases, clamped at 0. Round-round is
// base_distance(Position, Base, Position, Base) (distance.hpp), bit-identical.
[[nodiscard]] float base_distance(const BasePose& a, const BasePose& b);

// D-074: the signed distance between two bases: the gap when they are apart, minus the penetration depth (how far
// one must be moved to separate them) when they overlap. Round-round: centre_distance - r1 - r2 (float).
[[nodiscard]] float signed_base_distance(const BasePose& a, const BasePose& b);

// D-074 / D-027 (03.01 Ending a Move: "on another model"): the bases overlap when they still intersect after each is
// shrunk by EPS_MM/2, i.e. when they intersect by more than EPS_MM (penetration depth > EPS_MM). Bases in contact do
// not overlap. Round-round is bases_overlap(Position, Base, Position, Base), bit-identical.
[[nodiscard]] bool bases_overlap(const BasePose& a, const BasePose& b);

// ---- D-009 Within / Wholly Within ----

// D-009 Within: some part of the base is inside the rectangle (within EPS_MM). Round: within_area (distance.hpp).
[[nodiscard]] bool within_area(const Rect& area, const BasePose& pose);

// D-009 Wholly Within: every part of the base is inside the rectangle (EPS_MM tolerance). Round: wholly_within.
[[nodiscard]] bool wholly_within(const Rect& area, const BasePose& pose);

// Half the base's extent along the battlefield's x and y axes at `facing_deg` (mm). Round: the radius for both.
// A 0-degree Tyrannofex (92 x 120) is {46, 60}; at 90 degrees {60, 46}.
struct BaseExtents {
    float half_x_mm = 0.0f;
    float half_y_mm = 0.0f;
    friend bool operator==(const BaseExtents&, const BaseExtents&) = default;
};
[[nodiscard]] BaseExtents base_extents(const Base& base, float facing_deg);

// ---- 03.01 Moving a Model in a Straight Line ----
// The base moves from start.position to `end` keeping start.facing_deg. For convex bases the area it sweeps is the
// convex hull of its start and end placements (its support in any direction is the larger of the two).

// D-074: the signed distance between the swept area and `obstacle` (as signed_base_distance).
[[nodiscard]] float translation_signed_distance(const BasePose& start, const Position& end, const BasePose& obstacle);

// 03.01 "Its base cannot be moved through enemy models": the swept area and `obstacle` overlap by the EPS_MM/2 rule
// (bases_overlap). Round-round is check_model_path's capsule test, bit-identical.
[[nodiscard]] bool translation_crosses(const BasePose& start, const Position& end, const BasePose& obstacle);

// 03.01 "Its base cannot cross the edge of the battlefield": the battlefield is convex, so the moving base stays on it
// iff it is wholly within it at both ends (wholly_within).
[[nodiscard]] bool translation_stays_within(const Rect& battlefield, const BasePose& start, const Position& end);

// ---- 03.01 Rotating a Model ----
// The base turns about its centre from start.facing_deg to `to_deg` the shorter way (rotation_delta_deg). A round base
// ignores facing, so its turn is the static check at the start.
struct RotationCheck {
    // The turn passes through the obstacle (bases_overlap at some facing of the turn) or crosses the battlefield edge
    // (not wholly_within at some facing of the turn).
    bool blocked = false;
    // The facing (normalised) at the closest approach: where to report a blocked turn. For the conservative check, the
    // first facing it could not show clear.
    float facing_deg = 0.0f;
    // Against a base: the smallest signed distance over the turn (negative: overlap depth). Against the edge: the
    // smallest margin between the base and the battlefield edge (negative: how far the base pokes out). For the
    // conservative check: the smallest value found at the facings it tried.
    float clearance_mm = 0.0f;
    // True for the exact checks (the edge, a round obstacle, a round mover); false for oval against oval.
    bool exact = true;
    friend bool operator==(const RotationCheck&, const RotationCheck&) = default;
};

// 03.01: the turn against another base.
// - Round obstacle: exact. The signed distance to a circle is smallest at the end facings or where the major axis
//   points at the circle's centre (D-074 proof), so those are the only facings checked.
// - Oval obstacle: conservative advancement with a proven bound. Turning an oval with semi-axes a >= b by an angle t
//   (radians) moves its outline by at most (a - b) * t (Hausdorff distance), so the signed distance changes by at
//   most that much. Steps are taken so the distance provably stays above -EPS_MM + EPS_MM/4 between the facings
//   tried; the turn is reported blocked as soon as a facing tried is within EPS_MM/2 of overlapping. It never reports
//   clear when the bases overlap at some facing; it may report blocked when they would intersect by between EPS_MM/2
//   and EPS_MM.
[[nodiscard]] RotationCheck rotation_against_base(const BasePose& start, float to_deg, const BasePose& obstacle);

// 03.01: the turn against the battlefield edge. Exact: the base's extent along x is largest at the end facings or
// where its major axis lies along x (facing 90 mod 180), and along y at the ends or facing 0 mod 180.
[[nodiscard]] RotationCheck rotation_against_edge(const Rect& battlefield, const BasePose& start, float to_deg);

// ---- Building blocks, exposed for tests and the pose-aware model-level overloads ----
namespace oval_detail {

struct Vec {
    double x = 0.0;
    double y = 0.0;
};
[[nodiscard]] inline Vec operator+(Vec a, Vec b) noexcept { return {a.x + b.x, a.y + b.y}; }
[[nodiscard]] inline Vec operator-(Vec a, Vec b) noexcept { return {a.x - b.x, a.y - b.y}; }
[[nodiscard]] inline Vec operator*(Vec a, double s) noexcept { return {a.x * s, a.y * s}; }
[[nodiscard]] inline double dot(Vec a, Vec b) noexcept { return a.x * b.x + a.y * b.y; }
[[nodiscard]] inline double cross(Vec a, Vec b) noexcept { return a.x * b.y - a.y * b.x; }
[[nodiscard]] inline double length(Vec a) noexcept { return std::sqrt(dot(a, a)); }
[[nodiscard]] inline Vec perp(Vec a) noexcept { return {-a.y, a.x}; }  // turned +90 degrees
[[nodiscard]] inline Vec to_vec(const Position& p) noexcept { return {p.x, p.y}; }

// D-074: sine and cosine of an angle in degrees from + - * / only (Taylor series after reduction to [-45, 45]), so
// they are the same on every platform. Error below 1e-16.
void sin_cos_deg(double deg, double& s, double& c) noexcept;
// D-074: atan2 in degrees, in (-180, 180], from + - * / only. atan2_deg(0, 0) is 0.
[[nodiscard]] double atan2_deg(double y, double x) noexcept;
// `deg` in (-180, 180] (double).
[[nodiscard]] double normalise_deg(double deg) noexcept;

// A base as an ellipse: centre, unit major axis u, unit minor axis v, semi-axes a >= b (a == b for a round base).
struct Ellipse {
    Vec c;
    Vec u{0.0, 1.0};
    Vec v{1.0, 0.0};
    double a = 0.0;
    double b = 0.0;
};
[[nodiscard]] Ellipse ellipse_of(const BasePose& pose) noexcept;
// The ellipse of `base` centred on `c` with its major axis along the unit vector `axis`.
[[nodiscard]] Ellipse ellipse_along(const Base& base, Vec c, Vec axis) noexcept;

// Eberly's robust bisection ("Distance from a Point to an Ellipse, an Ellipsoid, or a Hyperellipsoid"): the distance
// from (y0, y1), y0, y1 >= 0, to the ellipse with semi-axes e0 >= e1 > 0 along x and y, inside or outside. At most
// kEberlyMaxIterations halvings, of w = s + 1 (D-074: full precision next to the major axis inside the evolute).
inline constexpr int kEberlyMaxIterations = 1100;
[[nodiscard]] double eberly_distance(double e0, double e1, double y0, double y1) noexcept;
// Signed distance from point p to the ellipse (negative inside).
[[nodiscard]] double point_ellipse_signed_distance(Vec p, const Ellipse& e) noexcept;

// A convex shape for GJK: centre + ellipse (u, v, a, b; a point when both are 0) + an axis-aligned box (half sizes
// hx, hy) + the segment from 0 to `sweep` (the hull of the shape at the centre and at centre + sweep).
struct Convex {
    Vec c;
    Vec u{0.0, 1.0};
    Vec v{1.0, 0.0};
    double a = 0.0;
    double b = 0.0;
    double hx = 0.0;
    double hy = 0.0;
    Vec sweep;
};
[[nodiscard]] Vec support(const Convex& s, Vec d) noexcept;

// A signed distance with a certified bracket: lo <= true value <= hi, and value in [lo, hi].
struct SignedDistance {
    double value = 0.0;
    double lo = 0.0;
    double hi = 0.0;
    int iterations = 0;  // GJK + EPA iterations used
};
inline constexpr int kGjkMaxIterations = 128;
inline constexpr int kEpaMaxIterations = 128;
inline constexpr double kGjkToleranceMm = 1e-9;
// The signed distance between two convex shapes (positive: the gap; negative: minus the penetration depth).
// - Separated: GJK on the support functions, until its gap bracket [support-plane bound, simplex distance] is below
//   kGjkToleranceMm.
// - Otherwise (overlapping, touching, or GJK not certain): an inner polygon of support points and an outer polygon of
//   support lines bracket the Minkowski difference (for two plain ellipses also a bound from its curvature, D-074);
//   they are refined at the features that bind either bound until the bracket is below kGjkToleranceMm, or after 96
//   refinements (the bracket is still certified). Over 100,000 random oval pairs the bracket stayed within 1e-9 mm
//   after at most 31 refinements.
[[nodiscard]] SignedDistance convex_signed_distance(const Convex& a, const Convex& b) noexcept;
// The same, refined only until the bracket shows which side of `decide_at` the signed distance is on (its value is
// then on that side too).
[[nodiscard]] SignedDistance convex_signed_distance(const Convex& a, const Convex& b, double decide_at) noexcept;

// The signed distance between two posed bases in double: exact formulas for round-round and round-oval, GJK/EPA for
// oval-oval (value of the bracket).
[[nodiscard]] SignedDistance pose_signed_distance(const BasePose& a, const BasePose& b) noexcept;

}  // namespace oval_detail

}  // namespace wh40k::core
