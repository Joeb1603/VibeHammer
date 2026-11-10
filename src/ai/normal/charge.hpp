#pragma once

// M42 (D-054, D-056): the Normal level's charge decisions (11.02, 11.04).
//
// - Placement: a charge move is the client's Auto move for charges (auto_charge_move, D-056) simulated in full on a
//   scratch state: StartChargeMove, every placement, move_end_problems, the commit, and no UnitMoveReverted. A roll
//   with no such move is a failed charge.
// - Odds: exact over the 2D6 totals, with charge_odds (M40) and that placement as its PlacementCheck, on scratch
//   states with ScriptedDice: the real roll is never looked at before the charge is declared (D-054).
// - Value (unit shares, value.hpp) over a short horizon: this Fight phase, the enemy's next turn and our next turn,
//   cut at the game's end (D-007). Each volley is the M40 expected outcome of a whole unit's attacks (melee,
//   [CLOSE-QUARTERS] shooting while engaged, shooting across the gap), scaled by the attacker's surviving fraction
//   and capped at what is left of the target.
//   - Holding (or a failed charge: the unit doesn't move, 11.02): the enemy shoots us, then charges us with
//     P(charge after a Normal move) if that is worth more to it than not charging (it strikes first, we reply, then
//     our turn engaged: close-quarters shooting and the fight), otherwise our next turn's shooting. An enemy that
//     is engaged already neither shoots us nor charges us.
//   - A successful charge: the fight now with Fights First (our models engaged or within pile-in reach strike
//     first, then theirs reply), the enemy's turn engaged (its close-quarters shooting, it fights first, we reply)
//     and ours (our close-quarters shooting, we fight first, it replies). So an engaged Termagant can't shoot, an
//     engaged Intercessor still fires its pistol.
//   Declaring costs nothing (a failed charge doesn't move; after the roll the bot may still make no charge move),
//   so a unit declares when some roll gives a charge move worth more than holding; the expected gain is
//   E[max(charge, hold)] - hold over the roll, and the unit with the largest gain declares first.
// - After the roll (SelectChargeTargets, the real roll is known): each offered target set's planned move; the best
//   one if it beats holding, otherwise SkipChargeMove (also when no legal move exists).
// - The charge move itself replays the plan (memoized); from a mid-move state it replans from the move's start.
// - D-083 part 3 (M81), joint charges (not under Generation::M55 / M77; nothing changes for a unit with no partner, so
//   one unit a side charges as before): a charge at one target credits the partners in that fight. Our units already
//   engaged with the target fight it too, and a partner in the army plan (the charger's group on that target) that may
//   still declare a charge this phase is counted at its charge odds (P(2D6 covers the gap)). The charge's value is the
//   fight of the charger and its partners against the target, the target's fight back split among the units it is
//   engaged with, less the partners' fight without the charger (the engaged ones, and a plan partner whose lone charge
//   pays): what the charge adds. The Charge step declares the
//   best-odds charger of the group whose target the most valuable charge is at, so the others follow it in.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "analysis/odds.hpp"
#include "auto_charge_move.hpp"
#include "bot.hpp"
#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"
#include "generation.hpp"
#include "normal/army_plan.hpp"

namespace wh40k::ai::normal {

// A charge move simulated in full from `after_roll` (SelectChargeTargets pending for `unit`): the steps (start,
// placements, commit), each with the hash of the state it expects, and the state after the commit. nullopt if no
// arrangement stands (`problems`, if given, says why).
struct ChargePlan {
    std::vector<core::UnitId> targets;
    std::vector<PlannedStep> steps;
    core::GameState after;
    client::ChargeArrangementScore score;
};
[[nodiscard]] std::optional<ChargePlan> plan_charge_move(const core::GameState& after_roll, core::Side side,
                                                         core::UnitId unit, const std::vector<core::UnitId>& targets,
                                                         std::uint64_t seed, const std::string& reason,
                                                         std::vector<std::string>* problems = nullptr);

// D-083 part 3 (M81): a partner in a charge's fight: engaged with the target already (weight 1) or a plan partner that may
// still charge it this phase (weight: its charge odds).
struct ChargePartner {
    core::UnitId unit;
    double weight = 1.0;
    bool engaged = true;
    // It fights the target without the charger too: engaged, or a plan partner that charges it alone (its lone exchange
    // with the target in the plan pays in Charge mode). Otherwise it charges only to join the charger.
    bool without = true;
    friend bool operator==(const ChargePartner&, const ChargePartner&) = default;
};
struct ChargeCredit {
    core::UnitId target;
    std::vector<ChargePartner> partners;  // unit order
};
// The partners of `unit`'s charge at `target` in `state` (`unit`'s side at its Charge decision, or with a charge of its
// in progress); `plan`, the army plan of `unit`'s side (nullptr: no potential chargers). nullopt: none, or a
// pre-teamwork generation.
[[nodiscard]] std::optional<ChargeCredit> charge_credit(const core::GameState& state, core::UnitId unit,
                                                        core::UnitId target, const ArmyPlan* plan,
                                                        Generation generation = Generation::Current);

// How many of the remaining half-turns the value looks at: 2 (the enemy's next turn and ours), 1 in the last
// battle round when the enemy still has its turn, 0 in the game's last turn (D-007).
[[nodiscard]] int turns_left(const core::GameState& state);

// The value of not charging with `unit` (also of a failed charge), for its side (see the file comment). D-066
// (M56): the shooting and charge threat of the other enemy units that have no other target count too (marginal
// exposure, as the move score's; M55: only the closest enemy unit's).
[[nodiscard]] double hold_value(analysis::OddsCache& cache, const core::GameState& state, core::UnitId unit,
                                Generation generation = Generation::Current);

// The value of the position after `unit`'s charge move against `targets` (`after_commit`: the charge resolved,
// Fights First set), for its side; `turns` as turns_left of the state the charge was planned from. D-066 (M56):
// several targets share our unit's strength (each of our models fights the target closest to it, and our losses
// add up), and the enemy units outside the charge with no other target may charge us next turn; one target with
// no other enemy unit is M42's value. M55 valued each target from our full strength and summed them (double
// counting).
// D-083 part 3 (M81): `credit`, the partners of a charge at one target (charge_credit): the value is what the charge adds
// to their fight (see the file comment); ignored for several targets or a pre-teamwork generation.
[[nodiscard]] double charged_value(analysis::OddsCache& cache, const core::GameState& after_commit, core::UnitId unit,
                                   std::span<const core::UnitId> targets, int turns,
                                   Generation generation = Generation::Current,
                                   const ChargeCredit* credit = nullptr);

// M43's seam: what declaring a charge with `unit` is worth from `state`.
// - `state`: `unit`'s side to act, at its Charge decision, or earlier in the same turn (a Shoot or MoveUnits
//   decision): a scratch copy is then taken to the Charge step with no more shooting and every unit not yet moved
//   remaining stationary.
// - `enemy_losses`: models our shooting is expected to destroy first; they are taken from the enemy unit closest
//   to `unit`, front models first (M40's distance_after_casualties assumption, the worst case for a charge).
// nullopt if `unit` can't declare a charge there. Cost: one planned charge move per roll until one engages every
// model, per target set (V1: one set), so M43 should rank its candidates with the quick bound first
// (analysis::ThreatRanges::p_charge_now) and call this for the few best.
struct ChargeProspect {
    core::UnitId unit;
    std::vector<core::UnitId> targets;          // the target set with the largest gain
    double p_success = 0.0;                     // P(some legal charge move stands), exact over 2D6
    double hold = 0.0;                          // hold_value: not charging, or a failed charge
    double expected = 0.0;                      // E[max(charge value, hold)] over the roll
    double gain = 0.0;                          // expected - hold (never negative)
    double success_mean = 0.0;                  // E[charge value | a legal move], of the best set per roll
    std::array<bool, 13> success_by_roll{};     // [n]: a roll of n has a legal charge move
    std::array<double, 13> value_by_roll{};     // [n]: its value (the best set), hold where it fails
    std::array<int, 13> engaged_by_roll{};      // [n]: models it ends engaged
    std::vector<ChargePartner> partners;        // D-083 part 3 (M81): the best set's partners (one target), if any
};
// D-066 (M56): `losses_from`, the enemy unit our shooting is at (the losses come off its front); nullopt: the
// closest one (M55 always). D-083 part 3 (M81): `plan`, the army plan of `unit`'s side, for the partners that may still
// charge (nullptr: only the partners already engaged are credited).
[[nodiscard]] std::optional<ChargeProspect> charge_prospect(analysis::OddsCache& cache, const core::GameState& state,
                                                            core::UnitId unit, std::uint64_t seed,
                                                            double enemy_losses = 0.0,
                                                            std::optional<core::UnitId> losses_from = std::nullopt,
                                                            Generation generation = Generation::Current,
                                                            const ArmyPlan* plan = nullptr);

// The decisions (nullopt: no answer of the Normal level's own; the caller uses the baseline's).
[[nodiscard]] std::optional<PolicyResult> decide_charge(analysis::OddsCache& cache, const core::GameState& state,
                                                        core::Side side, const std::vector<core::GameAction>& legal,
                                                        std::uint64_t seed,
                                                        Generation generation = Generation::Current);
[[nodiscard]] std::optional<PolicyResult> decide_charge_targets(analysis::OddsCache& cache,
                                                                const core::GameState& state, core::Side side,
                                                                const std::vector<core::GameAction>& legal,
                                                                std::uint64_t seed,
                                                                Generation generation = Generation::Current);
// A charge move in progress (MoveModels): the rest of the auto_charge_move arrangement planned from the move's
// start, so a replan gives the memo's steps.
[[nodiscard]] std::optional<PolicyResult> continue_charge_move(const core::GameState& state, core::Side side,
                                                               std::uint64_t seed);

}  // namespace wh40k::ai::normal
