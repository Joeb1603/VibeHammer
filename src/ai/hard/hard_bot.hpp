#pragma once

// Level Hard (M44, D-054): Normal's modules (M41-M43) with the game-aware evaluator (evaluator.hpp) and the
// lookahead (lookahead.hpp) on top. Hard answers these decisions itself, each by the evaluator's value of the
// position every option leaves (V = P(win) - P(loss), plus the tempo and material terms):
// - MoveUnits and the moves it starts (09.02-09.07): Remain Stationary, a Normal move, an Advance (before its roll:
//   the mean over the six results of each result's best move) and a Fall Back. A move's plan: Normal's candidates
//   (M43) ranked by M43's cheap score, the best few that stand when simulated valued with the chain, the best
//   LookaheadWeights::plans of those checked by the Monte Carlo, the best blend chosen. The losing side plays for
//   the draw (it keeps out of charge range and kites when that keeps it alive), the winning side for the wipe-out.
// - The charge (11.02, 11.04): declare when the chain prefers the fight (it strikes first) to holding; after the
//   roll, the planned charge move (D-056) if the position after it is worth more than not moving.
// - The casualty order (05.04, 06.02): Normal's choice, the front models first, the rear models first, or (M47)
//   the default order (D-058), whichever leaves the position worth most (the engine's preview_casualties says who
//   dies; coherency losses at the end of the turn are counted, 03.03); Normal's is kept unless beaten by margin.
// - The model removed to regain coherency (03.03), and the consolidation (12.07, 12.08), by the same measure.
// Every other decision is Normal's. Hard needs a duel (one unit per side, as in every V1 scenario); otherwise it
// plays as Normal. Like every level it is a pure function of the state (D-054): the chain tables it keeps are a
// cache of a pure function, and every random number is the bot's own, seeded from the dice-free state hash.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "analysis/odds.hpp"
#include "bot.hpp"
#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"
#include "hard/evaluator.hpp"
#include "hard/lookahead.hpp"
#include "hard/weights.hpp"
#include "normal/movement.hpp"

namespace wh40k::ai::hard {

// What Hard keeps between decisions: its weights and a cache of chain tables (never behaviour, D-054).
struct HardContext {
    BotWeights weights{};
    ChainCache chains;
};

[[nodiscard]] PolicyResult hard_policy(const core::GameState& state, core::Side side,
                                       const std::vector<core::GameAction>& legal, std::uint64_t seed,
                                       HardContext& context);

// The plan of the move just started in `origin` (MoveModels pending, no model moved yet). A pure function of
// `origin` and the weights (D-054), so a replan from any state along the move gives the same steps.
struct HardMovePlan {
    normal::PlannedMove move;
    Evaluation chain;                    // the evaluator at the position after the move
    std::optional<MonteCarloResult> mc;  // the lookahead, for the plans it checked
    double value = 0.0;                  // (1 - share) x chain.score + share x mc->mean (chain.score without it)
    std::size_t standing = 0;            // plans that stood and were valued
    bool normal_plan = false;            // it is Normal's own plan (normal::plan_unit_move), kept by the margin
};
// `normal_default`: Normal's own plan (normal::plan_unit_move) is the default, kept unless another is worth more
// than BotWeights::margin more.
[[nodiscard]] std::optional<HardMovePlan> hard_plan_unit_move(HardContext& context, analysis::OddsCache& cache,
                                                              const core::GameState& origin, bool lookahead,
                                                              bool normal_default = true);

// The value of each move type at a MoveUnits decision (tests, diagnostics), as hard_policy weighs them.
struct HardTypeValues {
    std::optional<double> remain_stationary;
    std::optional<double> normal;
    std::optional<double> advance;
    std::optional<double> fall_back;
};
[[nodiscard]] std::optional<PolicyResult> hard_move_units(HardContext& context, analysis::OddsCache& cache,
                                                          const core::GameState& state, core::Side side,
                                                          const std::vector<core::GameAction>& legal,
                                                          std::uint64_t seed, HardTypeValues* values = nullptr);

// The candidates of the pending casualty decision (tests, diagnostics, M47), as hard_policy weighs them: Normal's
// order, the front models first, the rear models first and the default order (D-058), in that order; `value` is
// nullopt for a candidate the preview refused.
struct HardCasualtyCandidate {
    std::vector<core::ModelId> priority;
    std::string text;
    std::optional<double> value;
};
[[nodiscard]] std::optional<PolicyResult> hard_casualties(HardContext& context, analysis::OddsCache& cache,
                                                          const core::GameState& state, core::Side side,
                                                          std::vector<HardCasualtyCandidate>* candidates = nullptr);

}  // namespace wh40k::ai::hard
