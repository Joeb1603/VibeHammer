#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace wh40k::core {

// 01.05 dice notation: "3", "D3", "D6", "2D6", "D6+1", "2D6+3".
// D-069 (replaces D-002): a random A or D is rolled when it is needed (roll_characteristic,
// rules/random_characteristics.hpp); a fixed value is never rolled.
struct DiceExpr {
    std::uint8_t num_dice = 0;  // 0 => fixed value equal to `modifier`
    std::uint8_t sides = 0;     // 3 or 6 when num_dice > 0
    std::int16_t modifier = 0;

    [[nodiscard]] static constexpr DiceExpr fixed(int value) noexcept {
        return DiceExpr{0, 0, static_cast<std::int16_t>(value)};
    }
    [[nodiscard]] constexpr bool is_fixed() const noexcept { return num_dice == 0; }
    // Precondition: is_fixed() (std::logic_error otherwise: a programmer error).
    [[nodiscard]] int fixed_value() const;
    // The most and the least it can roll (01.05: a D3 is 1-3); the value itself when fixed.
    [[nodiscard]] constexpr int max_value() const noexcept { return num_dice * sides + modifier; }
    [[nodiscard]] constexpr int min_value() const noexcept { return num_dice + modifier; }

    friend bool operator==(const DiceExpr&, const DiceExpr&) = default;
};

// Throws DataError on malformed input.
[[nodiscard]] DiceExpr parse_dice_expr(std::string_view text);

// Inverse of parse_dice_expr: "3", "D3", "2D6+1".
[[nodiscard]] std::string to_string(const DiceExpr& expr);

}  // namespace wh40k::core
