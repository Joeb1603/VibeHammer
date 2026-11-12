#include "drop_fix.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

#include "core/geometry/engagement.hpp"
#include "core/geometry/oval.hpp"

namespace wh40k::client {

namespace {

// The search works in doubles; every spot it returns is checked as the float Position that is submitted.
struct Vec {
    double x = 0.0;
    double y = 0.0;
};
Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y}; }
Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y}; }
Vec operator*(Vec a, double k) { return {a.x * k, a.y * k}; }
double dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y; }
double cross(Vec a, Vec b) { return a.x * b.y - a.y * b.x; }
double norm(Vec a) { return std::hypot(a.x, a.y); }
Vec vec(const core::Position& p) { return {p.x, p.y}; }
core::Position position(Vec v) { return core::Position{static_cast<float>(v.x), static_cast<float>(v.y), 0.0f}; }

struct Circle {
    Vec c;
    double r = 0.0;
};
struct Line {
    Vec a;
    Vec d;  // unit direction
};

struct Segment {
    Vec a;
    Vec b;
};

// The region's boundary pieces: the edges, every circle, and each shadow's two tangent lines (D-044: the
// shadow is the disc and the wedge between the tangents from `from`), with the tangent points. D-074 part 3: each
// shape as a chain of segments between points on its edge (shape_outline), and a shape shadow's tangent lines.
struct Pieces {
    std::vector<Circle> circles;
    std::vector<Line> lines;
    std::vector<Vec> vertices;
    std::vector<Segment> segments;
};

// ---- D-074 part 3 (M66): shapes ----

// A shape's edge is sampled every kShapeStepDeg round the obstacle's centre (the shape is convex and holds it, so each
// ray crosses the edge once), at a threshold kShapeSlackMm on the legal side, so the chords between the points stay on
// the legal side of the shape too: the chords of a convex set lie inside it, and with these steps they are within
// kShapeSlackMm of its edge (the edge's radius of curvature is at least the bases' inscribed radii, the steps a few mm).
constexpr int kShapeSteps = 180;  // every 2 degrees
constexpr double kShapeSlackMm = 0.05;

Vec ray_dir(int k) {
    double s = 0.0;
    double c = 1.0;
    core::oval_detail::sin_cos_deg(360.0 * static_cast<double>(k) / kShapeSteps, s, c);
    return Vec{c, s};
}

// The point on the edge of {c : signed distance < threshold} along direction `d` from the obstacle's centre, or
// nullopt if the shape holds no point.
std::optional<Vec> shape_edge_point(const core::ReachShape& shape, double threshold, Vec d) {
    const Vec c = vec(shape.obstacle.position);
    const double inner = static_cast<double>(core::inscribed_radius(shape.mover_base) +
                                             core::inscribed_radius(shape.obstacle.base)) + threshold;
    const double outer = static_cast<double>(core::circumscribed_radius(shape.mover_base) +
                                             core::circumscribed_radius(shape.obstacle.base)) + threshold;
    if (!(inner > 0.0)) return std::nullopt;
    const auto inside = [&](double rho) {
        const Vec p = c + d * rho;
        return core::signed_base_distance(core::BasePose{position(p), shape.mover_base, shape.mover_facing_deg},
                                          shape.obstacle) < threshold;
    };
    double lo = std::max(0.0, inner - 1.0);
    double hi = outer + 1.0;
    for (int i = 0; i < 26 && hi - lo > 1e-5; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (inside(mid)) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return c + d * (0.5 * (lo + hi));
}

// The shape's edge as a chain of segments (the legal side: outward for `outside`, inward otherwise), every step round
// it, or only the steps that can come within `limit` of `near` (with one step more at each end). Points into `p`.
void add_shape_outline(Pieces& p, const core::ReachShape& shape, bool outside, const Vec* near, double limit,
                       std::vector<Vec>* outline = nullptr) {
    const double threshold = static_cast<double>(shape.threshold_mm) + (outside ? kShapeSlackMm : -kShapeSlackMm);
    const Vec c = vec(shape.obstacle.position);
    const double outer = static_cast<double>(core::circumscribed_radius(shape.mover_base) +
                                             core::circumscribed_radius(shape.obstacle.base)) + threshold;
    const double inner = static_cast<double>(core::inscribed_radius(shape.mover_base) +
                                             core::inscribed_radius(shape.obstacle.base)) + threshold;
    if (!(inner > 0.0)) return;
    // The window of directions whose edge point can be within `limit` of `near`.
    double half = 180.0;
    double centre_deg = 0.0;
    if (near != nullptr) {
        const Vec v = *near - c;
        const double dn = norm(v);
        if (dn - outer > limit || inner - dn > limit) return;  // no edge point that near
        if (dn > limit) {
            // cos(angle) >= (rho^2 + dn^2 - limit^2) / (2 rho dn) for some rho in [inner, outer]
            double lowest = 1.0;
            const auto rhs = [&](double rho) { return (rho * rho + dn * dn - limit * limit) / (2.0 * rho * dn); };
            lowest = std::min(rhs(inner), rhs(outer));
            const double rho_min = std::sqrt(std::max(0.0, dn * dn - limit * limit));
            if (rho_min > inner && rho_min < outer) lowest = std::min(lowest, rhs(rho_min));
            lowest = std::clamp(lowest, -1.0, 1.0);
            half = std::acos(lowest) * 180.0 / 3.14159265358979323846;
            centre_deg = core::oval_detail::atan2_deg(v.y, v.x);
        }
    }
    constexpr double kStep = 360.0 / kShapeSteps;
    std::vector<int> ks;
    if (half >= 180.0) {
        for (int k = 0; k < kShapeSteps; ++k) ks.push_back(k);
    } else {
        const int span = static_cast<int>(std::ceil(half / kStep)) + 1;
        const int mid = static_cast<int>(std::lround(centre_deg / kStep));
        for (int j = -span; j <= span; ++j) ks.push_back(((mid + j) % kShapeSteps + kShapeSteps) % kShapeSteps);
    }
    std::vector<Vec> pts;
    pts.reserve(ks.size());
    for (const int k : ks) {
        if (const auto e = shape_edge_point(shape, threshold, ray_dir(k))) pts.push_back(*e);
    }
    if (pts.size() < 2) return;
    for (const Vec& v : pts) p.vertices.push_back(v);
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) p.segments.push_back(Segment{pts[i], pts[i + 1]});
    if (half >= 180.0 && pts.size() == ks.size()) p.segments.push_back(Segment{pts.back(), pts.front()});
    if (outline != nullptr) *outline = std::move(pts);
}

// A shape shadow's tangent lines from `from` (D-044), through the outline's extreme points as seen from `from`.
void add_shadow_tangents(Pieces& p, Vec from, const std::vector<Vec>& outline) {
    if (outline.size() < 2) return;
    // The outline's points by their angle about `from`, relative to the direction to the first point.
    const Vec base = outline.front() - from;
    const double bl = norm(base);
    if (!(bl > 0.0)) return;
    const Vec u{base.x / bl, base.y / bl};
    double lo = 0.0;
    double hi = 0.0;
    Vec at_lo = outline.front();
    Vec at_hi = outline.front();
    for (const Vec& v : outline) {
        const Vec w = v - from;
        const double a = std::atan2(cross(u, w), dot(u, w));
        if (a < lo) {
            lo = a;
            at_lo = v;
        }
        if (a > hi) {
            hi = a;
            at_hi = v;
        }
    }
    for (const Vec& t : {at_lo, at_hi}) {
        const Vec d = t - from;
        const double l = norm(d);
        if (!(l > 0.0)) continue;
        p.lines.push_back(Line{from, d * (1.0 / l)});
        p.vertices.push_back(t);
    }
}

// ---- M88 (D-087): terrain ----

// The terrain feature's outline in the mover's centre space, for the threshold `threshold` (moved kShapeSlackMm to the
// legal side, outward): for a round mover exact pieces (each polygon edge moved out by the radius plus the threshold,
// a circle of that radius at each vertex; a circle feature grown by it), for an oval a chain of points on its edge,
// every half degree round the feature's centre. With `from`, the shadow's tangent lines from it too (D-044). Pieces
// that aren't on the boundary only add candidates; every spot is checked with in_region.
void add_terrain_pieces(Pieces& p, const core::ReachTerrain& t, const Vec* from) {
    const double threshold = static_cast<double>(t.threshold_mm) + kShapeSlackMm;
    const Vec off = vec(t.offset);
    std::vector<Circle> corners;
    if (t.mover_base.shape == core::BaseShape::Round) {
        const double big = 0.5 * static_cast<double>(t.mover_base.diameter_mm) + threshold;
        if (!(big > 0.0)) return;
        if (t.shape.is_circle()) {
            corners.push_back(Circle{vec(t.shape.centre) + off, 0.5 * static_cast<double>(t.shape.diameter_mm) + big});
        } else {
            const std::vector<core::Position>& v = t.shape.vertices;
            for (std::size_t i = 0; i < v.size(); ++i) {
                const Vec a = vec(v[i]) + off;
                const Vec b = vec(v[(i + 1) % v.size()]) + off;
                const Vec e = b - a;
                const double l = norm(e);
                if (!(l > 0.0)) continue;
                const Vec n{e.y / l, -e.x / l};  // outward for a counter-clockwise polygon
                p.segments.push_back(Segment{a + n * big, b + n * big});
                p.vertices.push_back(a + n * big);
                p.vertices.push_back(b + n * big);
                corners.push_back(Circle{a, big});
            }
        }
    } else {
        // An oval: points on the edge of {c : signed distance < threshold} along rays from the feature's centre.
        const Vec c = vec(core::reach_terrain_centre(t));
        const double inner = static_cast<double>(core::reach_terrain_inner_radius_mm(t)) + kShapeSlackMm;
        const double outer = static_cast<double>(core::reach_terrain_outer_radius_mm(t)) + kShapeSlackMm;
        if (!(inner > 0.0)) return;
        core::ReachTerrain grown = t;
        grown.threshold_mm = static_cast<float>(threshold);
        constexpr int kSteps = 720;
        std::vector<Vec> pts;
        for (int k = 0; k < kSteps; ++k) {
            double sn = 0.0;
            double cs = 1.0;
            core::oval_detail::sin_cos_deg(360.0 * static_cast<double>(k) / kSteps, sn, cs);
            const Vec d{cs, sn};
            double lo = std::max(0.0, inner - 1.0);
            double hi = outer + 1.0;
            for (int i = 0; i < 26 && hi - lo > 1e-5; ++i) {
                const double mid = 0.5 * (lo + hi);
                if (core::reach_terrain_contains(grown, position(c + d * mid))) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            pts.push_back(c + d * (0.5 * (lo + hi)));
        }
        for (std::size_t i = 0; i < pts.size(); ++i) {
            p.vertices.push_back(pts[i]);
            p.segments.push_back(Segment{pts[i], pts[(i + 1) % pts.size()]});
        }
        if (from != nullptr) {
            // The outline's points stand in for the corners: the tangents through its extreme points.
            Pieces tangents;
            const Vec f = *from;
            const Vec base = pts.front() - f;
            const double bl = norm(base);
            if (bl > 0.0) {
                const Vec u{base.x / bl, base.y / bl};
                double lo = 0.0;
                double hi = 0.0;
                Vec at_lo = pts.front();
                Vec at_hi = pts.front();
                for (const Vec& v : pts) {
                    const Vec w = v - f;
                    const double a = std::atan2(cross(u, w), dot(u, w));
                    if (a < lo) {
                        lo = a;
                        at_lo = v;
                    }
                    if (a > hi) {
                        hi = a;
                        at_hi = v;
                    }
                }
                for (const Vec& tp : {at_lo, at_hi}) {
                    const Vec d = tp - f;
                    const double l = norm(d);
                    if (!(l > 0.0)) continue;
                    p.lines.push_back(Line{f, d * (1.0 / l)});
                    p.vertices.push_back(tp);
                }
            }
        }
        return;
    }
    for (const Circle& c : corners) {
        p.circles.push_back(c);
        if (from == nullptr) continue;
        // The tangent lines from `from` to this corner's circle, with their tangent points.
        const Vec f = *from;
        const Vec u0 = c.c - f;
        const double d = norm(u0);
        if (d <= c.r) continue;
        const Vec u = u0 * (1.0 / d);
        const double length = std::sqrt(d * d - c.r * c.r);
        const double sin_a = c.r / d;
        const double cos_a = length / d;
        const Vec t1{u.x * cos_a - u.y * sin_a, u.x * sin_a + u.y * cos_a};
        const Vec t2{u.x * cos_a + u.y * sin_a, -u.x * sin_a + u.y * cos_a};
        p.lines.push_back(Line{f, t1});
        p.lines.push_back(Line{f, t2});
        p.vertices.push_back(f + t1 * length);
        p.vertices.push_back(f + t2 * length);
    }
}

Pieces pieces_of(const ReachRegion& region) {
    Pieces p;
    const core::Rect& b = region.bounds;
    p.lines.push_back(Line{{b.min_x, b.min_y}, {1.0, 0.0}});
    p.lines.push_back(Line{{b.min_x, b.max_y}, {1.0, 0.0}});
    p.lines.push_back(Line{{b.min_x, b.min_y}, {0.0, 1.0}});
    p.lines.push_back(Line{{b.max_x, b.min_y}, {0.0, 1.0}});
    const auto add = [&](const core::ReachCircle& c) { p.circles.push_back(Circle{vec(c.centre), c.radius_mm}); };
    for (const core::ReachCircle& c : region.inside_all) add(c);
    for (const auto& group : region.inside_any) {
        for (const core::ReachCircle& c : group) add(c);
    }
    for (const core::ReachCircle& c : region.outside_all) add(c);
    for (const Shadow& s : region.outside_shadows) {
        add(s.disc);
        const Vec f = vec(s.from);
        const Vec u0 = vec(s.disc.centre) - f;
        const double d = norm(u0);
        const double r = s.disc.radius_mm;
        if (!(r > 0.0) || d <= r) continue;  // from inside the disc: no tangents
        const Vec u = u0 * (1.0 / d);
        const double length = std::sqrt(d * d - r * r);
        const double sin_a = r / d;
        const double cos_a = length / d;
        const Vec t1{u.x * cos_a - u.y * sin_a, u.x * sin_a + u.y * cos_a};
        const Vec t2{u.x * cos_a + u.y * sin_a, -u.x * sin_a + u.y * cos_a};
        p.lines.push_back(Line{f, t1});
        p.lines.push_back(Line{f, t2});
        p.vertices.push_back(f + t1 * length);
        p.vertices.push_back(f + t2 * length);
    }
    return p;
}

// `region` with every boundary moved `mm` towards the legal side.
ReachRegion inset(const ReachRegion& region, float mm) {
    ReachRegion out = region;
    out.bounds = core::Rect{region.bounds.min_x + mm, region.bounds.min_y + mm, region.bounds.max_x - mm,
                            region.bounds.max_y - mm};
    for (core::ReachCircle& c : out.inside_all) c.radius_mm -= mm;
    for (auto& group : out.inside_any) {
        for (core::ReachCircle& c : group) c.radius_mm -= mm;
    }
    for (core::ReachCircle& c : out.outside_all) c.radius_mm += mm;
    for (Shadow& s : out.outside_shadows) s.disc.radius_mm += mm;
    // D-074 part 3: a shape's threshold is a signed distance, so moving its edge `mm` is adding `mm` to it.
    for (auto& group : out.inside_any_shapes) {
        for (core::ReachShape& sh : group) sh.threshold_mm -= mm;
    }
    for (core::ReachShape& sh : out.outside_shapes) sh.threshold_mm += mm;
    for (ShapeShadow& s : out.outside_shape_shadows) s.shape.threshold_mm += mm;
    // M88: a terrain area's threshold is a signed distance too.
    for (core::ReachTerrain& t : out.outside_terrain) t.threshold_mm += mm;
    for (TerrainShadow& s : out.outside_terrain_shadows) s.terrain.threshold_mm += mm;
    return out;
}

double outer_radius(const core::ReachShape& sh) { return static_cast<double>(core::reach_shape_outer_radius_mm(sh)); }

// The distance from q to the segment [a, b].
double point_segment(Vec q, Vec a, Vec b) {
    const Vec d = b - a;
    const double l2 = dot(d, d);
    const double t = l2 > 0.0 ? std::clamp(dot(q - a, d) / l2, 0.0, 1.0) : 0.0;
    return norm(q - (a + d * t));
}

// Whether the shadow from `from` of the disc (centre c, radius r) comes within `limit` of q: q is in it, or near the
// disc, or near one of its two tangent rays.
bool near_disc_shadow(Vec from, Vec c, double r, Vec q, double limit) {
    if (norm(q - c) <= r + limit) return true;
    const Vec u0 = c - from;
    const double d = norm(u0);
    if (d <= r) return true;  // every line from there meets the disc
    const Vec u = u0 * (1.0 / d);
    const double sin_a = r / d;
    const double cos_a = std::sqrt(std::max(0.0, 1.0 - sin_a * sin_a));
    const Vec w = q - from;
    const double wl = norm(w);
    if (!(wl > 0.0)) return false;
    // Inside the cone beyond the disc: the angle from u at most the half angle and further out than the disc.
    if (dot(w, u) >= wl * cos_a && dot(w, u) >= d - r) return true;
    const double far = d + norm(w) + limit + r;  // long enough to pass q
    const Vec t1{u.x * cos_a - u.y * sin_a, u.x * sin_a + u.y * cos_a};
    const Vec t2{u.x * cos_a + u.y * sin_a, -u.x * sin_a + u.y * cos_a};
    return point_segment(q, from, from + t1 * far) <= limit || point_segment(q, from, from + t2 * far) <= limit;
}

// D-074 part 3: the region's shapes as pieces. Near the drop `q` (nearest): the parts of each outline that can be
// within `limit` of it, and each shape shadow that comes that near with its whole outline and its tangent lines from
// its `from`. Along the drag's line from `from` to `to` (along, `q` null): the whole outline of every shape the line
// comes near (a line from `from` enters a shape's shadow where it enters the shape).
void add_shapes(Pieces& p, const ReachRegion& region, const Vec* q, double limit, Vec from = {}, Vec to = {}) {
    const auto near_line = [&](const core::ReachShape& sh) {
        return point_segment(vec(sh.obstacle.position), from, to) <= outer_radius(sh) + 1.0;
    };
    for (std::size_t g = 0; g < region.inside_any.size(); ++g) {
        for (const core::ReachShape& sh : group_shapes(region, g)) {
            if (q != nullptr || near_line(sh)) add_shape_outline(p, sh, false, q, limit);
        }
    }
    for (const core::ReachShape& sh : region.outside_shapes) {
        if (q != nullptr || near_line(sh)) add_shape_outline(p, sh, true, q, limit);
    }
    for (const ShapeShadow& s : region.outside_shape_shadows) {
        if (q == nullptr) {
            if (near_line(s.shape)) add_shape_outline(p, s.shape, true, nullptr, limit);
            continue;
        }
        if (!near_disc_shadow(vec(s.from), vec(s.shape.obstacle.position), outer_radius(s.shape) + 1.0, *q, limit)) {
            continue;
        }
        std::vector<Vec> outline;
        add_shape_outline(p, s.shape, true, nullptr, limit, &outline);  // the whole outline, for its tangents
        add_shadow_tangents(p, vec(s.from), outline);
    }
    // M88: the terrain areas and shadows near the drop (or the drag's line).
    const auto terrain_outer = [](const core::ReachTerrain& t) {
        return static_cast<double>(core::reach_terrain_outer_radius_mm(t));
    };
    const auto terrain_near = [&](const core::ReachTerrain& t) {
        const Vec c = vec(core::reach_terrain_centre(t));
        if (q != nullptr) return norm(*q - c) <= terrain_outer(t) + limit + 1.0;
        return point_segment(c, from, to) <= terrain_outer(t) + 1.0;
    };
    for (const core::ReachTerrain& t : region.outside_terrain) {
        if (terrain_near(t)) add_terrain_pieces(p, t, nullptr);
    }
    for (const TerrainShadow& s : region.outside_terrain_shadows) {
        const Vec f = vec(s.from);
        if (q != nullptr) {
            if (!near_disc_shadow(f, vec(core::reach_terrain_centre(s.terrain)), terrain_outer(s.terrain) + 1.0, *q,
                                  limit)) {
                continue;
            }
            add_terrain_pieces(p, s.terrain, &f);
        } else if (terrain_near(s.terrain)) {
            add_terrain_pieces(p, s.terrain, nullptr);
        }
    }
}

Vec project(const Segment& s, Vec q) {
    const Vec d = s.b - s.a;
    const double l2 = dot(d, d);
    if (!(l2 > 0.0)) return s.a;
    return s.a + d * std::clamp(dot(q - s.a, d) / l2, 0.0, 1.0);
}

void intersect(const Segment& s, const Circle& c, std::vector<Vec>& out) {
    const Vec d = s.b - s.a;
    const double a = dot(d, d);
    if (!(a > 0.0)) return;
    const Vec w = s.a - c.c;
    const double b = 2.0 * dot(w, d);
    const double k = dot(w, w) - c.r * c.r;
    const double disc = b * b - 4.0 * a * k;
    if (disc < 0.0) return;
    const double root = std::sqrt(disc);
    for (const double t : {(-b - root) / (2.0 * a), (-b + root) / (2.0 * a)}) {
        if (t >= 0.0 && t <= 1.0) out.push_back(s.a + d * t);
    }
}

void intersect(const Segment& s, const Line& l, std::vector<Vec>& out) {
    const Vec d = s.b - s.a;
    const double denom = cross(d, l.d);
    if (std::fabs(denom) < 1e-12) return;
    const double t = cross(l.a - s.a, l.d) / denom;
    if (t >= 0.0 && t <= 1.0) out.push_back(s.a + d * t);
}

void intersect(const Segment& s, const Segment& r, std::vector<Vec>& out) {
    const Vec d = s.b - s.a;
    const Vec e = r.b - r.a;
    const double denom = cross(d, e);
    if (std::fabs(denom) < 1e-12) return;
    const double t = cross(r.a - s.a, e) / denom;
    const double u = cross(r.a - s.a, d) / denom;
    if (t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0) out.push_back(s.a + d * t);
}

// The distance from q to the segment.
double distance_to(const Segment& s, Vec q) { return norm(project(s, q) - q); }

Vec project(const Circle& c, Vec q) {
    const Vec v = q - c.c;
    const double d = norm(v);
    if (d <= 0.0) return c.c + Vec{c.r, 0.0};  // the drop on the centre: any point will do, this one always
    return c.c + v * (c.r / d);
}

Vec project(const Line& l, Vec q) { return l.a + l.d * dot(q - l.a, l.d); }

void intersect(const Circle& a, const Circle& b, std::vector<Vec>& out) {
    const Vec v = b.c - a.c;
    const double d = norm(v);
    if (!(d > 0.0) || d > a.r + b.r || d < std::fabs(a.r - b.r)) return;
    const double along = (a.r * a.r - b.r * b.r + d * d) / (2.0 * d);
    const double h = std::sqrt(std::max(0.0, a.r * a.r - along * along));
    const Vec mid = a.c + v * (along / d);
    const Vec perp{-v.y / d, v.x / d};
    out.push_back(mid + perp * h);
    out.push_back(mid - perp * h);
}

void intersect(const Circle& c, const Line& l, std::vector<Vec>& out) {
    const Vec foot = project(l, c.c);
    const double h = norm(c.c - foot);
    if (h > c.r) return;
    const double k = std::sqrt(std::max(0.0, c.r * c.r - h * h));
    out.push_back(foot + l.d * k);
    out.push_back(foot - l.d * k);
}

void intersect(const Line& a, const Line& b, std::vector<Vec>& out) {
    const double denom = cross(a.d, b.d);
    if (std::fabs(denom) < 1e-12) return;
    out.push_back(a.a + a.d * (cross(b.a - a.a, b.d) / denom));
}

// The nearest point of `check` to the drop `q`, at most kNearestSpotMaxMm away. The nearest point of a
// region bounded by circles and lines is a projection of q onto one piece or a corner where two meet; the
// pieces are those of `in` (the region moved inwards), so each spot is inside `check`.
std::optional<Vec> nearest(const ReachRegion& in, const ReachRegion& check, Vec from, Vec q) {
    const double limit = kNearestSpotMaxMm;
    Pieces all = pieces_of(in);
    if (in.has_shapes()) add_shapes(all, in, &q, limit);  // D-074 part 3 (none in a round-only region)
    Pieces nearby;
    for (const Circle& c : all.circles) {
        if (std::fabs(norm(q - c.c) - c.r) <= limit) nearby.circles.push_back(c);
    }
    for (const Line& l : all.lines) {
        if (std::fabs(cross(q - l.a, l.d)) <= limit) nearby.lines.push_back(l);
    }
    for (const Segment& sg : all.segments) {
        if (distance_to(sg, q) <= limit) nearby.segments.push_back(sg);
    }
    std::vector<Vec> candidates;
    for (const Vec& v : all.vertices) candidates.push_back(v);
    for (const Circle& c : nearby.circles) candidates.push_back(project(c, q));
    for (const Line& l : nearby.lines) candidates.push_back(project(l, q));
    for (const Segment& sg : nearby.segments) candidates.push_back(project(sg, q));
    for (std::size_t i = 0; i < nearby.circles.size(); ++i) {
        for (std::size_t j = i + 1; j < nearby.circles.size(); ++j) intersect(nearby.circles[i], nearby.circles[j], candidates);
        for (const Line& l : nearby.lines) intersect(nearby.circles[i], l, candidates);
    }
    for (std::size_t i = 0; i < nearby.lines.size(); ++i) {
        for (std::size_t j = i + 1; j < nearby.lines.size(); ++j) intersect(nearby.lines[i], nearby.lines[j], candidates);
    }
    for (std::size_t i = 0; i < nearby.segments.size(); ++i) {
        for (const Circle& c : nearby.circles) intersect(nearby.segments[i], c, candidates);
        for (const Line& l : nearby.lines) intersect(nearby.segments[i], l, candidates);
        for (std::size_t j = i + 1; j < nearby.segments.size(); ++j) {
            intersect(nearby.segments[i], nearby.segments[j], candidates);
        }
    }
    // Nearest first; ties to the spot nearer the start along the drag, then by x and y. The first one inside
    // `check` is the answer.
    const Vec drag = q - from;
    std::vector<std::tuple<double, double, double, double>> keyed;
    keyed.reserve(candidates.size());
    for (const Vec& c : candidates) {
        const Vec snapped = vec(position(c));  // as submitted
        const double d = norm(snapped - q);
        if (d <= limit) keyed.emplace_back(d, dot(snapped - from, drag), snapped.x, snapped.y);
    }
    std::sort(keyed.begin(), keyed.end());
    for (const auto& key : keyed) {
        const Vec spot{std::get<2>(key), std::get<3>(key)};
        if (in_region(check, position(spot))) return spot;
    }
    return std::nullopt;
}

// The furthest point of the line from `from` to `q` that is inside `check`. The line is cut where it
// crosses each piece of `in`; between cuts a point is in `in` or not throughout.
std::optional<Vec> along(const ReachRegion& in, const ReachRegion& check, Vec from, Vec q) {
    const Vec drag = q - from;
    const double a = dot(drag, drag);
    if (!(a > 0.0)) return std::nullopt;
    std::vector<double> cuts{0.0, 1.0};
    const auto add = [&](double t) {
        if (t > 0.0 && t < 1.0) cuts.push_back(t);
    };
    Pieces all = pieces_of(in);
    if (in.has_shapes()) {  // D-074 part 3: the shapes' outlines where the drag's line crosses them
        add_shapes(all, in, nullptr, 0.0, from, q);
        const Segment line{from, q};
        for (const Segment& sg : all.segments) {
            std::vector<Vec> hits;
            intersect(line, sg, hits);
            for (const Vec& h : hits) add(dot(h - from, drag) / a);
        }
    }
    for (const Circle& c : all.circles) {
        const Vec w = from - c.c;
        const double b = 2.0 * dot(w, drag);
        const double k = dot(w, w) - c.r * c.r;
        const double disc = b * b - 4.0 * a * k;
        if (disc < 0.0) continue;
        const double root = std::sqrt(disc);
        add((-b - root) / (2.0 * a));
        add((-b + root) / (2.0 * a));
    }
    for (const Line& l : all.lines) {
        const double denom = cross(drag, l.d);
        if (std::fabs(denom) < 1e-12) continue;
        add(cross(l.a - from, l.d) / denom);
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    const auto at = [&](double t) { return position(from + drag * t); };
    for (std::size_t i = cuts.size() - 1; i > 0; --i) {
        const double lo_t = cuts[i - 1];
        const double hi_t = cuts[i];
        double lo = (lo_t + hi_t) * 0.5;
        if (!in_region(in, at(lo))) continue;
        // The top end of this stretch, or the furthest point below it that is inside `check`.
        double hi = hi_t;
        if (in_region(check, at(hi))) return vec(at(hi));
        for (int k = 0; k < 60; ++k) {
            const double m = (lo + hi) * 0.5;
            if (in_region(check, at(m))) {
                lo = m;
            } else {
                hi = m;
            }
        }
        return vec(at(lo));
    }
    return std::nullopt;
}

}  // namespace

ReachRegion fix_region(const ReachRegion& region) {
    ReachRegion out = region;
    for (core::ReachCircle& c : out.outside_all) {
        if (!c.engagement) continue;
        // move_reach's engagement circle is (both base radii + 2" + EPS): keep "not on it" (both base radii).
        c.radius_mm -= core::kEngagementRangeMm + core::EPS_MM;
        c.engagement = false;
    }
    // D-074 part 3: an engagement shape's threshold is 2" + EPS: "not on it" is 0.
    for (core::ReachShape& sh : out.outside_shapes) {
        if (!sh.engagement) continue;
        sh.threshold_mm -= core::kEngagementRangeMm + core::EPS_MM;
        sh.engagement = false;
    }
    if (out.inside_any_shapes.empty()) {
        std::erase_if(out.inside_any, [](const std::vector<core::ReachCircle>& group) {
            return !group.empty() &&
                   std::all_of(group.begin(), group.end(), [](const core::ReachCircle& c) { return c.engagement; });
        });
        return out;
    }
    // With shapes: a group is a "still engaged" one when every circle and shape in it is an engagement one.
    std::vector<std::vector<core::ReachCircle>> circles;
    std::vector<std::vector<core::ReachShape>> shapes;
    for (std::size_t g = 0; g < out.inside_any.size(); ++g) {
        const std::vector<core::ReachCircle>& c = out.inside_any[g];
        const std::vector<core::ReachShape>& sh = group_shapes(out, g);
        const bool engaged = (!c.empty() || !sh.empty()) &&
                             std::all_of(c.begin(), c.end(), [](const core::ReachCircle& x) { return x.engagement; }) &&
                             std::all_of(sh.begin(), sh.end(), [](const core::ReachShape& x) { return x.engagement; });
        if (engaged) continue;
        circles.push_back(c);
        shapes.push_back(sh);
    }
    out.inside_any = std::move(circles);
    out.inside_any_shapes = std::move(shapes);
    return out;
}

std::optional<DropProblem> drop_problem(const ReachRegion& region, const core::Position& p) {
    if (region.empty) return DropProblem::CannotMove;
    // The same tests as in_region.
    const auto inside = [&](const core::ReachCircle& c) {
        return std::hypot(p.x - c.centre.x, p.y - c.centre.y) < c.radius_mm;
    };
    const auto holds = [&](const core::ReachShape& sh) { return shape_holds(sh, p); };  // D-074 part 3
    if (std::any_of(region.outside_shadows.begin(), region.outside_shadows.end(), [&](const Shadow& s) {
            return point_segment_distance(s.disc.centre, s.from, p) < s.disc.radius_mm;
        }) ||
        std::any_of(region.outside_shape_shadows.begin(), region.outside_shape_shadows.end(),
                    [&](const ShapeShadow& s) { return shadow_holds(s, p); })) {
        return DropProblem::ThroughEnemyModel;
    }
    if (std::any_of(region.outside_terrain_shadows.begin(), region.outside_terrain_shadows.end(),
                    [&](const TerrainShadow& s) { return terrain_shadow_holds(s, p); })) {
        return DropProblem::ThroughTerrain;  // M88
    }
    if (std::any_of(region.outside_all.begin(), region.outside_all.end(), inside) ||
        std::any_of(region.outside_shapes.begin(), region.outside_shapes.end(), holds)) {
        return DropProblem::OnModel;
    }
    if (std::any_of(region.outside_terrain.begin(), region.outside_terrain.end(),
                    [&](const core::ReachTerrain& t) { return terrain_holds(t, p); })) {
        return DropProblem::OnTerrain;  // M88
    }
    if (!std::all_of(region.inside_all.begin(), region.inside_all.end(), inside)) return DropProblem::TooFar;
    const core::Rect& b = region.bounds;
    if (p.x < b.min_x || p.x > b.max_x || p.y < b.min_y || p.y > b.max_y) return DropProblem::OffBattlefield;
    for (std::size_t g = 0; g < region.inside_any.size(); ++g) {
        const std::vector<core::ReachCircle>& group = region.inside_any[g];
        const std::vector<core::ReachShape>& shapes = group_shapes(region, g);
        if (!std::any_of(group.begin(), group.end(), inside) && !std::any_of(shapes.begin(), shapes.end(), holds)) {
            return DropProblem::NotCloser;
        }
    }
    return std::nullopt;
}

std::optional<FixedDrop> fix_drop(const ReachRegion& region, const core::Position& from, const core::Position& drop) {
    const std::optional<DropProblem> problem = drop_problem(region, drop);
    if (!problem || *problem == DropProblem::CannotMove) return std::nullopt;
    const ReachRegion in = inset(region, kFixNudgeMm);
    const ReachRegion check = inset(region, kFixNudgeMm * 0.5f);
    std::optional<Vec> spot;
    FixedDrop::How how = FixedDrop::How::Nearest;
    if (*problem != DropProblem::ThroughEnemyModel && *problem != DropProblem::ThroughTerrain) {
        spot = nearest(in, check, vec(from), vec(drop));
    }
    if (!spot) {
        spot = along(in, check, vec(from), vec(drop));
        how = FixedDrop::How::AlongDrag;
    }
    if (!spot || norm(*spot - vec(from)) <= core::EPS_MM) return std::nullopt;  // it wouldn't move
    return FixedDrop{position(*spot), *problem, how};
}

std::vector<ReachSpan> engaged_spans(const ReachRegion& region, float y_step_mm) {
    std::vector<ReachSpan> out;
    const std::vector<ReachSpan> outer = reach_spans(fix_region(region), y_step_mm);
    const std::vector<ReachSpan> inner = reach_spans(region, y_step_mm);
    // Both lists walk the same rows (fix_region keeps the bounds and the distance circles), bottom to top,
    // left to right; a row is matched within half a step.
    const float half = y_step_mm * 0.5f;
    std::size_t j = 0;
    for (const ReachSpan& s : outer) {
        while (j < inner.size() && inner[j].y < s.y - half) ++j;
        float x0 = s.x0;
        for (std::size_t k = j; k < inner.size() && inner[k].y <= s.y + half; ++k) {
            const ReachSpan& c = inner[k];
            if (c.x1 <= x0 || c.x0 >= s.x1) continue;
            if (c.x0 > x0) out.push_back(ReachSpan{s.y, x0, c.x0});
            x0 = std::max(x0, c.x1);
        }
        if (x0 < s.x1) out.push_back(ReachSpan{s.y, x0, s.x1});
    }
    return out;
}

std::string drop_problem_text(DropProblem problem) {
    switch (problem) {
        case DropProblem::CannotMove: return "the model can't move any further";
        case DropProblem::ThroughEnemyModel: return "its line ran into an enemy model (03.01)";
        case DropProblem::OnModel: return "it was on another model (03.01)";
        case DropProblem::TooFar: return "it was further than the model can move";
        case DropProblem::OffBattlefield: return "it was off the battlefield (03.01)";
        case DropProblem::NotCloser: return "it didn't end closer to the target";
        case DropProblem::ThroughTerrain: return "its line ran into a terrain feature it can't move through (13.06)";
        case DropProblem::OnTerrain: return "it was on a terrain feature (03.01)";
    }
    return "";
}

std::string fix_message(const FixedDrop& fixed) {
    const std::string head = fixed.how == FixedDrop::How::AlongDrag ? "Moved as far as it can along the drag: "
                                                                    : "Moved to the nearest spot it can go: ";
    return head + drop_problem_text(fixed.problem);
}

}  // namespace wh40k::client
