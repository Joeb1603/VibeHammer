#pragma once

// Fixed unit-vector tables for sampling directions (D-054: candidate directions come from a constant table,
// not sin/cos, so planned moves don't drift with the maths library). Each table is built at compile time by
// repeated rotation through an exact double literal (only + and *), so it is the same on every compiler.

#include <array>
#include <cstddef>

namespace wh40k::client {

struct UnitVector {
    float x = 0.0f;
    float y = 0.0f;
};

namespace detail {

// N unit vectors from (1, 0) counter-clockwise, each `cos_step`, `sin_step` on from the last.
template <std::size_t N>
constexpr std::array<UnitVector, N> rotation_table(double cos_step, double sin_step) {
    std::array<UnitVector, N> out{};
    double x = 1.0;
    double y = 0.0;
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = UnitVector{static_cast<float>(x), static_cast<float>(y)};
        const double nx = x * cos_step - y * sin_step;
        const double ny = x * sin_step + y * cos_step;
        x = nx;
        y = ny;
    }
    return out;
}

}  // namespace detail

// Every 5 degrees (cos 5°, sin 5°).
inline constexpr std::array<UnitVector, 72> kDirections72 =
    detail::rotation_table<72>(0.99619469809174553229501, 0.08715574274765817355806);
// Every 22.5 degrees (cos 22.5°, sin 22.5°): M43's movement directions.
inline constexpr std::array<UnitVector, 16> kDirections16 =
    detail::rotation_table<16>(0.92387953251128675613, 0.38268343236508977173);

}  // namespace wh40k::client
