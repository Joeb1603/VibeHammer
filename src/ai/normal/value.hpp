#pragma once

// M41 (D-054): the Normal level's common currency and small read-only helpers.
//
// Value unit: a share of a unit's starting strength (model equivalents / starting strength, M40's
// model_equivalents). Killing 1 of 5 Intercessors is worth 0.2, 1 of 10 Termagants 0.1. It adds up across
// volleys and makes the two sides' losses comparable, which model equivalents alone don't (a dead Intercessor
// is not a dead Termagant). V1 is won by wiping the enemy out (D-007), so progress towards that is the measure.

#include <algorithm>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "analysis/odds.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai::normal {

// D-077 (M68): what a share of `unit` is worth, material weighted by points. 1 (as before M68) unless every unit in
// `state` has points (the scenario gives them, D-076) and either side has more than one unit type (datasheet name;
// D-080, M73: before, `unit`'s own side); then the unit's points over the mean points of every unit (both sides,
// destroyed ones too: fixed for the game). V1, the bot scenarios and Strike have no points, so every weight there is 1;
// a battle of one datasheet a side keeps 1 too.
[[nodiscard]] double share_weight(const core::GameState& state, const core::Unit& unit);

// `model_equivalents` of `unit` as a share of its starting strength, times its share_weight (D-077).
[[nodiscard]] double unit_share(const core::GameState& state, const core::Unit& unit, double model_equivalents);

// The unit's models still on the battlefield, as a share of its starting strength (times its share_weight).
[[nodiscard]] double alive_share(const core::GameState& state, const core::Unit& unit);

// D-077 (M68): [HAZARDOUS]'s exchange rate: one model equivalent of `attacker` in model equivalents of `target` (the
// ratio of their unit_share of one model), so the attacker's loss to a hazard roll can be set against the damage its
// weapon does (analysis::ShotContext::hazard_rate).
[[nodiscard]] double hazard_rate(const core::GameState& state, const core::Unit& attacker, const core::Unit& target);

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
    // D-066 (M56): a loss of `share` of us from a unit this exchange doesn't follow (another enemy unit), already
    // scaled by that unit's own strength; capped at what is left of us.
    void we_lose(double share) { us_ -= std::min(us_, std::max(0.0, share)); }
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

// D-066 (M56): several opposing units sharing one attacker: our unit's strength (`us`) is used once, whichever
// target it hits, and every target's losses and attacks are its own. With one target it is Exchange, term by term.
class MultiExchange {
public:
    MultiExchange(double us, std::vector<double> them) : us0_(us), us_(us), them0_(them), them_(std::move(them)) {}
    void we_hit(std::size_t t, double rate) {
        if (us0_ > 0.0) them_[t] -= std::min(them_[t], rate * us_ / us0_);
    }
    void they_hit(std::size_t t, double rate) {
        if (them0_[t] > 0.0) us_ -= std::min(us_, rate * them_[t] / them0_[t]);
    }
    void we_lose(double share) { us_ -= std::min(us_, std::max(0.0, share)); }
    // Their losses (all targets) minus ours.
    [[nodiscard]] double value() const noexcept {
        double theirs = 0.0;
        for (std::size_t t = 0; t < them_.size(); ++t) theirs += them0_[t] - them_[t];
        return theirs - (us0_ - us_);
    }
    [[nodiscard]] double us() const noexcept { return us_; }
    [[nodiscard]] double them(std::size_t t) const noexcept { return them_[t]; }

private:
    double us0_;
    double us_;
    std::vector<double> them0_;
    std::vector<double> them_;
};

// Enemy units of `side` still on the battlefield, in unit order.
[[nodiscard]] std::vector<const core::Unit*> enemy_units(const core::GameState& state, core::Side side);

// The enemy unit closest to `unit` (edge to edge), nullptr if there is none.
[[nodiscard]] const core::Unit* closest_enemy(const core::GameState& state, const core::Unit& unit);

// D-066 (M56): whether enemy unit `enemy` can reach one of `unit`'s friendly units other than `unit` next turn (a
// Normal move and its longest usable range): then its attacks go to one of them whatever `unit` does, and `unit`'s
// choices don't add to its side's exposure (marginal exposure).
[[nodiscard]] bool reaches_another(const core::GameState& state, const core::Unit& enemy, const core::Unit& unit);

// D-066 (M56): the enemy unit whose attacks are being resolved against `unit` (the attacker of the attack in
// progress, if it is an enemy unit with models), else the closest enemy unit. With one enemy unit it is that unit.
[[nodiscard]] const core::Unit* attacking_enemy(const core::GameState& state, const core::Unit& unit);

// What each model is worth to its unit: its best ranged weapons (in range, [RAPID FIRE] and [HEAVY] applying)
// plus its best melee weapon, against `enemy`, in model equivalents of `enemy` (a small floor so a model with
// nothing still counts). A model with a sergeant's special weapon is worth more than one with a plain loadout.
[[nodiscard]] std::map<core::ModelId, double> model_worth(analysis::OddsCache& cache, const core::Unit& unit,
                                                          const core::Unit& enemy);

// Every model of `attacker` fighting `target` with its best melee weapon (M40's unit_melee_volley), as a share
// of `target`: what a combat between them is worth to the attacker if all its models fight. D-077: [HAZARDOUS]
// priced at hazard_rate.
[[nodiscard]] double melee_share(analysis::OddsCache& cache, const core::GameState& state, const core::Unit& attacker,
                                 const core::Unit& target);

// `attacker`'s best shooting at `target` across `gap_mm` (unengaged, Normal shooting, [HEAVY] applying), as a
// share of `target`. With `close_quarters`, its [CLOSE-QUARTERS] shooting while engaged (10.06). D-077: [HAZARDOUS]
// priced at hazard_rate.
[[nodiscard]] double ranged_share(analysis::OddsCache& cache, const core::GameState& state, const core::Unit& attacker,
                                  const core::Unit& target, float gap_mm, bool close_quarters = false);
// D-077 (M68): the same in any hypothetical context (who is engaged, 04.02, 10.06, 17.03); its hazard_rate is set here.
[[nodiscard]] double ranged_share(analysis::OddsCache& cache, const core::GameState& state, const core::Unit& attacker,
                                  const core::Unit& target, analysis::ShotContext context);

// D-077 (M68): a MONSTER/VEHICLE unit (17.01, 17.03, 10.06), as the engine's shooting rules test it.
[[nodiscard]] bool monster_or_vehicle(const core::Unit& unit);

// D-077 (M68): the hypothetical shooting `attacker` can do at `target` across `gap_mm` in its next Shooting phase,
// judged by who is engaged now (strategic knowledge; the live state asks the engine): unengaged, Normal shooting
// ([HEAVY] applying), at an unengaged target or an engaged MONSTER/VEHICLE (17.03); engaged with the target,
// close-quarters shooting at it; engaged with another unit, close-quarters shooting by its MONSTER/VEHICLE models
// (10.06). nullopt if none.
[[nodiscard]] std::optional<analysis::ShotContext> next_turn_shot(const core::GameState& state,
                                                                  const core::Unit& attacker, const core::Unit& target,
                                                                  float gap_mm);

}  // namespace wh40k::ai::normal
