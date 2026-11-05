#include "ring_outline.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "core/geometry/units.hpp"

namespace wh40k::client {

namespace {

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

using Interval = std::pair<float, float>;  // [start, end) in radians, start in [0, 2π)

float wrap(float a) {
    a = std::fmod(a, kTwoPi);
    return a < 0.0f ? a + kTwoPi : a;
}

// Adds [centre - half, centre + half] to `covered`, split at 2π so every interval lies in [0, 2π].
void add_covered(std::vector<Interval>& covered, float centre, float half) {
    const float start = wrap(centre - half);
    const float end = start + 2.0f * half;
    if (end <= kTwoPi) {
        covered.emplace_back(start, end);
    } else {
        covered.emplace_back(start, kTwoPi);
        covered.emplace_back(0.0f, end - kTwoPi);
    }
}

}  // namespace

std::vector<Arc> outline_arcs(const std::vector<Circle>& circles) {
    std::vector<Arc> out;
    for (std::size_t i = 0; i < circles.size(); ++i) {
        const Circle& a = circles[i];
        std::vector<Interval> covered;
        bool hidden = false;
        for (std::size_t j = 0; j < circles.size() && !hidden; ++j) {
            if (j == i || circles[j].kind != a.kind) continue;
            const Circle& b = circles[j];
            const float d = std::hypot(b.centre.x - a.centre.x, b.centre.y - a.centre.y);
            if (d <= core::EPS_MM && std::abs(a.radius_mm - b.radius_mm) <= core::EPS_MM) {
                hidden = j < i;  // duplicates: the first one is drawn
                continue;
            }
            if (d >= a.radius_mm + b.radius_mm) continue;   // apart (or touching)
            if (d + a.radius_mm <= b.radius_mm) {            // a lies inside b
                hidden = true;
                continue;
            }
            if (d + b.radius_mm <= a.radius_mm) continue;   // b lies inside a
            const float cos_half =
                (a.radius_mm * a.radius_mm + d * d - b.radius_mm * b.radius_mm) / (2.0f * a.radius_mm * d);
            const float half = std::acos(std::clamp(cos_half, -1.0f, 1.0f));
            add_covered(covered, std::atan2(b.centre.y - a.centre.y, b.centre.x - a.centre.x), half);
        }
        if (hidden) continue;
        if (covered.empty()) {
            out.push_back(Arc{a.centre, a.radius_mm, 0.0f, kTwoPi, a.kind});
            continue;
        }
        // The gaps between the merged covered intervals, wrapping round from the last to the first.
        std::sort(covered.begin(), covered.end());
        std::vector<Interval> merged;
        for (const Interval& c : covered) {
            if (!merged.empty() && c.first <= merged.back().second) {
                merged.back().second = std::max(merged.back().second, c.second);
            } else {
                merged.push_back(c);
            }
        }
        for (std::size_t k = 0; k < merged.size(); ++k) {
            const float gap_start = merged[k].second;
            const float gap_end = k + 1 < merged.size() ? merged[k + 1].first : merged.front().first + kTwoPi;
            if (gap_end - gap_start > 1e-4f) out.push_back(Arc{a.centre, a.radius_mm, gap_start, gap_end, a.kind});
        }
    }
    return out;
}

}  // namespace wh40k::client
