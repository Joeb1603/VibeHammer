#pragma once

// Human-readable labels for actions, for the decision panel's buttons. Formatting only.

#include <string>

#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

[[nodiscard]] std::string action_label(const core::GameState& state, const core::GameAction& action);

}  // namespace wh40k::client
