#pragma once

// D-054: the bot opponent. A player, not the rules: it reads a const GameState and answers the pending
// decision with one GameAction, which the caller submits through GameSession like a human's input. Legality
// comes only from the engine (legal_actions, validate_action, move_end_problems).
//
// D-085 (M85): one bot (the levels Easy and Hard and the hidden regression levels are gone; tag bots-three-levels).
// Its policy is a function (state, side, legal actions, seed) -> PolicyResult (normal/normal_bot.hpp). Bot::decide
// answers a deployment first (deploy_policy.hpp), validates the policy's answer and falls back when it is not legal.
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
#include "core/state/stratagems.hpp"
#include "fallback.hpp"
#include "normal/move_score.hpp"

namespace wh40k::ai {

struct BotConfig {
    // D-085 (M85): the move score's weights (the tuned defaults; the arena's --weights overrides them for tuning).
    normal::MoveWeights move{};
    // D-097 (M96): whether the bot plays `s` (its key on).
    [[nodiscard]] bool plays(core::Stratagem s) const noexcept {
        switch (s) {
            case core::Stratagem::CommandReroll: return move.command_reroll > 0.0;
            case core::Stratagem::InsaneBravery: return move.insane_bravery > 0.0;
            case core::Stratagem::FireOverwatch: return move.fire_overwatch > 0.0;
            case core::Stratagem::HeroicIntervention: return move.heroic_intervention > 0.0;
            case core::Stratagem::Counteroffensive: return move.counteroffensive > 0.0;
            case core::Stratagem::Explosives: return move.explosives > 0.0;           // M97 (D-099)
            case core::Stratagem::CrushingImpact: return move.crushing_impact > 0.0;  // M97 (D-099)
        }
        return false;
    }
    // D-096 (M95): whether the bot answers reaction windows (a stratagem key on). If not, its session declines every
    // window for it (GameSession::auto_decline), and the bot never sees one.
    [[nodiscard]] bool plays_stratagems() const noexcept {
        for (const core::Stratagem s : core::kAllStratagems) {
            if (plays(s)) return true;
        }
        return false;
    }
    // D-097 (M96): the stratagems it doesn't play: its session declines a window that offers it only these
    // (GameSession::set_declined_stratagems), so the bot sees only the windows it may use a stratagem in.
    [[nodiscard]] std::vector<core::Stratagem> declined_stratagems() const {
        std::vector<core::Stratagem> out;
        for (const core::Stratagem s : core::kAllStratagems) {
            if (!plays(s)) out.push_back(s);
        }
        return out;
    }
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

// What the policy answers: the action now and, optionally, the rest of its plan. A plan never runs past
// an action that rolls dice (Advance, Desperate Escape, DeclareCharge, Shoot, Fight; D-054).
struct PolicyResult {
    core::GameAction action;
    std::string reason;
    std::vector<PlannedStep> rest;
};

// The seed of the bot's own dice and choices for this state: a function of state_hash only (D-054).
[[nodiscard]] std::uint64_t decision_seed(const core::GameState& state);

class Bot {
public:
    explicit Bot(BotConfig config = {});

    // One action for `side`'s pending decision. Precondition: state.pending->side == side (otherwise the
    // choice is `stopped`).
    [[nodiscard]] BotChoice decide(const core::GameState& state, core::Side side);

    // Forget the plan memo and the loop guard's history (an undo, a new game or a load).
    void forget() noexcept;

    [[nodiscard]] const BotConfig& config() const noexcept { return config_; }
    void set_config(BotConfig config);

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
