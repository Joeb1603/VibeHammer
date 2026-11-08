#pragma once

#include <string>
#include <vector>

#include "core/engine/events.hpp"

namespace wh40k::client {

// One line for the event log, e.g. "[05.01] Hit rolls (batch 0): 3 5 6! 1x -> 3 hits".
[[nodiscard]] std::string format_event(const core::GameEvent& event);

// The dice log: events that report dice rolls (hit, wound, save, advance, charge, hazard, battle-shock; D-069: random
// A, and a random D's damage rolls in SavesResolved).
[[nodiscard]] bool is_roll_event(const core::GameEvent& event);

// D-069: a random characteristic's roll, e.g. "D6+3 -> 4+3 = 7" or "D3 -> 2[3] = 2" (a D3 shows its D6 in brackets).
[[nodiscard]] std::string characteristic_roll_text(const core::DiceExpr& expr, const core::CharacteristicRoll& roll);
// D-069: attack dice not all rolled yet: "4" (fixed), "2 + D6+3" (the known dice, bonus dice included, and each random A).
[[nodiscard]] std::string attack_count_text(int attack_dice, const std::vector<core::RandomAttackDice>& random);

}  // namespace wh40k::client
