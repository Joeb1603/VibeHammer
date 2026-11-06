#include "analysis/charge_odds.hpp"

#include <algorithm>
#include <cmath>
#include <variant>

#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/closest.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/units.hpp"
#include "core/rules/charge.hpp"
#include "core/rules/move_types.hpp"
#include "core/state/queries.hpp"
#include "simulator.hpp"

namespace wh40k::ai::analysis {

namespace {

constexpr int kFaces = 6;

std::array<double, 13> make_two_d6_pmf() {
    std::array<double, 13> pmf{};
    for (int a = 1; a <= kFaces; ++a) {
        for (int b = 1; b <= kFaces; ++b) {
            core::ScriptedDice dice{a, b};
            pmf[static_cast<std::size_t>(core::roll_2d6(dice).total())] += 1.0 / (kFaces * kFaces);
        }
    }
    return pmf;
}

// A face pair the engine sums to `total` (the first one found), for scripting a charge roll.
std::optional<std::array<int, 2>> pair_for_total(int total) {
    for (int a = 1; a <= kFaces; ++a) {
        for (int b = 1; b <= kFaces; ++b) {
            core::ScriptedDice dice{a, b};
            if (core::roll_2d6(dice).total() == total) return std::array<int, 2>{a, b};
        }
    }
    return std::nullopt;
}

bool same_targets(std::span<const core::UnitId> a, std::span<const core::UnitId> b) {
    if (a.size() != b.size()) return false;
    return std::all_of(a.begin(), a.end(), [&](core::UnitId id) { return std::find(b.begin(), b.end(), id) != b.end(); });
}

}  // namespace

const std::array<double, 13>& two_d6_pmf() {
    static const std::array<double, 13> pmf = make_two_d6_pmf();
    return pmf;
}

double p_2d6_at_least(int n) {
    double p = 0.0;
    for (int t = std::max(n, 0); t <= 12; ++t) p += two_d6_pmf()[static_cast<std::size_t>(t)];
    return p;
}

const std::array<double, 7>& d6_pmf() {
    static const std::array<double, 7> pmf = [] {
        std::array<double, 7> p{};
        for (int f = 1; f <= kFaces; ++f) {
            core::ScriptedDice dice{f};
            p[static_cast<std::size_t>(dice.roll_d6())] += 1.0 / kFaces;
        }
        return p;
    }();
    return pmf;
}

std::optional<ChargeOdds> charge_odds(const core::GameState& state, core::UnitId unit,
                                      std::span<const core::UnitId> targets, const PlacementCheck& placement) {
    const core::Unit* u = state.find_unit(unit);
    if (u == nullptr || targets.empty()) return std::nullopt;
    const core::Side side = u->side;
    const core::GameAction declare = core::action::DeclareCharge{unit};
    if (!core::validate_action(state, side, declare).ok()) return std::nullopt;

    ChargeOdds odds;
    odds.pmf = two_d6_pmf();
    for (int total = 0; total <= 12; ++total) {
        if (odds.pmf[static_cast<std::size_t>(total)] == 0.0) continue;
        const auto pair = pair_for_total(total);
        if (!pair) continue;
        Simulator sim(state, 0);  // the scripted roll is the only randomness used
        core::ScriptedDice dice{(*pair)[0], (*pair)[1]};
        if (!sim.step(side, declare, dice).ok()) continue;
        const core::GameState& after = sim.state();
        // 11.04 Before Moving: the engine offers a charge move against exactly these targets.
        bool offered = false;
        if (after.pending && std::holds_alternative<core::decision::SelectChargeTargets>(after.pending->kind)) {
            for (const core::GameAction& a : core::legal_actions(after, side)) {
                const auto* start = std::get_if<core::action::StartChargeMove>(&a);
                if (start != nullptr && start->unit == unit && same_targets(start->targets, targets)) offered = true;
            }
        }
        const bool ok = offered && (!placement || placement(after, unit, targets));
        odds.success_by_total[static_cast<std::size_t>(total)] = ok;
        if (ok) odds.p_success += odds.pmf[static_cast<std::size_t>(total)];
    }
    return odds;
}

double p_charge_roll_covers(float gap_mm) {
    double p = 0.0;
    for (int total = 0; total <= 12; ++total) {
        // As eligible_charge_targets: within the charge roll and within 12" (11.04 Before Moving, D-009).
        const float limit = core::inches_to_mm(std::min(core::kChargeRangeInches, static_cast<float>(total)));
        if (core::within(gap_mm, limit)) p += two_d6_pmf()[static_cast<std::size_t>(total)];
    }
    return p;
}

std::optional<AdvanceOdds> advance_odds(const core::GameState& state, core::UnitId unit) {
    const core::Unit* u = state.find_unit(unit);
    if (u == nullptr || u->models.empty()) return std::nullopt;
    const core::Side side = u->side;
    const core::GameAction start = core::action::StartMove{unit, core::MoveType::Advance, std::nullopt};
    if (!core::validate_action(state, side, start).ok()) return std::nullopt;
    AdvanceOdds odds;
    odds.pmf = d6_pmf();
    for (int face = 1; face <= kFaces; ++face) {
        Simulator sim(state, 0);
        core::ScriptedDice dice{face};  // not consumed when the roll is reused (D-005)
        if (!sim.step(side, start, dice).ok()) return std::nullopt;
        const auto max = core::max_move_distance_mm(sim.state(), u->models.front().id);
        if (!max) return std::nullopt;
        odds.max_move_mm[static_cast<std::size_t>(face)] = *max;
    }
    return odds;
}

std::optional<float> kth_closest_model_distance(const core::Unit& from, const core::Unit& enemy, int k) {
    if (k < 1 || from.models.empty() || static_cast<int>(enemy.models.size()) < k) return std::nullopt;
    const auto ours = core::placed_models(from);
    std::vector<float> gaps;
    for (const core::PlacedModel& m : core::placed_models(enemy)) {
        const auto d = core::closest_distance(std::span<const core::PlacedModel>(&m, 1), ours);
        if (d) gaps.push_back(*d);
    }
    if (static_cast<int>(gaps.size()) < k) return std::nullopt;
    std::sort(gaps.begin(), gaps.end());
    return gaps[static_cast<std::size_t>(k - 1)];
}

std::optional<float> distance_after_casualties(const core::Unit& from, const core::Unit& enemy, double kills) {
    const int k = static_cast<int>(std::floor(std::max(0.0, kills))) + 1;
    return kth_closest_model_distance(from, enemy, k);
}

}  // namespace wh40k::ai::analysis
