#include "core/geometry/closest.hpp"

#include <algorithm>
#include <cmath>

#include "core/geometry/distance.hpp"

namespace wh40k::core {

namespace {

// Two distances are "equally close" (D-010 tie) when they differ by no more than EPS_MM.
bool tied(float a, float b) { return std::abs(a - b) <= EPS_MM; }

float distance(const PlacedModel& a, const PlacedModel& b) {
    return base_distance(a.position, a.base, b.position, b.base);
}

}  // namespace

std::optional<float> closest_distance(std::span<const PlacedModel> a, std::span<const PlacedModel> b) {
    std::optional<float> best;
    for (const PlacedModel& ma : a) {
        for (const PlacedModel& mb : b) {
            const float d = distance(ma, mb);
            if (!best || d < *best) {
                best = d;
            }
        }
    }
    return best;
}

std::vector<ModelId> closest_models(const PlacedModel& from, std::span<const PlacedModel> candidates) {
    std::vector<ModelId> out;
    if (candidates.empty()) {
        return out;
    }
    float best = distance(from, candidates.front());
    for (const PlacedModel& c : candidates) {
        best = std::min(best, distance(from, c));
    }
    for (const PlacedModel& c : candidates) {
        if (tied(distance(from, c), best)) {
            out.push_back(c.id);
        }
    }
    return out;
}

std::vector<std::size_t> closest_units(std::span<const PlacedModel> from,
                                       std::span<const std::vector<PlacedModel>> candidates) {
    std::vector<std::optional<float>> dists;
    dists.reserve(candidates.size());
    std::optional<float> best;
    for (const auto& unit : candidates) {
        const std::optional<float> d = closest_distance(from, unit);
        dists.push_back(d);
        if (d && (!best || *d < *best)) {
            best = d;
        }
    }
    std::vector<std::size_t> out;
    if (!best) {
        return out;
    }
    for (std::size_t i = 0; i < dists.size(); ++i) {
        if (dists[i] && tied(*dists[i], *best)) {
            out.push_back(i);
        }
    }
    return out;
}

}  // namespace wh40k::core
