#pragma once

// M95 (D-096): the bot as it was before Command Re-roll went on by default: it plays no stratagem (its session declines
// every window for it). The bot-game replays written before M95 and the per-game pins of older bots are played with it,
// so they stay as they were (as M91 pinned its keys at 0).

#include "bot.hpp"

namespace wh40k::test {

inline wh40k::ai::BotConfig without_stratagems(wh40k::ai::BotConfig c = {}) {
    c.move.command_reroll = 0.0;
    c.move.insane_bravery = 0.0;
    return c;
}

}  // namespace wh40k::test
