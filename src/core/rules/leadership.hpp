#pragma once

#include <optional>

#include "core/dice/dice.hpp"
#include "core/model/unit.hpp"

namespace wh40k::core {

// 01.06 Leadership roll: 2D6; succeeds if the result is equal to or greater than one or more of the
// Ld characteristics in the unit (i.e. the lowest Ld target number).
struct LeadershipRoll {
    TwoD6 roll;
    int target = 0;  // the unit's best (lowest) Ld, e.g. 6 for Ld 6+
    bool success = false;
    friend bool operator==(const LeadershipRoll&, const LeadershipRoll&) = default;
};

// Precondition: the unit has at least one model with an Ld characteristic (std::logic_error
// otherwise: Synapse-style roll changes and units without Ld are out of scope).
[[nodiscard]] LeadershipRoll make_leadership_roll(DiceSource& dice, const Unit& unit);

// 01.07 Battle-shock roll: a leadership roll; if it fails, the unit becomes battle-shocked.
struct BattleShockRoll {
    LeadershipRoll leadership;
    bool becomes_battle_shocked = false;
    friend bool operator==(const BattleShockRoll&, const BattleShockRoll&) = default;
};

[[nodiscard]] BattleShockRoll make_battle_shock_roll(DiceSource& dice, const Unit& unit);

// 01.07: a failed roll makes the unit (and each model in it) battle-shocked. A successful roll means
// only that the unit "does not become battle-shocked"; clearing an existing battle-shock is the
// Command phase's job (08.03, M11), so a success leaves the flag unchanged here.
void apply_battle_shock_roll(Unit& unit, const BattleShockRoll& roll);

// 01.07: while a unit is battle-shocked, the OC of all of its models is '-' (nullopt). Otherwise the
// model's own OC.
[[nodiscard]] std::optional<int> objective_control(const Unit& unit, const Model& model);

}  // namespace wh40k::core
