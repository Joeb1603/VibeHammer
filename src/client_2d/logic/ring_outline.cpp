#include "ring_outline.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "base_shape.hpp"
#include "core/geometry/pose.hpp"
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

// D-074 part 3: the angles (radians, from +x) of circle a's rim inside oval ring b, as covered intervals; `hidden`
// when all of it is.
constexpr int kRimSamples = 360;

void add_oval_cover(const Circle& a, const Circle& b, std::vector<Interval>& covered, bool& hidden) {
    // Apart: the oval ring lies inside its circumscribed bound.
    const float d = std::hypot(b.centre.x - a.centre.x, b.centre.y - a.centre.y);
    if (d >= a.radius_mm + b.radius_mm) return;
    const auto rim = [&](float t) {
        return core::Position{a.centre.x + a.radius_mm * std::cos(t), a.centre.y + a.radius_mm * std::sin(t), 0.0f};
    };
    const auto in_b = [&](float t) { return ring_contains(b, rim(t)); };
    std::vector<char> inside(kRimSamples);
    int count = 0;
    for (int k = 0; k < kRimSamples; ++k) {
        inside[static_cast<std::size_t>(k)] = in_b(kTwoPi * static_cast<float>(k) / kRimSamples) ? 1 : 0;
        count += inside[static_cast<std::size_t>(k)];
    }
    if (count == kRimSamples) {
        hidden = true;
        return;
    }
    if (count == 0) return;
    // The edge between samples k and k + 1, by bisection (`from_inside`: sample k is inside).
    const auto edge = [&](int k, bool from_inside) {
        float lo = kTwoPi * static_cast<float>(k) / kRimSamples;
        float hi = kTwoPi * static_cast<float>(k + 1) / kRimSamples;
        for (int i = 0; i < 12; ++i) {
            const float mid = 0.5f * (lo + hi);
            if (in_b(mid) == from_inside) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        return 0.5f * (lo + hi);
    };
    // Each run of inside samples, from the edge before it to the edge after it.
    int start = 0;
    while (inside[static_cast<std::size_t>(start)] != 0) ++start;  // an outside sample to start from
    for (int n = 0; n < kRimSamples; ++n) {
        const int k = (start + n) % kRimSamples;
        const int next = (k + 1) % kRimSamples;
        if (inside[static_cast<std::size_t>(k)] == 0 && inside[static_cast<std::size_t>(next)] != 0) {
            const float enter = edge(start + n, false);
            int m = n + 1;
            while (inside[static_cast<std::size_t>((start + m) % kRimSamples)] != 0) ++m;
            const float leave = edge(start + m - 1, true);
            const float half = 0.5f * (leave - enter);
            add_covered(covered, enter + half, half);
            n = m - 1;
        }
    }
}

}  // namespace

bool ring_contains(const Circle& ring, const core::Position& p) {
    if (ring.oval) {
        return offset_contains(core::BasePose{ring.centre, ring.oval->base, ring.oval->facing_deg}, ring.oval->offset_mm,
                               p);
    }
    return std::hypot(p.x - ring.centre.x, p.y - ring.centre.y) <= ring.radius_mm;
}

std::vector<RingPath> outline_paths(const std::vector<Circle>& rings) {
    std::vector<RingPath> out;
    constexpr int kPoints = 240;
    for (std::size_t i = 0; i < rings.size(); ++i) {
        const Circle& a = rings[i];
        if (!a.oval) continue;
        // A duplicate of an earlier oval ring is drawn once.
        bool duplicate = false;
        for (std::size_t j = 0; j < i && !duplicate; ++j) {
            duplicate = rings[j].kind == a.kind && rings[j].oval == a.oval &&
                        std::hypot(rings[j].centre.x - a.centre.x, rings[j].centre.y - a.centre.y) <= core::EPS_MM;
        }
        if (duplicate) continue;
        const std::vector<core::Position> pts =
            offset_outline(core::BasePose{a.centre, a.oval->base, a.oval->facing_deg}, a.oval->offset_mm, kPoints);
        std::vector<char> visible(pts.size(), 1);
        for (std::size_t k = 0; k < pts.size(); ++k) {
            for (std::size_t j = 0; j < rings.size() && visible[k] != 0; ++j) {
                if (j == i || rings[j].kind != a.kind) continue;
                if (rings[j].oval && rings[j].oval == a.oval &&
                    std::hypot(rings[j].centre.x - a.centre.x, rings[j].centre.y - a.centre.y) <= core::EPS_MM) {
                    continue;  // its duplicate
                }
                // Strictly inside the other ring (a point on its line still shows).
                const Circle& b = rings[j];
                const bool inside =
                    b.oval ? offset_contains(core::BasePose{b.centre, b.oval->base, b.oval->facing_deg},
                                             b.oval->offset_mm - 1e-3f, pts[k])
                           : std::hypot(pts[k].x - b.centre.x, pts[k].y - b.centre.y) < b.radius_mm - 1e-3f;
                if (inside) visible[k] = 0;
            }
        }
        const std::size_t n = pts.size();
        if (std::all_of(visible.begin(), visible.end(), [](char v) { return v != 0; })) {
            RingPath path{pts, a.kind};
            path.points.push_back(pts.front());
            out.push_back(std::move(path));
            continue;
        }
        // Runs of visible points (wrapping round), each a polyline.
        std::size_t start = 0;
        while (start < n && visible[start] != 0) ++start;
        if (start == n) continue;
        for (std::size_t m = 0; m < n; ++m) {
            const std::size_t k = (start + m) % n;
            if (visible[k] == 0) continue;
            RingPath path{{}, a.kind};
            while (m < n && visible[(start + m) % n] != 0) {
                path.points.push_back(pts[(start + m) % n]);
                ++m;
            }
            if (path.points.size() >= 2) out.push_back(std::move(path));
        }
    }
    return out;
}

std::vector<Arc> outline_arcs(const std::vector<Circle>& circles) {
    std::vector<Arc> out;
    for (std::size_t i = 0; i < circles.size(); ++i) {
        const Circle& a = circles[i];
        if (a.oval) continue;  // D-074 part 3: outline_paths
        std::vector<Interval> covered;
        bool hidden = false;
        for (std::size_t j = 0; j < circles.size() && !hidden; ++j) {
            if (j == i || circles[j].kind != a.kind) continue;
            const Circle& b = circles[j];
            if (b.oval) {
                add_oval_cover(a, b, covered, hidden);
                continue;
            }
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
