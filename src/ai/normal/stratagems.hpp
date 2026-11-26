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
// D-097 (M96): three more keys, 0 or 1 each, for the windows that follow no roll (the session declines a window that
// offers only stratagems whose key is 0, so at 0 the bot never sees one). Values are in unit shares (value.hpp):
// - fire_overwatch (15.08): at the end of the enemy's Movement phase, the unit whose best snap shot (15.09: one target,
//   only 6s hit; plan_snap_shooting, its hazard losses taken off) is worth the most, when that is worth the CP
//   (kOverwatchMinShare);
// - heroic_intervention (15.11): at the end of the enemy's Charge phase, the unit and mode whose charge gains the most
//   over holding, E[max(charged, hold)] - hold over the 2D6 (M42's values: plan_charge_move, charged_value, hold_value;
//   the engine's own odds for the mode's targets and Into the Fray's cap), per CP spent (kHeroicMinGain a CP; a tie
//   goes to the cheaper Leap to Defend);
// - counteroffensive (15.12): just after an enemy unit has fought, only when it changes who fights next (a Fights First
//   step where the enemy has an eligible Fights First unit and we have none), for the unit whose fight first is worth
//   the most against the enemy's Fights First unit engaged with it: the share of its own fight it keeps plus the share
//   of the enemy's fight on it its strike takes away, when that is worth the 2CP (kCounterMinGain a CP).
// D-099 (M97): two more keys, 0 or 1 each, for the own-turn stratagems that end in mortal wounds (at 0 the session hides
// Explosives' answers and declines Crushing Impact's window, so the bot never sees either). Values are in unit shares,
// a distribution of mortal wounds valued by analysis::mortal_wound_model_equivalents:
// - explosives (15.05): at the Shoot decision, before a shot, the unit and enemy unit with the highest expected share
//   for six D6 at 4+ (Binomial(6, 1/2)), when that is worth the CP (kExplosivesMinShare); the selection then names that
//   enemy unit and its first legal model;
// - crushing_impact (15.06): in the window just after a charge move, the pair with the best expected gain: the enemy's
//   share for Binomial(T, 1/3) less the unit's own for Binomial(T, 1/6), each capped at 6, when that is worth the CP
//   (kCrushMinGain). The charge planning doesn't price it (it is judged only at the window).

#include <cstddef>
#include <cstdint>
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
inline constexpr double kOverwatchMinShare = 0.05;   // D-097: unit shares a snap shot must be worth (1CP)
inline constexpr double kHeroicMinGain = 0.05;       // D-097: unit shares a heroic charge must gain, per CP
inline constexpr double kCounterMinGain = 0.05;      // D-097: unit shares Counteroffensive must gain, per CP
inline constexpr double kExplosivesMinShare = 0.05;  // D-099: unit shares Explosives must be worth (1CP)
inline constexpr double kCrushMinGain = 0.05;        // D-099: unit shares Crushing Impact must gain (1CP)

// The best Command Re-roll in the pending window: the die (none for a charge roll, re-rolled whole) and its expected
// gain (a charge: the chance a new roll reaches a target). nullopt when Command Re-roll isn't offered or no die gains.
struct RerollPick {
    std::optional<std::size_t> die;
    double gain = 0.0;
    std::string why;
};
[[nodiscard]] std::optional<RerollPick> best_command_reroll(analysis::OddsCache& cache, const core::GameState& state);

// D-097 (M96): the best use of a stratagem of the windows that follow no roll, and what it is worth (unit shares;
// Heroic Intervention's and Counteroffensive's per CP spent). nullopt when it isn't offered or nothing gains.
struct ReactionPick {
    core::GameAction action;
    double value = 0.0;
    std::string why;
};
[[nodiscard]] std::optional<ReactionPick> best_fire_overwatch(analysis::OddsCache& cache, const core::GameState& state);
[[nodiscard]] std::optional<ReactionPick> best_heroic_intervention(analysis::OddsCache& cache,
                                                                   const core::GameState& state, std::uint64_t seed);
[[nodiscard]] std::optional<ReactionPick> best_counteroffensive(analysis::OddsCache& cache, const core::GameState& state);

// D-099 (M97): the Explosives use worth the most among the Shoot decision's answers `legal` (nullopt when none is
// offered or no pair exists); Crushing Impact's in the pending window (nullopt when it isn't offered); and the pending
// selection's answer (Explosives: the enemy unit worth the most, with its first legal model; Crushing Impact: the pair
// with the best gain; nullopt when no selection is pending).
[[nodiscard]] std::optional<ReactionPick> best_explosives(const core::GameState& state,
                                                          const std::vector<core::GameAction>& legal);
[[nodiscard]] std::optional<ReactionPick> best_crushing_impact(const core::GameState& state);
[[nodiscard]] std::optional<ReactionPick> best_effect_pair(const core::GameState& state);

// The answer to `side`'s pending window (UseStratagems), by the keys above. `seed`: the bot's (a heroic charge's move
// is planned as M42's).
[[nodiscard]] PolicyResult decide_stratagems(analysis::OddsCache& cache, const core::GameState& state, core::Side side,
                                             const MoveWeights& weights, std::uint64_t seed = 0);

}  // namespace wh40k::ai::normal
