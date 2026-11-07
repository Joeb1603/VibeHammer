// M45: the watch controls (D-054 M45 notes): Pause / Resume, Step and the bot speed, as the watch bar and its
// keys use them. The driver side (a live speed change, a single step) is tested in ai tests M45_Watch.

#include <gtest/gtest.h>

#include "settings.hpp"
#include "watch_controls.hpp"

using namespace wh40k::client;

TEST(M45_Watch, FasterAndSlowerStopAtTheEnds) {
    EXPECT_EQ(faster(BotSpeed::Slow), BotSpeed::Normal);
    EXPECT_EQ(faster(BotSpeed::Normal), BotSpeed::Fast);
    EXPECT_EQ(faster(BotSpeed::Fast), BotSpeed::Instant);
    EXPECT_EQ(faster(BotSpeed::Instant), BotSpeed::Instant);
    EXPECT_EQ(slower(BotSpeed::Instant), BotSpeed::Fast);
    EXPECT_EQ(slower(BotSpeed::Fast), BotSpeed::Normal);
    EXPECT_EQ(slower(BotSpeed::Normal), BotSpeed::Slow);
    EXPECT_EQ(slower(BotSpeed::Slow), BotSpeed::Slow);
}

TEST(M45_Watch, CommandsPauseStepAndChangeTheSpeed) {
    WatchControls w;
    BotSpeed speed = BotSpeed::Normal;
    EXPECT_FALSE(w.paused());
    EXPECT_EQ(watch_status(w, speed), "Playing at Normal speed");
    w.apply(WatchCommand::TogglePause, speed);
    EXPECT_TRUE(w.paused());
    EXPECT_EQ(watch_status(w, speed), "Paused");
    w.apply(WatchCommand::Faster, speed);
    EXPECT_EQ(speed, BotSpeed::Fast);
    w.apply(WatchCommand::Slower, speed);
    w.apply(WatchCommand::Slower, speed);
    EXPECT_EQ(speed, BotSpeed::Slow);
    EXPECT_TRUE(w.paused());  // the speed doesn't touch the pause

    EXPECT_FALSE(w.step_requested());
    w.apply(WatchCommand::Step, speed);
    EXPECT_TRUE(w.step_requested());
    w.delivered();  // the driver handed the action over
    EXPECT_FALSE(w.step_requested());

    w.apply(WatchCommand::TogglePause, speed);
    EXPECT_FALSE(w.paused());
    w.apply(WatchCommand::Step, speed);
    w.apply(WatchCommand::TogglePause, speed);
    w.reset();  // a new game
    EXPECT_FALSE(w.paused());
    EXPECT_FALSE(w.step_requested());
}

TEST(M45_Watch, AStepWithDiceShowingGoesToThePopUp) {
    WatchControls w;
    BotSpeed speed = BotSpeed::Normal;
    EXPECT_FALSE(w.step_closes_popup(ShownCard::Bots));  // no step pressed
    w.apply(WatchCommand::Step, speed);
    EXPECT_FALSE(w.step_closes_popup(ShownCard::None));  // no pop-up: the step waits for the bot's action
    EXPECT_TRUE(w.step_requested());
    EXPECT_TRUE(w.step_closes_popup(ShownCard::Bots));  // a (bot's, M48) pop-up: this step reveals or closes the card
    EXPECT_FALSE(w.step_requested());        // and is used up
}

TEST(M45_Watch, PausedTheBotsDiceWaitForAClick) {
    WatchControls w;
    BotSpeed speed = BotSpeed::Normal;
    EXPECT_TRUE(w.bot_dice_run());
    w.apply(WatchCommand::TogglePause, speed);
    EXPECT_FALSE(w.bot_dice_run());
}
