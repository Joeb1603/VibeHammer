#include "normal/value.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

#include "analysis/threat.hpp"
#include "core/geometry/distance.hpp"
#include "core/model/keywords.hpp"
#include "core/state/queries.hpp"

namespace wh40k::ai::normal {

namespace {

constexpr double kWorthFloor = 0.01;  // a model with no usable weapon still counts a little

// The unit's starting strength as unit_share divides by it (at least its models now, at least 1).
int strength_of(const core::Unit& unit) {
    return std::max(1, std::max(unit.starting_strength, static_cast<int>(unit.models.size())));
}

}  // namespace

bool monster_or_vehicle(const core::Unit& unit) {
    return std::any_of(unit.models.begin(), unit.models.end(),
                       [&unit](const core::Model& m) { return core::is_monster_or_vehicle(unit, m); });
}

double share_weight(const core::GameState& state, const core::Unit& unit) {
    if (unit.points <= 0) return 1.0;  // no points: as before M68
    std::int64_t total = 0;
    std::int64_t counted = 0;
    std::array<const std::string*, 2> type{};  // each side's first datasheet
    bool several_types = false;
    for (const core::Unit& u : state.units) {
        if (u.points <= 0) return 1.0;  // the scenario doesn't give every unit points
        total += u.points;
        ++counted;
        const std::string*& first = type[core::side_index(u.side)];
        if (first == nullptr) {
            first = &u.name;
        } else if (u.name != *first) {
            several_types = true;
        }
    }
    // D-080 (M73, amends D-077; agreed with the user): by points whenever either side has several datasheets, so both
    // sides' shares are on one scale; both sides of one datasheet each keep weight 1, as before.
    if (!several_types || total <= 0) return 1.0;
    return static_cast<double>(unit.points) * static_cast<double>(counted) / static_cast<double>(total);
}

double vp_share(const core::GameState& state) {
    if (!state.mission) return 0.0;
    std::int64_t total = 0;
    std::int64_t counted = 0;
    for (const core::Unit& u : state.units) {
        if (u.points <= 0) return 0.0;
        total += u.points;
        ++counted;
    }
    if (total <= 0 || counted == 0) return 0.0;
    const double mean = static_cast<double>(total) / static_cast<double>(counted);
    const core::Mission& m = *state.mission;
    const double points_per_vp =
        static_cast<double>(m.kill_points) / static_cast<double>(m.kill_vp > 0 ? m.kill_vp : 1);
    return points_per_vp / mean;
}

double unit_share(const core::GameState& state, const core::Unit& unit, double model_equivalents) {
    const double share = model_equivalents / static_cast<double>(strength_of(unit));
    const double w = share_weight(state, unit);
    return w == 1.0 ? share : share * w;  // D-077: unweighted, the M67 value exactly
}

double alive_share(const core::GameState& state, const core::Unit& unit) {
    return unit_share(state, unit, static_cast<double>(unit.models.size()));
}

double hazard_rate(const core::GameState& state, const core::Unit& attacker, const core::Unit& target) {
    const double ours = unit_share(state, attacker, 1.0);
    const double theirs = unit_share(state, target, 1.0);
    return theirs > 0.0 ? ours / theirs : 0.0;
}

double share_value(const core::GameState& state, std::span<const analysis::TargetOutcome> outcomes) {
    double total = 0.0;
    for (const analysis::TargetOutcome& o : outcomes) {
        if (const core::Unit* t = state.find_unit(o.target)) total += unit_share(state, *t, o.outcome.model_equivalents);
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

bool reaches_another(const core::GameState& state, const core::Unit& enemy, const core::Unit& unit) {
    const float reach = analysis::threat_ranges(enemy).shooting_reach_mm();
    for (const core::Unit& f : state.units) {
        if (f.side != unit.side || f.id == unit.id || f.models.empty()) continue;
        const std::optional<float> gap = core::unit_distance(f, enemy);
        if (gap && core::within(*gap, reach)) return true;
    }
    return false;
}

const core::Unit* attacking_enemy(const core::GameState& state, const core::Unit& unit) {
    if (state.flow.attack) {
        const core::Unit* a = state.find_unit(state.flow.attack->attacker);
        if (a != nullptr && a->side != unit.side && !a->models.empty()) return a;
    }
    return closest_enemy(state, unit);
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

double melee_share(analysis::OddsCache& cache, const core::GameState& state, const core::Unit& attacker,
                   const core::Unit& target) {
    if (attacker.models.empty() || target.models.empty()) return 0.0;
    const analysis::Volley v = analysis::unit_melee_volley(cache, attacker, target, hazard_rate(state, attacker, target));
    return unit_share(state, target,
                      analysis::expected_outcome(cache, v, analysis::snapshot_of(target)).model_equivalents);
}

double ranged_share(analysis::OddsCache& cache, const core::GameState& state, const core::Unit& attacker,
                    const core::Unit& target, float gap_mm, bool close_quarters) {
    analysis::ShotContext ctx;
    ctx.type = close_quarters ? core::ShootingType::CloseQuarters : core::ShootingType::Normal;
    ctx.heavy = !close_quarters;
    ctx.gap_mm = close_quarters ? 0.0f : gap_mm;
    return ranged_share(cache, state, attacker, target, ctx);
}

double ranged_share(analysis::OddsCache& cache, const core::GameState& state, const core::Unit& attacker,
                    const core::Unit& target, analysis::ShotContext context) {
    if (attacker.models.empty() || target.models.empty()) return 0.0;
    context.hazard_rate = hazard_rate(state, attacker, target);  // D-077
    const analysis::Volley v = analysis::unit_ranged_volley(cache, attacker, target, context);
    return unit_share(state, target,
                      analysis::expected_outcome(cache, v, analysis::snapshot_of(target)).model_equivalents);
}

std::optional<analysis::ShotContext> next_turn_shot(const core::GameState& state, const core::Unit& attacker,
                                                    const core::Unit& target, float gap_mm) {
    if (attacker.models.empty() || target.models.empty()) return std::nullopt;
    analysis::ShotContext ctx;
    const std::vector<core::UnitId> engaged = core::engaged_enemy_units(state, attacker);
    if (std::find(engaged.begin(), engaged.end(), target.id) != engaged.end()) {
        ctx.type = core::ShootingType::CloseQuarters;  // 10.06: at the unit it is engaged with
        return ctx;
    }
    ctx.target_engaged = core::is_engaged(state, target);
    if (!engaged.empty()) {
        // 10.06: an engaged unit shoots only in close-quarters shooting; at a unit it isn't engaged with, only its
        // MONSTER/VEHICLE models can.
        if (!monster_or_vehicle(attacker)) return std::nullopt;
        ctx.type = core::ShootingType::CloseQuarters;
        ctx.elsewhere = true;
        ctx.gap_mm = gap_mm;
        return ctx;
    }
    ctx.type = core::ShootingType::Normal;
    ctx.heavy = true;
    ctx.gap_mm = gap_mm;
    return ctx;
}

}  // namespace wh40k::ai::normal
