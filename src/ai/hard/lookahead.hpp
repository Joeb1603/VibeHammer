#pragma once

// M44 (D-054): the Hard level's lookahead. An engine-backed Monte Carlo of the rest of our turn and the enemy's
// reply, from a position a plan would leave (e.g. just after a move), valued at the start of our next turn with
// the evaluator (evaluator.hpp), or exactly when the game ends first.
//
// - Every rollout is a Simulator game (D-054: the only ai file that applies actions is simulator.cpp) with the
//   bot's own dice, seeded from `seed` (rollout_seed: a dice-free hash of the position) and the rollout's index,
//   never from state.dice. The same seeds serve every plan compared, so they meet the same luck where they can.
// - The rollout policy is cheap: the chain's own choices for the move (a Normal move, Advance or Fall Back by the
//   distance the chain likes best, made as a straight translation along the line between the units and checked
//   against the battlefield edges, enemy bases and engagement range before it is proposed) and for declaring a
//   charge (the chain's fight against holding); a declared charge is the client's Auto move for charges (D-056)
//   simulated in full (Normal's plan_charge_move); every other decision is the baseline's (level Easy).
// - The budget is fixed in simulated actions, never time: at most LookaheadWeights::rollout_actions per rollout and
//   budget_actions for the whole Monte Carlo. A rollout cut short is valued where it stopped. D-078 (M69): with more
//   than full_budget_units units with models when a Monte Carlo starts, its rollouts and action budget scale down
//   (rollout_budget), so a decision's time stays bounded on a 14-unit game.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "analysis/odds.hpp"
#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"
#include "generation.hpp"
#include "hard/evaluator.hpp"
#include "hard/weights.hpp"

namespace wh40k::ai::hard {

// What a rollout does first, before the rollout policy takes over.
struct Opening {
    std::vector<core::GameAction> actions;  // from the start state, in order
    // [f]: the moves after the opening's StartMove{Advance} rolled f (the Advance option before its roll; empty:
    // the rollout policy moves the unit).
    std::array<std::vector<core::GameAction>, 7> after_advance_roll;
};

struct MonteCarloResult {
    double mean = 0.0;    // mean Evaluation::score for `side` where the rollouts ended
    double mean_v = 0.0;  // mean V
    std::size_t rollouts = 0;
    std::size_t actions = 0;  // simulated actions, all rollouts
    std::size_t cut = 0;      // rollouts stopped by an action cap (or a refused action) before their end
    std::size_t game_over = 0;  // rollouts in which the game ended
};

// D-078 (M69): the budget of a Monte Carlo started from `start`, by the units with models in it (both sides): as
// `weights` for at most full_budget_units of them, else the rollouts and the action budget scaled by
// full_budget_units / units (rounded down; at least 3 rollouts, or the weights' own if fewer). The cap per rollout is
// unchanged. Counted in simulated actions and rollouts, never time (D-054).
struct RolloutBudget {
    std::size_t rollouts = 0;
    std::size_t rollout_actions = 0;
    std::size_t budget_actions = 0;
};
[[nodiscard]] RolloutBudget rollout_budget(const LookaheadWeights& weights, const core::GameState& start);

// D-078 (tests): the rollout policy's straight translation of the move in progress in `state` (no model moved yet)
// along (dx, dy), up to `mm` (or the closest fixed direction that stands, as far as it goes): its proposals and commit,
// or nullopt if none moves at least a quarter inch.
[[nodiscard]] std::optional<std::vector<core::GameAction>> rollout_translation(const Evaluator& eval,
                                                                             const core::GameState& state, float dx,
                                                                             float dy, float mm);

// A dice-free hash of the position for the lookahead's seeds: the battle round and turn, every model's id, its
// position (for a move in progress, where it started) and its wounds. The same for a MoveUnits decision and for
// the state just after the move was started, so every option of one decision meets the same rollout dice.
[[nodiscard]] std::uint64_t rollout_seed(const core::GameState& state);

// Diagnostics (tests, probes): each rollout's actions in order and the state it ended in.
struct RolloutTrace {
    std::vector<core::GameAction> actions;
    core::GameState end;
};

// rollout_budget(weights, start).rollouts rollouts from `start` (a state in `side`'s turn), each to the start of
// `side`'s next turn.
// Deterministic: a pure function of its arguments. `traces`, if given, gets every rollout (diagnostics only).
// D-083 part 3 (M81): `generation`, Hard's (HardContext::generation): the rollouts' shooting is Normal's at it. D-083 part 4
// (M82): that shooting reads the evaluator's army plans (the decision's, both sides': Evaluator::plans) instead of working
// one out at every Shoot decision.
[[nodiscard]] MonteCarloResult monte_carlo(const Evaluator& eval, analysis::OddsCache& odds,
                                           const core::GameState& start, core::Side side, std::uint64_t seed,
                                           const LookaheadWeights& weights, const Opening& opening = {},
                                           std::vector<RolloutTrace>* traces = nullptr,
                                           Generation generation = Generation::Current);

}  // namespace wh40k::ai::hard
