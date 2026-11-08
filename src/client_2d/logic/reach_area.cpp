#include "reach_area.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "core/geometry/distance.hpp"
#include "core/geometry/oval.hpp"

namespace wh40k::client {

namespace {

using Interval = std::pair<float, float>;

core::ReachCircle shifted(core::ReachCircle c, float dx, float dy) {
    c.centre.x += dx;
    c.centre.y += dy;
    return c;
}

// The chord of circle `c` on line `y`, if it crosses it.
std::optional<Interval> chord(const core::ReachCircle& c, float y) {
    const float dy = y - c.centre.y;
    const float k2 = c.radius_mm * c.radius_mm - dy * dy;
    if (k2 <= 0.0f) return std::nullopt;
    const float k = std::sqrt(k2);
    return Interval{c.centre.x - k, c.centre.x + k};
}

// `parts` minus [a, b].
void subtract(std::vector<Interval>& parts, float a, float b) {
    std::vector<Interval> out;
    for (const Interval& p : parts) {
        if (b <= p.first || a >= p.second) {
            out.push_back(p);
            continue;
        }
        if (a > p.first) out.emplace_back(p.first, a);
        if (b < p.second) out.emplace_back(b, p.second);
    }
    parts = std::move(out);
}

// `parts` intersected with the union of `others` (each sorted or not).
void intersect_union(std::vector<Interval>& parts, std::vector<Interval> others) {
    std::sort(others.begin(), others.end());
    std::vector<Interval> merged;
    for (const Interval& o : others) {
        if (!merged.empty() && o.first <= merged.back().second) {
            merged.back().second = std::max(merged.back().second, o.second);
        } else {
            merged.push_back(o);
        }
    }
    std::vector<Interval> out;
    for (const Interval& p : parts) {
        for (const Interval& m : merged) {
            const float a = std::max(p.first, m.first);
            const float b = std::min(p.second, m.second);
            if (a < b) out.emplace_back(a, b);
        }
    }
    std::sort(out.begin(), out.end());
    parts = std::move(out);
}

// The part of line `y` in shadow `s`: the disc's chord, and (when `from` is outside the disc) the wedge
// between the tangents from `from`, beyond the chord through the tangent points: a straight line from
// `from` to a point there crosses that chord, which is inside the disc. A point in the wedge before that
// chord and outside the disc is reached before the line meets the disc.
std::vector<Interval> shadow_on_row(const Shadow& s, float y) {
    std::vector<Interval> out;
    if (const auto ch = chord(s.disc, y)) out.push_back(*ch);
    const float ux0 = s.disc.centre.x - s.from.x;
    const float uy0 = s.disc.centre.y - s.from.y;
    const float d = std::hypot(ux0, uy0);
    const float r = s.disc.radius_mm;
    if (!(r > 0.0f)) return out;
    if (d <= r) {  // starting inside the disc: every line from here touches it
        out.push_back(Interval{-std::numeric_limits<float>::max(), std::numeric_limits<float>::max()});
        return out;
    }
    const float ux = ux0 / d;
    const float uy = uy0 / d;
    const float sin_a = r / d;
    const float cos_a = std::sqrt(1.0f - sin_a * sin_a);
    // Tangent directions: u turned by +a and -a.
    const float t1x = ux * cos_a - uy * sin_a, t1y = ux * sin_a + uy * cos_a;
    const float t2x = ux * cos_a + uy * sin_a, t2y = -ux * sin_a + uy * cos_a;
    const float chord_at = (d * d - r * r) / d;  // distance along u from `from` to the tangent chord
    // Each condition is a*x + b*y + c >= 0 at (x, y), with v = p - from.
    struct HalfPlane {
        float a, b, c;
    };
    const float fx = s.from.x, fy = s.from.y;
    const HalfPlane planes[] = {
        // u's side of t1: cross(t1, v) <= 0  ->  -t1x*vy + t1y*vx >= 0
        {t1y, -t1x, -t1y * fx + t1x * fy},
        // u's side of t2: cross(t2, v) >= 0  ->  t2x*vy - t2y*vx >= 0
        {-t2y, t2x, t2y * fx - t2x * fy},
        // beyond the chord: dot(v, u) >= chord_at
        {ux, uy, -ux * fx - uy * fy - chord_at},
    };
    float lo = -std::numeric_limits<float>::max();
    float hi = std::numeric_limits<float>::max();
    for (const HalfPlane& h : planes) {
        const float rest = h.b * y + h.c;  // a*x + rest >= 0
        if (std::fabs(h.a) < 1e-9f) {
            if (rest < 0.0f) return out;  // no x on this row
            continue;
        }
        const float x = -rest / h.a;
        if (h.a > 0.0f) {
            lo = std::max(lo, x);
        } else {
            hi = std::min(hi, x);
        }
    }
    if (lo < hi) out.push_back(Interval{lo, hi});
    return out;
}

Shadow shifted(Shadow s, float dx, float dy) {
    s.from.x += dx;
    s.from.y += dy;
    s.disc = shifted(s.disc, dx, dy);
    return s;
}

core::Rect intersect(const core::Rect& a, const core::Rect& b) {
    return core::Rect{std::max(a.min_x, b.min_x), std::max(a.min_y, b.min_y), std::min(a.max_x, b.max_x),
                      std::min(a.max_y, b.max_y)};
}

// ---- D-074 part 3 (M66): shapes ----

// The discs that bound a shape (core::reach_shape_inner_radius_mm / _outer_radius_mm) decide a point only beyond this
// margin; nearer them core's exact test decides (far above the kernel's rounding).
constexpr float kDiscMarginMm = 0.001f;

const std::vector<core::ReachShape> kNoShapes;

core::ReachShape shifted(core::ReachShape s, float dx, float dy) {
    s.obstacle.position.x += dx;
    s.obstacle.position.y += dy;
    return s;
}

ShapeShadow shifted(ShapeShadow s, float dx, float dy) {
    s.from.x += dx;
    s.from.y += dy;
    s.shape = shifted(s.shape, dx, dy);
    return s;
}

// `region`'s groups and their shapes, kept in step: inside_any_shapes empty or one group per inside_any group.
void add_group(ReachRegion& region, std::vector<core::ReachCircle> circles, std::vector<core::ReachShape> shapes) {
    const std::size_t before = region.inside_any.size();
    region.inside_any.push_back(std::move(circles));
    if (shapes.empty() && region.inside_any_shapes.empty()) return;
    region.inside_any_shapes.resize(before);
    region.inside_any_shapes.push_back(std::move(shapes));
}

// The convex set {x : holds(x)} on a line within [lo, hi], given the part [in_lo, in_hi] known to be inside it: its
// ends, each the midpoint of a bisection down to `tol` (an end of [lo, hi] that holds is that end).
template <typename Holds>
Interval convex_ends(const Holds& holds, float lo, float hi, float in_lo, float in_hi, float tol) {
    Interval out{lo, hi};
    if (!holds(lo)) {
        float a = lo;     // outside
        float b = in_lo;  // inside
        for (int i = 0; i < 64 && b - a > tol; ++i) {
            const float m = 0.5f * (a + b);
            if (holds(m)) {
                b = m;
            } else {
                a = m;
            }
        }
        out.first = 0.5f * (a + b);
    }
    if (!holds(hi)) {
        float c = in_hi;  // inside
        float d = hi;     // outside
        for (int i = 0; i < 64 && d - c > tol; ++i) {
            const float m = 0.5f * (c + d);
            if (holds(m)) {
                c = m;
            } else {
                d = m;
            }
        }
        out.second = 0.5f * (c + d);
    }
    return out;
}

// The part of row `y` (within [x0, x1]) in `shape`: one interval or none (the shape is convex).
std::optional<Interval> shape_on_row(const core::ReachShape& shape, float y, float x0, float x1, float tol) {
    const float outer = core::reach_shape_outer_radius_mm(shape) + 1.0f;
    const core::Position c = shape.obstacle.position;
    const float dy = y - c.y;
    if (!(outer > 0.0f) || std::fabs(dy) >= outer) return std::nullopt;
    const float ko = std::sqrt(outer * outer - dy * dy);
    const float lo = std::max(x0, c.x - ko);
    const float hi = std::min(x1, c.x + ko);
    if (!(lo < hi)) return std::nullopt;
    const auto holds = [&](float x) { return shape_holds(shape, core::Position{x, y, 0.0f}); };
    const float inner = core::reach_shape_inner_radius_mm(shape) - 2.0f * kDiscMarginMm;
    float in_lo = 0.0f;
    float in_hi = 0.0f;
    if (inner > 0.0f && std::fabs(dy) < inner) {
        const float ki = std::sqrt(inner * inner - dy * dy);
        in_lo = c.x - ki;
        in_hi = c.x + ki;
    } else {
        // The signed distance is convex along the row: its least value, by golden section.
        const auto f = [&](float x) {
            return core::signed_base_distance(core::BasePose{core::Position{x, y, 0.0f}, shape.mover_base,
                                                             shape.mover_facing_deg},
                                              shape.obstacle);
        };
        constexpr float kGolden = 0.6180339887f;
        float a = c.x - ko;
        float b = c.x + ko;
        float p = b - kGolden * (b - a);
        float q = a + kGolden * (b - a);
        float fp = f(p);
        float fq = f(q);
        for (int i = 0; i < 40 && b - a > tol * 0.25f; ++i) {
            if (fp < fq) {
                b = q;
                q = p;
                fq = fp;
                p = b - kGolden * (b - a);
                fp = f(p);
            } else {
                a = p;
                p = q;
                fp = fq;
                q = a + kGolden * (b - a);
                fq = f(q);
            }
        }
        const float best = fp < fq ? p : q;
        if (!holds(best)) return std::nullopt;
        in_lo = in_hi = best;
    }
    // Clip the inside interval to [lo, hi] (the row's part being worked on).
    if (in_hi < lo || in_lo > hi) {
        // The known inside part is off this part of the row; the set may still reach into it.
        if (in_hi < lo) {
            if (!holds(lo)) return std::nullopt;
            in_lo = in_hi = lo;
        } else {
            if (!holds(hi)) return std::nullopt;
            in_lo = in_hi = hi;
        }
    }
    in_lo = std::max(in_lo, lo);
    in_hi = std::min(in_hi, hi);
    return convex_ends(holds, lo, hi, in_lo, in_hi, tol);
}

// The hull of shadow_on_row's pieces (the disc's shadow on the row is one interval), within [x0, x1].
std::optional<Interval> disc_shadow_on_row(const core::Position& from, const core::Position& centre, float radius, float y,
                                           float x0, float x1) {
    if (!(radius > 0.0f)) return std::nullopt;
    const std::vector<Interval> pieces = shadow_on_row(Shadow{from, core::ReachCircle{centre, radius, false, {}}}, y);
    if (pieces.empty()) return std::nullopt;
    float lo = std::numeric_limits<float>::max();
    float hi = -std::numeric_limits<float>::max();
    for (const Interval& i : pieces) {
        lo = std::min(lo, i.first);
        hi = std::max(hi, i.second);
    }
    lo = std::max(lo, x0);
    hi = std::min(hi, x1);
    if (!(lo < hi)) return std::nullopt;
    return Interval{lo, hi};
}

// The part of row `y` (within [x0, x1]) in a shape's shadow: one interval or none (the shadow is convex).
std::optional<Interval> shape_shadow_on_row(const ShapeShadow& s, float y, float x0, float x1, float tol) {
    const core::Position c = s.shape.obstacle.position;
    const float outer = core::reach_shape_outer_radius_mm(s.shape) + 1.0f;
    const std::optional<Interval> bracket = disc_shadow_on_row(s.from, c, outer, y, x0, x1);
    if (!bracket) return std::nullopt;
    const float lo = bracket->first;
    const float hi = bracket->second;
    const auto holds = [&](float x) { return shadow_holds(s, core::Position{x, y, 0.0f}); };
    std::optional<Interval> known;
    const float inner = core::reach_shape_inner_radius_mm(s.shape) - 2.0f * kDiscMarginMm;
    if (inner > 0.0f) {
        if (core::centre_distance(s.from, c) < inner) return Interval{lo, hi};  // every line from there meets it
        known = disc_shadow_on_row(s.from, c, inner, y, lo, hi);
    }
    if (!known) {
        // Beyond the centre, on the line from `from` through it: in the shadow (the centre is in the shape).
        const float ry = c.y - s.from.y;
        if (std::fabs(ry) > 1e-6f) {
            const float t = (y - s.from.y) / ry;
            const float x = s.from.x + t * (c.x - s.from.x);
            if (t >= 1.0f && x >= lo && x <= hi && holds(x)) known = Interval{x, x};
        }
    }
    if (!known) {
        // Samples across the bracket (at most 1024).
        const float width = hi - lo;
        const int n = std::clamp(static_cast<int>(std::ceil(width / tol)), 2, 1024);
        for (int i = 0; i <= n && !known; ++i) {
            const float x = lo + width * static_cast<float>(i) / static_cast<float>(n);
            if (holds(x)) known = Interval{x, x};
        }
    }
    if (!known) return std::nullopt;
    const float in_lo = std::max(known->first, lo);
    const float in_hi = std::min(known->second, hi);
    return convex_ends(holds, lo, hi, in_lo, in_hi, tol);
}

}  // namespace

const std::vector<core::ReachShape>& group_shapes(const ReachRegion& region, std::size_t g) {
    return g < region.inside_any_shapes.size() ? region.inside_any_shapes[g] : kNoShapes;
}

bool shape_holds(const core::ReachShape& shape, const core::Position& p) {
    const float dx = p.x - shape.obstacle.position.x;
    const float dy = p.y - shape.obstacle.position.y;
    const float d2 = dx * dx + dy * dy;
    const float outer = core::reach_shape_outer_radius_mm(shape) + kDiscMarginMm;
    if (!(outer > 0.0f) || d2 > outer * outer) return false;
    const float inner = core::reach_shape_inner_radius_mm(shape) - kDiscMarginMm;
    if (inner > 0.0f && d2 < inner * inner) return true;
    return core::reach_shape_contains(shape, p);
}

bool shadow_holds(const ShapeShadow& s, const core::Position& p) {
    const core::ReachShape& shape = s.shape;
    const float outer = core::reach_shape_outer_radius_mm(shape) + kDiscMarginMm;
    if (!(outer > 0.0f)) return false;
    const float d = point_segment_distance(shape.obstacle.position, s.from, p);
    if (d > outer) return false;
    const float inner = core::reach_shape_inner_radius_mm(shape) - kDiscMarginMm;
    if (inner > 0.0f && d < inner) return true;
    const core::BasePose start{s.from, shape.mover_base, shape.mover_facing_deg};
    // 03.01 / D-044: the engine's own test for move_reach's blocking threshold (-EPS); for another threshold (an inset
    // region, D-052) the swept base's signed distance, which is below the threshold exactly when some placement on the
    // line is (shrinking a convex base commutes with sweeping it, D-074 part 1).
    if (shape.threshold_mm == -core::EPS_MM) return core::translation_crosses(start, p, shape.obstacle);
    return core::translation_signed_distance(start, p, shape.obstacle) < shape.threshold_mm;
}

float point_segment_distance(const core::Position& p, const core::Position& a, const core::Position& b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

bool hypot_less(float dx, float dy, float r) {
    // hypot(dx, dy) >= max(|dx|, |dy|) for any faithfully rounded hypot (the true value is at least that float).
    // This also answers r <= 0.
    if (std::fabs(dx) >= r || std::fabs(dy) >= r) return false;
    if (r > 1e-10f && r < 1e10f) {  // r * r far from underflow and overflow
        // Products of two floats are exact in double; the sum rounds by about 1e-16, far inside the margin.
        const double q = static_cast<double>(dx) * dx + static_cast<double>(dy) * dy;
        const double r2 = static_cast<double>(r) * r;
        if (q < r2 * (1.0 - 1e-6)) return true;
        if (q > r2 * (1.0 + 1e-6)) return false;
    }
    return std::hypot(dx, dy) < r;  // no fast answer (close to the edge, or not finite): the plain test
}

bool point_segment_closer_than(const core::Position& p, const core::Position& a, const core::Position& b, float r) {
    // The same arithmetic as point_segment_distance up to its hypot.
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    return hypot_less(p.x - (a.x + t * dx), p.y - (a.y + t * dy), r);
}

ReachRegion model_region(const core::MoveReach& reach) {
    ReachRegion region;
    region.bounds = reach.centre_bounds;
    region.inside_all.push_back(core::ReachCircle{reach.from, reach.remaining_mm, false, {}});
    region.inside_any = reach.required_any;
    region.outside_all = reach.forbidden;
    for (const core::ReachCircle& c : reach.blocking) region.outside_shadows.push_back(Shadow{reach.from, c});  // D-044
    // D-074 part 3: the pairs that aren't round to round, as core's shapes; nothing at a facing the model can't turn
    // to (03.01).
    region.inside_any_shapes = reach.required_any_shapes;
    region.outside_shapes = reach.forbidden_shapes;
    for (const core::ReachShape& b : reach.blocking_shapes) region.outside_shape_shadows.push_back(ShapeShadow{reach.from, b});
    region.empty = reach.immobile || !(reach.remaining_mm > 0.0f) || reach.turn_blocked;
    return region;
}

ReachRegion unit_region(const core::GameState& state, core::UnitId unit, const core::Position& grab) {
    ReachRegion region;
    region.empty = true;
    const core::Unit* u = state.find_unit(unit);
    if (u == nullptr) return region;
    bool first = true;
    for (const core::Model& m : u->models) {
        const std::optional<core::MoveReach> reach = core::move_reach(state, m.id);
        if (!reach || reach->immobile) continue;  // the engine refuses this model's part of the drag
        // Model m ends at its position + (mouse - grab): the mouse is in m's region moved by (grab - pos).
        const float dx = grab.x - m.position.x;
        const float dy = grab.y - m.position.y;
        const ReachRegion own = model_region(*reach);
        if (own.empty) continue;  // no distance left: its part of the drag is refused, the rest still move
        const core::Rect b{own.bounds.min_x + dx, own.bounds.min_y + dy, own.bounds.max_x + dx, own.bounds.max_y + dy};
        region.bounds = first ? b : intersect(region.bounds, b);
        first = false;
        for (const core::ReachCircle& c : own.inside_all) region.inside_all.push_back(shifted(c, dx, dy));
        for (std::size_t k = 0; k < own.inside_any.size(); ++k) {
            std::vector<core::ReachCircle> g;
            for (const core::ReachCircle& c : own.inside_any[k]) g.push_back(shifted(c, dx, dy));
            std::vector<core::ReachShape> gs;  // D-074 part 3
            for (const core::ReachShape& sh : group_shapes(own, k)) gs.push_back(shifted(sh, dx, dy));
            add_group(region, std::move(g), std::move(gs));
        }
        const auto own_unit = [&](core::ModelId id) {
            for (const core::Model& o : u->models) {
                if (o.id == id) return true;
            }
            return false;
        };
        for (const core::ReachCircle& c : own.outside_all) {
            if (!own_unit(c.model)) region.outside_all.push_back(shifted(c, dx, dy));
        }
        for (const Shadow& s : own.outside_shadows) region.outside_shadows.push_back(shifted(s, dx, dy));
        for (const core::ReachShape& sh : own.outside_shapes) {
            if (!own_unit(sh.model)) region.outside_shapes.push_back(shifted(sh, dx, dy));
        }
        for (const ShapeShadow& s : own.outside_shape_shadows) region.outside_shape_shadows.push_back(shifted(s, dx, dy));
    }
    region.empty = first;
    return region;
}

std::vector<ReachSpan> reach_spans(const ReachRegion& region, float y_step_mm) {
    std::vector<ReachSpan> out;
    if (region.empty || !(y_step_mm > 0.0f)) return out;
    float y_lo = region.bounds.min_y;
    float y_hi = region.bounds.max_y;
    for (const core::ReachCircle& c : region.inside_all) {
        y_lo = std::max(y_lo, c.centre.y - c.radius_mm);
        y_hi = std::min(y_hi, c.centre.y + c.radius_mm);
    }
    if (region.inside_all.empty()) return out;  // unbounded: never asked for
    // D-074 part 3: the shapes' row intervals by bisection, to within this much.
    const float tol = std::min(kShapeSpanToleranceMm, 0.5f * y_step_mm);
    // The parts' extent on the row (shapes are only worked out where they could matter).
    const auto extent = [](const std::vector<Interval>& parts) {
        return Interval{parts.front().first, parts.back().second};
    };
    for (float y = y_lo; y <= y_hi; y += y_step_mm) {
        std::vector<Interval> parts{{region.bounds.min_x, region.bounds.max_x}};
        for (const core::ReachCircle& c : region.inside_all) {
            const auto ch = chord(c, y);
            if (!ch) {
                parts.clear();
                break;
            }
            intersect_union(parts, {*ch});
        }
        for (std::size_t g = 0; g < region.inside_any.size(); ++g) {
            if (parts.empty()) break;
            std::vector<Interval> chords;
            for (const core::ReachCircle& c : region.inside_any[g]) {
                if (const auto ch = chord(c, y)) chords.push_back(*ch);
            }
            const std::vector<core::ReachShape>& shapes = group_shapes(region, g);
            if (!shapes.empty()) {
                const Interval e = extent(parts);
                for (const core::ReachShape& sh : shapes) {
                    if (const auto in = shape_on_row(sh, y, e.first, e.second, tol)) chords.push_back(*in);
                }
            }
            intersect_union(parts, std::move(chords));
        }
        for (const core::ReachCircle& c : region.outside_all) {
            if (parts.empty()) break;
            if (const auto ch = chord(c, y)) subtract(parts, ch->first, ch->second);
        }
        for (const Shadow& s : region.outside_shadows) {
            if (parts.empty()) break;
            for (const Interval& i : shadow_on_row(s, y)) subtract(parts, i.first, i.second);
        }
        for (const core::ReachShape& sh : region.outside_shapes) {
            if (parts.empty()) break;
            const Interval e = extent(parts);
            if (const auto in = shape_on_row(sh, y, e.first, e.second, tol)) subtract(parts, in->first, in->second);
        }
        for (const ShapeShadow& s : region.outside_shape_shadows) {
            if (parts.empty()) break;
            const Interval e = extent(parts);
            if (const auto in = shape_shadow_on_row(s, y, e.first, e.second, tol)) {
                subtract(parts, in->first, in->second);
            }
        }
        for (const Interval& p : parts) {
            if (p.first < p.second) out.push_back(ReachSpan{y, p.first, p.second});
        }
    }
    return out;
}

ReachRegion trimmed_region(const ReachRegion& region) {
    ReachRegion out = region;
    if (region.empty || region.inside_all.empty()) return out;
    // Every point of the region is inside the smallest inside_all circle, so within `reach` of its centre.
    const core::ReachCircle* bound = &region.inside_all.front();
    for (const core::ReachCircle& c : region.inside_all) {
        if (c.radius_mm < bound->radius_mm) bound = &c;
    }
    const core::Position centre = bound->centre;
    const float reach = std::max(0.0f, bound->radius_mm);
    constexpr float kMarginMm = 1.0f;  // far more than the rounding of any distance on the battlefield
    // A circle no point within `reach` of the centre can be inside.
    const auto out_of_reach = [&](const core::ReachCircle& c) {
        return core::centre_distance(centre, c.centre) > reach + c.radius_mm + kMarginMm;
    };
    for (std::vector<core::ReachCircle>& group : out.inside_any) std::erase_if(group, out_of_reach);  // kept if empty
    std::erase_if(out.outside_all, out_of_reach);
    // A shadow's line runs from its `from` to the point, so it stays within the larger of their distances from the
    // centre (a disc is convex).
    std::erase_if(out.outside_shadows, [&](const Shadow& s) {
        const float line = std::max(reach, core::centre_distance(centre, s.from));
        return core::centre_distance(centre, s.disc.centre) > line + s.disc.radius_mm + kMarginMm;
    });
    // D-074 part 3: a shape lies inside its outer disc.
    const auto shape_out_of_reach = [&](const core::ReachShape& sh) {
        return core::centre_distance(centre, sh.obstacle.position) >
               reach + core::reach_shape_outer_radius_mm(sh) + kMarginMm;
    };
    for (std::vector<core::ReachShape>& group : out.inside_any_shapes) std::erase_if(group, shape_out_of_reach);
    std::erase_if(out.outside_shapes, shape_out_of_reach);
    std::erase_if(out.outside_shape_shadows, [&](const ShapeShadow& s) {
        const float line = std::max(reach, core::centre_distance(centre, s.from));
        return core::centre_distance(centre, s.shape.obstacle.position) >
               line + core::reach_shape_outer_radius_mm(s.shape) + kMarginMm;
    });
    return out;
}

namespace {

constexpr double kRefusalMarginMm = 0.05;

// The distances along the ray origin + d * (ux, uy) at which the ideal point is inside circle `c` widened by
// `extra` (negative: shrunk), in double: the open interval (lo, hi), or nothing.
std::optional<std::pair<double, double>> ray_inside(const core::Position& origin, float ux, float uy,
                                                    const core::ReachCircle& c, double extra) {
    const double radius = static_cast<double>(c.radius_mm) + extra;
    if (!(radius > 0.0)) return std::nullopt;
    const double dx = static_cast<double>(c.centre.x) - origin.x;
    const double dy = static_cast<double>(c.centre.y) - origin.y;
    const double a = static_cast<double>(ux) * ux + static_cast<double>(uy) * uy;
    const double t = static_cast<double>(ux) * dx + static_cast<double>(uy) * dy;
    const double c0 = dx * dx + dy * dy - radius * radius;
    const double disc = t * t - a * c0;  // |origin + d u - centre|^2 < radius^2  <=>  a d^2 - 2 t d + c0 < 0
    if (!(disc > 0.0) || !(a > 0.0)) return std::nullopt;
    const double root = std::sqrt(disc);
    return std::make_pair((t - root) / a, (t + root) / a);
}

}  // namespace

RayRefusals::RayRefusals(const ReachRegion& region, const core::Position& origin, std::span<const float> cosines,
                         std::span<const float> sines, std::span<const float> distances)
    : rays_(std::min(cosines.size(), sines.size())), spots_(distances.size()), refused_(rays_ * spots_, 0) {
    if (spots_ == 0) return;
    const auto first_above = [&](double v) {  // the first distance > v
        return static_cast<std::size_t>(std::upper_bound(distances.begin(), distances.end(), v,
                                                         [](double x, float d) { return x < static_cast<double>(d); }) -
                                        distances.begin());
    };
    const auto first_at_least = [&](double v) {  // the first distance >= v
        return static_cast<std::size_t>(std::lower_bound(distances.begin(), distances.end(), v,
                                                         [](float d, double x) { return static_cast<double>(d) < x; }) -
                                        distances.begin());
    };
    char* const out = refused_.data();
    for (std::size_t i = 0; i < rays_; ++i) {
        char* const ray = out + i * spots_;
        // Certainly inside a forbidden circle: the distances strictly inside the shrunk circle's interval.
        for (const core::ReachCircle& c : region.outside_all) {
            const auto in = ray_inside(origin, cosines[i], sines[i], c, -kRefusalMarginMm);
            if (!in) continue;
            const std::size_t from = first_above(in->first);
            const std::size_t to = first_at_least(in->second);
            if (from < to) std::fill(ray + from, ray + to, char{1});
        }
    }
    std::vector<std::pair<std::size_t, std::size_t>> maybe;  // [from, to) index ranges
    for (std::size_t g = 0; g < region.inside_any.size(); ++g) {
        const std::vector<core::ReachCircle>& group = region.inside_any[g];
        // D-074 part 3: a shape of the group lies inside its outer disc, so only beyond that is it no help.
        std::vector<core::ReachCircle> bounds;
        for (const core::ReachShape& sh : group_shapes(region, g)) {
            bounds.push_back(core::ReachCircle{sh.obstacle.position, core::reach_shape_outer_radius_mm(sh), false,
                                               sh.model});
        }
        for (std::size_t i = 0; i < rays_; ++i) {
            // Perhaps inside one of the group's circles: the distances within the widened circle's closed interval.
            maybe.clear();
            for (const std::vector<core::ReachCircle>* list :
                 std::initializer_list<const std::vector<core::ReachCircle>*>{&group, &bounds}) {
                for (const core::ReachCircle& c : *list) {
                    const auto in = ray_inside(origin, cosines[i], sines[i], c, kRefusalMarginMm);
                    if (!in) continue;
                    const std::size_t from = first_at_least(in->first);
                    const std::size_t to = first_above(in->second);
                    if (from < to) maybe.emplace_back(from, to);
                }
            }
            std::sort(maybe.begin(), maybe.end());
            // Every distance outside all of them is refused.
            char* const ray = out + i * spots_;
            std::size_t next = 0;
            for (const auto& [from, to] : maybe) {
                if (from > next) std::fill(ray + next, ray + from, char{1});
                next = std::max(next, to);
            }
            if (next < spots_) std::fill(ray + next, ray + spots_, char{1});
        }
    }
}

bool in_region(const ReachRegion& region, const core::Position& p) {
    if (region.empty) return false;
    const core::Rect& b = region.bounds;
    if (p.x < b.min_x || p.x > b.max_x || p.y < b.min_y || p.y > b.max_y) return false;
    // Plain loops (the auto moves call this for thousands of candidate spots). M54: hypot_less, the same test.
    const auto inside = [&](const core::ReachCircle& c) {
        return hypot_less(p.x - c.centre.x, p.y - c.centre.y, c.radius_mm);
    };
    for (std::size_t i = 0; i < region.inside_all.size(); ++i) {
        if (!inside(region.inside_all[i])) return false;
    }
    for (std::size_t g = 0; g < region.inside_any.size(); ++g) {
        const std::vector<core::ReachCircle>& group = region.inside_any[g];
        bool any = false;
        for (std::size_t i = 0; i < group.size() && !any; ++i) any = inside(group[i]);
        if (!any && g < region.inside_any_shapes.size()) {  // D-074 part 3
            const std::vector<core::ReachShape>& shapes = region.inside_any_shapes[g];
            for (std::size_t i = 0; i < shapes.size() && !any; ++i) any = shape_holds(shapes[i], p);
        }
        if (!any) return false;
    }
    for (std::size_t i = 0; i < region.outside_all.size(); ++i) {
        if (inside(region.outside_all[i])) return false;
    }
    for (std::size_t i = 0; i < region.outside_shadows.size(); ++i) {
        const Shadow& s = region.outside_shadows[i];
        if (point_segment_closer_than(s.disc.centre, s.from, p, s.disc.radius_mm)) return false;
    }
    for (std::size_t i = 0; i < region.outside_shapes.size(); ++i) {
        if (shape_holds(region.outside_shapes[i], p)) return false;
    }
    for (std::size_t i = 0; i < region.outside_shape_shadows.size(); ++i) {
        if (shadow_holds(region.outside_shape_shadows[i], p)) return false;
    }
    return true;
}

}  // namespace wh40k::client
