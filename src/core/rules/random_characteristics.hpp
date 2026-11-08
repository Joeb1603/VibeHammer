#pragma once

// D-069 Random Characteristics (app-only rule; wording supplied by the user, AI-sourced, not verified against the app):
// - a random A is rolled for each model and weapon when its batch is gathered for resolution (04.03), and the
//   [BLAST] / [RAPID FIRE] dice are added after the roll;
// - a random D is rolled for each attack that inflicts damage, after the model it is allocated to is selected (05.04).
// A fixed value is never rolled. The data types (CharacteristicRoll, RandomAttackDice, AttackDiceRoll) are in
// attack_types.hpp.

#include <span>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/model/dice_expr.hpp"
#include "core/rules/attack_types.hpp"

namespace wh40k::core {

// 01.05: rolls `expr`: one D6 per die, in order, each halved (rounding up) for a D3, then the modifier is added. A fixed
// expression rolls no dice and gives its value.
[[nodiscard]] CharacteristicRoll roll_characteristic(DiceSource& dice, const DiceExpr& expr);

// D-069 (amends D-051): what the casualty-decision check and the casualty preview use for a random D, without
// touching the dice: the most it can roll (D6+1: 7), with no dice.
[[nodiscard]] CharacteristicRoll damage_stand_in(const DiceExpr& expr);

// D-069 (04.03 Gather Attack Dice): each random A of a batch is rolled for its model and weapon, in the order given
// (the selections' order); its bonus dice are added after the roll (AttackDiceRoll::total).
[[nodiscard]] std::vector<AttackDiceRoll> roll_random_attacks(DiceSource& dice, std::span<const RandomAttackDice> random);

}  // namespace wh40k::core
