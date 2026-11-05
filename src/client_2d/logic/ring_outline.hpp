#pragma once

// Merged ring outlines (D-028): overlapping rings of one kind are drawn as a single border, the outer
// edge of the union of their circles. Pure geometry in world millimetres (y up), no rendering.

#include <vector>

#include "core/geometry/primitives.hpp"

namespace wh40k::client {

enum class RingKind { MoveRange, Engagement, Coherency, WeaponRange, ChargeDistance };

struct Circle {
    core::Position centre;
    float radius_mm = 0.0f;
    RingKind kind = RingKind::Engagement;
};

// Part of a circle's rim, counter-clockwise from start_rad to end_rad (end_rad > start_rad; a whole
// circle is 0 to 2π). Angles are measured in world space, from +x towards +y.
struct Arc {
    core::Position centre;
    float radius_mm = 0.0f;
    float start_rad = 0.0f;
    float end_rad = 0.0f;
    RingKind kind = RingKind::Engagement;
};

// For each circle, the parts of its rim not inside another circle of the same kind. Together they are
// the outer border of each overlapping group. Duplicate circles count once.
[[nodiscard]] std::vector<Arc> outline_arcs(const std::vector<Circle>& circles);

}  // namespace wh40k::client
