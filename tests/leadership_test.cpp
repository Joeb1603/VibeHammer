#include <gtest/gtest.h>

#include <stdexcept>

#include "core/dice/dice.hpp"
#include "core/rules/leadership.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::load_v1_scenario;

TEST(R01_06_LeadershipRolls, EqualToLdSucceeds) {
    const GameState gs = load_v1_scenario();
    const Unit& gants = gs.units[1];  // Ld 8+
    ScriptedDice d{4, 4};
    const LeadershipRoll r = make_leadership_roll(d, gants);
    EXPECT_EQ(r.roll.total(), 8);
    EXPECT_EQ(r.target, 8);
    EXPECT_TRUE(r.success);
}

TEST(R01_06_LeadershipRolls, BelowLdFails) {
    const GameState gs = load_v1_scenario();
    ScriptedDice d{3, 4};
    EXPECT_FALSE(make_leadership_roll(d, gs.units[1]).success);
}

TEST(R01_06_LeadershipRolls, AnyLdInTheUnitIsEnough) {
    GameState gs = load_v1_scenario();
    Unit gants = gs.units[1];
    gants.models[3].characteristics.leadership = 6;  // one model with Ld 6+
    ScriptedDice d{3, 3};
    const LeadershipRoll r = make_leadership_roll(d, gants);
    EXPECT_EQ(r.target, 6);
    EXPECT_TRUE(r.success);
}

TEST(R01_06_LeadershipRolls, UnitWithoutLdIsProgrammerError) {
    GameState gs = load_v1_scenario();
    Unit u = gs.units[0];
    for (auto& m : u.models) m.characteristics.leadership.reset();
    ScriptedDice d{6, 6};
    EXPECT_THROW((void)make_leadership_roll(d, u), std::logic_error);
}

TEST(R01_07_BattleShockRolls, FailedRollBattleShocksTheUnit) {
    GameState gs = load_v1_scenario();
    Unit& marines = gs.units[0];  // Ld 6+
    ScriptedDice d{1, 4};
    const BattleShockRoll r = make_battle_shock_roll(d, marines);
    EXPECT_TRUE(r.becomes_battle_shocked);
    apply_battle_shock_roll(marines, r);
    EXPECT_TRUE(marines.battle_shocked);
}

TEST(R01_07_BattleShockRolls, PassedRollDoesNotBattleShock) {
    GameState gs = load_v1_scenario();
    Unit& marines = gs.units[0];
    ScriptedDice d{3, 3};
    const BattleShockRoll r = make_battle_shock_roll(d, marines);
    EXPECT_FALSE(r.becomes_battle_shocked);
    apply_battle_shock_roll(marines, r);
    EXPECT_FALSE(marines.battle_shocked);
}

TEST(R01_07_BattleShockRolls, PassedRollLeavesExistingBattleShockForTheCommandPhase) {
    // Clearing battle-shock is 08.03 (M11); 01.07 only says the unit "does not become" battle-shocked.
    GameState gs = load_v1_scenario();
    Unit& marines = gs.units[0];
    marines.battle_shocked = true;
    ScriptedDice d{6, 6};
    apply_battle_shock_roll(marines, make_battle_shock_roll(d, marines));
    EXPECT_TRUE(marines.battle_shocked);
}
