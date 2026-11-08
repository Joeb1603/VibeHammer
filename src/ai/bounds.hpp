#pragma once

// D-074 part 3 (M66): the bots' own geometry with oval bases. No bot level turns an oval (its facing stays fixed for
// the whole move), and every move is still confirmed by the engine (a refused or reverted plan is dropped). The bots'
// quick checks use bounding circles, chosen per site so the bot rarely plans a move the engine refuses:
// - where a site avoids something (enemy bases on a straight line, keeping out of Engagement Range, not ending on a
//   model, parking room) the circumscribed circle (half the length; it holds the base at every facing);
// - where a site needs to reach something (range, contact, the charge reach) core's exact pose-aware distances, or the
//   inscribed circle (half the width; it lies inside the base at every facing);
// - the battlefield edge by the base's exact half extents at its fixed facing (core::base_extents).
// For a round base each circle is its radius, so every formula here is the round one, the same float as before.

#include <algorithm>

#include "core/geometry/distance.hpp"
#include "core/geometry/oval.hpp"
#include "core/geometry/pose.hpp"
#include "core/geometry/primitives.hpp"

namespace wh40k::ai {

// The radius of the circle that holds the base at every facing (round: its radius).
[[nodiscard]] inline float avoid_radius_mm(const core::Base& b) noexcept { return core::circumscribed_radius(b); }

// The radius of the circle the base holds at every facing (round: its radius).
[[nodiscard]] inline float reach_radius_mm(const core::Base& b) noexcept { return core::inscribed_radius(b); }

// 01.04 between the bases' circumscribed circles: a lower bound on the true distance at any facings (base_distance's
// formula, so the same float for round bases).
[[nodiscard]] inline float avoid_gap_mm(const core::Position& a, const core::Base& base_a, const core::Position& b,
                                        const core::Base& base_b) {
    return std::max(0.0f, core::centre_distance(a, b) - avoid_radius_mm(base_a) - avoid_radius_mm(base_b));
}

// 01.04 between the bases' inscribed circles: an upper bound on the true distance at any facings (round: the same
// float as base_distance).
[[nodiscard]] inline float reach_gap_mm(const core::Position& a, const core::Base& base_a, const core::Position& b,
                                        const core::Base& base_b) {
    return std::max(0.0f, core::centre_distance(a, b) - reach_radius_mm(base_a) - reach_radius_mm(base_b));
}

// D-027 by the circumscribed circles: they intersect by more than EPS_MM (bases_overlap's formula: the same answer for
// round bases). An oval pair may be refused here while the bases don't overlap; never the other way round.
[[nodiscard]] inline bool avoid_overlap(const core::Position& a, const core::Base& base_a, const core::Position& b,
                                        const core::Base& base_b) {
    return core::centre_distance(a, b) < avoid_radius_mm(base_a) + avoid_radius_mm(base_b) - core::EPS_MM;
}

// The base's half extents along x and y at its fixed facing (round: its radius both ways, inline: the bots' hot loops),
// for the battlefield edge.
[[nodiscard]] inline core::BaseExtents edge_extents(const core::Base& b, float facing_deg) {
    if (b.shape == core::BaseShape::Round) {
        const float r = b.diameter_mm * 0.5f;  // core::base_extents' round case
        return core::BaseExtents{r, r};
    }
    return core::base_extents(b, facing_deg);
}

// Whether every model of `models` has a round base (the round fast paths: the old round-only calls).
template <typename Models>
[[nodiscard]] bool all_round(const Models& models) {
    for (const auto& m : models) {
        if (m.base.shape != core::BaseShape::Round) return false;
    }
    return true;
}

}  // namespace wh40k::ai
