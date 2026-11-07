// M48 (D-059): Slow is three times Normal (it was twice): 2.4 s before a unit's activation, 0.36 s per model
// placement, and about 5 s per bot dice card (1.05 s before the roll, the 0.45 s tumble, 3.6 s on show).

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <variant>

#include "bot.hpp"
#include "bot_driver.hpp"
#include "core/engine/actions.hpp"
#include "session.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;

namespace {

// The time at which a synchronous driver first hands over the action, ticking every 10 ms from 0.
double first_delivery(BotSpeed speed, const GameState& gs) {
    BotDriver d(BotConfig{BotLevel::Easy}, speed, BotDriver::Threading::Synchronous);
    DriverInputs in;
    in.state = &gs;
    in.bot_sides = std::array<bool, 2>{true, true};
    for (int i = 0; i <= 1000; ++i) {
        const double t = i * 0.01;
        if (d.tick(t, in)) return t;
    }
    return -1.0;
}

}  // namespace

TEST(M48_Pacing, SlowIsThreeTimesNormal) {
    EXPECT_DOUBLE_EQ(speed_factor(BotSpeed::Slow), 3.0 * speed_factor(BotSpeed::Normal));
    EXPECT_DOUBLE_EQ(pause_seconds(action::StartMove{}, BotSpeed::Slow), 2.4);
    EXPECT_DOUBLE_EQ(pause_seconds(action::ProposeModelMove{}, BotSpeed::Slow), 0.36);
    EXPECT_DOUBLE_EQ(pause_seconds(action::CommitUnitMove{}, BotSpeed::Slow), 1.2);
    EXPECT_DOUBLE_EQ(pause_seconds(action::OrderCasualties{}, BotSpeed::Slow), 1.8);
    EXPECT_DOUBLE_EQ(bot_dice_roll_pause_seconds(BotSpeed::Slow), 1.05);
    EXPECT_DOUBLE_EQ(bot_dice_dwell_seconds(BotSpeed::Slow), 3.6);
    // The other speeds are unchanged.
    EXPECT_DOUBLE_EQ(speed_factor(BotSpeed::Normal), 1.0);
    EXPECT_DOUBLE_EQ(speed_factor(BotSpeed::Fast), 0.4);
    EXPECT_DOUBLE_EQ(speed_factor(BotSpeed::Instant), 0.0);
}

TEST(M48_Pacing, ASlowBotHandsOverItsActivationAfter2Point4Seconds) {
    const client::GameSession s(wh40k::test::load_v1_scenario());  // Side A selects a unit to move
    const GameState& gs = s.state();
    const BotChoice expected = Bot(BotConfig{BotLevel::Easy}).decide(gs, gs.pending->side);
    ASSERT_TRUE(std::holds_alternative<action::StartMove>(expected.action));
    EXPECT_NEAR(first_delivery(BotSpeed::Slow, gs), 2.4, 0.011);
    EXPECT_NEAR(first_delivery(BotSpeed::Normal, gs), 0.8, 0.011);
}
