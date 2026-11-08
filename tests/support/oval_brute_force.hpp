#pragma once

// M64 (D-074): brute-force references for the oval geometry kernel, built only from boundary sampling, the support
// functions in closed form and golden-section refinement, so they share no code with GJK, the polygon bracket or
// Eberly's bisection. std::sin / std::cos are fine here (tests only).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

#include "core/geometry/pose.hpp"
#include "core/geometry/primitives.hpp"

namespace wh40k::test::oval {

constexpr double kPi = std::numbers::pi;

// SplitMix64 with a fixed seed per test.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uniform(double lo, double hi) {
        return lo + (hi - lo) * static_cast<double>(next() >> 11) / 9007199254740992.0;
    }
    float uniform_f(double lo, double hi) { return static_cast<float>(uniform(lo, hi)); }
    bool chance(double p) { return uniform(0.0, 1.0) < p; }

private:
    std::uint64_t state_;
};

struct P2 {
    double x = 0.0;
    double y = 0.0;
};

// An ellipse from a pose by the D-074 convention, independently of oval_detail: at facing f the major axis points
// along (-sin f, cos f).
struct Ell {
    P2 c;
    P2 u{0.0, 1.0};
    P2 v{1.0, 0.0};
    double a = 0.0;
    double b = 0.0;
};

inline Ell ell_of(const core::BasePose& p) {
    Ell e;
    e.c = P2{p.position.x, p.position.y};
    if (p.base.shape == core::BaseShape::Round) {
        e.a = e.b = 0.5 * p.base.diameter_mm;
        return e;
    }
    const double f = static_cast<double>(p.facing_deg) * kPi / 180.0;
    e.u = P2{-std::sin(f), std::cos(f)};
    e.v = P2{std::cos(f), std::sin(f)};
    e.a = 0.5 * p.base.length_mm;
    e.b = 0.5 * p.base.diameter_mm;
    return e;
}

inline P2 boundary(const Ell& e, double t) {
    const double ct = std::cos(t);
    const double st = std::sin(t);
    return P2{e.c.x + e.a * ct * e.u.x + e.b * st * e.v.x, e.c.y + e.a * ct * e.u.y + e.b * st * e.v.y};
}

inline double dist(P2 p, P2 q) { return std::hypot(p.x - q.x, p.y - q.y); }

inline bool inside(const Ell& e, P2 p) {
    const double dx = p.x - e.c.x;
    const double dy = p.y - e.c.y;
    const double pu = (dx * e.u.x + dy * e.u.y) / e.a;
    const double pv = (dx * e.v.x + dy * e.v.y) / e.b;
    return pu * pu + pv * pv < 1.0;
}

// Golden-section minimum of f on [lo, hi].
template <class F>
double golden_min(const F& f, double lo, double hi, int iterations = 80) {
    constexpr double g = 0.6180339887498949;
    double m1 = hi - g * (hi - lo);
    double m2 = lo + g * (hi - lo);
    double f1 = f(m1);
    double f2 = f(m2);
    for (int i = 0; i < iterations; ++i) {
        if (f1 < f2) {
            hi = m2;
            m2 = m1;
            f2 = f1;
            m1 = hi - g * (hi - lo);
            f1 = f(m1);
        } else {
            lo = m1;
            m1 = m2;
            f1 = f2;
            m2 = lo + g * (hi - lo);
            f2 = f(m2);
        }
    }
    return std::min(f1, f2);
}

// The smallest value of f over [0, 2 pi): `samples` samples, then each local minimum among them refined.
template <class F>
double sampled_min_on_circle(const F& f, int samples) {
    std::vector<double> v(static_cast<std::size_t>(samples));
    const double h = 2.0 * kPi / samples;
    for (int i = 0; i < samples; ++i) v[static_cast<std::size_t>(i)] = f(h * i);
    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i < samples; ++i) {
        const double a = v[static_cast<std::size_t>((i + samples - 1) % samples)];
        const double b = v[static_cast<std::size_t>(i)];
        const double c = v[static_cast<std::size_t>((i + 1) % samples)];
        if (b <= a && b <= c) best = std::min(best, golden_min(f, h * (i - 1), h * (i + 1)));
    }
    return best;
}

// Signed distance from p to the ellipse by boundary sampling (negative inside).
inline double point_ellipse_sd(P2 p, const Ell& e) {
    const double d = sampled_min_on_circle([&](double t) { return dist(boundary(e, t), p); }, 1024);
    return inside(e, p) ? -d : d;
}

// 01.04 by boundary sampling: 0 when the bases overlap (a sampled boundary point of one inside the other, or the
// boundaries meet), else the smallest distance between the two boundaries (128 x 128 samples, then the best pair
// refined by nested golden-section searches).
inline double ellipse_ellipse_distance(const Ell& a, const Ell& b) {
    constexpr int n = 128;
    const double h = 2.0 * kPi / n;
    std::vector<P2> pa;
    std::vector<P2> pb;
    for (int i = 0; i < n; ++i) {
        pa.push_back(boundary(a, h * i));
        pb.push_back(boundary(b, h * i));
    }
    if (inside(b, a.c) || inside(a, b.c)) return 0.0;
    for (int i = 0; i < n; ++i) {
        if (inside(b, pa[static_cast<std::size_t>(i)]) || inside(a, pb[static_cast<std::size_t>(i)])) return 0.0;
    }
    double best = std::numeric_limits<double>::infinity();
    int bi = 0;
    int bj = 0;
    for (int i = 0; i < n; ++i) {
        const P2 p = pa[static_cast<std::size_t>(i)];
        for (int j = 0; j < n; ++j) {
            const P2 q = pb[static_cast<std::size_t>(j)];
            const double d = (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y);  // squared
            if (d < best) {
                best = d;
                bi = i;
                bj = j;
            }
        }
    }
    const double ti = h * bi;
    const double tj = h * bj;
    const auto inner = [&](double t) {
        const P2 p = boundary(a, t);
        return golden_min([&](double s) { return dist(p, boundary(b, s)); }, tj - 2.0 * h, tj + 2.0 * h, 50);
    };
    return std::max(0.0, golden_min(inner, ti - 2.0 * h, ti + 2.0 * h, 50));
}

// The signed distance between two convex sets by the support-function duality: the largest separation along a
// direction, max over n of [min over B of n . x - max over A of n . x]. `extra_a(n)` adds to A's support (a sweep).
inline double ellipse_support(const Ell& e, P2 n) {
    const double du = n.x * e.u.x + n.y * e.u.y;
    const double dv = n.x * e.v.x + n.y * e.v.y;
    return std::sqrt(e.a * e.a * du * du + e.b * e.b * dv * dv);
}

template <class Extra>
double support_signed_distance(const Ell& a, const Ell& b, const Extra& extra_a, int samples = 4096) {
    const auto g = [&](double th) {
        const P2 n{std::cos(th), std::sin(th)};
        return -(n.x * (b.c.x - a.c.x) + n.y * (b.c.y - a.c.y) - ellipse_support(a, n) - ellipse_support(b, n) -
                 extra_a(n));
    };
    return -sampled_min_on_circle(g, samples);
}

inline double support_signed_distance(const Ell& a, const Ell& b) {
    return support_signed_distance(a, b, [](P2) { return 0.0; });
}

}  // namespace wh40k::test::oval
