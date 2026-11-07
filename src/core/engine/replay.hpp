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

struct Replay {
    int format = 1;
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

// Builds the initial state, starts the game if needed, then validates and applies every step.
[[nodiscard]] ReplayResult run_replay(const Replay& replay, const std::filesystem::path& repo_root);

// What a replay file's `expect` block records: result, stage, battle round, and every model's
// wounds and position.
[[nodiscard]] nlohmann::json replay_expectation(const GameState& state);

// A game in progress with its log: the initial state, every accepted step and every event.
class Match {
public:
    // Starts the game if `initial` has not started.
    explicit Match(GameState initial);

    // Validates and, if valid, applies and records the action. The state is unchanged if invalid.
    ValidationResult submit(Side side, const GameAction& action);

    [[nodiscard]] const GameState& state() const noexcept { return state_; }
    [[nodiscard]] const GameState& initial_state() const noexcept { return initial_; }
    [[nodiscard]] const std::vector<ReplayStep>& steps() const noexcept { return steps_; }
    [[nodiscard]] const std::vector<GameEvent>& events() const noexcept { return events_; }

    // A replay of this match from its initial state, with the current state as its expectation.
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
};

}  // namespace wh40k::core
