#pragma once

// 04.03 Resolve Attacks and the 05 attack sequence, run by the engine with PendingDecisions for the
// players' choices: batch order (attacker, 04.03), allocation order (opposing player, 05.03) and
// casualties (opposing player, 05.04 via D-013). D-093 (04.03.03) re-targets or drops batches whose target is gone. Once every batch
// is resolved, the attacking unit makes its [HAZARDOUS] hazard rolls (24.15, 06.03) and its player allocates the
// mortal wounds (06.02, OrderCasualties for the attacking unit; D-071). A [TWIN-LINKED] batch with a failed wound roll
// waits for its attacker's RerollWoundRolls answer (24.38, D-011, D-072).

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
// belongs to it, and every target is an enemy unit. D-095 (M94): every dice roll on the way is made at once (as
// apply_action), so the next decision is never a RollDice.
std::vector<GameEvent> begin_attacks(GameState& state, DiceSource& dice, UnitId attacker,
                                     const AttackContext& context, std::vector<WeaponSelection> selections);
std::vector<GameEvent> begin_attacks(GameState& state, UnitId attacker, const AttackContext& context,
                                     std::vector<WeaponSelection> selections);

// 04.03.03 (D-093): one weapon selection waiting on a target that is gone, and its eligible new targets (unit order).
struct RetargetChoice {
    std::size_t selection = 0;  // index into the attack's WeaponSelections
    std::vector<UnitId> options;
    friend bool operator==(const RetargetChoice&, const RetargetChoice&) = default;
};
// Read-only, for clients and bots (M93): every such selection, in selection order, including those with no option.
// Empty unless an attack is in progress.
[[nodiscard]] std::vector<RetargetChoice> retarget_choices(const GameState& state);

}  // namespace wh40k::core
