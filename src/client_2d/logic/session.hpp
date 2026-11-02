#pragma once

// GameSession: a live game driven through the engine API only. Every change goes through submit(), which
// validates the action (validate_action) before applying it (apply_action). The client never mutates a
// GameState itself (CLAUDE.md, D-024).

#include <optional>
#include <string>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/events.hpp"
#include "core/engine/replay.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

// One applied action (or the game start) and the events it produced.
struct LogEntry {
    std::string label;  // e.g. "Step 4: Side A EndShootStep"
    std::vector<core::GameEvent> events;
};

class GameSession {
public:
    // Starts the game (start_game) if it has not started.
    explicit GameSession(core::GameState initial);

    [[nodiscard]] const core::GameState& state() const noexcept { return state_; }
    [[nodiscard]] const core::GameState& initial() const noexcept { return initial_; }
    [[nodiscard]] const std::optional<core::PendingDecision>& pending() const noexcept { return state_.pending; }
    // legal_actions for the side whose decision is pending (empty when nothing is pending).
    [[nodiscard]] std::vector<core::GameAction> legal() const;
    // The side that must act now (the pending decision's side, or the active side).
    [[nodiscard]] core::Side deciding_side() const;

    // validate_action for the deciding side, without applying it.
    [[nodiscard]] core::ValidationResult check(const core::GameAction& action) const;
    // Validates, then applies. On a failure nothing changes and the rule-cited reasons are returned.
    core::ValidationResult submit(const core::GameAction& action);

    [[nodiscard]] const std::vector<LogEntry>& log() const noexcept { return log_; }
    [[nodiscard]] const std::vector<core::ReplayStep>& history() const noexcept { return history_; }

private:
    core::GameState initial_;
    core::GameState state_;
    std::vector<LogEntry> log_;
    std::vector<core::ReplayStep> history_;
};

}  // namespace wh40k::client
