// M48 (D-059): bot pacing and Pause in the sidebar. The sidebar's "Pause the bots (P)" checkbox and key P agree,
// Step (and key N) only touch a bot's dice card, the bot's dice run follows the checkbox, and only a click on the
// pop-up or the bare board skips a paced bot card's wait. The Slow factor is in ai tests M48_Pacing.

#include <gtest/gtest.h>

#include "settings.hpp"
#include "watch_controls.hpp"

using namespace wh40k::client;

TEST(M48_Watch, SetPausedAndTheToggleAgree) {
    WatchControls w;
    BotSpeed speed = BotSpeed::Normal;
    w.set_paused(true);  // the checkbox
    EXPECT_TRUE(w.paused());
    EXPECT_EQ(watch_status(w, speed), "Paused");
    w.apply(WatchCommand::TogglePause, speed);  // key P
    EXPECT_FALSE(w.paused());
    w.apply(WatchCommand::TogglePause, speed);
    EXPECT_TRUE(w.paused());
    w.set_paused(false);
    EXPECT_FALSE(w.paused());
    w.set_paused(false);  // setting it twice is not a toggle
    EXPECT_FALSE(w.paused());
    EXPECT_EQ(watch_status(w, speed), "Playing at Normal speed");
    EXPECT_EQ(speed, BotSpeed::Normal);  // pausing doesn't touch the speed

    w.set_paused(true);
    w.reset();  // a new game, a load or the start screen: never kept between games
    EXPECT_FALSE(w.paused());
}

TEST(M48_Watch, StepIgnoresAHumansCard) {
    WatchControls w;
    BotSpeed speed = BotSpeed::Normal;
    w.apply(WatchCommand::Step, speed);  // key N while the human's own dice show
    EXPECT_FALSE(w.step_closes_popup(ShownCard::Humans));
    EXPECT_FALSE(w.step_requested());  // dropped: it doesn't wait to act for the bot later either
    EXPECT_FALSE(w.step_closes_popup(ShownCard::Humans));

    // A bot's card: the step reveals or closes it, once.
    w.apply(WatchCommand::Step, speed);
    EXPECT_TRUE(w.step_closes_popup(ShownCard::Bots));
    EXPECT_FALSE(w.step_requested());
    EXPECT_FALSE(w.step_closes_popup(ShownCard::Bots));

    // No card: the step waits for the bot's action, as before.
    w.apply(WatchCommand::Step, speed);
    EXPECT_FALSE(w.step_closes_popup(ShownCard::None));
    EXPECT_TRUE(w.step_requested());
}

TEST(M48_Watch, TheBotDiceRunFollowsThePauseCheckbox) {
    WatchControls w;
    EXPECT_TRUE(w.bot_dice_run());
    w.set_paused(true);
    EXPECT_FALSE(w.bot_dice_run());  // paused: the bot's dice wait for a click or Step
    w.set_paused(false);
    EXPECT_TRUE(w.bot_dice_run());
}

TEST(M48_Watch, OnlyAClickOnThePopUpOrTheBareBoardSkipsTheWait) {
    EXPECT_TRUE(click_skips_bot_dice_wait(true, DiceClickTarget::BareBoard));
    EXPECT_TRUE(click_skips_bot_dice_wait(true, DiceClickTarget::PopUp));
    // Its own Roll / Continue button acts by itself on release; a second skip would close the next card too.
    EXPECT_FALSE(click_skips_bot_dice_wait(true, DiceClickTarget::PopUpButton));
    // The sidebar (Pause the bots), the action bar (Step, the speed), a datasheet: no skip.
    EXPECT_FALSE(click_skips_bot_dice_wait(true, DiceClickTarget::OtherWindow));
    for (const DiceClickTarget t : {DiceClickTarget::BareBoard, DiceClickTarget::PopUp, DiceClickTarget::PopUpButton,
                                    DiceClickTarget::OtherWindow}) {
        EXPECT_FALSE(click_skips_bot_dice_wait(false, t));  // no click
    }
}
