// M51 (D-062): no "Bot rolls its own dice automatically" setting. Pause alone decides: unpaused, a bot's dice
// cards roll and continue by themselves; paused, they wait for Roll / Continue like a human's.

#include <gtest/gtest.h>

#include "settings.hpp"
#include "watch_controls.hpp"

using namespace wh40k::client;
using nlohmann::json;

TEST(M51_BotDice, ABotsCardIsPacedExactlyWhenTheBotsArentPaused) {
    WatchControls w;
    EXPECT_TRUE(w.bot_dice_run());
    EXPECT_TRUE(w.card_paced(true));
    EXPECT_FALSE(w.card_paced(false));
    w.set_paused(true);
    EXPECT_FALSE(w.bot_dice_run());
    EXPECT_FALSE(w.card_paced(true));
    EXPECT_FALSE(w.card_paced(false));
    w.reset();  // a new game starts unpaused: the bot rolls its own dice again
    EXPECT_TRUE(w.card_paced(true));
}

TEST(M51_BotDice, AnOldSettingsFileWithTheRemovedKeyStillLoads) {
    AppSettings expected;
    expected.bot_level = BotLevel::Easy;
    expected.show_dice = false;
    json old = settings_to_json(expected);
    old["bot_auto_dice"] = false;  // written by M38-M50
    EXPECT_EQ(settings_from_json(old), expected);
    EXPECT_FALSE(settings_to_json(settings_from_json(old)).contains("bot_auto_dice"));
}
