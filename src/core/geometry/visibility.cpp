#include "core/geometry/visibility.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "core/geometry/distance.hpp"
#include "core/geometry/units.hpp"

namespace wh40k::core {

namespace {

// Internal geometry in double precision; inputs and the tolerance stay in float mm.
struct Vec {
    double x = 0.0;
    double y = 0.0;
};

Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y}; }
Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y}; }
Vec operator*(Vec a, double s) { return {a.x * s, a.y * s}; }
double dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y; }
double length(Vec a) { return std::sqrt(dot(a, a)); }

struct Circle {
    Vec c;
    double r = 0.0;
};

// The line a*x + b*y + c = 0 with a^2 + b^2 = 1. Direction (-b, a).
struct Line {
    double a = 0.0;
    double b = 0.0;
    double c = 0.0;
};

constexpr int kSamplesPerBase = 32;
constexpr double kChordSlackMm = 1e-6;  // a line tangent to a base still touches it

Circle circle_of(const PlacedModel& m, double extra = 0.0) {
    return Circle{Vec{m.position.x, m.position.y}, static_cast<double>(base_radius(m.base)) + extra};
}

double point_segment_distance(Vec p, Vec a, Vec b) {
    const Vec ab = b - a;
    const double len2 = dot(ab, ab);
    if (len2 <= 0.0) {
        return length(p - a);
    }
    double t = dot(p - a, ab) / len2;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return length(p - (a + ab * t));
}

// 06.01: the 1 mm wide line from p to q passes no blocker. Blockers are already inflated by half the
// line's width, so the test is on the centre line. Overlap up to EPS_MM is allowed.
bool segment_clear(Vec p, Vec q, const std::vector<Circle>& blockers) {
    for (const Circle& b : blockers) {
        if (point_segment_distance(b.c, p, q) < b.r - static_cast<double>(EPS_MM)) {
            return false;
        }
    }
    return true;
}

Line line_through(Vec p, Vec q) {
    const Vec d = q - p;
    const double len = length(d);
    const Vec n{-d.y / len, d.x / len};
    return Line{n.x, n.y, -dot(n, p)};
}

struct Chord {
    double lo = 0.0;
    double hi = 0.0;
};

// Where the line crosses the circle, as positions along the line from its foot point.
bool chord(const Line& l, const Circle& c, Chord& out) {
    const double dist = std::abs(l.a * c.c.x + l.b * c.c.y + l.c);
    if (dist > c.r + kChordSlackMm) {
        return false;
    }
    const double h = std::sqrt(std::max(0.0, c.r * c.r - dist * dist));
    const Vec dir{-l.b, l.a};
    const Vec foot{-l.a * l.c, -l.b * l.c};
    const double t0 = dot(c.c - foot, dir);
    out = Chord{t0 - h, t0 + h};
    return true;
}

// Tests the part of line `l` between the two bases: any part of each base may be the end, so the
// shortest stretch between them is the one that matters.
bool line_clear(const Line& l, const Circle& from, const Circle& to, const std::vector<Circle>& blockers) {
    Chord ca;
    Chord cb;
    if (!chord(l, from, ca) || !chord(l, to, cb)) {
        return false;
    }
    const Vec dir{-l.b, l.a};
    const Vec foot{-l.a * l.c, -l.b * l.c};
    double ta = 0.0;
    double tb = 0.0;
    if (ca.hi <= cb.lo) {
        ta = ca.hi;
        tb = cb.lo;
    } else if (cb.hi <= ca.lo) {
        ta = ca.lo;
        tb = cb.hi;
    } else {
        ta = tb = 0.5 * (std::max(ca.lo, cb.lo) + std::min(ca.hi, cb.hi));  // bases touch along the line
    }
    return segment_clear(foot + dir * ta, foot + dir * tb, blockers);
}

// The common tangent lines of two circles (up to four).
void common_tangents(const Circle& a, const Circle& b, std::vector<Line>& out) {
    const Vec c = b.c - a.c;
    const double z = dot(c, c);
    if (z <= 0.0) {
        return;
    }
    for (int i = -1; i <= 1; i += 2) {
        for (int j = -1; j <= 1; j += 2) {
            const double r1 = a.r * i;
            const double r = b.r * j - r1;
            double d = z - r * r;
            if (d < 0.0) {
                continue;  // one circle inside the other: no such tangent
            }
            d = std::sqrt(d);
            Line l{(c.x * r + c.y * d) / z, (c.y * r - c.x * d) / z, r1};
            l.c -= l.a * a.c.x + l.b * a.c.y;
            out.push_back(l);
        }
    }
}

std::vector<Vec> sample_points(const Circle& c) {
    std::vector<Vec> out{c.c};
    for (int k = 0; k < kSamplesPerBase; ++k) {
        const double angle = 2.0 * std::numbers::pi * k / kSamplesPerBase;
        out.push_back(Vec{c.c.x + c.r * std::cos(angle), c.c.y + c.r * std::sin(angle)});
    }
    return out;
}

}  // namespace

bool has_line_of_sight(const PlacedModel& observer, const PlacedModel& observed,
                       std::span<const PlacedModel> blockers) {
    const Circle from = circle_of(observer);
    const Circle to = circle_of(observed);
    const double half_width = kLineOfSightWidthMm / 2.0;

    // Only blockers near the hull of the two bases can matter.
    std::vector<Circle> near;
    const double reach = std::max(from.r, to.r) + half_width;
    for (const PlacedModel& b : blockers) {
        const Circle inflated = circle_of(b, half_width);
        if (point_segment_distance(inflated.c, from.c, to.c) < inflated.r + reach) {
            near.push_back(inflated);
        }
    }
    if (near.empty()) {
        return true;
    }

    // D-017 candidate lines: base centre to base centre, every pair of sample points on the two bases,
    // and every common tangent of two of the circles (the bases and the inflated blockers).
    if (length(to.c - from.c) > 0.0 && line_clear(line_through(from.c, to.c), from, to, near)) {
        return true;
    }
    const std::vector<Vec> pa = sample_points(from);
    const std::vector<Vec> pb = sample_points(to);
    for (const Vec& p : pa) {
        for (const Vec& q : pb) {
            if (length(q - p) <= 0.0) {
                continue;
            }
            if (line_clear(line_through(p, q), from, to, near)) {
                return true;
            }
        }
    }
    std::vector<Circle> circles{from, to};
    circles.insert(circles.end(), near.begin(), near.end());
    std::vector<Line> lines;
    for (std::size_t i = 0; i < circles.size(); ++i) {
        for (std::size_t j = i + 1; j < circles.size(); ++j) {
            common_tangents(circles[i], circles[j], lines);
        }
    }
    for (const Line& l : lines) {
        if (line_clear(l, from, to, near)) {
            return true;
        }
    }
    return false;
}

}  // namespace wh40k::core
