#include "reach_area.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

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

}  // namespace

float point_segment_distance(const core::Position& p, const core::Position& a, const core::Position& b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

ReachRegion model_region(const core::MoveReach& reach) {
    ReachRegion region;
    region.bounds = reach.centre_bounds;
    region.inside_all.push_back(core::ReachCircle{reach.from, reach.remaining_mm, false, {}});
    region.inside_any = reach.required_any;
    region.outside_all = reach.forbidden;
    for (const core::ReachCircle& c : reach.blocking) region.outside_shadows.push_back(Shadow{reach.from, c});  // D-044
    region.empty = reach.immobile || !(reach.remaining_mm > 0.0f);
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
        for (const auto& group : own.inside_any) {
            std::vector<core::ReachCircle> g;
            for (const core::ReachCircle& c : group) g.push_back(shifted(c, dx, dy));
            region.inside_any.push_back(std::move(g));
        }
        for (const core::ReachCircle& c : own.outside_all) {
            bool own_unit = false;
            for (const core::Model& o : u->models) own_unit = own_unit || o.id == c.model;
            if (!own_unit) region.outside_all.push_back(shifted(c, dx, dy));
        }
        for (const Shadow& s : own.outside_shadows) region.outside_shadows.push_back(shifted(s, dx, dy));
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
        for (const auto& group : region.inside_any) {
            if (parts.empty()) break;
            std::vector<Interval> chords;
            for (const core::ReachCircle& c : group) {
                if (const auto ch = chord(c, y)) chords.push_back(*ch);
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
        for (const Interval& p : parts) {
            if (p.first < p.second) out.push_back(ReachSpan{y, p.first, p.second});
        }
    }
    return out;
}

bool in_region(const ReachRegion& region, const core::Position& p) {
    if (region.empty) return false;
    const core::Rect& b = region.bounds;
    if (p.x < b.min_x || p.x > b.max_x || p.y < b.min_y || p.y > b.max_y) return false;
    // Plain loops (the auto moves call this for thousands of candidate spots).
    const auto inside = [&](const core::ReachCircle& c) {
        return std::hypot(p.x - c.centre.x, p.y - c.centre.y) < c.radius_mm;
    };
    for (std::size_t i = 0; i < region.inside_all.size(); ++i) {
        if (!inside(region.inside_all[i])) return false;
    }
    for (std::size_t g = 0; g < region.inside_any.size(); ++g) {
        const std::vector<core::ReachCircle>& group = region.inside_any[g];
        bool any = false;
        for (std::size_t i = 0; i < group.size() && !any; ++i) any = inside(group[i]);
        if (!any) return false;
    }
    for (std::size_t i = 0; i < region.outside_all.size(); ++i) {
        if (inside(region.outside_all[i])) return false;
    }
    for (std::size_t i = 0; i < region.outside_shadows.size(); ++i) {
        const Shadow& s = region.outside_shadows[i];
        if (point_segment_distance(s.disc.centre, s.from, p) < s.disc.radius_mm) return false;
    }
    return true;
}

}  // namespace wh40k::client
