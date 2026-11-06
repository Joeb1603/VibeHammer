#include "analysis/threat.hpp"

#include <algorithm>

#include "analysis/charge_odds.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/units.hpp"
#include "core/rules/charge.hpp"
#include "core/rules/shooting.hpp"

namespace wh40k::ai::analysis {

namespace {

Volley concat(Volley a, const Volley& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

}  // namespace

// ---- what a unit may still do ----

TurnOptions current_options(const core::GameState& state, const core::Unit& unit) {
    TurnOptions o;
    o.shooting_types = core::eligible_shooting_types(state, unit);
    o.heavy = core::shooting_context(state, unit).heavy_conditions_met;
    o.charge_problem = core::declare_charge_problem(state, unit);
    return o;
}

TurnAllowance allowance_after(MovePlan plan) {
    switch (plan) {
        case MovePlan::RemainStationary:
        case MovePlan::NormalUpTo3: return TurnAllowance{true, core::ShootingType::Normal, true, true};
        case MovePlan::Normal: return TurnAllowance{true, core::ShootingType::Normal, false, true};
        case MovePlan::Advance: return TurnAllowance{true, core::ShootingType::Assault, false, false};
        case MovePlan::FallBack: return TurnAllowance{false, core::ShootingType::Normal, false, false};
    }
    return TurnAllowance{};
}

// ---- threat ranges ----

double ThreatRanges::p_advance_shoot(float gap_mm) const {
    if (max_assault_range_mm <= 0.0f) return 0.0;
    double p = 0.0;
    for (std::size_t f = 1; f <= 6; ++f) {
        if (core::within(gap_mm, advance_mm[f] + max_assault_range_mm)) p += d6_pmf()[f];
    }
    return p;
}

double ThreatRanges::p_charge_after_move(float gap_mm) const {
    // A Normal move must end unengaged (09.05), so the charge starts at least an engagement range away.
    const float left = std::max(gap_mm - move_mm, core::kEngagementRangeMm);
    return p_charge_roll_covers(left);
}

double ThreatRanges::p_charge_now(float gap_mm) { return p_charge_roll_covers(gap_mm); }

ThreatRanges threat_ranges(const core::Unit& unit) {
    ThreatRanges t;
    if (!unit.models.empty()) {
        float m = -1.0f;
        for (const core::Model& model : unit.models) {
            const float mm = model.characteristics.move_mm.value_or(0.0f);
            m = m < 0.0f ? mm : std::min(m, mm);  // the unit moves as fast as its slowest model
        }
        t.move_mm = std::max(m, 0.0f);
    }
    for (std::size_t f = 1; f <= 6; ++f) {
        t.advance_mm[f] = t.move_mm + core::inches_to_mm(static_cast<float>(f));  // 09.06: M + the advance roll
    }
    for (const core::Model& model : unit.models) {
        for (const core::Weapon& w : model.weapons) {
            for (const core::WeaponProfile& p : w.profiles) {
                if (p.kind != core::WeaponKind::Ranged || !p.range_mm || !profile_supported(p)) continue;
                t.max_range_mm = std::max(t.max_range_mm, *p.range_mm);
                if (p.has<core::ability::Assault>()) t.max_assault_range_mm = std::max(t.max_assault_range_mm, *p.range_mm);
                if (p.has<core::ability::CloseQuarters>()) {
                    t.max_close_quarters_range_mm = std::max(t.max_close_quarters_range_mm, *p.range_mm);
                }
            }
        }
    }
    return t;
}

// ---- roles ----

UnitRole unit_role(OddsCache& cache, const core::Unit& unit, const core::Unit& enemy) {
    UnitRole r;
    const ThreatRanges t = threat_ranges(unit);
    const TargetSnapshot snap = snapshot_of(enemy);
    if (t.max_range_mm > 0.0f) {
        const ShotContext best{core::ShootingType::Normal, true, t.max_range_mm * 0.5f};
        r.ranged = expected_outcome(cache, unit_ranged_volley(cache, unit, enemy, best), snap).model_equivalents;
    }
    r.melee = expected_outcome(cache, unit_melee_volley(cache, unit, enemy), snap).model_equivalents;
    const double total = r.ranged + r.melee;
    r.ranged_share = total > 0.0 ? r.ranged / total : 0.5;
    r.lean = r.ranged_share > 0.5 + kLeanMargin   ? Lean::Ranged
             : r.ranged_share < 0.5 - kLeanMargin ? Lean::Melee
                                                  : Lean::Balanced;
    const double models = static_cast<double>(enemy.models.size());
    if (models > 0.0) {
        r.ranged_vs_enemy = r.ranged / models;
        r.melee_vs_enemy = r.melee / models;
    }
    r.strong_ranged = r.ranged_vs_enemy >= kStrongShare;
    r.strong_melee = r.melee_vs_enemy >= kStrongShare;
    return r;
}

// ---- matchup per range band ----

DirectionTable direction_table(OddsCache& cache, const core::Unit& attacker, const core::Unit& target) {
    DirectionTable table;
    table.attacker = attacker.id;
    table.target = target.id;
    const ThreatRanges t = threat_ranges(attacker);
    const TargetSnapshot snap = snapshot_of(target);
    const AttackOutcome nothing = expected_outcome(cache, Volley{}, snap);
    for (std::size_t i = 0; i < kRangeBands; ++i) {
        BandValue& v = table.bands[i];
        v.band = static_cast<RangeBand>(i);
        v.stationary = v.moved = v.advanced = v.melee = v.turn = nothing;
    }

    // Engaged: close-quarters shooting (24.16 needs an unengaged unit), then the fight.
    BandValue& engaged = table.bands[static_cast<std::size_t>(RangeBand::Engaged)];
    engaged.gap_mm = 0.0f;
    const Volley cq = unit_ranged_volley(cache, attacker, target, ShotContext{core::ShootingType::CloseQuarters, false, 0.0f});
    const Volley melee = unit_melee_volley(cache, attacker, target);
    engaged.stationary = engaged.moved = expected_outcome(cache, cq, snap);
    engaged.melee = expected_outcome(cache, melee, snap);
    engaged.turn = expected_outcome(cache, concat(cq, melee), snap);

    const auto shooting_band = [&](RangeBand band, float gap) {
        BandValue& v = table.bands[static_cast<std::size_t>(band)];
        v.gap_mm = gap;
        if (t.max_range_mm <= 0.0f) return;
        v.stationary = expected_outcome(
            cache, unit_ranged_volley(cache, attacker, target, ShotContext{core::ShootingType::Normal, true, gap}), snap);
        v.moved = expected_outcome(
            cache, unit_ranged_volley(cache, attacker, target, ShotContext{core::ShootingType::Normal, false, gap}), snap);
        v.advanced = expected_outcome(
            cache, unit_ranged_volley(cache, attacker, target, ShotContext{core::ShootingType::Assault, false, gap}), snap);
        v.turn = v.stationary;
    };
    shooting_band(RangeBand::HalfRange, t.max_range_mm * 0.5f);
    shooting_band(RangeBand::InRange, t.max_range_mm);
    table.bands[static_cast<std::size_t>(RangeBand::OutOfRange)].gap_mm = t.max_range_mm + core::inches_to_mm(1.0f);
    return table;
}

Matchup matchup(OddsCache& cache, const core::Unit& a, const core::Unit& b) {
    return Matchup{direction_table(cache, a, b), direction_table(cache, b, a)};
}

}  // namespace wh40k::ai::analysis
