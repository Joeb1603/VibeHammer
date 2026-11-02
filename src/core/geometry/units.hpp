#pragma once

// Units and tolerances (CLAUDE.md, Geometry).
//
// Battlefield coordinate system: origin at the south-west corner of the battlefield, +x east,
// +y north, all lengths in float millimetres. Data files and the UI use inches; convert at the
// boundary with the helpers below.

namespace wh40k::core {

constexpr float MM_PER_INCH = 25.4f;

// All "within X" checks use dist <= X + EPS_MM; all "more than X" checks use dist > X + EPS_MM.
constexpr float EPS_MM = 0.01f;

[[nodiscard]] constexpr float inches_to_mm(float inches) noexcept {
    return inches * MM_PER_INCH;
}

[[nodiscard]] constexpr float mm_to_inches(float mm) noexcept {
    return mm / MM_PER_INCH;
}

}  // namespace wh40k::core
