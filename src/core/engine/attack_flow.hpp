#pragma once

// 04.03 Resolve Attacks and the 05 attack sequence, run by the engine with PendingDecisions for the
// players' choices: batch order (attacker, 04.03), allocation order (opposing player, 05.03) and
// casualties (opposing player, 05.04 via D-013). D-012 applies to batches whose target is gone.

#include <vector>

#include "core/dice/dice.hpp"
#include "core/engine/events.hpp"
#include "core/rules/attack_types.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::core {

// Starts resolving `attacker`'s attacks with the given weapon selections and advances to the next
// decision. The Shoot action (M8) and the Fight action (M10) use the same flow once their selections
// are validated; tests call this directly to start mid-phase. Preconditions (std::logic_error otherwise): the game is in the Shoot or
// Fight step, nothing else is in progress, the attacker has models, every selection's attacker
// belongs to it, and every target is an enemy unit.
std::vector<GameEvent> begin_attacks(GameState& state, DiceSource& dice, UnitId attacker,
                                     const AttackContext& context, std::vector<WeaponSelection> selections);
std::vector<GameEvent> begin_attacks(GameState& state, UnitId attacker, const AttackContext& context,
                                     std::vector<WeaponSelection> selections);

}  // namespace wh40k::core
