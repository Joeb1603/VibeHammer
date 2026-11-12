#include "core/geometry/terrain_shape.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "core/geometry/distance.hpp"
#include "core/geometry/units.hpp"

namespace wh40k::core {

using oval_detail::Convex;
using oval_detail::Vec;
using oval_detail::cross;
using oval_detail::dot;
using oval_detail::length;
using oval_detail::to_vec;

namespace {

constexpr double kEps = static_cast<double>(EPS_MM);

bool is_round(const Base& b) noexcept { return b.shape == BaseShape::Round; }

double point_segment_distance(Vec p, Vec a, Vec b) noexcept {
    const Vec ab = b - a;
    const double len2 = dot(ab, ab);
    if (len2 <= 0.0) return length(p - a);
    double t = dot(p - a, ab) / len2;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return length(p - (a + ab * t));
}

// cross(v[i+1] - v[i], p - v[i]): positive when p is on the inner (left) side of edge i of a counter-clockwise polygon.
double edge_side(const std::vector<Position>& v, std::size_t i, Vec p) noexcept {
    const Vec a = to_vec(v[i]);
    const Vec b = to_vec(v[(i + 1) % v.size()]);
    return cross(b - a, p - a);
}

bool inside_polygon(const std::vector<Position>& v, Vec p) noexcept {
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (edge_side(v, i, p) < 0.0) return false;
    }
    return true;
}

// The signed distance from p to a convex counter-clockwise polygon: outside, the distance to its boundary; inside,
// minus the distance to the nearest edge's line (for a convex polygon, the nearest boundary point).
double point_polygon_signed_distance(const std::vector<Position>& v, Vec p) noexcept {
    bool inside = true;
    double inner = std::numeric_limits<double>::infinity();
    double outer = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < v.size(); ++i) {
        const Vec a = to_vec(v[i]);
        const Vec b = to_vec(v[(i + 1) % v.size()]);
        const double len = length(b - a);
        const double side = len > 0.0 ? cross(b - a, p - a) / len : 0.0;  // signed distance to the edge's line
        if (side < 0.0) inside = false;
        inner = std::min(inner, side);
        outer = std::min(outer, point_segment_distance(p, a, b));
    }
    return inside ? -inner : outer;
}

// Whether segment [p, q] meets the convex polygon (Cyrus-Beck clipping against each edge's inner half-plane).
bool segment_meets_polygon(const std::vector<Position>& v, Vec p, Vec q) noexcept {
    double t0 = 0.0;
    double t1 = 1.0;
    const Vec d = q - p;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const Vec a = to_vec(v[i]);
        const Vec e = to_vec(v[(i + 1) % v.size()]) - a;
        const double f0 = cross(e, p - a);  // inner side >= 0
        const double fd = cross(e, d);
        if (fd == 0.0) {
            if (f0 < 0.0) return false;  // parallel and outside
            continue;
        }
        const double t = -f0 / fd;
        if (fd > 0.0) {
            t0 = std::max(t0, t);  // entering
        } else {
            t1 = std::min(t1, t);  // leaving
        }
        if (t0 > t1) return false;
    }
    return true;
}

}  // namespace

TerrainShape polygon_shape(std::vector<Position> vertices) {
    TerrainShape s;
    s.vertices = std::move(vertices);
    return s;
}

TerrainShape circle_shape(const Position& centre, float diameter_mm) {
    TerrainShape s;
    s.centre = Position{centre.x, centre.y, 0.0f};
    s.diameter_mm = diameter_mm;
    return s;
}

double polygon_doubled_area(std::span<const Position> vertices) {
    double area = 0.0;
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        area += cross(to_vec(vertices[i]), to_vec(vertices[(i + 1) % vertices.size()]));
    }
    return area;
}

bool is_convex_ccw(std::span<const Position> vertices) {
    const std::size_t n = vertices.size();
    if (n < 3) return false;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec a = to_vec(vertices[i]);
        const Vec e = to_vec(vertices[(i + 1) % n]) - a;
        for (std::size_t j = 0; j < n; ++j) {
            if (j == i || j == (i + 1) % n) continue;
            if (!(cross(e, to_vec(vertices[j]) - a) > 0.0)) return false;
        }
    }
    return true;
}

Rect shape_bounds(const TerrainShape& shape) {
    if (shape.is_circle()) {
        const float r = shape.diameter_mm * 0.5f;
        return Rect{shape.centre.x - r, shape.centre.y - r, shape.centre.x + r, shape.centre.y + r};
    }
    Rect r{shape.vertices[0].x, shape.vertices[0].y, shape.vertices[0].x, shape.vertices[0].y};
    for (const Position& p : shape.vertices) {
        r.min_x = std::min(r.min_x, p.x);
        r.min_y = std::min(r.min_y, p.y);
        r.max_x = std::max(r.max_x, p.x);
        r.max_y = std::max(r.max_y, p.y);
    }
    return r;
}

Position shape_inner_point(const TerrainShape& shape) {
    if (shape.is_circle()) return shape.centre;
    double x = 0.0;
    double y = 0.0;
    for (const Position& p : shape.vertices) {
        x += static_cast<double>(p.x);
        y += static_cast<double>(p.y);
    }
    const double n = static_cast<double>(shape.vertices.size());
    return Position{static_cast<float>(x / n), static_cast<float>(y / n), 0.0f};
}

bool shape_wholly_within(const TerrainShape& inner, const TerrainShape& outer) {
    if (outer.is_circle()) {
        const Vec c = to_vec(outer.centre);
        const double big = 0.5 * static_cast<double>(outer.diameter_mm);
        if (inner.is_circle()) return length(to_vec(inner.centre) - c) + 0.5 * static_cast<double>(inner.diameter_mm) <= big;
        return std::all_of(inner.vertices.begin(), inner.vertices.end(),
                           [&](const Position& p) { return length(to_vec(p) - c) <= big; });
    }
    if (inner.is_circle()) {
        return point_polygon_signed_distance(outer.vertices, to_vec(inner.centre)) <=
               -0.5 * static_cast<double>(inner.diameter_mm);
    }
    // A convex polygon holds a polygon exactly when it holds its vertices.
    return std::all_of(inner.vertices.begin(), inner.vertices.end(),
                       [&](const Position& p) { return inside_polygon(outer.vertices, to_vec(p)); });
}

bool shape_wholly_within(const TerrainShape& shape, const Rect& rect) {
    const Rect b = shape_bounds(shape);
    return b.min_x >= rect.min_x - EPS_MM && b.min_y >= rect.min_y - EPS_MM && b.max_x <= rect.max_x + EPS_MM &&
           b.max_y <= rect.max_y + EPS_MM;
}

double point_shape_signed_distance(Vec p, const TerrainShape& shape) {
    if (shape.is_circle()) return length(p - to_vec(shape.centre)) - 0.5 * static_cast<double>(shape.diameter_mm);
    return point_polygon_signed_distance(shape.vertices, p);
}

double segment_shape_distance(Vec p, Vec q, const TerrainShape& shape) {
    if (shape.is_circle()) {
        return std::max(0.0, point_segment_distance(to_vec(shape.centre), p, q) -
                                 0.5 * static_cast<double>(shape.diameter_mm));
    }
    if (segment_meets_polygon(shape.vertices, p, q)) return 0.0;
    // Disjoint convex sets: the closest pair has an end of the segment or a vertex of the polygon in it.
    double d = std::min(point_polygon_signed_distance(shape.vertices, p),
                        point_polygon_signed_distance(shape.vertices, q));
    for (const Position& v : shape.vertices) d = std::min(d, point_segment_distance(to_vec(v), p, q));
    return std::max(0.0, d);
}

namespace terrain_detail {

BasePose circle_pose(const TerrainShape& shape) noexcept {
    // D-084's area_pose: the circle as a round base, so within_shape agrees with within_objective.
    return BasePose{Position{shape.centre.x, shape.centre.y, 0.0f}, Base{BaseShape::Round, shape.diameter_mm, 0.0f},
                    0.0f};
}

Convex PolygonConvex::convex() const noexcept {
    Convex c;
    c.c = centre;
    c.u = Vec{0.0, 1.0};
    c.v = Vec{1.0, 0.0};
    c.poly = offsets.data();
    c.poly_n = static_cast<int>(offsets.size());
    return c;
}

PolygonConvex polygon_convex(const TerrainShape& shape) {
    PolygonConvex out;
    double x = 0.0;
    double y = 0.0;
    for (const Position& p : shape.vertices) {
        x += static_cast<double>(p.x);
        y += static_cast<double>(p.y);
    }
    const double n = static_cast<double>(shape.vertices.size());
    out.centre = Vec{x / n, y / n};
    out.offsets.reserve(shape.vertices.size());
    for (const Position& p : shape.vertices) out.offsets.push_back(to_vec(p) - out.centre);
    return out;
}

}  // namespace terrain_detail

namespace {

Convex base_convex(const BasePose& pose) noexcept {
    const oval_detail::Ellipse e = oval_detail::ellipse_of(pose);
    Convex c;
    c.c = e.c;
    c.u = e.u;
    c.v = e.v;
    c.a = e.a;
    c.b = e.b;
    return c;
}

// The signed distance between an oval base and a polygon, refined until it is decided against `decide_at` (NaN: never
// decided early).
double oval_polygon_signed_distance(const BasePose& pose, const TerrainShape& shape, double decide_at) {
    const terrain_detail::PolygonConvex poly = terrain_detail::polygon_convex(shape);
    return oval_detail::convex_signed_distance(base_convex(pose), poly.convex(), decide_at).value;
}

}  // namespace

float shape_signed_distance(const BasePose& pose, const TerrainShape& shape) {
    if (shape.is_circle()) return signed_base_distance(pose, terrain_detail::circle_pose(shape));
    if (is_round(pose.base)) {
        return static_cast<float>(point_polygon_signed_distance(shape.vertices, to_vec(pose.position)) -
                                  0.5 * static_cast<double>(pose.base.diameter_mm));
    }
    return static_cast<float>(oval_polygon_signed_distance(pose, shape, std::numeric_limits<double>::quiet_NaN()));
}

bool within_shape(const BasePose& pose, const TerrainShape& shape) {
    // D-084's within_objective for a circle: the clamped base distance, within 0.
    if (shape.is_circle()) return within(base_distance(pose, terrain_detail::circle_pose(shape)), 0.0f);
    if (is_round(pose.base)) {
        return point_polygon_signed_distance(shape.vertices, to_vec(pose.position)) -
                   0.5 * static_cast<double>(pose.base.diameter_mm) <=
               kEps;
    }
    return oval_polygon_signed_distance(pose, shape, kEps) <= kEps;
}

bool overlaps_shape(const BasePose& pose, const TerrainShape& shape) {
    if (shape.is_circle()) return bases_overlap(pose, terrain_detail::circle_pose(shape));
    if (is_round(pose.base)) {
        return point_polygon_signed_distance(shape.vertices, to_vec(pose.position)) -
                   0.5 * static_cast<double>(pose.base.diameter_mm) <
               -kEps;
    }
    return oval_polygon_signed_distance(pose, shape, -kEps) < -kEps;
}

bool translation_crosses_shape(const BasePose& start, const Position& end, const TerrainShape& shape) {
    if (shape.is_circle()) return translation_crosses(start, end, terrain_detail::circle_pose(shape));
    if (is_round(start.base)) {
        // The capsule overlaps the polygon by more than EPS_MM: its centre line comes within r - EPS of it.
        const double r = 0.5 * static_cast<double>(start.base.diameter_mm);
        return segment_shape_distance(to_vec(start.position), to_vec(end), shape) < r - kEps;
    }
    if (start.position == end) return overlaps_shape(start, shape);
    Convex mover = base_convex(start);
    mover.sweep = to_vec(end) - to_vec(start.position);
    const terrain_detail::PolygonConvex poly = terrain_detail::polygon_convex(shape);
    return oval_detail::convex_signed_distance(mover, poly.convex(), -kEps).value < -kEps;
}

RotationCheck rotation_against_shape(const BasePose& start, float to_deg, const TerrainShape& shape) {
    if (shape.is_circle()) return rotation_against_base(start, to_deg, terrain_detail::circle_pose(shape));
    if (!is_round(start.base)) {
        // Far from the shape the turn can't reach it: every part of the base stays within its circumscribed radius.
        const double gap = point_polygon_signed_distance(shape.vertices, to_vec(start.position)) -
                           static_cast<double>(circumscribed_radius(start.base));
        if (gap > kEps) {
            RotationCheck out;
            out.facing_deg = normalise_facing_deg(start.facing_deg);
            out.clearance_mm = static_cast<float>(gap);
            return out;
        }
    }
    const terrain_detail::PolygonConvex poly = terrain_detail::polygon_convex(shape);
    return oval_detail::rotation_against_convex(start, to_deg, poly.convex());
}

}  // namespace wh40k::core
