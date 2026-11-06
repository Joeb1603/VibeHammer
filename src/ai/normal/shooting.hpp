#pragma once

// M41 (D-054): the Normal level's shooting decisions (10.02, 04.01, 04.02, 04.03).
// - Which unit shoots next and with which shooting type: the one whose best declaration is worth the most;
//   the Shoot step ends when nothing is worth anything.
// - Each model's weapons, profiles and targets: start from each model's best choice valued alone against the
//   fresh target, then improve one model at a time against the whole volley's value (coordinate descent). The
//   volley's exact outcome loses excess damage and attacks after a wipe (M40), so overkill avoidance falls out:
//   a model moves its shots off a unit already likely to be destroyed when another target is worth more.
//   Saturation vs Focused Fire falls out of the same numbers ([HEAVY] after <= 3" of movement, [RAPID FIRE]
//   within half range).
// - The next attack batch (04.03): the order of a target's remaining batches with the most expected value (so
//   D2 attacks go before D1 attacks against W2 models), derived from the exact outcome, not a fixed rule.

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "analysis/odds.hpp"
#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai::normal {

struct ShootingChoice {
    core::UnitId unit;
    core::ShootingType type = core::ShootingType::Normal;
    std::vector<core::ShotDeclaration> shots;
    std::vector<analysis::TargetOutcome> outcomes;
    double value = 0.0;  // unit shares (value.hpp)
};

// The best declaration for `unit` shooting with `type` in `state` (nullopt: no valid shot).
[[nodiscard]] std::optional<ShootingChoice> plan_shooting(analysis::OddsCache& cache, const core::GameState& state,
                                                          core::UnitId unit, core::ShootingType type);

// From the Shoot decision's legal actions: the unit, type and declaration worth the most, or nullopt when no
// offered Shoot is worth anything (end the step).
[[nodiscard]] std::optional<ShootingChoice> choose_shooting(analysis::OddsCache& cache, const core::GameState& state,
                                                            const std::vector<core::GameAction>& legal);

// The value of `volley` resolved in `order` (indices into it) against `target`, in model equivalents.
[[nodiscard]] double order_value(analysis::OddsCache& cache, std::span<const analysis::VolleyBatch> volley,
                                 std::span<const std::size_t> order, const analysis::TargetSnapshot& target);

// The order of `volley` (indices) with the most expected model equivalents against `target`: every permutation
// for up to kPermutedBatches batches, else by damage per attack (highest first). Ties keep the given order.
inline constexpr std::size_t kPermutedBatches = 6;
[[nodiscard]] std::vector<std::size_t> best_batch_order(analysis::OddsCache& cache,
                                                        std::span<const analysis::VolleyBatch> volley,
                                                        const analysis::TargetSnapshot& target);

// 04.03 SelectAttackBatch: the batch to resolve next (an index offered in `legal`).
[[nodiscard]] std::size_t choose_attack_batch(analysis::OddsCache& cache, const core::GameState& state,
                                              const std::vector<core::GameAction>& legal);

// The batches grouped by target, each target's batches in their best order (best_batch_order), evaluated against
// the target as it is in `state` (the value of a declaration as the bot will resolve it).
[[nodiscard]] std::vector<analysis::TargetOutcome> evaluate_in_best_order(analysis::OddsCache& cache,
                                                                         const core::GameState& state,
                                                                         std::span<const core::AttackBatch> batches);

// "Intercessor Squad: 5x Bolt Rifle - Focused Fire at Termagants (expected 4.6 models)".
[[nodiscard]] std::string describe_shooting(const core::GameState& state, const ShootingChoice& choice);

}  // namespace wh40k::ai::normal
