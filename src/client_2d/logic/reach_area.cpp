#include "reach_area.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace wh40k::client {

namespace {

using Interval = std::pair<float, float>;

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

}  // namespace

std::vector<ReachSpan> reach_spans(const core::MoveReach& reach, float y_step_mm) {
    std::vector<ReachSpan> out;
    if (!(y_step_mm > 0.0f) || reach.max_mm < 0.0f) return out;
    const float y_lo = std::max(reach.start.y - reach.max_mm, reach.centre_bounds.min_y);
    const float y_hi = std::min(reach.start.y + reach.max_mm, reach.centre_bounds.max_y);
    for (float y = y_lo; y <= y_hi; y += y_step_mm) {
        const float dy = y - reach.start.y;
        const float h2 = reach.max_mm * reach.max_mm - dy * dy;
        if (h2 < 0.0f) continue;
        const float h = std::sqrt(h2);
        const float x0 = std::max(reach.start.x - h, reach.centre_bounds.min_x);
        const float x1 = std::min(reach.start.x + h, reach.centre_bounds.max_x);
        if (x1 <= x0) continue;
        std::vector<Interval> parts{{x0, x1}};
        for (const core::ReachCircle& c : reach.forbidden) {
            const float cy = y - c.centre.y;
            const float k2 = c.radius_mm * c.radius_mm - cy * cy;
            if (k2 <= 0.0f) continue;
            const float k = std::sqrt(k2);
            subtract(parts, c.centre.x - k, c.centre.x + k);
        }
        for (const Interval& p : parts) out.push_back(ReachSpan{y, p.first, p.second});
    }
    return out;
}

bool in_reach(const core::MoveReach& reach, const core::Position& centre) {
    const core::Rect& b = reach.centre_bounds;
    if (centre.x < b.min_x || centre.x > b.max_x || centre.y < b.min_y || centre.y > b.max_y) return false;
    if (std::hypot(centre.x - reach.start.x, centre.y - reach.start.y) > reach.max_mm) return false;
    return std::none_of(reach.forbidden.begin(), reach.forbidden.end(), [&](const core::ReachCircle& c) {
        return std::hypot(centre.x - c.centre.x, centre.y - c.centre.y) < c.radius_mm;
    });
}

}  // namespace wh40k::client
