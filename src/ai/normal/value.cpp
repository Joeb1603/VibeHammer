#include "normal/value.hpp"

#include <algorithm>

#include "core/state/queries.hpp"

namespace wh40k::ai::normal {

namespace {

constexpr double kWorthFloor = 0.01;  // a model with no usable weapon still counts a little

}  // namespace

double unit_share(const core::Unit& unit, double model_equivalents) {
    const int n = std::max(1, std::max(unit.starting_strength, static_cast<int>(unit.models.size())));
    return model_equivalents / static_cast<double>(n);
}

double alive_share(const core::Unit& unit) { return unit_share(unit, static_cast<double>(unit.models.size())); }

double share_value(const core::GameState& state, std::span<const analysis::TargetOutcome> outcomes) {
    double total = 0.0;
    for (const analysis::TargetOutcome& o : outcomes) {
        if (const core::Unit* t = state.find_unit(o.target)) total += unit_share(*t, o.outcome.model_equivalents);
    }
    return total;
}

std::vector<const core::Unit*> enemy_units(const core::GameState& state, core::Side side) {
    std::vector<const core::Unit*> out;
    for (const core::Unit& u : state.units) {
        if (u.side != side && !u.models.empty()) out.push_back(&u);
    }
    return out;
}

const core::Unit* closest_enemy(const core::GameState& state, const core::Unit& unit) {
    const core::Unit* best = nullptr;
    float best_gap = 0.0f;
    for (const core::Unit* e : enemy_units(state, unit.side)) {
        const std::optional<float> gap = core::unit_distance(unit, *e);
        if (!gap) continue;
        if (best == nullptr || *gap < best_gap) {
            best = e;
            best_gap = *gap;
        }
    }
    return best;
}

std::map<core::ModelId, double> model_worth(analysis::OddsCache& cache, const core::Unit& unit,
                                            const core::Unit& enemy) {
    std::map<core::ModelId, double> out;
    for (const core::Model& m : unit.models) {
        core::Unit one = unit;
        one.models = {m};
        double value = kWorthFloor;
        if (!enemy.models.empty()) {
            const analysis::TargetSnapshot snap = analysis::snapshot_of(enemy);
            // At 0" every weapon is in range and within half range: the model's best possible shooting.
            const analysis::Volley ranged =
                analysis::unit_ranged_volley(cache, one, enemy, analysis::ShotContext{core::ShootingType::Normal, true, 0.0f});
            const analysis::Volley melee = analysis::unit_melee_volley(cache, one, enemy);
            value += analysis::expected_outcome(cache, ranged, snap).model_equivalents;
            value += analysis::expected_outcome(cache, melee, snap).model_equivalents;
        }
        out[m.id] = value;
    }
    return out;
}

double melee_share(analysis::OddsCache& cache, const core::Unit& attacker, const core::Unit& target) {
    if (attacker.models.empty() || target.models.empty()) return 0.0;
    const analysis::Volley v = analysis::unit_melee_volley(cache, attacker, target);
    return unit_share(target, analysis::expected_outcome(cache, v, analysis::snapshot_of(target)).model_equivalents);
}

double ranged_share(analysis::OddsCache& cache, const core::Unit& attacker, const core::Unit& target, float gap_mm,
                    bool close_quarters) {
    if (attacker.models.empty() || target.models.empty()) return 0.0;
    const analysis::ShotContext ctx{close_quarters ? core::ShootingType::CloseQuarters : core::ShootingType::Normal,
                                    !close_quarters, close_quarters ? 0.0f : gap_mm};
    const analysis::Volley v = analysis::unit_ranged_volley(cache, attacker, target, ctx);
    return unit_share(target, analysis::expected_outcome(cache, v, analysis::snapshot_of(target)).model_equivalents);
}

}  // namespace wh40k::ai::normal
