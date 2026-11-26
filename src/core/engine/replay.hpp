#pragma once

// Replays (an initial GameState plus (side, action) steps reproduce the game exactly) and Match, the
// running event log (D-014).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/engine/actions.hpp"
#include "core/engine/events.hpp"
#include "core/engine/validation.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::core {

struct ReplayStep {
    Side side = Side::A;
    GameAction action;
    friend bool operator==(const ReplayStep&, const ReplayStep&) = default;
};

// Either a scenario file (repo-relative) and a dice seed, or a full GameState (D-014).
struct ReplayInitial {
    std::optional<std::string> scenario;
    std::uint64_t seed = 0;
    std::optional<GameState> state;
    friend bool operator==(const ReplayInitial&, const ReplayInitial&) = default;
};

// D-095 (M94): format 1 records only the players' choices, every roll made inside the action that led to it
// (apply_action); format 2 also records each roll as a RollDice step (apply_action_paced), as a GameSession plays it.
// D-096 (M95): format 3 also records each reaction window's answer (UseStratagem, DeclineStratagems), and a random D as
// one Damage roll per attack that inflicts damage (format 2 made a batch's in one).
// D-097 (M96): format 4 also records the answers to the windows that follow no roll (15.08 Fire Overwatch and 15.11
// Heroic Intervention at the end of a phase, 15.12 Counteroffensive after a fight), and what they lead to (a snap shot,
// a heroic charge). Formats 1 to 3 are converted.
inline constexpr int kReplayFormatRollsInline = 1;
inline constexpr int kReplayFormatRollsRecorded = 2;
inline constexpr int kReplayFormatWindowsRecorded = 3;
inline constexpr int kReplayFormatReactionsRecorded = 4;

struct Replay {
    int format = kReplayFormatRollsInline;
    std::string description;
    ReplayInitial initial;
    std::vector<ReplayStep> steps;
    nlohmann::json expect;  // replay_expectation() of the final state; null: not checked
    friend bool operator==(const Replay&, const Replay&) = default;
};

void to_json(nlohmann::json& j, const ReplayStep& v);
void from_json(const nlohmann::json& j, ReplayStep& v);
void to_json(nlohmann::json& j, const ReplayInitial& v);
void from_json(const nlohmann::json& j, ReplayInitial& v);
void to_json(nlohmann::json& j, const Replay& v);
void from_json(const nlohmann::json& j, Replay& v);

[[nodiscard]] Replay load_replay(const std::filesystem::path& path);
void save_replay(const Replay& replay, const std::filesystem::path& path);

// The replay's starting state (not yet started).
[[nodiscard]] GameState replay_initial_state(const Replay& replay, const std::filesystem::path& repo_root);

struct ReplayResult {
    GameState final_state;
    std::vector<GameEvent> events;
    std::optional<std::size_t> failed_step;  // the first step that did not validate
    std::vector<std::string> reasons;        // its validation reasons
    [[nodiscard]] bool ok() const noexcept { return !failed_step.has_value(); }
};

// Builds the initial state, starts the game if needed, then validates and applies every step: format 1 with every
// roll made and every window declined inside its step (start_game, apply_action); format 4 stopping at each roll and
// window for its recorded step (start_game_paced, apply_action_paced; D-095, D-096, D-097); format 3 as format 4 with
// D-097's windows declined; format 2 as format 4 with every window declined and a batch's further Damage rolls made
// where it recorded one (D-096: the same game).
[[nodiscard]] ReplayResult run_replay(const Replay& replay, const std::filesystem::path& repo_root);

// D-095 (M94), D-096 (M95), D-097 (M96): a format-1 step list from `initial` (started here if it hasn't started) as
// format 4: played through a paced Match, with each roll and window that the start and every step lead to recorded as
// its step (each roll made, each window declined). The game is the same (the closure). Throws DataError on a step that
// doesn't validate.
[[nodiscard]] std::vector<ReplayStep> with_roll_steps(GameState initial, const std::vector<ReplayStep>& steps);
// D-096: the same from a format-2 step list, step by step: each window waiting before a recorded step is declined,
// after a recorded Damage roll the batch's further Damage rolls are made (format 2 made them in one), and the windows
// waiting at the end are declined; a roll waiting at the end stays (a save can stop at one).
[[nodiscard]] std::vector<ReplayStep> with_roll_steps_from_format2(GameState initial,
                                                                   const std::vector<ReplayStep>& steps);
// D-097: the same from a format-3 step list: each of D-097's windows (EndOfPhase, AfterAttacks) waiting before a
// recorded step or at the end is declined; the recorded steps answer every roll and M95 window.
[[nodiscard]] std::vector<ReplayStep> with_roll_steps_from_format3(GameState initial,
                                                                   const std::vector<ReplayStep>& steps);
// The same for a replay: a format-4 replay is returned as it is; a format-1, 2 or 3 one comes back as format 4, its
// other fields unchanged (its expect block holds: the final state is the same).
[[nodiscard]] Replay with_roll_steps(const Replay& replay, const std::filesystem::path& repo_root);

// What a replay file's `expect` block records: result, stage, battle round, and every model's
// wounds and position.
[[nodiscard]] nlohmann::json replay_expectation(const GameState& state);

// D-095 (M94): how a Match applies its steps. Inline: every roll is made inside the step that leads to it
// (start_game, apply_action), so a RollDice is never pending and never recorded (a format-1 replay); D-096: every
// window is declined there too. Paced: the match stops at each roll and window, a player answers it with a step, which
// is recorded like any other (format 3).
enum class RollMode : std::uint8_t { Inline, Paced };

// A game in progress with its log: the initial state, every accepted step and every event.
class Match {
public:
    // Starts the game if `initial` has not started.
    explicit Match(GameState initial, RollMode mode = RollMode::Inline);

    // Validates and, if valid, applies and records the action. The state is unchanged if invalid.
    ValidationResult submit(Side side, const GameAction& action);

    [[nodiscard]] RollMode roll_mode() const noexcept { return mode_; }

    [[nodiscard]] const GameState& state() const noexcept { return state_; }
    [[nodiscard]] const GameState& initial_state() const noexcept { return initial_; }
    [[nodiscard]] const std::vector<ReplayStep>& steps() const noexcept { return steps_; }
    [[nodiscard]] const std::vector<GameEvent>& events() const noexcept { return events_; }

    // A replay of this match from its initial state, with the current state as its expectation: format 1 for an
    // Inline match, format 4 for a Paced one (D-095, D-096, D-097).
    [[nodiscard]] Replay to_replay(std::string description) const;

private:
    // M54: apply_action's rollback copy, reused from one submit to the next (copy assignment keeps its
    // allocations). Scratch only, not part of the match: a copied Match starts with an empty one.
    struct Scratch {
        GameState state;
        Scratch() = default;
        Scratch(const Scratch&) {}
        Scratch& operator=(const Scratch&) { return *this; }
        Scratch(Scratch&&) = default;
        Scratch& operator=(Scratch&&) = default;
        ~Scratch() = default;
    };

    GameState initial_;
    GameState state_;
    std::vector<ReplayStep> steps_;
    std::vector<GameEvent> events_;
    Scratch scratch_;
    RollMode mode_ = RollMode::Inline;
};

}  // namespace wh40k::core
