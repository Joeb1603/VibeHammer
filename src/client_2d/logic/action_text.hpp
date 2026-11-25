#pragma once

// Human-readable labels for actions, for the decision panel's buttons. Formatting only.

#include <string>

#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

[[nodiscard]] std::string action_label(const core::GameState& state, const core::GameAction& action);

// D-095 (M94): what a roll is called, as the dice pop-up's cards title it: "Hit rolls", "Charge roll", "Attack dice".
[[nodiscard]] std::string roll_label(core::RollKind roll);

}  // namespace wh40k::client
