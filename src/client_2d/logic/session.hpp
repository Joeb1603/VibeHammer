#pragma once

// GameSession: a live game driven through the engine API only. Every change goes through submit(), which
// validates the action (validate_action) before applying it (apply_action). The client never mutates a
// GameState itself (CLAUDE.md, D-024).

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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

    // A new game from a scenario's state, with the dice seed overridden if given (M13).
    [[nodiscard]] static GameSession new_game(core::GameState scenario, std::optional<std::uint64_t> seed);
    // Rebuilds a game by submitting `steps` from `initial`. Throws core::DataError if a step is invalid.
    [[nodiscard]] static GameSession from_steps(core::GameState initial, const std::vector<core::ReplayStep>& steps);

    // ---- save / load (D-025) ----
    // {"format": 1, "kind": "vibehammer-save", "initial", "steps", "state"}: the initial state and the
    // accepted steps (so undo and replay export keep working), plus the current state as a check.
    [[nodiscard]] nlohmann::json to_save_json() const;
    // Rebuilds the game from "initial" + "steps" and checks it matches "state" (core::DataError if not).
    // A bare GameState JSON loads as a game with no history.
    [[nodiscard]] static GameSession from_save_json(const nlohmann::json& j);
    void save(const std::filesystem::path& path) const;
    [[nodiscard]] static GameSession load(const std::filesystem::path& path);

    // The game so far as a replay (initial state form, D-014), with the current state as its expectation.
    [[nodiscard]] core::Replay to_replay(std::string description) const;

    // Undo (hotseat): rebuild from the initial state with every step but the last. False if there is none.
    [[nodiscard]] bool can_undo() const noexcept { return !history_.empty(); }
    bool undo();

    [[nodiscard]] std::uint64_t seed() const noexcept { return initial_.dice.seed; }

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
