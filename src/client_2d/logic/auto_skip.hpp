#pragma once

// The "Skip steps with nothing to do" setting (D-036, M23): when the only legal action is to end a step or
// pass, the client submits it itself. Client only: every skip still goes through GameSession::submit.

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "core/engine/actions.hpp"
#include "session.hpp"

namespace wh40k::client {

// End shooting / charging / pile-ins / consolidations, pass in the fight sequence, skip an overrun pile-in.
[[nodiscard]] bool is_skip(const core::GameAction& action);

// The action, if `legal` is exactly one action and it is a skip (nothing else can be done).
[[nodiscard]] std::optional<core::GameAction> lone_skip(const std::vector<core::GameAction>& legal);

// Submits lone skips, one after another, while the session's decision has nothing else to do. `submit`
// applies one (true if accepted); the default is session.submit. Returns the skips that were applied.
// `limit` only guards against a bug.
std::vector<core::GameAction> run_auto_skips(GameSession& session,
                                             const std::function<bool(const core::GameAction&)>& submit = {},
                                             std::size_t limit = 64);

// Undo with the setting on: `auto_steps` holds the history indices of the steps skipped automatically
// (ascending). Removes the trailing skipped steps, then the player's action before them, so one Undo goes
// back to before the player's last choice. False if there was nothing to undo.
bool undo_with_auto_skips(GameSession& session, std::vector<std::size_t>& auto_steps);

}  // namespace wh40k::client
