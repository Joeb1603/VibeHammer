#include "base_shape.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/geometry/distance.hpp"
#include "core/geometry/oval.hpp"

namespace wh40k::client {

namespace {

constexpr double kPi = std::numbers::pi;

bool is_round(const core::Base& b) { return b.shape == core::BaseShape::Round; }

// Semi-axes: a along the major axis, b along the minor one (a round base: both the radius).
void semi_axes(const core::Base& base, double& a, double& b) {
    if (is_round(base)) {
        a = b = 0.5 * static_cast<double>(base.diameter_mm);
        return;
    }
    a = 0.5 * static_cast<double>(std::max(base.length_mm, base.diameter_mm));
    b = 0.5 * static_cast<double>(std::min(base.length_mm, base.diameter_mm));
}

core::Position at(const core::Position& c, double x, double y) {
    return core::Position{static_cast<float>(static_cast<double>(c.x) + x),
                          static_cast<float>(static_cast<double>(c.y) + y), c.z};
}

// The ellipse's radius in the direction at angle `alpha` from its major axis.
double radius_at(double a, double b, double alpha) {
    const double c = std::cos(alpha);
    const double s = std::sin(alpha);
    return a * b / std::sqrt(b * b * c * c + a * a * s * s);
}

constexpr int kSpotBisections = 40;  // brackets of at most a few hundred mm to below 1e-4 mm

}  // namespace

BaseAxes base_axes(float facing_deg) {
    double s = 0.0;
    double c = 1.0;
    core::oval_detail::sin_cos_deg(static_cast<double>(facing_deg), s, c);
    return BaseAxes{-s, c, c, s};
}

std::vector<core::Position> base_outline(const core::BasePose& pose, int segments) {
    return offset_outline(pose, 0.0f, segments);
}

std::vector<core::Position> offset_outline(const core::BasePose& pose, float offset_mm, int segments) {
    std::vector<core::Position> out;
    if (segments < 3) segments = 3;
    out.reserve(static_cast<std::size_t>(segments));
    double a = 0.0;
    double b = 0.0;
    semi_axes(pose.base, a, b);
    const BaseAxes ax = is_round(pose.base) ? BaseAxes{} : base_axes(pose.facing_deg);
    const double d = static_cast<double>(offset_mm);
    for (int i = 0; i < segments; ++i) {
        const double t = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(segments);
        const double ct = std::cos(t);
        const double st = std::sin(t);
        // Local coordinates: x along the major axis, y along the minor axis turned so the outline runs counter-
        // clockwise in the world (u, then -v is +90 degrees from u: the minor axis v is u turned -90 degrees).
        double x = a * ct;
        double y = b * st;
        // The outward normal of (x/a)^2 + (y/b)^2 = 1 is along (x / a^2, y / b^2) = (cos t / a, sin t / b).
        const double nx = b * ct;
        const double ny = a * st;
        const double nl = std::sqrt(nx * nx + ny * ny);
        if (nl > 0.0) {
            x += d * nx / nl;
            y += d * ny / nl;
        }
        // World: x * u + y * (-v) (counter-clockwise from the +major end).
        out.push_back(at(pose.position, x * ax.ux - y * ax.vx, x * ax.uy - y * ax.vy));
    }
    return out;
}

bool base_contains(const core::BasePose& pose, const core::Position& p) {
    if (is_round(pose.base)) {  // the pre-M66 test (board_view model_at)
        const float dx = p.x - pose.position.x;
        const float dy = p.y - pose.position.y;
        const float r = pose.base.diameter_mm * 0.5f;
        return dx * dx + dy * dy <= r * r;
    }
    return offset_contains(pose, 0.0f, p);
}

bool offset_contains(const core::BasePose& pose, float offset_mm, const core::Position& p) {
    if (is_round(pose.base)) {
        return std::hypot(p.x - pose.position.x, p.y - pose.position.y) <= pose.base.diameter_mm * 0.5f + offset_mm;
    }
    const core::oval_detail::Ellipse e = core::oval_detail::ellipse_of(pose);
    return core::oval_detail::point_ellipse_signed_distance(core::oval_detail::to_vec(p), e) <=
           static_cast<double>(offset_mm);
}

std::vector<core::Position> turn_swept_outline(const core::Base& base, const core::Position& centre, float from_deg,
                                               float to_deg, int segments) {
    std::vector<core::Position> out;
    if (segments < 3) segments = 3;
    double a = 0.0;
    double b = 0.0;
    semi_axes(base, a, b);
    const double delta = static_cast<double>(core::rotation_delta_deg(from_deg, to_deg));
    const double lo = std::min(0.0, delta);
    const double hi = std::max(0.0, delta);
    for (int i = 0; i < segments; ++i) {
        // A world direction, as an angle from +x (degrees); the major axis at facing f is at 90 + f.
        const double theta = 360.0 * static_cast<double>(i) / static_cast<double>(segments);
        double radius = b;
        if (is_round(base)) {
            radius = a;
        } else {
            // alpha = theta - (90 + from + s) for s in [lo, hi]: the radius is largest where alpha is nearest a
            // multiple of 180 (the major axis points that way).
            const double alpha_hi = theta - 90.0 - static_cast<double>(from_deg) - lo;
            const double alpha_lo = theta - 90.0 - static_cast<double>(from_deg) - hi;
            const double k = std::ceil(alpha_lo / 180.0);
            if (k * 180.0 <= alpha_hi) {
                radius = a;  // the major axis points along theta at some facing of the turn
            } else {
                const double r1 = radius_at(a, b, alpha_lo * kPi / 180.0);
                const double r2 = radius_at(a, b, alpha_hi * kPi / 180.0);
                radius = std::max(r1, r2);
            }
        }
        const double t = theta * kPi / 180.0;
        out.push_back(at(centre, radius * std::cos(t), radius * std::sin(t)));
    }
    return out;
}

double base_perimeter_mm(const core::Base& base) {
    double a = 0.0;
    double b = 0.0;
    semi_axes(base, a, b);
    if (is_round(base)) return kPi * static_cast<double>(base.diameter_mm);
    const double h = (a - b) * (a - b) / ((a + b) * (a + b));
    return kPi * (a + b) * (1.0 + 3.0 * h / (10.0 + std::sqrt(4.0 - 3.0 * h)));
}

int contact_direction_count(const core::Base& target) {
    // A round base: ceil(d / 32), exact for d = 32 (one, the old 72 directions).
    const double ratio = is_round(target)
                             ? static_cast<double>(target.diameter_mm) / static_cast<double>(kContactReferenceDiameterMm)
                             : base_perimeter_mm(target) / (kPi * static_cast<double>(kContactReferenceDiameterMm));
    const double k = std::ceil(ratio);
    if (!(k > 1.0)) return kContactDirections;
    return kContactDirections * static_cast<int>(std::min(k, 64.0));
}

std::vector<UnitVector> direction_table(int count) {
    if (count == kContactDirections) return std::vector<UnitVector>(kDirections72.begin(), kDirections72.end());
    std::vector<UnitVector> out;
    if (count <= 0) return out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        double s = 0.0;
        double c = 1.0;
        core::oval_detail::sin_cos_deg(360.0 * static_cast<double>(i) / static_cast<double>(count), s, c);
        out.push_back(UnitVector{static_cast<float>(c), static_cast<float>(s)});
    }
    return out;
}

std::optional<core::Position> ring_spot(const core::BasePose& target, const core::Base& mover, float mover_facing_deg,
                                        UnitVector dir, float gap_mm) {
    if (is_round(target.base) && is_round(mover)) {
        const float r = core::base_radius(target.base) + core::base_radius(mover) + gap_mm;
        if (!(r > 0.0f)) return std::nullopt;
        return core::Position{target.position.x + r * dir.x, target.position.y + r * dir.y, 0.0f};
    }
    const auto spot = [&](double rho) {
        return core::Position{static_cast<float>(static_cast<double>(target.position.x) + rho * dir.x),
                              static_cast<float>(static_cast<double>(target.position.y) + rho * dir.y), 0.0f};
    };
    const auto gap_at = [&](double rho) {
        return core::signed_base_distance(core::BasePose{spot(rho), mover, mover_facing_deg}, target);
    };
    // The spot lies between the inscribed and the circumscribed radii (plus the gap) out from the centre.
    double lo = std::max(0.0, static_cast<double>(core::inscribed_radius(target.base) + core::inscribed_radius(mover) +
                                                  gap_mm) - 1.0);
    double hi = static_cast<double>(core::circumscribed_radius(target.base) + core::circumscribed_radius(mover) +
                                    gap_mm) + 1.0;
    if (!(hi > 0.0)) return std::nullopt;
    if (!(gap_at(lo) < gap_mm)) {
        if (lo > 0.0) return spot(lo);  // already that far at the inner bound (rounding): take it
        return std::nullopt;
    }
    if (gap_at(hi) < gap_mm) return std::nullopt;  // cannot happen for a valid base; refuse rather than guess
    for (int i = 0; i < kSpotBisections && hi - lo > 1e-4; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (gap_at(mid) < gap_mm) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return spot(hi);
}

std::vector<std::optional<core::Position>> contact_ring(const core::BasePose& target, const core::Base& mover,
                                                       float mover_facing_deg, int count, float gap_mm) {
    std::vector<std::optional<core::Position>> out;
    if (count <= 0) return out;
    out.reserve(static_cast<std::size_t>(count));
    const bool round_target = is_round(target.base);
    const bool round_mover = is_round(mover);
    if (round_target && round_mover) {
        const float r = core::base_radius(target.base) + core::base_radius(mover) + gap_mm;
        for (const UnitVector& d : direction_table(count)) {
            out.push_back(core::Position{target.position.x + r * d.x, target.position.y + r * d.y, 0.0f});
        }
        return out;
    }
    if (round_target != round_mover) {
        // The oval (whichever it is) centred on the target, at its facing; the round base's radius plus the gap out.
        const core::Base& oval = round_target ? mover : target.base;
        const float facing = round_target ? mover_facing_deg : target.facing_deg;
        const float round_radius = round_target ? core::base_radius(target.base) : core::base_radius(mover);
        const double d = static_cast<double>(round_radius) + static_cast<double>(gap_mm);
        if (!(d > 0.0)) {
            out.assign(static_cast<std::size_t>(count), std::nullopt);
            return out;
        }
        double a = 0.0;
        double b = 0.0;
        semi_axes(oval, a, b);
        const BaseAxes ax = base_axes(facing);
        for (int i = 0; i < count; ++i) {
            double st = 0.0;
            double ct = 1.0;
            core::oval_detail::sin_cos_deg(360.0 * static_cast<double>(i) / static_cast<double>(count), st, ct);
            const double nx = b * ct;  // the outward normal of the ellipse at parameter t, in its own axes
            const double ny = a * st;
            const double nl = std::sqrt(nx * nx + ny * ny);
            const double x = a * ct + d * nx / nl;
            const double y = b * st + d * ny / nl;
            out.push_back(at(target.position, x * ax.ux - y * ax.vx, x * ax.uy - y * ax.vy));
        }
        return out;
    }
    for (const UnitVector& dir : direction_table(count)) out.push_back(ring_spot(target, mover, mover_facing_deg, dir, gap_mm));
    return out;
}

float distance_until_gap(const core::BasePose& mover_at_from, const core::BasePose& target, float gap_mm) {
    const core::Position& from = mover_at_from.position;
    const float centres = core::centre_distance(from, target.position);
    if (is_round(target.base) && is_round(mover_at_from.base)) {
        return std::max(0.0f, centres - core::base_radius(target.base) - core::base_radius(mover_at_from.base) - gap_mm);
    }
    if (!(centres > 0.0f)) return 0.0f;
    if (core::signed_base_distance(mover_at_from, target) <= gap_mm) return 0.0f;
    const double ux = (static_cast<double>(target.position.x) - from.x) / centres;
    const double uy = (static_cast<double>(target.position.y) - from.y) / centres;
    const auto gap_at = [&](double t) {
        const core::Position p{static_cast<float>(from.x + t * ux), static_cast<float>(from.y + t * uy), 0.0f};
        return core::signed_base_distance(core::BasePose{p, mover_at_from.base, mover_at_from.facing_deg}, target);
    };
    double lo = 0.0;  // outside (more than the gap)
    double hi = static_cast<double>(centres);  // at the target's centre: inside
    if (!(gap_at(hi) < gap_mm)) return centres;
    for (int i = 0; i < kSpotBisections && hi - lo > 1e-4; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (gap_at(mid) < gap_mm) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return static_cast<float>(lo);
}

WoundDisplay wound_display(int wounds_remaining, int wounds) {
    WoundDisplay d;
    if (wounds <= 0 || wounds_remaining >= wounds) return d;  // unharmed (or no wounds): nothing
    d.left = std::max(0, wounds_remaining);
    d.total = wounds;
    if (wounds <= kMaxWoundPips) {
        d.kind = WoundDisplay::Kind::Pips;
        return d;
    }
    d.kind = WoundDisplay::Kind::Bar;
    d.text = std::to_string(d.left) + "/" + std::to_string(d.total);
    return d;
}

}  // namespace wh40k::client
