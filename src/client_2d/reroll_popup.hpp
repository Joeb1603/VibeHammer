#pragma once

// M61 (D-072): the [TWIN-LINKED] re-roll pop-up (24.38). Over the board while the attacker's player chooses which wound
// rolls to re-roll: the batch's wound dice, failed ones outlined in red; a click picks a die (any die, a success too)
// or drops it again. "Re-roll selected", "Re-roll all failed" and "Keep" answer the decision. Display and input only:
// the choice lives in RerollPicks and the answer is submitted through GameSession like any other.

#include <raylib.h>

#include <optional>

#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"
#include "reroll_picks.hpp"

namespace wh40k::client {

// Draws the pop-up centred in `area` (screen pixels). Returns the answer when a button is pressed.
[[nodiscard]] std::optional<core::GameAction> draw_reroll_popup(Rectangle area, float ui_scale,
                                                                const core::GameState& state, RerollPicks& picks);

}  // namespace wh40k::client
