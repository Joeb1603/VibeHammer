#pragma once

// D-087 / D-088 (M88): terrain footprints on the battlefield plane (terrain is 2D: no floors, no heights but a Dense
// feature's, which 13.06 reads). A footprint is a convex polygon (3 to 16 vertices, counter-clockwise) or a circle.
// Pure geometry: the rules that use it are in core/rules/terrain.hpp, the state types in core/state/terrain.hpp.
//
// - A circle is measured exactly as a round base of its diameter (D-084 measures objective areas that way), so a
//   terrain area linked to an objective agrees with the objective to the last bit.
// - A polygon against a round base uses exact point-to-polygon formulas in double; against an oval base, D-074's
//   GJK with the polygon support of oval_detail::Convex.
// - Tolerances (CLAUDE.md): "within" is a signed distance of at most EPS_MM (D-009, D-088); "overlapping" (a model on
//   a feature, D-027) is a penetration of more than EPS_MM; base contact is neither.

#include <cstddef>
#include <span>
#include <vector>

#include "core/geometry/oval.hpp"
#include "core/geometry/pose.hpp"
#include "core/geometry/primitives.hpp"

namespace wh40k::core {

// D-087: a polygon has 3 to 16 vertices.
inline constexpr std::size_t kMinPolygonVertices = 3;
inline constexpr std::size_t kMaxPolygonVertices = 16;

// A terrain footprint (mm): a convex polygon (counter-clockwise, z 0) or, when `vertices` is empty, a circle.
struct TerrainShape {
    std::vector<Position> vertices;  // the polygon; empty for a circle
    Position centre;                 // the circle's centre (unused for a polygon)
    float diameter_mm = 0.0f;        // the circle's diameter (0 for a polygon)
    [[nodiscard]] bool is_circle() const noexcept { return vertices.empty(); }
    friend bool operator==(const TerrainShape&, const TerrainShape&) = default;
};

[[nodiscard]] TerrainShape polygon_shape(std::vector<Position> vertices);
[[nodiscard]] TerrainShape circle_shape(const Position& centre, float diameter_mm);

// ---- The data's checks (check_terrain_data) ----

// Twice the polygon's signed area (mm^2): positive when the vertices run counter-clockwise.
[[nodiscard]] double polygon_doubled_area(std::span<const Position> vertices);
// D-087: a convex polygon, counter-clockwise, every vertex strictly on the inner side of every edge it is not on (so
// no three in a line, no repeated vertex, no self-crossing, a positive area). Any vertex count.
[[nodiscard]] bool is_convex_ccw(std::span<const Position> vertices);
// The axis-aligned box round the shape.
[[nodiscard]] Rect shape_bounds(const TerrainShape& shape);
// A point inside the shape: the circle's centre, or the polygon's vertex mean.
[[nodiscard]] Position shape_inner_point(const TerrainShape& shape);
// D-087 "a feature wholly within its area": every point of `inner` is in `outer`, exactly (no tolerance; M88 relies
// on it: a line that passes a feature by less than the line-of-sight limit passes its area too).
[[nodiscard]] bool shape_wholly_within(const TerrainShape& inner, const TerrainShape& outer);
// D-087 "an area wholly on the battlefield": every point of the shape in `rect`, within EPS_MM (as D-084's objectives).
[[nodiscard]] bool shape_wholly_within(const TerrainShape& shape, const Rect& rect);

// ---- Distances ----

// The signed distance from point p to the shape (negative inside), in double.
[[nodiscard]] double point_shape_signed_distance(oval_detail::Vec p, const TerrainShape& shape);
// The distance from segment [p, q] to the shape (0 when they meet), in double.
[[nodiscard]] double segment_shape_distance(oval_detail::Vec p, oval_detail::Vec q, const TerrainShape& shape);
// The signed distance between a base and the shape (the gap, or minus the penetration depth).
[[nodiscard]] float shape_signed_distance(const BasePose& pose, const TerrainShape& shape);
// D-009 / D-088: some part of the base is within the shape (signed distance at most EPS_MM). For a circle this is
// within_objective's arithmetic (D-084).
[[nodiscard]] bool within_shape(const BasePose& pose, const TerrainShape& shape);
// D-087 / D-027: the base overlaps the shape by more than EPS_MM (a model "on" a feature). Base contact does not.
[[nodiscard]] bool overlaps_shape(const BasePose& pose, const TerrainShape& shape);

// ---- 03.01 / 13.06 moves ----

// The base moving in a straight line from start.position to `end` (keeping its facing) passes through the shape: the
// swept area overlaps it by more than EPS_MM (as translation_crosses against a base).
[[nodiscard]] bool translation_crosses_shape(const BasePose& start, const Position& end, const TerrainShape& shape);
// The base turning in place from start.facing_deg to `to_deg` (the shorter way) against the shape: a circle as
// rotation_against_base against a round base of its size (exact), a polygon by rotation_against_convex
// (conservative). A round base doesn't turn: the static check at the start.
[[nodiscard]] RotationCheck rotation_against_shape(const BasePose& start, float to_deg, const TerrainShape& shape);

namespace terrain_detail {
// The circle as a round base (D-084's objective measure).
[[nodiscard]] BasePose circle_pose(const TerrainShape& shape) noexcept;
// The polygon for GJK: its vertices as offsets from their mean, which is `centre` (inside the polygon).
struct PolygonConvex {
    std::vector<oval_detail::Vec> offsets;
    oval_detail::Vec centre;
    [[nodiscard]] oval_detail::Convex convex() const noexcept;
};
[[nodiscard]] PolygonConvex polygon_convex(const TerrainShape& shape);
}  // namespace terrain_detail

}  // namespace wh40k::core
