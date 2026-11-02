#pragma once

#include <compare>
#include <cstdint>

namespace wh40k::core {

// 01.01 / 01.03: there are exactly two armies, one per side.
enum class Side : std::uint8_t { A, B };

[[nodiscard]] constexpr Side opponent(Side s) noexcept {
    return s == Side::A ? Side::B : Side::A;
}

[[nodiscard]] constexpr std::size_t side_index(Side s) noexcept {
    return s == Side::A ? 0u : 1u;
}

struct UnitId {
    std::uint32_t v = 0;
    friend constexpr auto operator<=>(const UnitId&, const UnitId&) = default;
};

struct ModelId {
    std::uint32_t v = 0;
    friend constexpr auto operator<=>(const ModelId&, const ModelId&) = default;
};

}  // namespace wh40k::core
