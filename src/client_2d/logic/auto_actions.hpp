#pragma once

// Decisions the client answers by itself (client only: every action still goes through
// GameSession::submit, so it is validated and recorded like any other step):
// - "Skip steps with nothing to do" (D-036, M23): the only legal action is to end a step or pass, or
//   (D-047, M31) every pile-in or consolidation left is too small to matter (fight_move_skip).
// - "Allocate damage automatically" (D-038, M25): the defender's allocation order (05.03) and casualty
//   order (05.04) take their defaults, unless a bot plays the defender (D-054: the bot answers its own). The
//   default casualty order is D-058's (M47): furthest from the action first, keeping coherency.

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"
#include "session.hpp"

namespace wh40k::client {

struct AutoSettings {
    bool skip_empty_steps = true;  // D-036, D-047
    bool auto_allocate = false;    // D-038; off by default since D-051 (casualties picked on the board)
    // D-054 (M39): the sides a bot plays, indexed by side_index. auto_allocate never answers their decisions;
    // the forced skips (a lone skip, D-047) still apply to them, as to a human.
    std::array<bool, 2> bot_controlled{};
};

// End shooting / charging / pile-ins / consolidations, pass in the fight sequence, skip an overrun pile-in.
[[nodiscard]] bool is_skip(const core::GameAction& action);

// The action, if `legal` is exactly one action and it is a skip (nothing else can be done).
[[nodiscard]] std::optional<core::GameAction> lone_skip(const std::vector<core::GameAction>& legal);

// The action the client takes by itself for this decision under `settings`, if any: a lone skip, the
// first (default) allocation order, or the default casualty order (CasualtyPlan's, D-058). Under
// skip_empty_steps, also the step's skip when every pile-in or consolidation left is pointless (D-047).
[[nodiscard]] std::optional<core::GameAction> next_auto_action(const core::GameState& state,
                                                               const std::vector<core::GameAction>& legal,
                                                               AutoSettings settings);

// Submits next_auto_action, one after another, while there is one. `submit` applies one (true if
// accepted); the default is session.submit. Returns the actions applied. `limit` only guards against a bug.
std::vector<core::GameAction> run_auto_actions(GameSession& session, AutoSettings settings,
                                               const std::function<bool(const core::GameAction&)>& submit = {},
                                               std::size_t limit = 256);

// D-038: the units that still have a valid shot (the engine offers a Shoot template only for a unit with
// at least one, D-016), in legal-action order, each once.
[[nodiscard]] std::vector<core::UnitId> units_that_can_still_shoot(const std::vector<core::GameAction>& legal);

}  // namespace wh40k::client
