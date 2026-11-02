#pragma once

#include <algorithm>

namespace wh40k::core {

// D-011 (Modifying Dice Rolls, app-only): hit and wound rolls can never be modified by a net total
// of more than +1 or -1. Pass the sum of all applicable modifiers.
[[nodiscard]] constexpr int cap_hit_wound_modifier(int net_modifier) noexcept {
    return std::clamp(net_modifier, -1, 1);
}

// D-011: a modified result is never less than 1 and has no maximum (a D6 can be modified above 6).
// `modifier` is already capped where a cap applies (hit/wound rolls); save modifiers are not capped.
[[nodiscard]] constexpr int modified_result(int unmodified, int modifier) noexcept {
    return std::max(1, unmodified + modifier);
}

}  // namespace wh40k::core
