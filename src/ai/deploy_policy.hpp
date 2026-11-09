#pragma once

// D-081 (M74): how a bot sets its units up in the deployment step (DeployUnits). In M74 every level answers the same
// way, before its own policy runs (so Hard never builds chain tables from a half-deployed army): the lowest-id unit
// waiting to be set up, at the engine's proposal (Auto-place's spot, core::propose_set_up), or CannotSetUp when the
// engine finds no legal spot for it. A hand-deployed bot army so ends exactly where Auto-place puts it. M77 adds the
// Normal and Hard heuristic.

#include <optional>
#include <vector>

#include "bot.hpp"
#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai {

// The answer to `side`'s pending DeployUnits, taken from `legal` (legal_actions): nullopt for any other decision, or if
// `legal` has no fully specified answer for the side's lowest-id unit waiting to be set up.
[[nodiscard]] std::optional<PolicyResult> deploy_policy(const core::GameState& state, core::Side side,
                                                        const std::vector<core::GameAction>& legal);

}  // namespace wh40k::ai
