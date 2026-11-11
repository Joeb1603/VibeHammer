#pragma once

// D-085 (M85): what level Easy answered before the bots became one is now the bot's defaults (normal/defaults.hpp),
// which the bot falls back to for any decision it has no answer of its own for. Tests that checked Easy's answer to a
// decision check the defaults' answer instead.

#include "bot.hpp"
#include "core/engine/engine.hpp"
#include "normal/defaults.hpp"

namespace wh40k::test {

inline ai::BotChoice default_choice(const core::GameState& gs, core::Side side) {
    const ai::PolicyResult r = ai::normal::default_answer(gs, side, core::legal_actions(gs, side), ai::decision_seed(gs));
    return ai::BotChoice{r.action, r.reason};
}

// The defaults as a player, for helpers written for a Bot (decide). No memo is needed: each answer is planned from the
// state, and a plan comes out the same from any state along it (D-054).
struct DefaultsPlayer {
    [[nodiscard]] ai::BotChoice decide(const core::GameState& gs, core::Side side) const { return default_choice(gs, side); }
};

}  // namespace wh40k::test
