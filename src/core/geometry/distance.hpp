#pragma once

#include "core/geometry/primitives.hpp"

namespace wh40k::core {

// 01.04: distance between two models, measured between the closest parts of their bases.
// center_distance - r1 - r2, clamped at 0. Round bases only in V1 (other shapes throw
// std::logic_error: programmer error).
[[nodiscard]] float base_distance(const Position& a, const Base& base_a, const Position& b,
                                  const Base& base_b);

// Tolerance helpers. Never compare raw floats in rules code.
[[nodiscard]] constexpr bool within(float distance_mm, float limit_mm) noexcept;
[[nodiscard]] constexpr bool more_than(float distance_mm, float limit_mm) noexcept;

// D-009 (Wholly Within, app-only): every part of the model's base is inside the rectangle,
// with EPS_MM tolerance.
[[nodiscard]] bool wholly_within(const Rect& area, const Position& pos, const Base& base);

}  // namespace wh40k::core

#include "core/geometry/units.hpp"

namespace wh40k::core {

constexpr bool within(float distance_mm, float limit_mm) noexcept {
    return distance_mm <= limit_mm + EPS_MM;
}

constexpr bool more_than(float distance_mm, float limit_mm) noexcept {
    return distance_mm > limit_mm + EPS_MM;
}

}  // namespace wh40k::core
