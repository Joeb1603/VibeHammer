#pragma once

#include <cstdint>

namespace wh40k::core {

// Position of a model's base centre on the battlefield plane (mm). z is reserved for terrain
// (post-V1) and is always 0 in V1.
struct Position {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    friend bool operator==(const Position&, const Position&) = default;
};

enum class BaseShape : std::uint8_t { Round, Oval };

// V1 uses round bases only; don't assume circles outside geometry/.
struct Base {
    BaseShape shape = BaseShape::Round;
    float diameter_mm = 0.0f;
    float length_mm = 0.0f;  // oval bases only; 0 for round
    friend bool operator==(const Base&, const Base&) = default;
};

// Axis-aligned rectangle on the battlefield plane (mm), min corner inclusive.
struct Rect {
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
    friend bool operator==(const Rect&, const Rect&) = default;
};

}  // namespace wh40k::core
