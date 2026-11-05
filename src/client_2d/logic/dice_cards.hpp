#pragma once

// Dice roll pop-ups (M20, D-033): the rolls in the game log, one card per roll event, with what the roll
// is for, what it needs and each die's result. Presentation only: the engine has already made every roll
// when the action was applied, and the cards are built from its events. No rendering.

#include <cstddef>
#include <string>
#include <vector>

#include "core/state/game_state.hpp"
#include "session.hpp"

namespace wh40k::client {

struct DieFace {
    int value = 0;          // 1-6; 0 for an automatic hit ([TORRENT], no roll is made)
    bool pass = false;      // the die succeeded (hit, wound, save, ...)
    bool critical = false;  // a critical hit or wound (unmodified 6)
    bool neutral = false;   // no pass or fail on its own (advance and charge rolls)
    friend bool operator==(const DieFace&, const DieFace&) = default;
};

struct DiceCard {
    std::string title;    // "Hit rolls"
    std::string purpose;  // "Intercessor Squad attacking Termagants (Bolt Rifle - Focused Fire)"
    std::string need;     // "Need 3+ to hit (an unmodified 6 is a critical hit)"
    std::string result;   // "3 hits (1 critical)"
    std::string rule;     // "05.01"
    core::Side roller = core::Side::A;  // the side making the roll
    std::vector<DieFace> dice;
};

// The cards for the roll events in log entries [from, log.size()), in order. Earlier entries give context
// (which batch and weapon is attacking, the target's allocation groups). `state` is the state after the
// last entry, for unit names and sides.
[[nodiscard]] std::vector<DiceCard> dice_cards(const core::GameState& state, const std::vector<LogEntry>& log,
                                               std::size_t from);

}  // namespace wh40k::client
