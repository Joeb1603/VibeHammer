#include "drop_fix.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

#include "core/geometry/engagement.hpp"

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

// The region's boundary pieces: the edges, every circle, and each shadow's two tangent lines (D-044: the
// shadow is the disc and the wedge between the tangents from `from`), with the tangent points.
struct Pieces {
    std::vector<Circle> circles;
    std::vector<Line> lines;
    std::vector<Vec> vertices;
};

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
    return out;
}

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
    const Pieces all = pieces_of(in);
    Pieces nearby;
    for (const Circle& c : all.circles) {
        if (std::fabs(norm(q - c.c) - c.r) <= limit) nearby.circles.push_back(c);
    }
    for (const Line& l : all.lines) {
        if (std::fabs(cross(q - l.a, l.d)) <= limit) nearby.lines.push_back(l);
    }
    std::vector<Vec> candidates;
    for (const Vec& v : all.vertices) candidates.push_back(v);
    for (const Circle& c : nearby.circles) candidates.push_back(project(c, q));
    for (const Line& l : nearby.lines) candidates.push_back(project(l, q));
    for (std::size_t i = 0; i < nearby.circles.size(); ++i) {
        for (std::size_t j = i + 1; j < nearby.circles.size(); ++j) intersect(nearby.circles[i], nearby.circles[j], candidates);
        for (const Line& l : nearby.lines) intersect(nearby.circles[i], l, candidates);
    }
    for (std::size_t i = 0; i < nearby.lines.size(); ++i) {
        for (std::size_t j = i + 1; j < nearby.lines.size(); ++j) intersect(nearby.lines[i], nearby.lines[j], candidates);
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
    const Pieces all = pieces_of(in);
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
    std::erase_if(out.inside_any, [](const std::vector<core::ReachCircle>& group) {
        return !group.empty() &&
               std::all_of(group.begin(), group.end(), [](const core::ReachCircle& c) { return c.engagement; });
    });
    return out;
}

std::optional<DropProblem> drop_problem(const ReachRegion& region, const core::Position& p) {
    if (region.empty) return DropProblem::CannotMove;
    // The same tests as in_region.
    const auto inside = [&](const core::ReachCircle& c) {
        return std::hypot(p.x - c.centre.x, p.y - c.centre.y) < c.radius_mm;
    };
    if (std::any_of(region.outside_shadows.begin(), region.outside_shadows.end(), [&](const Shadow& s) {
            return point_segment_distance(s.disc.centre, s.from, p) < s.disc.radius_mm;
        })) {
        return DropProblem::ThroughEnemyModel;
    }
    if (std::any_of(region.outside_all.begin(), region.outside_all.end(), inside)) return DropProblem::OnModel;
    if (!std::all_of(region.inside_all.begin(), region.inside_all.end(), inside)) return DropProblem::TooFar;
    const core::Rect& b = region.bounds;
    if (p.x < b.min_x || p.x > b.max_x || p.y < b.min_y || p.y > b.max_y) return DropProblem::OffBattlefield;
    for (const auto& group : region.inside_any) {
        if (!std::any_of(group.begin(), group.end(), inside)) return DropProblem::NotCloser;
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
    if (*problem != DropProblem::ThroughEnemyModel) spot = nearest(in, check, vec(from), vec(drop));
    if (!spot) {
        spot = along(in, check, vec(from), vec(drop));
        how = FixedDrop::How::AlongDrag;
    }
    if (!spot || norm(*spot - vec(from)) <= core::EPS_MM) return std::nullopt;  // it wouldn't move
    return FixedDrop{position(*spot), *problem, how};
}

std::string drop_problem_text(DropProblem problem) {
    switch (problem) {
        case DropProblem::CannotMove: return "the model can't move any further";
        case DropProblem::ThroughEnemyModel: return "its line ran into an enemy model (03.01)";
        case DropProblem::OnModel: return "it was on another model (03.01)";
        case DropProblem::TooFar: return "it was further than the model can move";
        case DropProblem::OffBattlefield: return "it was off the battlefield (03.01)";
        case DropProblem::NotCloser: return "it didn't end closer to the target";
    }
    return "";
}

std::string fix_message(const FixedDrop& fixed) {
    const std::string head = fixed.how == FixedDrop::How::AlongDrag ? "Moved as far as it can along the drag: "
                                                                    : "Moved to the nearest spot it can go: ";
    return head + drop_problem_text(fixed.problem);
}

}  // namespace wh40k::client
