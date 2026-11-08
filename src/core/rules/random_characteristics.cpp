#include "core/rules/random_characteristics.hpp"

namespace wh40k::core {

CharacteristicRoll roll_characteristic(DiceSource& dice, const DiceExpr& expr) {
    CharacteristicRoll r;
    r.value = expr.modifier;
    for (int i = 0; i < expr.num_dice; ++i) {
        const int face = dice.roll_d6();
        r.dice.push_back(face);
        r.value += expr.sides == 3 ? (face + 1) / 2 : face;  // 01.05: a D3 is a D6 halved, rounding up
    }
    return r;
}

CharacteristicRoll damage_stand_in(const DiceExpr& expr) { return CharacteristicRoll{{}, expr.max_value()}; }

std::vector<AttackDiceRoll> roll_random_attacks(DiceSource& dice, std::span<const RandomAttackDice> random) {
    std::vector<AttackDiceRoll> out;
    out.reserve(random.size());
    for (const RandomAttackDice& r : random) {
        out.push_back(AttackDiceRoll{r.selection, r.model, r.attacks, roll_characteristic(dice, r.attacks), r.bonus});
    }
    return out;
}

}  // namespace wh40k::core
