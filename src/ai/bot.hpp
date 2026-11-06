#pragma once

// D-054: the bot opponent. A player, not the rules: it reads a const GameState and answers the pending
// decision with one GameAction, which the caller submits through GameSession like a human's input. Legality
// comes only from the engine (legal_actions, validate_action, move_end_problems).
//
// How a level plugs in: a policy is a function (state, side, legal actions, seed) -> PolicyResult. Bot::decide
// dispatches on the effective level, validates the policy's answer and falls back when it is not legal.
// A policy may return the rest of a multi-step plan (a unit's model moves and its commit): Bot memoizes it
// and replays a step only while state_hash matches what the plan expected, otherwise it asks the policy again.
// A plan is a pure function of the state when planning starts, and the policy must give the same answer from
// any state along the plan, so the memo is only a cache: undo, save/load and the arena reproduce decisions.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"
#include "fallback.hpp"
#include "settings.hpp"

namespace wh40k::ai {

// One source of truth for the levels: the client's (settings.hpp, D-055).
using BotLevel = client::BotLevel;

struct BotConfig {
    BotLevel level = BotLevel::Easy;
};

struct BotChoice {
    core::GameAction action;
    std::string reason;  // what the bot is doing and why, for the panel and (M45) the log
    // D-054: no legal answer was found, not even the fallback: the bot stops and `problems` says why.
    bool stopped = false;
    std::vector<std::string> problems;
};

// A step of a memoized plan: replayed only while the state's hash is `expect_hash`.
struct PlannedStep {
    std::uint64_t expect_hash = 0;
    core::GameAction action;
    std::string reason;
};

// What a level's policy answers: the action now and, optionally, the rest of its plan. A plan never runs past
// an action that rolls dice (Advance, Desperate Escape, DeclareCharge, Shoot, Fight; D-054).
struct PolicyResult {
    core::GameAction action;
    std::string reason;
    std::vector<PlannedStep> rest;
};

// The level actually played: Hard (M44) isn't built yet, so it plays as Normal (M41; M42-M43 extend it).
[[nodiscard]] BotLevel effective_level(BotLevel requested);

// The seed of the bot's own dice and choices for this state: a function of state_hash only (D-054).
[[nodiscard]] std::uint64_t decision_seed(const core::GameState& state);

class Bot {
public:
    explicit Bot(BotConfig config = {}) : config_(config) {}

    // One action for `side`'s pending decision. Precondition: state.pending->side == side (otherwise the
    // choice is `stopped`).
    [[nodiscard]] BotChoice decide(const core::GameState& state, core::Side side);

    // Forget the plan memo and the loop guard's history (an undo, a new game or a load).
    void forget() noexcept;

    [[nodiscard]] const BotConfig& config() const noexcept { return config_; }
    void set_config(BotConfig config) noexcept {
        config_ = config;
        forget();
    }

    // Decisions answered from the memo, and by the fallback (for tests and the arena).
    [[nodiscard]] std::size_t memo_hits() const noexcept { return memo_hits_; }
    [[nodiscard]] std::size_t fallbacks() const noexcept { return fallbacks_; }

private:
    BotConfig config_;
    std::vector<PlannedStep> memo_;  // the plan's remaining steps, next first
    LoopGuard guard_;
    std::size_t memo_hits_ = 0;
    std::size_t fallbacks_ = 0;
};

}  // namespace wh40k::ai
