// M92 (D-093): the bot answers 04.03.03 RetargetAttacks: the new target with the most expected value, through
// Bot::decide, the defaults and the always-legal fallback.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "bot.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/model/keywords.hpp"
#include "core/state/queries.hpp"
#include "default_choice.hpp"
#include "fallback.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::add_unit_copy;
using wh40k::test::model_ids;
using wh40k::test::select;

namespace {

// As tests/m92_test.cpp: the first Termagant unit (one model) is destroyed by batch 0, batch 1 (Focused Fire) waits
// for a new target, and the second unit is eligible.
GameState at_the_decision(UnitId* second_out) {
    GameState gs = wh40k::test::shoot_step_state();
    const UnitId gants = gs.units[1].id;
    const UnitId second = add_unit_copy(gs, 1);
    gs.units[1].models.resize(1);
    const AttackContext shooting{AttackType::Shooting, true};
    const std::vector<WeaponSelection> sel{select(gs, 0, 0, 0, 1, gants), select(gs, 0, 1, 0, 0, gants),
                                           select(gs, 0, 2, 0, 1, second)};
    ScriptedDice dice{3, 3, 3, 3, 3, 3, 1, 1, 1};
    begin_attacks(gs, dice, gs.units[0].id, shooting, sel);
    apply_action(gs, Side::A, action::SelectAttackBatch{0}, dice);
    apply_action(gs, Side::B, action::OrderCasualties{gants, model_ids(gs.units[1])}, dice);
    *second_out = second;
    return gs;
}

}  // namespace

TEST(M92_Bots, TheBotRetargetsAtTheEligibleUnit) {
    UnitId second;
    const GameState gs = at_the_decision(&second);
    ASSERT_TRUE(std::holds_alternative<decision::RetargetAttacks>(gs.pending->kind));
    const GameAction expected = action::RetargetAttacks{{AttackRetarget{1, second}}};
    for (const bool defaults : {false, true}) {
        Bot bot(BotConfig{});
        const BotChoice c = defaults ? wh40k::test::default_choice(gs, Side::A) : bot.decide(gs, Side::A);
        ASSERT_FALSE(c.stopped) << defaults;
        EXPECT_EQ(c.action, expected) << defaults;
        EXPECT_TRUE(validate_action(gs, Side::A, c.action).ok());
        EXPECT_EQ(bot.fallbacks(), 0u) << defaults;
    }
    const FallbackResult fb = fallback_action(gs, Side::A);
    ASSERT_TRUE(fb.action.has_value());
    EXPECT_EQ(*fb.action, expected);
}
