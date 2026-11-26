#pragma once

// D-096 (M95): the bot's answers to reaction windows (01.03.02), behind two MoveWeights keys, 0 or 1 each:
// - command_reroll (15.02): just after one of its rolls, the die whose re-roll is expected to gain the most, in model
//   equivalents (odds.hpp's unit of value), when that is worth the CP (kRerollMinGain): a missed hit or failed wound
//   of a batch (worth a fresh attack die, or a hit), a failed save of its own unit's (the defender's roll), a low
//   damage or random-A roll, a failed hazard roll; a charge roll that reaches no target, when a new 2D6 reaches one
//   often enough. Advance rolls are kept.
// - insane_bravery (15.04): just before a battle-shock roll that fails often enough (kBraveryRisk).
// With both keys at 0 the bot plays no stratagem: the session declines every window for it
// (BotConfig::plays_stratagems), so it plays exactly as at M94. Any other window is declined (its first answer).

#include <cstddef>
#include <optional>
#include <string>

#include "analysis/odds.hpp"
#include "bot.hpp"
#include "core/state/game_state.hpp"
#include "normal/move_score.hpp"

namespace wh40k::ai::normal {

inline constexpr double kRerollMinGain = 0.25;       // model equivalents a Command Re-roll must be expected to gain
inline constexpr double kChargeRerollChance = 0.25;  // a charge reaching no target: re-rolled if a new 2D6 reaches one so often
inline constexpr double kBraveryRisk = 0.25;         // Insane Bravery when the battle-shock test fails this often

// The best Command Re-roll in the pending window: the die (none for a charge roll, re-rolled whole) and its expected
// gain (a charge: the chance a new roll reaches a target). nullopt when Command Re-roll isn't offered or no die gains.
struct RerollPick {
    std::optional<std::size_t> die;
    double gain = 0.0;
    std::string why;
};
[[nodiscard]] std::optional<RerollPick> best_command_reroll(analysis::OddsCache& cache, const core::GameState& state);

// The answer to `side`'s pending window (UseStratagems), by the keys above.
[[nodiscard]] PolicyResult decide_stratagems(analysis::OddsCache& cache, const core::GameState& state, core::Side side,
                                             const MoveWeights& weights);

}  // namespace wh40k::ai::normal
