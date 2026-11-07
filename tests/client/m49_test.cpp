// M49 (D-060): the Roll and Continue buttons show on a dice card only when it waits for a click. A bot's card
// that rolls by itself (the bot's card, "Bot rolls its own dice automatically" on, the bots not paused) has
// neither; a human's card always has both. dice_buttons_shown is exactly the opposite of bot_dice_paced, which
// decides whether DicePopup::draw gets a DicePace (main.cpp bot_dice_pace).

#include <gtest/gtest.h>

#include "settings.hpp"
#include "watch_controls.hpp"

using namespace wh40k::client;

TEST(M49_DiceButtons, ABotsCardRollingByItselfHasNoButtons) {
    EXPECT_TRUE(bot_dice_paced(/*bot_card=*/true, /*auto_dice_setting=*/true, /*paused=*/false));
    EXPECT_FALSE(dice_buttons_shown(true, true, false));
}

TEST(M49_DiceButtons, ABotsCardShowsTheButtonsWhenPaused) {
    EXPECT_FALSE(bot_dice_paced(true, true, true));
    EXPECT_TRUE(dice_buttons_shown(true, true, true));
}

TEST(M49_DiceButtons, ABotsCardShowsTheButtonsWithTheSettingOff) {
    EXPECT_FALSE(bot_dice_paced(true, false, false));
    EXPECT_TRUE(dice_buttons_shown(true, false, false));
    EXPECT_FALSE(bot_dice_paced(true, false, true));
    EXPECT_TRUE(dice_buttons_shown(true, false, true));
}

TEST(M49_DiceButtons, AHumansCardAlwaysShowsTheButtons) {
    for (const bool setting : {false, true}) {
        for (const bool paused : {false, true}) {
            EXPECT_FALSE(bot_dice_paced(false, setting, paused)) << setting << paused;
            EXPECT_TRUE(dice_buttons_shown(false, setting, paused)) << setting << paused;
        }
    }
}

TEST(M49_DiceButtons, ShownIsExactlyNotPacedInEveryCombination) {
    int paced = 0;
    for (const bool bot : {false, true}) {
        for (const bool setting : {false, true}) {
            for (const bool paused : {false, true}) {
                EXPECT_NE(dice_buttons_shown(bot, setting, paused), bot_dice_paced(bot, setting, paused));
                if (bot_dice_paced(bot, setting, paused)) ++paced;
            }
        }
    }
    EXPECT_EQ(paced, 1);  // only the bot's card, setting on, not paused
}

TEST(M49_DiceButtons, PausingMidCardShowsTheButtonsAndResumingHidesThem) {
    WatchControls w;
    BotSpeed speed = BotSpeed::Normal;
    EXPECT_TRUE(w.card_paced(true, true));  // a bot's card rolling by itself
    w.set_paused(true);                     // the sidebar checkbox, mid-card
    EXPECT_FALSE(w.card_paced(true, true));
    EXPECT_TRUE(dice_buttons_shown(true, true, w.paused()));
    w.apply(WatchCommand::TogglePause, speed);  // key P resumes
    EXPECT_TRUE(w.card_paced(true, true));
    EXPECT_FALSE(dice_buttons_shown(true, true, w.paused()));
    EXPECT_FALSE(w.card_paced(false, true));  // a human's card is never paced
    EXPECT_EQ(w.card_paced(true, true), w.bot_dice_run(true));  // agrees with M48's bot_dice_run on a bot's card
}

TEST(M49_DiceButtons, AClickOnAPacedCardStillSkipsItsWait) {
    // With no button on a paced card the whole pop-up counts as PopUp (dice_popup.cpp), so a click there skips.
    EXPECT_TRUE(click_skips_bot_dice_wait(true, DiceClickTarget::PopUp));
    EXPECT_TRUE(click_skips_bot_dice_wait(true, DiceClickTarget::BareBoard));
    EXPECT_FALSE(click_skips_bot_dice_wait(true, DiceClickTarget::OtherWindow));
    EXPECT_FALSE(click_skips_bot_dice_wait(false, DiceClickTarget::PopUp));
}
