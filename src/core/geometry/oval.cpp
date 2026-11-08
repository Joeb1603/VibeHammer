#include "core/geometry/oval.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

#include "core/geometry/distance.hpp"
#include "core/geometry/units.hpp"

namespace wh40k::core {

namespace oval_detail {

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

// atan(t) in radians for t in [0, 1] (+ - * / only).
double atan_unit(double t) noexcept {
    // atan(t) = pi/6 + atan((t sqrt3 - 1) / (t + sqrt3)) brings t in (tan 15deg, 1] to [-tan 15deg, tan 15deg].
    constexpr double kTan15 = 0.2679491924311227;  // 2 - sqrt(3)
    constexpr double kSqrt3 = 1.7320508075688772;
    double offset = 0.0;
    if (t > kTan15) {
        t = (t * kSqrt3 - 1.0) / (t + kSqrt3);
        offset = kPi / 6.0;
    }
    // |t| <= tan 15deg: the Taylor series to t^31 (the next term is below 4e-21).
    const double t2 = t * t;
    double sum = 0.0;
    for (int k = 15; k >= 0; --k) {
        sum = sum * t2 + ((k % 2 == 0) ? 1.0 : -1.0) / static_cast<double>(2 * k + 1);
    }
    return offset + t * sum;
}

double robust_length(double v0, double v1) noexcept {
    const double m = std::max(std::abs(v0), std::abs(v1));
    if (m == 0.0) return 0.0;
    const double r0 = v0 / m;
    const double r1 = v1 / m;
    return m * std::sqrt(r0 * r0 + r1 * r1);
}

// Eberly's GetRoot: the root of F(s) = (r0 z0 / (s + r0))^2 + (z1 / (s + 1))^2 - 1 on s > -1 by bisection, here
// in w = s + 1 (D-074): near the major axis inside the evolute the root is s = -1 + tiny, and bisecting s itself
// would leave s + 1 (so the closest point) with few correct digits. Returns w; r0m1 = r0 - 1.
double eberly_root(double r0, double r0m1, double z0, double z1, double g) noexcept {
    const double n0 = r0 * z0;
    double w0 = z1;                                      // s0 = z1 - 1: F >= 0
    double w1 = g < 0.0 ? 1.0 : robust_length(n0, z1);  // s1 = 0 inside, |(n0, z1)| - 1 outside: F <= 0
    double w = 0.0;
    for (int i = 0; i < kEberlyMaxIterations; ++i) {
        w = 0.5 * (w0 + w1);
        if (w == w0 || w == w1) break;
        const double ratio0 = n0 / (w + r0m1);
        const double ratio1 = z1 / w;
        g = ratio0 * ratio0 + ratio1 * ratio1 - 1.0;
        if (g > 0.0) {
            w0 = w;
        } else if (g < 0.0) {
            w1 = w;
        } else {
            break;
        }
    }
    return w;
}

// ---- Signed distance from a point to a convex set given by its support function ----

constexpr double kTinySq = 1e-24;  // |v|^2 below this: the origin is on the set (to 1e-12 mm)

// The point of segment [a, b] closest to the origin; `keep` = 0 (a), 1 (b) or 2 (inside the segment).
Vec closest_on_segment(Vec a, Vec b, int& keep) noexcept {
    const Vec ab = b - a;
    const double len2 = dot(ab, ab);
    if (!(len2 > 0.0)) {
        keep = 0;
        return a;
    }
    const double t = -dot(a, ab) / len2;
    if (t <= 0.0) {
        keep = 0;
        return a;
    }
    if (t >= 1.0) {
        keep = 1;
        return b;
    }
    keep = 2;
    // The foot of the perpendicular, as (n . a) n with n the unit normal: accurate when it is near the origin.
    const double len = std::sqrt(len2);
    const Vec n{-ab.y / len, ab.x / len};
    return n * dot(n, a);
}

// GJK's simplex step: reduces s[0..n) to the smallest face holding its point closest to the origin and returns that
// point in v. Returns true when the triangle contains the origin.
bool reduce_simplex(std::array<Vec, 3>& s, int& n, Vec& v) noexcept {
    if (n == 1) {
        v = s[0];
        return false;
    }
    if (n == 2) {
        int keep = 0;
        v = closest_on_segment(s[0], s[1], keep);
        if (keep == 0) {
            n = 1;
        } else if (keep == 1) {
            s[0] = s[1];
            n = 1;
        }
        return false;
    }
    const Vec a = s[0];
    const Vec b = s[1];
    const Vec c = s[2];
    const double area = cross(b - a, c - a);
    if (area != 0.0) {
        const double e0 = cross(b - a, Vec{} - a);
        const double e1 = cross(c - b, Vec{} - b);
        const double e2 = cross(a - c, Vec{} - c);
        if ((area > 0.0 && e0 >= 0.0 && e1 >= 0.0 && e2 >= 0.0) ||
            (area < 0.0 && e0 <= 0.0 && e1 <= 0.0 && e2 <= 0.0)) {
            v = Vec{};
            return true;
        }
    }
    // The closest point is on an edge (or a vertex).
    const std::array<std::array<int, 2>, 3> edges{{{0, 1}, {1, 2}, {2, 0}}};
    double best = std::numeric_limits<double>::infinity();
    std::array<Vec, 3> kept{};
    int kept_n = 0;
    for (const auto& e : edges) {
        int keep = 0;
        const Vec p = closest_on_segment(s[static_cast<std::size_t>(e[0])], s[static_cast<std::size_t>(e[1])], keep);
        const double d = dot(p, p);
        if (d < best) {
            best = d;
            v = p;
            if (keep == 2) {
                kept[0] = s[static_cast<std::size_t>(e[0])];
                kept[1] = s[static_cast<std::size_t>(e[1])];
                kept_n = 2;
            } else {
                kept[0] = s[static_cast<std::size_t>(e[keep == 0 ? 0 : 1])];
                kept_n = 1;
            }
        }
    }
    s = kept;
    n = kept_n;
    return false;
}

// GJK: the distance from the origin to the convex set E (support function `sup`), starting from `v`, a point of E.
// True when it certifies that E does not contain the origin, with the distance bracketed to kGjkToleranceMm.
template <class Sup>
bool gjk_separated(const Sup& sup, Vec v, double threshold, SignedDistance& out) noexcept {
    std::array<Vec, 3> s{};
    int n = 0;
    double lower = 0.0;  // a certified lower bound on the distance (from the support planes)
    for (int it = 0; it < kGjkMaxIterations; ++it) {
        const double vv = dot(v, v);
        if (!(vv > kTinySq)) return false;
        const double vlen = std::sqrt(vv);
        const Vec w = sup(v * (-1.0 / vlen));
        // E lies on the far side of the support line through w with normal -v: every point x has x . v/|v| >= w . v/|v|.
        lower = std::min(vlen, std::max(lower, dot(v, w) / vlen));  // (rounding can put it an ulp above vlen)
        if (lower > 0.0 && (vlen - lower <= kGjkToleranceMm || lower >= threshold)) {
            out = SignedDistance{0.5 * (lower + vlen), lower, vlen, it + 1};
            return true;
        }
        for (int k = 0; k < n; ++k) {
            if (w.x == s[static_cast<std::size_t>(k)].x && w.y == s[static_cast<std::size_t>(k)].y) return false;
        }
        s[static_cast<std::size_t>(n++)] = w;
        Vec next{};
        if (reduce_simplex(s, n, next)) return false;  // the origin is inside
        if (!(dot(next, next) < vv)) return false;     // no progress
        v = next;
    }
    return false;
}

// One normal of the bracketing polygons: unit outward normal n, the support point p and value h = n . p.
struct BracketNode {
    Vec n;
    Vec p;
    double h = 0.0;
};

constexpr int kBracketMaxIterations = 96;
constexpr int kBracketMaxNodes = 8 + 3 * kBracketMaxIterations;

// For K the Minkowski sum of two ellipses (static oval-oval): a lower bound on K's radius of curvature over an arc of
// normals. An ellipse's radius of curvature at normal n is a^2 b^2 / s(n)^3 with s(n)^2 = b^2 + (a^2 - b^2)(n . u)^2,
// so it is smallest where (n . u)^2 is largest (1 if the arc holds +-u, else at an end), and K's is the sum.
struct CurvatureBound {
    std::array<Vec, 2> u{};
    std::array<double, 2> a{};
    std::array<double, 2> b{};
    [[nodiscard]] double rho_lo(Vec n0, Vec n1) const noexcept {
        double rho = 0.0;
        for (std::size_t k = 0; k < 2; ++k) {
            const Vec w = u[k];
            const auto holds = [&](Vec d) { return cross(n0, d) >= 0.0 && cross(d, n1) >= 0.0; };
            double c2 = std::max(dot(n0, w) * dot(n0, w), dot(n1, w) * dot(n1, w));
            if (holds(w) || holds(w * -1.0)) c2 = 1.0;
            const double s2 = b[k] * b[k] + (a[k] * a[k] - b[k] * b[k]) * c2;
            const double sigma = std::sqrt(s2);
            if (sigma > 0.0) rho += a[k] * a[k] * b[k] * b[k] / (s2 * sigma);
        }
        return rho;
    }
};

// min over d in [0, D] of A cos d + B sin d + C, given cos D and sin D (0 < D < pi/2).
double min_sinusoid(double a, double b, double c, double cos_d, double sin_d) noexcept {
    const double f0 = a + c;
    const double f1 = a * cos_d + b * sin_d + c;
    if (b < 0.0 && -a * sin_d + b * cos_d > 0.0) return c - std::sqrt(a * a + b * b);  // the interior minimum
    return std::min(f0, f1);
}

// The signed distance from point x to the convex set K (support `sup`, unit directions), bracketed between the
// inner polygon P of support points (P inside K, so sd(x, P) >= sd(x, K)) and the outer polygon Q cut by the support
// lines (K inside Q, so sd(x, Q) <= sd(x, K)). Refined at the features that bind either bound until the bracket is
// below kGjkToleranceMm. Works inside and outside K.
template <class Sup>
SignedDistance bracket_signed_distance(const Sup& sup, Vec x, double threshold, const CurvatureBound* curvature) noexcept {
    std::array<BracketNode, kBracketMaxNodes> nodes{};
    int count = 8;
    {
        // Eight normals 45 degrees apart, the first pointing at x (the closest part, when x is outside).
        const double xl = length(x);
        Vec n = xl > 0.0 ? x * (1.0 / xl) : Vec{1.0, 0.0};
        const double r = std::sqrt(0.5);
        for (int i = 0; i < 8; ++i) {
            BracketNode& node = nodes[static_cast<std::size_t>(i)];
            node.n = n;
            node.p = sup(n);
            node.h = dot(n, node.p);
            n = Vec{r * (n.x - n.y), r * (n.x + n.y)};  // +45 degrees
            const double nl = length(n);
            n = n * (1.0 / nl);
        }
    }
    const auto at = [&](int i) -> BracketNode& { return nodes[static_cast<std::size_t>((i % count + count) % count)]; };
    // Corner of Q between the support lines of nodes i and i+1, computed from p_i (precise when they are close).
    const auto corner = [&](int i) {
        const BracketNode& a = at(i);
        const BracketNode& b = at(i + 1);
        const double det = cross(a.n, b.n);
        const double hb = b.h - dot(b.n, a.p);
        if (!(det > 0.0)) return a.p;
        return a.p + perp(a.n) * (std::max(0.0, hb) / det);
    };
    // How far Q's corner lies outside P's edge in interval i.
    const auto gap = [&](int i) {
        const Vec e = at(i + 1).p - at(i).p;
        const double el = length(e);
        if (!(el > 0.0)) return 0.0;
        return std::abs(cross(e, corner(i) - at(i).p)) / el;
    };
    SignedDistance out;
    for (int it = 0; it <= kBracketMaxIterations; ++it) {
        // ---- P (inner polygon): upper bound ----
        bool inside_p = true;
        bool any_edge = false;
        double min_line = std::numeric_limits<double>::infinity();
        double min_seg = std::numeric_limits<double>::infinity();
        int bind_in = 0;
        int bind_out = 0;
        int bind_out_other = 0;      // when the closest point of P is a vertex: the other interval next to it
        bool bind_out_vertex = false;
        for (int i = 0; i < count; ++i) {
            const Vec a = at(i).p;
            const Vec e = at(i + 1).p - a;
            const double len2 = dot(e, e);
            if (!(len2 > 0.0)) continue;
            any_edge = true;
            const double len = std::sqrt(len2);
            const double side = cross(e, x - a) / len;  // > 0: x on P's inner side of this edge
            if (side < 0.0) inside_p = false;
            if (side < min_line) {
                min_line = side;
                bind_in = i;
            }
            const double t = std::clamp(dot(x - a, e) / len2, 0.0, 1.0);
            const double d = length(x - (a + e * t));
            if (d < min_seg) {
                min_seg = d;
                bind_out = i;
                bind_out_vertex = t <= 0.0 || t >= 1.0;
                bind_out_other = t <= 0.0 ? i - 1 : i + 1;
            }
        }
        double hi = 0.0;
        int bind_p = 0;
        int bind_p2 = -1;  // -1: none
        if (!any_edge) {
            hi = length(x - at(0).p);
        } else if (inside_p) {
            hi = -min_line;
            bind_p = bind_in;
        } else {
            hi = min_seg;
            bind_p = bind_out;
            if (bind_out_vertex) bind_p2 = (bind_out_other % count + count) % count;
        }
        if (curvature != nullptr) {
            // sd = -min over normals of S(n) = h_K(n) - n . x, and S'' + S = rho_K (D-074), so from each end of an
            // interval, with rho_K >= rho_lo there, S(d) >= (S0 - rho_lo) cos d + S0' sin d + rho_lo. Each end's bound
            // is used over its half of the interval. Tight where K's curvature hardly changes, where P is slow.
            double min_lb = std::numeric_limits<double>::infinity();
            int bind_c = 0;
            for (int i = 0; i < count; ++i) {
                const BracketNode& na = at(i);
                const BracketNode& nb = at(i + 1);
                const double cos_full = dot(na.n, nb.n);
                const double sin_full = cross(na.n, nb.n);
                const double sa = na.h - dot(na.n, x);
                const double sb = nb.h - dot(nb.n, x);
                double lb = std::min(sa, sb);
                if (sin_full > 0.0) {
                    const double ch = std::sqrt(std::max(0.0, 0.5 * (1.0 + cos_full)));
                    const double sh = sin_full / (2.0 * ch);
                    const double rho = curvature->rho_lo(na.n, nb.n);
                    const double da = dot(perp(na.n), na.p - x);  // S'(n_i)
                    const double db = dot(perp(nb.n), nb.p - x);  // S'(n_i+1)
                    lb = std::min(min_sinusoid(sa - rho, da, rho, ch, sh), min_sinusoid(sb - rho, -db, rho, ch, sh));
                }
                if (lb < min_lb) {
                    min_lb = lb;
                    bind_c = i;
                }
            }
            if (-min_lb < hi) {
                hi = -min_lb;
                bind_p = bind_c;
                bind_p2 = -1;
            }
        }
        // ---- Q (outer polygon): lower bound ----
        bool inside_q = true;
        double min_slack = std::numeric_limits<double>::infinity();
        int bind_line = 0;
        for (int i = 0; i < count; ++i) {
            const double slack = at(i).h - dot(at(i).n, x);
            if (slack < 0.0) inside_q = false;
            if (slack < min_slack) {
                min_slack = slack;
                bind_line = i;
            }
        }
        double lo = 0.0;
        int bind_q = 0;
        if (inside_q) {
            lo = -min_slack;
            bind_q = gap(bind_line - 1) > gap(bind_line) ? bind_line - 1 : bind_line;
        } else {
            double best = std::numeric_limits<double>::infinity();
            for (int i = 0; i < count; ++i) {
                // Q's edge on support line i runs from corner(i-1) to corner(i).
                const Vec a = corner(i - 1);
                const Vec b = corner(i);
                const Vec e = b - a;
                const double len2 = dot(e, e);
                const double t = len2 > 0.0 ? std::clamp(dot(x - a, e) / len2, 0.0, 1.0) : 0.0;
                const double d = length(x - (a + e * t));
                if (d < best) {
                    best = d;
                    bind_q = t >= 1.0 ? i : (t <= 0.0 ? i - 1 : (gap(i - 1) > gap(i) ? i - 1 : i));
                }
            }
            lo = best;
        }
        lo = std::min(lo, hi);
        // Inside, the value is Q's bound: the best separation found along a sampled normal. It converges fast where
        // P's bound converges slowly (a nearly flat penetration depth, e.g. nearly concentric bases).
        out = SignedDistance{inside_q ? lo : 0.5 * (lo + hi), lo, hi, it};
        if (hi - lo <= kGjkToleranceMm || it == kBracketMaxIterations) break;
        if (hi < threshold || lo >= threshold) break;  // decided (never when threshold is NaN: only the value is wanted)
        // ---- refine the binding intervals (highest index first, so earlier indices stay valid) ----
        std::array<int, 3> todo{(bind_p % count + count) % count, (bind_q % count + count) % count, bind_p2};
        std::sort(todo.begin(), todo.end(), [](int a, int b) { return a > b; });
        bool refined = false;
        for (std::size_t k = 0; k < todo.size(); ++k) {
            const int i = todo[k];
            if (i < 0 || (k > 0 && todo[k - 1] == i)) continue;
            if (count >= kBracketMaxNodes) break;
            if (!(gap(i) > 1e-13)) continue;
            const Vec e = at(i + 1).p - at(i).p;
            const double el = length(e);
            if (!(el > 0.0)) continue;
            const Vec m{e.y / el, -e.x / el};  // the outward normal of P's edge
            if (!(cross(at(i).n, m) > 0.0 && cross(m, at(i + 1).n) > 0.0)) continue;
            BracketNode node;
            node.n = m;
            node.p = sup(m);
            node.h = dot(m, node.p);
            const int pos = i + 1;  // insert after i (at the end when i is the last node)
            for (int j = count; j > pos; --j) nodes[static_cast<std::size_t>(j)] = nodes[static_cast<std::size_t>(j - 1)];
            nodes[static_cast<std::size_t>(pos)] = node;
            ++count;
            refined = true;
        }
        if (!refined) break;
    }
    return out;
}

}  // namespace

// ---- trigonometry ----

void sin_cos_deg(double deg, double& s, double& c) noexcept {
    if (!std::isfinite(deg)) {
        s = 0.0;
        c = 1.0;
        return;
    }
    const double d = std::fmod(deg, 360.0);  // exact
    const double k = std::floor(d / 90.0 + 0.5);
    const double x = (d - 90.0 * k) * kDegToRad;  // |x| <= pi/4 (up to rounding)
    const double x2 = x * x;
    // Taylor series to x^17 and x^18: the next terms are below 1e-19.
    const double sx =
        x * (1.0 + x2 * (-1.0 / 6.0 +
                         x2 * (1.0 / 120.0 +
                               x2 * (-1.0 / 5040.0 +
                                     x2 * (1.0 / 362880.0 +
                                           x2 * (-1.0 / 39916800.0 +
                                                 x2 * (1.0 / 6227020800.0 +
                                                       x2 * (-1.0 / 1307674368000.0 +
                                                             x2 * (1.0 / 355687428096000.0)))))))));
    const double cx =
        1.0 + x2 * (-0.5 + x2 * (1.0 / 24.0 +
                                 x2 * (-1.0 / 720.0 +
                                       x2 * (1.0 / 40320.0 +
                                             x2 * (-1.0 / 3628800.0 +
                                                   x2 * (1.0 / 479001600.0 +
                                                         x2 * (-1.0 / 87178291200.0 +
                                                               x2 * (1.0 / 20922789888000.0 +
                                                                     x2 * (-1.0 / 6402373705728000.0)))))))));
    int q = static_cast<int>(k) % 4;
    if (q < 0) q += 4;
    switch (q) {
        case 0:
            s = sx;
            c = cx;
            break;
        case 1:
            s = cx;
            c = -sx;
            break;
        case 2:
            s = -sx;
            c = -cx;
            break;
        default:
            s = -cx;
            c = sx;
            break;
    }
}

double atan2_deg(double y, double x) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || (x == 0.0 && y == 0.0)) return 0.0;
    const double ax = std::abs(x);
    const double ay = std::abs(y);
    double ang = ay <= ax ? atan_unit(ay / ax) : 0.5 * kPi - atan_unit(ax / ay);
    if (x < 0.0) ang = kPi - ang;
    if (y < 0.0) ang = -ang;
    return normalise_deg(ang * kRadToDeg);
}

double normalise_deg(double deg) noexcept {
    if (!std::isfinite(deg)) return 0.0;
    double r = std::fmod(deg, 360.0);  // exact, in (-360, 360)
    if (r > 180.0) {
        r -= 360.0;
    } else if (r <= -180.0) {
        r += 360.0;
    }
    return r == 0.0 ? 0.0 : r;  // never -0
}

// ---- ellipses ----

Ellipse ellipse_along(const Base& base, Vec c, Vec axis) noexcept {
    Ellipse e;
    e.c = c;
    if (base.shape == BaseShape::Round) {
        e.a = e.b = 0.5 * static_cast<double>(base.diameter_mm);
        return e;
    }
    e.u = axis;
    e.v = Vec{axis.y, -axis.x};  // at facing 0: u = (0, 1), v = (1, 0)
    e.a = 0.5 * static_cast<double>(base.length_mm);
    e.b = 0.5 * static_cast<double>(base.diameter_mm);
    if (e.a < e.b) {  // not a valid oval (the loader refuses it); keep a >= b
        std::swap(e.a, e.b);
        std::swap(e.u, e.v);
    }
    return e;
}

Ellipse ellipse_of(const BasePose& pose) noexcept {
    double s = 0.0;
    double c = 1.0;
    if (pose.base.shape != BaseShape::Round) sin_cos_deg(pose.facing_deg, s, c);
    // D-074: at facing f the major axis points along (-sin f, cos f): +y at 0, -x at 90.
    return ellipse_along(pose.base, to_vec(pose.position), Vec{-s, c});
}

double eberly_distance(double e0, double e1, double y0, double y1) noexcept {
    if (y1 > 0.0) {
        if (y0 > 0.0) {
            const double z0 = y0 / e0;
            const double z1 = y1 / e1;
            const double g = z0 * z0 + z1 * z1 - 1.0;
            if (g != 0.0) {
                const double r0 = (e0 / e1) * (e0 / e1);
                const double r0m1 = (e0 - e1) * (e0 + e1) / (e1 * e1);  // r0 - 1 without cancellation
                const double w = eberly_root(r0, r0m1, z0, z1, g);    // sbar + 1
                const double x0 = r0 * y0 / (w + r0m1);
                const double x1 = y1 / w;
                return std::sqrt((x0 - y0) * (x0 - y0) + (x1 - y1) * (x1 - y1));
            }
            return 0.0;
        }
        return std::abs(y1 - e1);
    }
    const double numer0 = e0 * y0;
    const double denom0 = e0 * e0 - e1 * e1;
    if (numer0 < denom0) {
        const double xde0 = numer0 / denom0;
        const double x0 = e0 * xde0;
        const double x1 = e1 * std::sqrt(std::max(0.0, 1.0 - xde0 * xde0));
        return std::sqrt((x0 - y0) * (x0 - y0) + x1 * x1);
    }
    return std::abs(y0 - e0);
}

double point_ellipse_signed_distance(Vec p, const Ellipse& e) noexcept {
    const Vec d = p - e.c;
    const double y0 = std::abs(dot(d, e.u));
    const double y1 = std::abs(dot(d, e.v));
    if (!(e.b > 0.0)) {  // a segment or a point, not a base
        const double dx = std::max(0.0, y0 - std::max(0.0, e.a));
        return std::sqrt(dx * dx + y1 * y1);
    }
    const double dist = eberly_distance(e.a, e.b, y0, y1);
    const double z0 = y0 / e.a;
    const double z1 = y1 / e.b;
    return z0 * z0 + z1 * z1 < 1.0 ? -dist : dist;
}

// ---- convex shapes ----

Vec support(const Convex& s, Vec d) noexcept {
    Vec p = s.c;
    if (s.a > 0.0 || s.b > 0.0) {
        const double du = dot(d, s.u);
        const double dv = dot(d, s.v);
        const double den = std::sqrt(s.a * s.a * du * du + s.b * s.b * dv * dv);
        if (den > 0.0) p = p + s.u * (s.a * s.a * du / den) + s.v * (s.b * s.b * dv / den);
    }
    if (s.hx > 0.0) p.x += d.x > 0.0 ? s.hx : (d.x < 0.0 ? -s.hx : 0.0);
    if (s.hy > 0.0) p.y += d.y > 0.0 ? s.hy : (d.y < 0.0 ? -s.hy : 0.0);
    if (dot(s.sweep, d) > 0.0) p = p + s.sweep;
    return p;
}

namespace {

SignedDistance convex_signed_distance_impl(const Convex& a, const Convex& b, double threshold) noexcept {
    // sd(A, B) is the signed distance from x = c_B - c_A to K = A0 - B0 (both centred on the origin, so 0 is in K).
    const Vec x = b.c - a.c;
    const auto sup_k = [&](Vec d) { return (support(a, d) - a.c) - (support(b, d * -1.0) - b.c); };
    SignedDistance out;
    if (gjk_separated([&](Vec d) { return sup_k(d) - x; }, x * -1.0, threshold, out)) return out;
    // Two plain ellipses (no sweep, no box): K is smooth, with the curvature bound.
    const auto plain = [](const Convex& s) {
        return s.hx == 0.0 && s.hy == 0.0 && s.sweep.x == 0.0 && s.sweep.y == 0.0 && s.b > 0.0;
    };
    if (plain(a) && plain(b)) {
        const CurvatureBound curvature{{a.u, b.u}, {a.a, b.a}, {a.b, b.b}};
        return bracket_signed_distance(sup_k, x, threshold, &curvature);
    }
    return bracket_signed_distance(sup_k, x, threshold, nullptr);
}

}  // namespace

SignedDistance convex_signed_distance(const Convex& a, const Convex& b) noexcept {
    return convex_signed_distance_impl(a, b, std::numeric_limits<double>::quiet_NaN());  // NaN: never decided early
}

SignedDistance convex_signed_distance(const Convex& a, const Convex& b, double decide_at) noexcept {
    return convex_signed_distance_impl(a, b, decide_at);
}

SignedDistance pose_signed_distance(const BasePose& a, const BasePose& b) noexcept {
    const bool ra = a.base.shape == BaseShape::Round;
    const bool rb = b.base.shape == BaseShape::Round;
    if (ra && rb) {
        const double d = length(to_vec(b.position) - to_vec(a.position)) - 0.5 * static_cast<double>(a.base.diameter_mm) -
                         0.5 * static_cast<double>(b.base.diameter_mm);
        return SignedDistance{d, d, d, 0};
    }
    if (ra || rb) {
        const BasePose& disc = ra ? a : b;
        const Ellipse e = ellipse_of(ra ? b : a);
        const double d = point_ellipse_signed_distance(to_vec(disc.position), e) -
                         0.5 * static_cast<double>(disc.base.diameter_mm);
        return SignedDistance{d, d, d, 0};
    }
    const Ellipse ea = ellipse_of(a);
    const Ellipse eb = ellipse_of(b);
    return convex_signed_distance(Convex{ea.c, ea.u, ea.v, ea.a, ea.b, 0.0, 0.0, Vec{}},
                                  Convex{eb.c, eb.u, eb.v, eb.a, eb.b, 0.0, 0.0, Vec{}});
}

}  // namespace oval_detail

using namespace oval_detail;

namespace {

constexpr double kEps = static_cast<double>(EPS_MM);

bool is_round(const Base& b) noexcept { return b.shape == BaseShape::Round; }

// The base's ellipse at `facing` (degrees, in double: the turn checks don't round facings to float).
Ellipse ellipse_at(const BasePose& pose, double facing) noexcept {
    double s = 0.0;
    double c = 1.0;
    if (!is_round(pose.base)) sin_cos_deg(facing, s, c);
    return ellipse_along(pose.base, to_vec(pose.position), Vec{-s, c});
}

Convex convex_of(const Ellipse& e, Vec sweep = Vec{}) noexcept { return Convex{e.c, e.u, e.v, e.a, e.b, 0.0, 0.0, sweep}; }

// The swept area of `start` moving to `end`, without the round radius (returned in `radius`): a round base is the
// segment (its centre's path) grown by its radius.
Convex swept_core(const BasePose& start, const Position& end, double& radius) noexcept {
    const Vec p0 = to_vec(start.position);
    const Vec sweep = to_vec(end) - p0;
    if (is_round(start.base)) {
        radius = 0.5 * static_cast<double>(start.base.diameter_mm);
        return Convex{p0, Vec{0.0, 1.0}, Vec{1.0, 0.0}, 0.0, 0.0, 0.0, 0.0, sweep};
    }
    radius = 0.0;
    const Ellipse e = ellipse_of(start);
    return Convex{e.c, e.u, e.v, e.a, e.b, 0.0, 0.0, sweep};
}

Convex static_core(const BasePose& pose, double& radius) noexcept { return swept_core(pose, pose.position, radius); }

double point_segment_distance_d(Vec p, Vec a, Vec b) noexcept {
    const Vec ab = b - a;
    const double len2 = dot(ab, ab);
    double t = 0.0;
    if (len2 > 0.0) t = std::clamp(dot(p - a, ab) / len2, 0.0, 1.0);
    return length(p - (a + ab * t));
}

// check_model_path's float capsule distance (path.cpp), kept bit-identical for round-round sweeps.
float point_segment_distance_f(const Position& p, const Position& a, const Position& b) {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float len2 = abx * abx + aby * aby;
    float t = 0.0f;
    if (len2 > 0.0f) {
        t = std::clamp(((p.x - a.x) * abx + (p.y - a.y) * aby) / len2, 0.0f, 1.0f);
    }
    return centre_distance(p, Position{a.x + t * abx, a.y + t * aby, a.z});
}

// sin^2 / cos^2 of the facing, for the extents.
void axis_extents(const Base& base, double facing_deg, double& half_x, double& half_y) noexcept {
    if (is_round(base)) {
        half_x = half_y = 0.5 * static_cast<double>(base.diameter_mm);
        return;
    }
    const Ellipse e = ellipse_along(base, Vec{}, Vec{0.0, 1.0});  // a, b in order
    double s = 0.0;
    double c = 1.0;
    sin_cos_deg(facing_deg, s, c);
    // u = (-s, c), v = (c, s): the half extent along x is sqrt(a^2 u_x^2 + b^2 v_x^2).
    half_x = std::sqrt(e.a * e.a * s * s + e.b * e.b * c * c);
    half_y = std::sqrt(e.a * e.a * c * c + e.b * e.b * s * s);
}

// The smallest margin between a base of half extents (hx, hy) at `c` and the rectangle's sides (negative: outside).
double edge_margin(const Rect& r, Vec c, double hx, double hy) noexcept {
    const double mx = std::min(c.x - static_cast<double>(r.min_x), static_cast<double>(r.max_x) - c.x) - hx;
    const double my = std::min(c.y - static_cast<double>(r.min_y), static_cast<double>(r.max_y) - c.y) - hy;
    return std::min(mx, my);
}

// Whether some angle equal to `base_deg` mod `period` lies in [lo, hi]; the first such angle in turn order.
bool interval_contains(double lo, double hi, double base_deg, double period, double& found) noexcept {
    const double k = std::ceil((lo - base_deg) / period);
    const double a = base_deg + k * period;
    if (a <= hi) {
        found = a;
        return true;
    }
    return false;
}

RotationCheck static_rotation_check(const BasePose& start, double sd) {
    RotationCheck out;
    out.facing_deg = normalise_facing_deg(start.facing_deg);
    out.clearance_mm = static_cast<float>(sd);
    out.blocked = sd < -kEps;
    return out;
}

}  // namespace

// ---- facing ----

float normalise_facing_deg(float deg) noexcept {
    float out = static_cast<float>(normalise_deg(static_cast<double>(deg)));
    if (out <= -180.0f) out = 180.0f;  // a value just above -180 rounded to -180
    return out == 0.0f ? 0.0f : out;
}

float rotation_delta_deg(float from_deg, float to_deg) noexcept {
    float out = static_cast<float>(normalise_deg(static_cast<double>(to_deg) - static_cast<double>(from_deg)));
    if (out <= -180.0f) out = 180.0f;
    return out == 0.0f ? 0.0f : out;
}

// ---- 01.04 ----

float base_distance(const BasePose& a, const BasePose& b) {
    if (is_round(a.base) && is_round(b.base)) return base_distance(a.position, a.base, b.position, b.base);
    return std::max(0.0f, static_cast<float>(pose_signed_distance(a, b).value));
}

float signed_base_distance(const BasePose& a, const BasePose& b) {
    if (is_round(a.base) && is_round(b.base)) {
        return centre_distance(a.position, b.position) - base_radius(a.base) - base_radius(b.base);
    }
    return static_cast<float>(pose_signed_distance(a, b).value);
}

bool bases_overlap(const BasePose& a, const BasePose& b) {
    if (is_round(a.base) && is_round(b.base)) return bases_overlap(a.position, a.base, b.position, b.base);
    // D-074: shrunk by EPS/2 each, they still intersect <=> the penetration depth is more than EPS.
    if (is_round(a.base) || is_round(b.base)) return pose_signed_distance(a, b).value < -kEps;  // Eberly: exact
    return convex_signed_distance(convex_of(ellipse_of(a)), convex_of(ellipse_of(b)), -kEps).value < -kEps;
}

// ---- D-009 ----

bool within_area(const Rect& area, const BasePose& pose) {
    if (is_round(pose.base)) return within_area(area, pose.position, pose.base);
    const Vec c = to_vec(pose.position);
    if (c.x >= area.min_x && c.x <= area.max_x && c.y >= area.min_y && c.y <= area.max_y) return true;
    const Ellipse e = ellipse_of(pose);
    const Convex box{Vec{0.5 * (static_cast<double>(area.min_x) + static_cast<double>(area.max_x)),
                         0.5 * (static_cast<double>(area.min_y) + static_cast<double>(area.max_y))},
                     Vec{0.0, 1.0},
                     Vec{1.0, 0.0},
                     0.0,
                     0.0,
                     std::max(0.0, 0.5 * (static_cast<double>(area.max_x) - static_cast<double>(area.min_x))),
                     std::max(0.0, 0.5 * (static_cast<double>(area.max_y) - static_cast<double>(area.min_y))),
                     Vec{}};
    return convex_signed_distance(box, convex_of(e), kEps).value <= kEps;
}

bool wholly_within(const Rect& area, const BasePose& pose) {
    if (is_round(pose.base)) return wholly_within(area, pose.position, pose.base);
    double hx = 0.0;
    double hy = 0.0;
    axis_extents(pose.base, pose.facing_deg, hx, hy);
    return edge_margin(area, to_vec(pose.position), hx, hy) >= -kEps;
}

BaseExtents base_extents(const Base& base, float facing_deg) {
    if (is_round(base)) return BaseExtents{base.diameter_mm * 0.5f, base.diameter_mm * 0.5f};
    double hx = 0.0;
    double hy = 0.0;
    axis_extents(base, facing_deg, hx, hy);
    return BaseExtents{static_cast<float>(hx), static_cast<float>(hy)};
}

// ---- 03.01 straight lines ----

float translation_signed_distance(const BasePose& start, const Position& end, const BasePose& obstacle) {
    if (is_round(start.base) && is_round(obstacle.base)) {
        const double d = point_segment_distance_d(to_vec(obstacle.position), to_vec(start.position), to_vec(end)) -
                         0.5 * static_cast<double>(start.base.diameter_mm) -
                         0.5 * static_cast<double>(obstacle.base.diameter_mm);
        return static_cast<float>(d);
    }
    if (start.position == end) return static_cast<float>(pose_signed_distance(start, obstacle).value);
    double r_mover = 0.0;
    double r_obstacle = 0.0;
    const Convex mover = swept_core(start, end, r_mover);
    const Convex other = static_core(obstacle, r_obstacle);
    return static_cast<float>(convex_signed_distance(mover, other).value - r_mover - r_obstacle);
}

bool translation_crosses(const BasePose& start, const Position& end, const BasePose& obstacle) {
    if (is_round(start.base) && is_round(obstacle.base)) {
        // check_model_path (03.01): the capsule passes through the base if they overlap by more than EPS_MM.
        const float limit = base_radius(start.base) + base_radius(obstacle.base) - EPS_MM;
        return point_segment_distance_f(obstacle.position, start.position, end) < limit;
    }
    if (start.position == end) return bases_overlap(start, obstacle);
    double r_mover = 0.0;
    double r_obstacle = 0.0;
    const Convex mover = swept_core(start, end, r_mover);
    const Convex other = static_core(obstacle, r_obstacle);
    const double offset = r_mover + r_obstacle;
    return convex_signed_distance(mover, other, offset - kEps).value - offset < -kEps;
}

bool translation_stays_within(const Rect& battlefield, const BasePose& start, const Position& end) {
    return wholly_within(battlefield, start) && wholly_within(battlefield, BasePose{end, start.base, start.facing_deg});
}

// ---- 03.01 rotating ----

RotationCheck rotation_against_edge(const Rect& battlefield, const BasePose& start, float to_deg) {
    const Vec c = to_vec(start.position);
    const double f0 = static_cast<double>(start.facing_deg);
    const double delta = is_round(start.base) ? 0.0 : normalise_deg(static_cast<double>(to_deg) - f0);
    if (delta == 0.0) {
        double hx = 0.0;
        double hy = 0.0;
        axis_extents(start.base, f0, hx, hy);
        RotationCheck out;
        out.facing_deg = normalise_facing_deg(start.facing_deg);
        out.clearance_mm = static_cast<float>(edge_margin(battlefield, c, hx, hy));
        out.blocked = is_round(start.base) ? !wholly_within(battlefield, start.position, start.base)
                                        : !(edge_margin(battlefield, c, hx, hy) >= -kEps);
        return out;
    }
    const double f1 = f0 + delta;
    const double lo = std::min(f0, f1);
    const double hi = std::max(f0, f1);
    const Ellipse e = ellipse_along(start.base, Vec{}, Vec{0.0, 1.0});
    // The extent along x is largest where the major axis lies along x (facing 90 mod 180), else at an end; along y
    // where it lies along y (facing 0 mod 180). Exact.
    const auto worst = [&](double critical, bool along_x, double& facing) {
        double found = 0.0;
        if (interval_contains(lo, hi, critical, 180.0, found)) {
            facing = found;
            return e.a;
        }
        double hx0 = 0.0;
        double hy0 = 0.0;
        double hx1 = 0.0;
        double hy1 = 0.0;
        axis_extents(start.base, f0, hx0, hy0);
        axis_extents(start.base, f1, hx1, hy1);
        const double v0 = along_x ? hx0 : hy0;
        const double v1 = along_x ? hx1 : hy1;
        facing = v1 > v0 ? f1 : f0;
        return std::max(v0, v1);
    };
    double fx = f0;
    double fy = f0;
    const double hx = worst(90.0, true, fx);
    const double hy = worst(0.0, false, fy);
    const double mx = std::min(c.x - static_cast<double>(battlefield.min_x), static_cast<double>(battlefield.max_x) - c.x) - hx;
    const double my = std::min(c.y - static_cast<double>(battlefield.min_y), static_cast<double>(battlefield.max_y) - c.y) - hy;
    RotationCheck out;
    const double margin = std::min(mx, my);
    out.clearance_mm = static_cast<float>(margin);
    out.facing_deg = normalise_facing_deg(static_cast<float>(mx <= my ? fx : fy));
    out.blocked = margin < -kEps;
    return out;
}

RotationCheck rotation_against_base(const BasePose& start, float to_deg, const BasePose& obstacle) {
    const double f0 = static_cast<double>(start.facing_deg);
    const double delta = is_round(start.base) ? 0.0 : normalise_deg(static_cast<double>(to_deg) - f0);
    if (delta == 0.0) {
        RotationCheck out = static_rotation_check(start, pose_signed_distance(start, obstacle).value);
        out.blocked = bases_overlap(start, obstacle);
        return out;
    }
    const double f1 = f0 + delta;
    const double lo = std::min(f0, f1);
    const double hi = std::max(f0, f1);
    const Vec centre = to_vec(start.position);
    if (is_round(obstacle.base)) {
        // Exact (D-074): for a point at a fixed distance from the centre, the signed distance to the ellipse grows from
        // the major axis to the minor axis, so over the turn it is smallest at an end or where the major axis points
        // at the circle's centre.
        const double r = 0.5 * static_cast<double>(obstacle.base.diameter_mm);
        const Vec q = to_vec(obstacle.position);
        const auto sd_at = [&](double facing) { return point_ellipse_signed_distance(q, ellipse_at(start, facing)) - r; };
        double best = sd_at(f0);
        double best_f = f0;
        const double s1 = sd_at(f1);
        if (s1 < best) {
            best = s1;
            best_f = f1;
        }
        const Vec w = q - centre;
        const double rho = length(w);
        if (rho > 0.0) {
            // The major axis (-sin f, cos f) points along +-w at f = atan2(-w.x, w.y) mod 180.
            double found = 0.0;
            if (interval_contains(lo, hi, atan2_deg(-w.x, w.y), 180.0, found)) {
                const Ellipse e = ellipse_along(start.base, Vec{}, Vec{0.0, 1.0});
                const double d = eberly_distance(e.a, e.b, rho, 0.0);
                const double sd = (rho < e.a ? -d : d) - r;
                if (sd < best) {
                    best = sd;
                    best_f = found;
                }
            }
        }
        RotationCheck out;
        out.facing_deg = normalise_facing_deg(static_cast<float>(best_f));
        out.clearance_mm = static_cast<float>(best);
        out.blocked = best < -kEps;
        return out;
    }
    // Oval against oval: conservative advancement. Turning by t radians moves the outline by at most (a - b) t, so the
    // signed distance changes by at most that (Hausdorff bound, D-074).
    const Ellipse e = ellipse_along(start.base, Vec{}, Vec{0.0, 1.0});
    const double lipschitz = (e.a - e.b) * kDegToRad;  // mm per degree
    const double block_below = -kEps + 0.5 * kEps;      // a facing tried within EPS/2 of overlapping: blocked
    const double keep_above = -kEps + 0.25 * kEps;      // the steps keep the signed distance above this
    const double dir = delta > 0.0 ? 1.0 : -1.0;
    const double total = std::abs(delta);
    RotationCheck out;
    out.exact = false;
    double done = 0.0;
    double best = std::numeric_limits<double>::infinity();
    double best_f = f0;
    const Convex other = convex_of(ellipse_of(obstacle));
    constexpr int kMaxSteps = 1000000;
    for (int step = 0; step < kMaxSteps; ++step) {
        const double facing = f0 + dir * done;
        const double sd = convex_signed_distance(convex_of(ellipse_at(start, facing)), other).lo;
        if (sd < best) {
            best = sd;
            best_f = facing;
        }
        if (sd < block_below) {
            out.blocked = true;
            out.facing_deg = normalise_facing_deg(static_cast<float>(facing));
            out.clearance_mm = static_cast<float>(sd);
            return out;
        }
        if (done >= total) break;
        if (!(lipschitz > 0.0)) {
            done = total;  // a circle: turning changes nothing
            continue;
        }
        done = std::min(total, done + (sd - keep_above) / lipschitz);
    }
    if (done < total) {  // not shown clear within the step budget: blocked (conservative)
        out.blocked = true;
        out.facing_deg = normalise_facing_deg(static_cast<float>(f0 + dir * done));
        out.clearance_mm = static_cast<float>(best);
        return out;
    }
    out.facing_deg = normalise_facing_deg(static_cast<float>(best_f));
    out.clearance_mm = static_cast<float>(best);
    return out;
}

}  // namespace wh40k::core
