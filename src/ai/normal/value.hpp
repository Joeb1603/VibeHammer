#pragma once

// M41 (D-054): the Normal level's common currency and small read-only helpers.
//
// Value unit: a share of a unit's starting strength (model equivalents / starting strength, M40's
// model_equivalents). Killing 1 of 5 Intercessors is worth 0.2, 1 of 10 Termagants 0.1. It adds up across
// volleys and makes the two sides' losses comparable, which model equivalents alone don't (a dead Intercessor
// is not a dead Termagant). V1 is won by wiping the enemy out (D-007), so progress towards that is the measure.

#include <algorithm>
#include <map>
#include <span>
#include <vector>

#include "analysis/odds.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai::normal {

// `model_equivalents` of `unit` as a share of its starting strength.
[[nodiscard]] double unit_share(const core::Unit& unit, double model_equivalents);

// The unit's models still on the battlefield, as a share of its starting strength.
[[nodiscard]] double alive_share(const core::Unit& unit);

// M42/M43: what is left of two opposing units, as shares of their starting strengths, as volleys land in
// sequence. A volley is worth `rate` (shares of the target) at the attacker's strength when the exchange started,
// so it is scaled by the attacker's surviving fraction and capped at what is left of the target.
class Exchange {
public:
    Exchange(double us, double them) : us0_(us), them0_(them), us_(us), them_(them) {}
    void we_hit(double rate) {
        if (us0_ > 0.0) them_ -= std::min(them_, rate * us_ / us0_);
    }
    void they_hit(double rate) {
        if (them0_ > 0.0) us_ -= std::min(us_, rate * them_ / them0_);
    }
    [[nodiscard]] double us() const noexcept { return us_; }
    [[nodiscard]] double them() const noexcept { return them_; }
    [[nodiscard]] double us0() const noexcept { return us0_; }
    [[nodiscard]] double them0() const noexcept { return them0_; }
    // Their losses minus ours.
    [[nodiscard]] double value() const noexcept { return (them0_ - them_) - (us0_ - us_); }

private:
    double us0_;
    double them0_;
    double us_;
    double them_;
};

// The sum of unit_share over the outcomes (each against its own target unit in `state`).
[[nodiscard]] double share_value(const core::GameState& state, std::span<const analysis::TargetOutcome> outcomes);

// Enemy units of `side` still on the battlefield, in unit order.
[[nodiscard]] std::vector<const core::Unit*> enemy_units(const core::GameState& state, core::Side side);

// The enemy unit closest to `unit` (edge to edge), nullptr if there is none.
[[nodiscard]] const core::Unit* closest_enemy(const core::GameState& state, const core::Unit& unit);

// What each model is worth to its unit: its best ranged weapons (in range, [RAPID FIRE] and [HEAVY] applying)
// plus its best melee weapon, against `enemy`, in model equivalents of `enemy` (a small floor so a model with
// nothing still counts). A model with a sergeant's special weapon is worth more than one with a plain loadout.
[[nodiscard]] std::map<core::ModelId, double> model_worth(analysis::OddsCache& cache, const core::Unit& unit,
                                                          const core::Unit& enemy);

// Every model of `attacker` fighting `target` with its best melee weapon (M40's unit_melee_volley), as a share
// of `target`: what a combat between them is worth to the attacker if all its models fight.
[[nodiscard]] double melee_share(analysis::OddsCache& cache, const core::Unit& attacker, const core::Unit& target);

// `attacker`'s best shooting at `target` across `gap_mm` (unengaged, Normal shooting, [HEAVY] applying), as a
// share of `target`. With `close_quarters`, its [CLOSE-QUARTERS] shooting while engaged (10.06).
[[nodiscard]] double ranged_share(analysis::OddsCache& cache, const core::Unit& attacker, const core::Unit& target,
                                  float gap_mm, bool close_quarters = false);

}  // namespace wh40k::ai::normal
