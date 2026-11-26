#pragma once

// GameSession: a live game driven through the engine API only. Every change goes through submit(), which
// hands the action to core::Match (validate_action, then apply_action_paced). The client never mutates a
// GameState itself (CLAUDE.md, D-024). On top of Match it keeps a per-step log, save/load, undo and the
// seed.
// D-095 (M94): the session is paced: the engine stops at every dice roll (a RollDice decision) and the roll is a step
// of its own. A side with auto_roll on (a bot's, or a human's with the dice pop-ups off) has its rolls made at once,
// after the action that led to them; any other side makes each one by submitting its RollDice (the Roll button).
// D-096 (M95): the engine also stops at every reaction window (UseStratagems). A side with auto_decline on (the default:
// a bot that plays no stratagems, or a human with "Offer stratagems" off) declines each at once, a step of its own.

#include <array>
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
    // Starts the game (start_game_paced) if it has not started, then makes the pending rolls of the sides with
    // `auto_roll` on (D-095).
    explicit GameSession(core::GameState initial, std::array<bool, 2> auto_roll = {true, true},
                         std::array<bool, 2> auto_decline = {true, true});

    // A new game from a scenario's state, with the dice seed overridden if given (M13).
    [[nodiscard]] static GameSession new_game(core::GameState scenario, std::optional<std::uint64_t> seed,
                                              std::array<bool, 2> auto_roll = {true, true},
                                              std::array<bool, 2> auto_decline = {true, true});
    // Rebuilds a game by submitting `steps` from `initial` (the rolls are steps of their own, D-095). No roll is made
    // automatically, so the game stops where the steps stop. Throws core::DataError if a step is invalid.
    [[nodiscard]] static GameSession from_steps(core::GameState initial, const std::vector<core::ReplayStep>& steps);

    // ---- save / load (D-025) ----
    // {"format": 2, "kind": "vibehammer-save", "initial", "steps", "state"}: the initial state and the
    // accepted steps (so undo and replay export keep working), plus the current state as a check. D-095 (M94): format 2
    // records every roll as a step; a format-1 save (every roll inside its action) loads with its rolls added as steps.
    [[nodiscard]] nlohmann::json to_save_json() const;
    // Rebuilds the game from "initial" + "steps" and checks it matches "state" (core::DataError if not). No roll is
    // made automatically (a game saved while a player's roll waited still waits; roll_automatically makes it).
    // A bare GameState JSON loads as a game with no history.
    [[nodiscard]] static GameSession from_save_json(const nlohmann::json& j);
    void save(const std::filesystem::path& path) const;
    [[nodiscard]] static GameSession load(const std::filesystem::path& path);

    // The game so far as a replay (initial state form, D-014), with the current state as its expectation.
    [[nodiscard]] core::Replay to_replay(std::string description) const;

    // Undo (hotseat): rebuild from the initial state without the last step. D-095: a roll is never taken back on its
    // own: the last step that isn't a roll goes, with every roll after it. D-096: nor is a declined window (a decline
    // goes with the input that led to it; a stratagem used is an input of its own). False if there is no step.
    [[nodiscard]] bool can_undo() const noexcept { return !history().empty(); }
    bool undo();
    // The game rebuilt from its first `n` steps (one rebuild; nothing if n >= the number of steps). D-095: when the
    // first step taken back is a roll, its action and the rolls between go too (back to before the last step that
    // isn't a roll). Afterwards the pending rolls of the auto_roll sides are made.
    void truncate(std::size_t n);

    // ---- D-095 (M94): automatic rolls ----
    // Whether `side`'s rolls are made at once (a bot's, or a human's with the dice pop-ups off). Kept by undo.
    void set_auto_roll(core::Side side, bool on) noexcept { auto_roll_[core::side_index(side)] = on; }
    [[nodiscard]] bool auto_roll(core::Side side) const noexcept { return auto_roll_[core::side_index(side)]; }
    [[nodiscard]] const std::array<bool, 2>& auto_roll() const noexcept { return auto_roll_; }
    // Makes every pending roll of a side with auto_roll on, and declines every window of a side with auto_decline on
    // (D-096), each its own step and log entry, until the decision is another kind or another side's. Returns the
    // answers made. submit and the constructor call it.
    std::vector<core::GameAction> roll_automatically();

    // ---- D-096 (M95): automatic declines ----
    // Whether `side`'s reaction windows are declined at once (a bot that plays no stratagems, or a human with "Offer
    // stratagems" off). Kept by undo.
    void set_auto_decline(core::Side side, bool on) noexcept { auto_decline_[core::side_index(side)] = on; }
    [[nodiscard]] bool auto_decline(core::Side side) const noexcept { return auto_decline_[core::side_index(side)]; }

    [[nodiscard]] std::uint64_t seed() const noexcept { return match_.initial_state().dice.seed; }

    [[nodiscard]] const core::GameState& state() const noexcept { return match_.state(); }
    [[nodiscard]] const core::GameState& initial() const noexcept { return match_.initial_state(); }
    [[nodiscard]] const std::optional<core::PendingDecision>& pending() const noexcept { return state().pending; }
    // legal_actions for the side whose decision is pending (empty when nothing is pending).
    [[nodiscard]] std::vector<core::GameAction> legal() const;
    // The side that must act now (the pending decision's side, or the active side).
    [[nodiscard]] core::Side deciding_side() const;

    // validate_action for the deciding side, without applying it.
    [[nodiscard]] core::ValidationResult check(const core::GameAction& action) const;
    // Validates, then applies, then makes the pending rolls of the auto_roll sides (D-095). On a failure nothing
    // changes and the rule-cited reasons are returned. An engine exception (a programmer error) is also returned as a
    // failure, with the state unchanged.
    core::ValidationResult submit(const core::GameAction& action);

    [[nodiscard]] const std::vector<LogEntry>& log() const noexcept { return log_; }
    [[nodiscard]] const std::vector<core::ReplayStep>& history() const noexcept { return match_.steps(); }

private:
    struct NoAutoRoll {};
    GameSession(core::GameState initial, NoAutoRoll);
    // Validates and applies one action (no automatic roll after it); its log entry.
    core::ValidationResult submit_one(const core::GameAction& action);

    core::Match match_;
    std::vector<LogEntry> log_;
    std::array<bool, 2> auto_roll_{true, true};
    std::array<bool, 2> auto_decline_{true, true};  // D-096
};

}  // namespace wh40k::client
