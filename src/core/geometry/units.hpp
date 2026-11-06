#pragma once

// Units and tolerances (CLAUDE.md, Geometry).
//
// Battlefield coordinate system: origin at the south-west corner of the battlefield, +x east,
// +y north, all lengths in float millimetres. Data files and the UI use inches; convert at the
// boundary with the helpers below.

#include <cstdio>
#include <string>

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

// A length for messages and logs, in inches with two decimals: 152.4 mm -> "6.00\"". Rounds, so a float
// just under a whole inch still reads as that inch.
[[nodiscard]] inline std::string format_inches(float mm) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f\"", static_cast<double>(mm_to_inches(mm)));
    return buf;
}

}  // namespace wh40k::core
