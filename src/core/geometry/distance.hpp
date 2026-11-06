#pragma once

#include "core/geometry/primitives.hpp"
#include "core/geometry/units.hpp"

namespace wh40k::core {

// Distance between two base centres on the battlefield plane (mm). V1 has no terrain, so this is
// horizontal only; the 5" vertical clauses of 03.03/03.04 are trivially satisfied.
[[nodiscard]] float centre_distance(const Position& a, const Position& b);

// 01.04: distance between two models, measured between the closest parts of their bases.
// centre_distance - r1 - r2, clamped at 0. Round bases only in V1 (other shapes throw
// std::logic_error: programmer error).
[[nodiscard]] float base_distance(const Position& a, const Base& base_a, const Position& b,
                                  const Base& base_b);

// Radius of a round base (mm). Throws std::logic_error for other shapes.
[[nodiscard]] float base_radius(const Base& base);

// ---- Tolerance helpers (CLAUDE.md, Tolerance policy). Never compare raw floats in rules code. ----

// "within X": dist <= X + EPS_MM.
[[nodiscard]] constexpr bool within(float distance_mm, float limit_mm) noexcept {
    return distance_mm <= limit_mm + EPS_MM;
}

// "more than X": dist > X + EPS_MM. Exactly !within.
[[nodiscard]] constexpr bool more_than(float distance_mm, float limit_mm) noexcept {
    return distance_mm > limit_mm + EPS_MM;
}

// Base contact: dist <= 0.05" (D-048: leeway for mouse placement; a house rule, not rules text).
[[nodiscard]] constexpr bool in_base_contact(float distance_mm) noexcept {
    return distance_mm <= kBaseContactToleranceMm;
}

// Two bases overlap (one model is "on" another, 03.01 Ending a Move) when they intersect by more
// than EPS_MM. Bases in contact do not overlap.
[[nodiscard]] bool bases_overlap(const Position& a, const Base& base_a, const Position& b,
                                 const Base& base_b);

// ---- Areas (D-009) ----

// D-009 Within: some part of the model's base is inside the rectangle.
[[nodiscard]] bool within_area(const Rect& area, const Position& pos, const Base& base);

// D-009 Wholly Within: every part of the model's base is inside the rectangle.
[[nodiscard]] bool wholly_within(const Rect& area, const Position& pos, const Base& base);

}  // namespace wh40k::core
