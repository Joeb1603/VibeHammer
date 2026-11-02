#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace wh40k::core {

// 01.05 dice notation: "3", "D3", "D6", "2D6", "D6+1", "2D6+3".
// D-002: random characteristics are parsed and stored but never rolled in V1.
struct DiceExpr {
    std::uint8_t num_dice = 0;  // 0 => fixed value equal to `modifier`
    std::uint8_t sides = 0;     // 3 or 6 when num_dice > 0
    std::int16_t modifier = 0;

    [[nodiscard]] static constexpr DiceExpr fixed(int value) noexcept {
        return DiceExpr{0, 0, static_cast<std::int16_t>(value)};
    }
    [[nodiscard]] constexpr bool is_fixed() const noexcept { return num_dice == 0; }
    // Precondition: is_fixed().
    [[nodiscard]] int fixed_value() const;

    friend bool operator==(const DiceExpr&, const DiceExpr&) = default;
};

// Throws DataError on malformed input.
[[nodiscard]] DiceExpr parse_dice_expr(std::string_view text);

// Inverse of parse_dice_expr: "3", "D3", "2D6+1".
[[nodiscard]] std::string to_string(const DiceExpr& expr);

}  // namespace wh40k::core
