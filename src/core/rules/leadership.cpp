#include "core/rules/leadership.hpp"

#include <optional>
#include <stdexcept>

namespace wh40k::core {

LeadershipRoll make_leadership_roll(DiceSource& dice, const Unit& unit) {
    // 01.06: "equal to or greater than one or more of the Ld characteristics" == >= the lowest one.
    std::optional<int> best;
    for (const Model& m : unit.models) {
        if (m.characteristics.leadership && (!best || *m.characteristics.leadership < *best)) {
            best = m.characteristics.leadership;
        }
    }
    if (!best) {
        throw std::logic_error("01.06: unit '" + unit.name + "' has no model with an Ld characteristic");
    }
    LeadershipRoll r;
    r.roll = roll_2d6(dice);
    r.target = *best;
    r.success = r.roll.total() >= r.target;
    return r;
}

BattleShockRoll make_battle_shock_roll(DiceSource& dice, const Unit& unit) {
    BattleShockRoll r;
    r.leadership = make_leadership_roll(dice, unit);  // 01.07
    r.becomes_battle_shocked = !r.leadership.success;
    return r;
}

void apply_battle_shock_roll(Unit& unit, const BattleShockRoll& roll) {
    if (roll.becomes_battle_shocked) {
        unit.battle_shocked = true;  // 01.07: that unit, and each model in it, is battle-shocked
    }
}

}  // namespace wh40k::core
