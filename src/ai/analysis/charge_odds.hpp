#pragma once

// M40 (D-054): charge and advance odds. The dice distributions come from core's own dice functions
// (core::roll_2d6 / roll_d6 over ScriptedDice faces), and "does this roll work?" from the engine itself: for each
// roll, a scratch copy of the state where the roll was made (Simulator with ScriptedDice), asked through
// legal_actions / max_move_distance_mm. Nothing here re-types 11.02 or 09.06.

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "core/state/game_state.hpp"

namespace wh40k::ai::analysis {

// ---- dice distributions ----

// [n] = P(2D6 total is n), n = 0..12, enumerated over the 36 face pairs through core::roll_2d6.
[[nodiscard]] const std::array<double, 13>& two_d6_pmf();
// P(2D6 >= n); 1 for n <= 2, 0 for n > 12.
[[nodiscard]] double p_2d6_at_least(int n);
// [f] = P(D6 shows f), f = 1..6 (index 0 unused).
[[nodiscard]] const std::array<double, 7>& d6_pmf();

// ---- charges ----

// The placement check M42 supplies: in `after_roll` (the charge roll made, SelectChargeTargets pending), can
// `unit` make a charge move that the engine would accept against `targets`? Without one, a roll that makes every
// target eligible (11.04 Before Moving, via legal_actions) counts as a success: the quick bound, since a model
// within the roll of a target can reach its engagement range.
using PlacementCheck =
    std::function<bool(const core::GameState& after_roll, core::UnitId unit, std::span<const core::UnitId> targets)>;

struct ChargeOdds {
    double p_success = 0.0;
    std::array<bool, 13> success_by_total{};  // [n]: a charge roll of n succeeds
    std::array<double, 13> pmf{};             // two_d6_pmf()
};

// Exact P(the charge succeeds) for `unit` declaring a charge against `targets` now. Precondition: `side`'s Charge
// decision is pending; nullopt if DeclareCharge isn't valid for `unit` (e.g. more than 12" away, advanced, fell
// back). One scratch state per 2D6 total (11 at most).
[[nodiscard]] std::optional<ChargeOdds> charge_odds(const core::GameState& state, core::UnitId unit,
                                                    std::span<const core::UnitId> targets,
                                                    const PlacementCheck& placement = {});

// The quick bound without a state at the charge decision (strategic knowledge for hypothetical turns; it matches
// charge_odds in tests): P(a charge roll makes a target at edge-to-edge `gap_mm` eligible: within the roll and
// within 12", 11.04 Before Moving).
[[nodiscard]] double p_charge_roll_covers(float gap_mm);

// ---- advances ----

struct AdvanceOdds {
    std::array<float, 7> max_move_mm{};  // [f]: the unit's maximum move distance after an advance roll of f
    std::array<double, 7> pmf{};         // d6_pmf() (a reused roll, D-005, gives the same distance for every f)
};

// Precondition: `side`'s MoveUnits decision is pending; nullopt if an Advance isn't valid for `unit` now. Reads
// max_move_distance_mm for the unit's first model after StartMove{Advance} with each face scripted.
[[nodiscard]] std::optional<AdvanceOdds> advance_odds(const core::GameState& state, core::UnitId unit);

// ---- distances after casualties ----

// The edge-to-edge distance from `from` to the k-th closest model of `enemy` (k = 1 is the closest; each enemy
// model measured to the closest model of `from`). nullopt if `enemy` has fewer than k models or `from` has none.
[[nodiscard]] std::optional<float> kth_closest_model_distance(const core::Unit& from, const core::Unit& enemy, int k);

// The distance to the (expected kills + 1)-th closest enemy model: the gap left if our shooting removes `kills`
// models and the defender removes the front ones (the worst case for a charge; D-054 M40 notes). `kills` is
// rounded down. nullopt if no model would be left.
[[nodiscard]] std::optional<float> distance_after_casualties(const core::Unit& from, const core::Unit& enemy,
                                                             double kills);

}  // namespace wh40k::ai::analysis
