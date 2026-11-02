#pragma once

// Rules Appendix, Starting Strength and Half-strength. The starting strength is the number of models a
// unit contains at the start of the first battle round (Unit::starting_strength, set by the loader; V1 has
// no deployment, D-003).

#include "core/model/unit.hpp"

namespace wh40k::core {

// Starting strength of 1: the model's remaining wounds are less than its W characteristic.
// Starting strength of 2 or more: fewer models remain than the starting strength.
[[nodiscard]] bool below_starting_strength(const Unit& unit);

// Starting strength of 1: the model's remaining wounds are half of its W. Starting strength of 2 or
// more: the remaining models are half of the starting strength. A W or starting strength that cannot
// be evenly halved can never be at half-strength (only below it).
[[nodiscard]] bool at_half_strength(const Unit& unit);

// Starting strength of 1: remaining wounds less than half of W. Starting strength of 2 or more:
// remaining models less than half of the starting strength.
[[nodiscard]] bool below_half_strength(const Unit& unit);

// 08.03: "at, or below, half-strength".
[[nodiscard]] bool at_or_below_half_strength(const Unit& unit);

}  // namespace wh40k::core
