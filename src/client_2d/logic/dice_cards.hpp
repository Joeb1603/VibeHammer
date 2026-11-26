#pragma once

// Dice roll pop-ups (M20, D-033): the rolls in the game log, one card per roll event, with what the roll
// is for, what it needs and each die's result, built from the events of rolls already made. D-095 (M94): roll_prompt
// is the card of a roll still waiting to be made (a RollDice decision), its dice face down. No rendering.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "core/state/game_state.hpp"
#include "rules_help.hpp"
#include "session.hpp"

namespace wh40k::client {

struct DieFace {
    int value = 0;          // 1-6; 0 for an automatic hit ([TORRENT], no roll is made)
    bool pass = false;      // the die succeeded (hit, wound, save, ...)
    bool critical = false;  // a critical hit or wound (unmodified 6)
    bool neutral = false;   // no pass or fail on its own (advance and charge rolls)
    bool rerolled = false;  // D-096 (M95): the die Command Re-roll replaced (15.02)
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
    // D-037: what the attacker's weapon abilities did to these attacks (hit cards only), e.g.
    // "10 attack dice from 5 models", "Rapid Fire 1: +5 attacks (target within half range, 12\")".
    std::vector<EffectLine> notes{};
    // D-096 (M95): the roll the card shows, as core names it (the kind, and the unit the roll is for), so the window
    // waiting just after a roll finds its card. nullopt on a card with no roll of its own.
    std::optional<core::RollKind> roll{};
    core::UnitId unit{};
};

// D-053 (M37): whether the card has dice to roll. A card with none ("No hits to roll for") is shown with its
// result at once and an OK button, without a Roll click.
[[nodiscard]] inline bool needs_roll(const DiceCard& card) { return !card.dice.empty(); }

// The cards for the roll events in log entries [from, log.size()), in order. Earlier entries give context
// (which batch and weapon is attacking, the target's allocation groups). `state` is the state after the
// last entry, for unit names and sides.
[[nodiscard]] std::vector<DiceCard> dice_cards(const core::GameState& state, const std::vector<LogEntry>& log,
                                               std::size_t from);

// D-095 (M94): the card for the pending RollDice before it is made: who rolls, what for and what it needs, with its
// dice face down (DieFace::value 0; core's roll_dice_info gives how many). The log gives the attack's context, as for
// dice_cards. nullopt unless a RollDice is pending.
[[nodiscard]] std::optional<DiceCard> roll_prompt(const core::GameState& state, const std::vector<LogEntry>& log);

// D-096 (M95): the card of the roll a BeforeRoll window comes before (Insane Bravery's battle-shock roll, 15.04), face
// down as roll_prompt draws it once the roll waits (core's window_info gives the roll). nullopt unless such a window is
// pending.
[[nodiscard]] std::optional<DiceCard> window_prompt(const core::GameState& state, const std::vector<LogEntry>& log);

// D-096: the card of the roll the pending AfterRoll window comes after, from core's judgement of its dice now (the
// window's pop-up, when that roll's card isn't showing). nullopt unless such a window is pending.
[[nodiscard]] std::optional<DiceCard> window_card(const core::GameState& state, const std::vector<LogEntry>& log);

// D-096: whether `card` is the card of the roll the pending AfterRoll window comes after (the same roll, unit and
// roller), so the window's buttons go on it.
[[nodiscard]] bool is_window_card(const core::GameState& state, const DiceCard& card);

}  // namespace wh40k::client
