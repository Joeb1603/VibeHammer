#include "core/rules/random_characteristics.hpp"

#include <stdexcept>
#include <string>

namespace wh40k::core {

CharacteristicRoll characteristic_from_faces(const DiceExpr& expr, std::span<const int> faces) {
    if (static_cast<int>(faces.size()) != expr.num_dice) {
        throw std::logic_error("D-069: " + to_string(expr) + " takes " + std::to_string(expr.num_dice) + " dice, not " +
                               std::to_string(faces.size()));
    }
    CharacteristicRoll r;
    r.value = expr.modifier;
    for (const int face : faces) {
        r.dice.push_back(face);
        r.value += expr.sides == 3 ? (face + 1) / 2 : face;  // 01.05: a D3 is a D6 halved, rounding up
    }
    return r;
}

CharacteristicRoll roll_characteristic(DiceSource& dice, const DiceExpr& expr) {
    const std::vector<int> faces = roll_d6s(dice, expr.num_dice);
    return characteristic_from_faces(expr, faces);
}

CharacteristicRoll damage_stand_in(const DiceExpr& expr) { return CharacteristicRoll{{}, expr.max_value()}; }

std::vector<AttackDiceRoll> random_attacks_from_faces(std::span<const RandomAttackDice> random,
                                                     std::span<const int> faces) {
    std::vector<AttackDiceRoll> out;
    out.reserve(random.size());
    std::size_t next = 0;
    for (const RandomAttackDice& r : random) {
        const auto n = static_cast<std::size_t>(r.attacks.num_dice);
        if (next + n > faces.size()) throw std::logic_error("D-069: too few dice for the batch's random attacks");
        out.push_back(AttackDiceRoll{r.selection, r.model, r.attacks,
                                     characteristic_from_faces(r.attacks, faces.subspan(next, n)), r.bonus});
        next += n;
    }
    if (next != faces.size()) throw std::logic_error("D-069: too many dice for the batch's random attacks");
    return out;
}

int random_attack_dice_count(std::span<const RandomAttackDice> random) {
    int n = 0;
    for (const RandomAttackDice& r : random) n += r.attacks.num_dice;
    return n;
}

std::vector<AttackDiceRoll> roll_random_attacks(DiceSource& dice, std::span<const RandomAttackDice> random) {
    std::vector<AttackDiceRoll> out;
    out.reserve(random.size());
    for (const RandomAttackDice& r : random) {
        out.push_back(AttackDiceRoll{r.selection, r.model, r.attacks, roll_characteristic(dice, r.attacks), r.bonus});
    }
    return out;
}

}  // namespace wh40k::core
