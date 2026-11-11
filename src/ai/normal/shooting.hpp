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
// - D-083 part 3 (M81), focus fire (only with several units on a side, so one unit a side shoots as before): a declaration's value adds, for each target, a finishing bonus (its reply in one of its
//   turns, the worst for us as the move score values a reply, x the enemy's turns left (at most kFinishTurns) x the
//   expected share of its models destroyed: a wounded model finished takes its attacks with it, wounds alone don't) and
//   a small bonus for the army plan's group targets (the focus). A unit splits its weapons over several targets only when that beats its best concentrated
//   declaration (every weapon that can at one target) by kSplitMargin, and a unit that shoots at a focus target shoots
//   before one that doesn't.

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "analysis/odds.hpp"
#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"
#include "normal/army_plan.hpp"

namespace wh40k::ai::normal {

// D-083 part 3 (M81): the weights of focus fire (M83 may tune them).
inline constexpr int kFinishTurns = 3;        // a destroyed target's reply is removed for its next turns, at most these
inline constexpr double kFocusWeight = 0.1;   // x the share destroyed of a target of the army plan's groups
inline constexpr double kSplitMargin = 0.02;  // a split declaration must beat the best concentrated one by this

// D-083 part 3 (M81): what focus fire knows about the shooting side's targets, for one Shoot decision. Inactive (every
// value as before M81) with one unit a side.
class ShootingAim {
public:
    ShootingAim() = default;
    // The aim of `side` (the side shooting) in `state` (the army plan's group targets are its focus targets). `cache`
    // and `state` must outlive it.
    ShootingAim(analysis::OddsCache& cache, const core::GameState& state, core::Side side);
    [[nodiscard]] bool active() const noexcept { return active_; }
    // The bonus of a declaration with these outcomes (unit shares; 0 when inactive).
    [[nodiscard]] double bonus(std::span<const analysis::TargetOutcome> outcomes) const;
    // `target`'s reply in one of its turns on the one of our units it hurts most in its next turn (shares of that unit,
    // at its strength now; 0 if the enemy has no turn left).
    [[nodiscard]] double reply(core::UnitId target) const;
    [[nodiscard]] bool focus(core::UnitId target) const;

private:
    analysis::OddsCache* cache_ = nullptr;
    const core::GameState* state_ = nullptr;
    core::Side side_ = core::Side::A;
    bool active_ = false;
    double finish_turns_ = 0.0;  // the enemy's turns left after the shooting side's turn, at most kFinishTurns
    std::vector<core::UnitId> focus_;
    mutable std::vector<std::pair<core::UnitId, double>> replies_;  // computed on first use, unit order of first use
};

struct ShootingChoice {
    core::UnitId unit;
    core::ShootingType type = core::ShootingType::Normal;
    std::vector<core::ShotDeclaration> shots;
    std::vector<analysis::TargetOutcome> outcomes;
    double value = 0.0;  // unit shares (value.hpp); D-083 part 3 (M81): with an active aim, its bonus included
    // D-083 part 3: an active aim chose the declaration concentrated on this target over the split one it had found.
    std::optional<core::UnitId> concentrated;
    std::string notes;  // D-083 part 3: choose_shooting's focus-fire notes for the reason (empty without)
};

// The best declaration for `unit` shooting with `type` in `state` (nullopt: no valid shot).
// D-083 part 3 (M81): with an active `aim`, the value adds its bonus and a split must beat concentrating (see the file
// comment); without one (or inactive) exactly as before.
[[nodiscard]] std::optional<ShootingChoice> plan_shooting(analysis::OddsCache& cache, const core::GameState& state,
                                                          core::UnitId unit, core::ShootingType type,
                                                          const ShootingAim* aim = nullptr);

// From the Shoot decision's legal actions: the unit, type and declaration worth the most, or nullopt when no
// offered Shoot is worth anything (end the step). D-083 part 3 (M81): with focus fire active (ShootingAim), the values
// with its bonus, and a unit shooting at a focus target first.
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
// D-083 part 3 (M81): followed by the choice's notes ("..., on the focus Screamer-killer 3, to finish Screamer-killer 3
// (64% to destroy it)").
[[nodiscard]] std::string describe_shooting(const core::GameState& state, const ShootingChoice& choice);

}  // namespace wh40k::ai::normal
