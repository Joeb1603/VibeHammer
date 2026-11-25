// M94 (D-095): engine-paced dice rolls and the bot. GameSession makes a bot side's rolls itself, so the bot never
// decides a RollDice, even against a human who rolls by hand; its own simulations roll inline (apply_action).

#include <gtest/gtest.h>

#include <variant>

#include "bot.hpp"
#include "core/engine/engine.hpp"
#include "fallback.hpp"
#include "session.hpp"
#include "simulator.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::ai::Bot;
using wh40k::ai::BotChoice;

TEST(M94_Bots, AgainstAHumanWhoRollsByHandTheBotNeverMeetsARollAndTheGameEnds) {
    // Side A: a "human" who answers with random legal actions and makes each of its rolls by hand (its pop-ups on);
    // Side B: the bot, whose rolls the session makes. No deadlock: the game reaches its end.
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario(), seed, {false, true});
        Bot bot;
        wh40k::test::TestRng rng(seed);
        int human_rolls = 0;
        for (int i = 0; i < 3000 && s.pending(); ++i) {
            if (s.pending()->side == Side::B) {
                ASSERT_FALSE(roll_pending(s.state())) << "the session makes the bot's rolls";
                const BotChoice c = bot.decide(s.state(), Side::B);
                ASSERT_FALSE(c.stopped) << c.reason;
                ASSERT_TRUE(s.submit(c.action).ok()) << action_name(c.action);
                continue;
            }
            if (roll_pending(s.state())) {
                const auto& d = std::get<decision::RollDice>(s.pending()->kind);
                ASSERT_TRUE(s.submit(action::RollDice{d.roll, d.unit}).ok());
                ++human_rolls;
                continue;
            }
            ASSERT_TRUE(s.submit(wh40k::test::random_action(s.state(), rng)).ok());
        }
        EXPECT_TRUE(s.state().result.has_value());
        EXPECT_GT(human_rolls, 0);
    }
}

TEST(M94_Bots, TheFallbackAnswersARollPendingForTheBot) {
    // After a load or an undo a roll can wait for a bot's side; the fallback (and so the bot) answers it.
    GameSession s(wh40k::test::shoot_step_state(), {false, false});
    action::Shoot a{s.state().units[0].id, ShootingType::Normal, {}};
    for (const Model& m : s.state().units[0].models) a.shots.push_back(ShotDeclaration{m.id, 0, 0, s.state().units[1].id});
    ASSERT_TRUE(s.submit(a).ok());
    ASSERT_TRUE(roll_pending(s.state()));
    const auto& d = std::get<decision::RollDice>(s.pending()->kind);
    const wh40k::ai::FallbackResult f = wh40k::ai::fallback_action(s.state(), Side::A);
    ASSERT_TRUE(f.action.has_value());
    EXPECT_EQ(*f.action, (GameAction{action::RollDice{d.roll, d.unit}}));
    const BotChoice c = Bot().decide(s.state(), Side::A);
    EXPECT_FALSE(c.stopped);
    EXPECT_EQ(c.action, (GameAction{action::RollDice{d.roll, d.unit}}));
}

TEST(M94_Bots, TheSimulatorRollsInlineAsBefore) {
    // D-054's simulator applies with apply_action: a charge declared with scripted dice is rolled within the step.
    GameState start = wh40k::test::load_v1_scenario();
    start_game(start);
    while (!(start.flow.stage == Stage::Charge && turn_side(start) == Side::A)) {
        apply_action(start, wh40k::test::decider(start), legal_actions(start, wh40k::test::decider(start)).front());
    }
    wh40k::ai::Simulator sim(start, 1);
    ScriptedDice dice{6, 6};
    ASSERT_TRUE(sim.step(Side::A, action::DeclareCharge{start.units[0].id}, dice).ok());
    EXPECT_EQ(dice.consumed(), 2u);
    EXPECT_FALSE(roll_pending(sim.state()));
    EXPECT_EQ(sim.steps(), 1u);
}
