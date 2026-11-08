#pragma once

// Merged ring outlines (D-028): overlapping rings of one kind are drawn as a single border, the outer
// edge of the union of their circles. Pure geometry in world millimetres (y up), no rendering.

#include <optional>
#include <vector>

#include "core/geometry/primitives.hpp"

namespace wh40k::client {

enum class RingKind { MoveRange, Engagement, Coherency, WeaponRange, ChargeDistance };

// D-074 part 3 (M66): a ring round an oval base, `offset_mm` out from its edge at `facing_deg`: the ellipse's offset
// curve (offset_outline), not a scaled ellipse.
struct OvalRing {
    core::Base base;
    float facing_deg = 0.0f;
    float offset_mm = 0.0f;
    friend bool operator==(const OvalRing&, const OvalRing&) = default;
};

// A ring: a circle, or (D-074 part 3) with `oval` set, the oval's ring, of which radius_mm is then the circumscribed
// bound (the base's circumscribed radius + offset_mm).
struct Circle {
    core::Position centre;
    float radius_mm = 0.0f;
    RingKind kind = RingKind::Engagement;
    std::optional<OvalRing> oval;
};

// Whether `p` is inside the ring: within radius_mm of a circle's centre, within offset_mm of an oval's edge.
[[nodiscard]] bool ring_contains(const Circle& ring, const core::Position& p);

// D-074 part 3: the visible parts of an oval ring (not inside another ring of the same kind), as polylines in world
// millimetres (closed when the whole ring shows: first point repeated at the end).
struct RingPath {
    std::vector<core::Position> points;
    RingKind kind = RingKind::Engagement;
};
[[nodiscard]] std::vector<RingPath> outline_paths(const std::vector<Circle>& rings);

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
// the outer border of each overlapping group. Duplicate circles count once. D-074 part 3: an oval ring of the same
// kind covers the parts of a circle's rim inside it too (found by sampling the rim every degree and bisecting); an
// oval ring itself gives no arcs (outline_paths draws it). Without oval rings, exactly as before.
[[nodiscard]] std::vector<Arc> outline_arcs(const std::vector<Circle>& circles);

}  // namespace wh40k::client
