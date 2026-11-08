// D-074 (M64): 06.01 line of sight when an oval base is involved (the observer, the observed model or a blocker).
// Inputs with only round bases go to the D-017 code in visibility.cpp, bit-identical.

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/geometry/oval.hpp"
#include "core/geometry/units.hpp"
#include "core/geometry/visibility.hpp"

namespace wh40k::core {

namespace {

using oval_detail::Ellipse;
using oval_detail::Vec;
using oval_detail::dot;
using oval_detail::length;
using oval_detail::perp;

constexpr double kEpsMm = static_cast<double>(EPS_MM);
constexpr double kChordSlackMm = 1e-6;  // a line tangent to a base still touches it (as D-017)
constexpr int kRoundSamples = 32;       // D-017's samples on a round base
constexpr int kOvalSamples = 64;        // D-074: samples on an oval base
constexpr int kTangentSamples = 360;    // D-074: normals tried (1 degree apart) to bracket the common tangents
constexpr int kRootIterations = 60;

// A base (grow 0), or a blocker widened by half the line's width (grow 0.5 mm).
struct Shape {
    Ellipse e;
    double grow = 0.0;
    bool is_round = true;
};

// The line a x + b y + c = 0 with a^2 + b^2 = 1. Direction (-b, a).
struct Line {
    double a = 0.0;
    double b = 0.0;
    double c = 0.0;
};

Shape shape_of(const PosedModel& m, double grow) {
    return Shape{oval_detail::ellipse_of(m.pose()), grow, m.base.shape == BaseShape::Round};
}

Vec unit_at_deg(double deg) {
    double s = 0.0;
    double c = 1.0;
    oval_detail::sin_cos_deg(deg, s, c);
    return Vec{c, s};
}

// The shape's extent from its centre along the unit normal n.
double support_radius(const Shape& s, Vec n) {
    const double du = dot(n, s.e.u);
    const double dv = dot(n, s.e.v);
    return std::sqrt(s.e.a * s.e.a * du * du + s.e.b * s.e.b * dv * dv) + s.grow;
}

double point_segment_distance(Vec p, Vec a, Vec b) {
    const Vec ab = b - a;
    const double len2 = dot(ab, ab);
    if (len2 <= 0.0) return length(p - a);
    double t = dot(p - a, ab) / len2;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return length(p - (a + ab * t));
}

// Positions along the line foot + t dir (t from the foot point) where it crosses the ellipse (without grow), or
// false if it misses. Closed form: the quadratic in t of the ellipse's equation.
bool ellipse_chord(Vec foot, Vec dir, const Ellipse& e, double& lo, double& hi) {
    const Vec q0 = foot - e.c;
    const double qu = dot(q0, e.u);
    const double qv = dot(q0, e.v);
    const double wu = dot(dir, e.u);
    const double wv = dot(dir, e.v);
    const double ia2 = 1.0 / (e.a * e.a);
    const double ib2 = 1.0 / (e.b * e.b);
    const double qa = wu * wu * ia2 + wv * wv * ib2;
    const double qb = 2.0 * (qu * wu * ia2 + qv * wv * ib2);
    const double qc = qu * qu * ia2 + qv * qv * ib2 - 1.0;
    if (!(qa > 0.0)) return false;
    const double disc = qb * qb - 4.0 * qa * qc;
    const double mid = -qb / (2.0 * qa);
    const double h = disc > 0.0 ? std::sqrt(disc) / (2.0 * qa) : 0.0;
    lo = mid - h;
    hi = mid + h;
    return true;
}

// Where the line crosses the base (D-017's chord), for the observer and the observed model.
bool chord(const Line& l, const Shape& s, double& lo, double& hi) {
    const Vec n{l.a, l.b};
    const Vec dir{-l.b, l.a};
    const Vec foot{-l.a * l.c, -l.b * l.c};
    const double off = dot(n, s.e.c) + l.c;  // signed distance from the line to the centre
    if (std::abs(off) > support_radius(s, n) + kChordSlackMm) return false;
    if (s.is_round) {
        const double r = s.e.a + s.grow;
        const double h = std::sqrt(std::max(0.0, r * r - off * off));
        const double t0 = dot(s.e.c - foot, dir);
        lo = t0 - h;
        hi = t0 + h;
        return true;
    }
    return ellipse_chord(foot, dir, s.e, lo, hi);
}

// The distance from segment [p, q] to the ellipse (0 if they meet). The distance to a convex set is convex along a
// line, so it is smallest where the line comes closest (inside the segment) or at the nearer end.
double segment_ellipse_distance(Vec p, Vec q, const Ellipse& e) {
    const Vec d = q - p;
    const double len = length(d);
    const auto at_point = [&](Vec x) { return std::max(0.0, oval_detail::point_ellipse_signed_distance(x, e)); };
    if (!(len > 0.0)) return at_point(p);
    const Vec dir = d * (1.0 / len);
    const Vec n = perp(dir);
    const double off = dot(n, e.c - p);  // signed distance from the line to the centre
    const double du = dot(n, e.u);
    const double dv = dot(n, e.v);
    const double sr = std::sqrt(e.a * e.a * du * du + e.b * e.b * dv * dv);
    if (std::abs(off) <= sr) {
        double t1 = 0.0;
        double t2 = 0.0;
        if (!ellipse_chord(p, dir, e, t1, t2)) return at_point(p);
        if (t2 >= 0.0 && t1 <= len) return 0.0;
        return at_point(t2 < 0.0 ? p : q);
    }
    // The line misses: the ellipse's closest point to it is its support point towards the line.
    const Vec m = (e.u * (e.a * e.a * du) + e.v * (e.b * e.b * dv)) * (1.0 / sr);
    const Vec sp = off > 0.0 ? e.c - m : e.c + m;
    const double ts = dot(sp - p, dir);
    if (ts >= 0.0 && ts <= len) return std::abs(off) - sr;
    return at_point(ts < 0.0 ? p : q);
}

// 06.01 / D-017: the 1 mm wide line along [p, q] passes the blocker; overlap up to EPS_MM is allowed.
bool blocks(Vec p, Vec q, const Shape& b) {
    if (b.is_round) return point_segment_distance(b.e.c, p, q) < (b.e.a + b.grow) - kEpsMm;
    return segment_ellipse_distance(p, q, b.e) < b.grow - kEpsMm;
}

bool segment_clear(Vec p, Vec q, const std::vector<Shape>& blockers) {
    for (const Shape& b : blockers) {
        if (blocks(p, q, b)) return false;
    }
    return true;
}

Line line_through(Vec p, Vec q) {
    const Vec d = q - p;
    const double len = length(d);
    const Vec n{-d.y / len, d.x / len};
    return Line{n.x, n.y, -dot(n, p)};
}

// As D-017: the shortest stretch of the line between the two bases must pass every blocker.
bool line_clear(const Line& l, const Shape& from, const Shape& to, const std::vector<Shape>& blockers) {
    double a_lo = 0.0;
    double a_hi = 0.0;
    double b_lo = 0.0;
    double b_hi = 0.0;
    if (!chord(l, from, a_lo, a_hi) || !chord(l, to, b_lo, b_hi)) return false;
    const Vec dir{-l.b, l.a};
    const Vec foot{-l.a * l.c, -l.b * l.c};
    double ta = 0.0;
    double tb = 0.0;
    if (a_hi <= b_lo) {
        ta = a_hi;
        tb = b_lo;
    } else if (b_hi <= a_lo) {
        ta = a_lo;
        tb = b_hi;
    } else {
        ta = tb = 0.5 * (std::max(a_lo, b_lo) + std::min(a_hi, b_hi));  // the bases touch along the line
    }
    return segment_clear(foot + dir * ta, foot + dir * tb, blockers);
}

// D-017's closed-form common tangents of two circles (radius a + grow).
void circle_tangents(const Shape& sa, const Shape& sb, std::vector<Line>& out) {
    const Vec c = sb.e.c - sa.e.c;
    const double z = dot(c, c);
    if (z <= 0.0) return;
    const double ra = sa.e.a + sa.grow;
    const double rb = sb.e.a + sb.grow;
    for (int i = -1; i <= 1; i += 2) {
        for (int j = -1; j <= 1; j += 2) {
            const double r1 = ra * i;
            const double r = rb * j - r1;
            double d = z - r * r;
            if (d < 0.0) continue;
            d = std::sqrt(d);
            Line l{(c.x * r + c.y * d) / z, (c.y * r - c.x * d) / z, r1};
            l.c -= l.a * sa.e.c.x + l.b * sa.e.c.y;
            out.push_back(l);
        }
    }
}

// The line with unit normal n tangent to X on its n side: n . x = n . c_X + s_X(n).
Line tangent_line(const Shape& x, Vec n) { return Line{n.x, n.y, -(dot(n, x.e.c) + support_radius(x, n))}; }

// A root of f in [lo, hi] (f(lo) and f(hi) of opposite signs) by bisection on the normal's angle.
template <class F>
double bisect(const F& f, double lo, double hi) {
    const bool lo_negative = f(lo) < 0.0;
    for (int i = 0; i < kRootIterations; ++i) {
        const double mid = 0.5 * (lo + hi);
        if ((f(mid) < 0.0) == lo_negative) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

// D-074: the common tangent lines of two shapes when one is an oval (support-line tangents), from the normals n where
// the support lines coincide:
// - outer (both shapes on the same side): h_X(n) = h_Y(n), roots of n . (c_X - c_Y) + s_X(n) - s_Y(n), bracketed by
//   sampling the normal every degree;
// - inner (on opposite sides): h_X(n) = -h_Y(-n), the ends of the arc of normals where
//   g(n) = n . (c_X - c_Y) + s_X(n) + s_Y(n) < 0 (the support function of X - Y: one arc, around its minimum, when
//   the shapes are apart). The minimum is found by sampling and a golden-section search, so a gap far narrower than a
//   degree of normals is still found.
void oval_tangents(const Shape& x, const Shape& y, std::vector<Line>& out) {
    const Vec dc = x.e.c - y.e.c;
    const auto outer = [&](double deg) {
        const Vec n = unit_at_deg(deg);
        return dot(n, dc) + support_radius(x, n) - support_radius(y, n);
    };
    const auto inner = [&](double deg) {
        const Vec n = unit_at_deg(deg);
        return dot(n, dc) + support_radius(x, n) + support_radius(y, n);
    };
    double prev = outer(0.0);
    double best_deg = 0.0;
    double best = inner(0.0);
    for (int k = 1; k <= kTangentSamples; ++k) {
        const double deg = 360.0 * static_cast<double>(k) / static_cast<double>(kTangentSamples);
        const double f = outer(deg);
        if ((prev < 0.0) != (f < 0.0)) {
            out.push_back(tangent_line(x, unit_at_deg(bisect(outer, deg - 360.0 / kTangentSamples, deg))));
        }
        prev = f;
        const double g = inner(deg);
        if (g < best) {
            best = g;
            best_deg = deg;
        }
    }
    // Golden-section search for the minimum of g within a sample spacing of the best sample.
    double lo = best_deg - 360.0 / kTangentSamples;
    double hi = best_deg + 360.0 / kTangentSamples;
    constexpr double kGolden = 0.6180339887498949;
    double m1 = hi - kGolden * (hi - lo);
    double m2 = lo + kGolden * (hi - lo);
    double g1 = inner(m1);
    double g2 = inner(m2);
    for (int i = 0; i < kRootIterations; ++i) {
        if (g1 < g2) {
            hi = m2;
            m2 = m1;
            g2 = g1;
            m1 = hi - kGolden * (hi - lo);
            g1 = inner(m1);
        } else {
            lo = m1;
            m1 = m2;
            g1 = g2;
            m2 = lo + kGolden * (hi - lo);
            g2 = inner(m2);
        }
    }
    const double min_deg = 0.5 * (lo + hi);
    if (!(inner(min_deg) < 0.0)) return;  // overlapping (or touching): no inner tangents
    // g(n) + g(-n) >= 0, so g is not negative half a turn away: each end of the arc is within half a turn.
    out.push_back(tangent_line(x, unit_at_deg(bisect(inner, min_deg, min_deg + 180.0))));
    out.push_back(tangent_line(x, unit_at_deg(bisect(inner, min_deg, min_deg - 180.0))));
}

void common_tangents(const Shape& a, const Shape& b, std::vector<Line>& out) {
    if (a.is_round && b.is_round) {
        circle_tangents(a, b, out);
    } else {
        oval_tangents(a, b, out);
    }
}

// The centre and points round the edge: D-017's 32 on a round base, 64 on an oval.
std::vector<Vec> sample_points(const Shape& s) {
    std::vector<Vec> out{s.e.c};
    const int n = s.is_round ? kRoundSamples : kOvalSamples;
    for (int k = 0; k < n; ++k) {
        const Vec t = unit_at_deg(360.0 * static_cast<double>(k) / static_cast<double>(n));
        out.push_back(s.e.c + s.e.u * (s.e.a * t.x) + s.e.v * (s.e.b * t.y));
    }
    return out;
}

}  // namespace

bool has_line_of_sight(const PosedModel& observer, const PosedModel& observed, std::span<const PosedModel> blockers) {
    const auto is_round = [](const PosedModel& m) { return m.base.shape == BaseShape::Round; };
    if (is_round(observer) && is_round(observed) && std::all_of(blockers.begin(), blockers.end(), is_round)) {
        // Round bases only: D-017, bit-identical.
        std::vector<PlacedModel> round_blockers;
        round_blockers.reserve(blockers.size());
        for (const PosedModel& b : blockers) round_blockers.push_back(PlacedModel{b.id, b.position, b.base});
        return has_line_of_sight(PlacedModel{observer.id, observer.position, observer.base},
                                 PlacedModel{observed.id, observed.position, observed.base}, round_blockers);
    }
    const double half_width = kLineOfSightWidthMm / 2.0;
    const Shape from = shape_of(observer, 0.0);
    const Shape to = shape_of(observed, 0.0);

    // Only blockers near the hull of the two bases can matter (bounding circles: the semi-major axis).
    std::vector<Shape> near;
    const double reach = std::max(from.e.a, to.e.a) + half_width;
    for (const PosedModel& b : blockers) {
        const Shape s = shape_of(b, half_width);
        if (point_segment_distance(s.e.c, from.e.c, to.e.c) < s.e.a + s.grow + reach) near.push_back(s);
    }
    if (near.empty()) return true;

    // D-017 / D-074 candidate lines: centre to centre, every pair of sample points on the two bases, and every common
    // tangent of two of the shapes (the bases and the widened blockers).
    if (length(to.e.c - from.e.c) > 0.0 && line_clear(line_through(from.e.c, to.e.c), from, to, near)) return true;
    const std::vector<Vec> pa = sample_points(from);
    const std::vector<Vec> pb = sample_points(to);
    for (const Vec& p : pa) {
        for (const Vec& q : pb) {
            if (length(q - p) <= 0.0) continue;
            if (line_clear(line_through(p, q), from, to, near)) return true;
        }
    }
    std::vector<Shape> shapes{from, to};
    shapes.insert(shapes.end(), near.begin(), near.end());
    std::vector<Line> lines;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        for (std::size_t j = i + 1; j < shapes.size(); ++j) {
            lines.clear();
            common_tangents(shapes[i], shapes[j], lines);
            for (const Line& l : lines) {
                if (line_clear(l, from, to, near)) return true;
            }
        }
    }
    return false;
}

}  // namespace wh40k::core
