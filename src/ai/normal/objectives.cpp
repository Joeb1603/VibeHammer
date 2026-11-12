#include "normal/objectives.hpp"

#include <algorithm>
#include <cmath>

#include "analysis/charge_odds.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/pose.hpp"
#include "core/rules/leadership.hpp"
#include "core/state/objectives.hpp"
#include "core/state/queries.hpp"
#include "normal/army_plan.hpp"
#include "normal/value.hpp"

namespace wh40k::ai::normal {

namespace {

int model_oc(const core::Unit& unit, const core::Model& model) {
    const std::optional<int> oc = core::objective_control(unit, model);  // 01.07: '-' while battle-shocked
    return oc && *oc > 0 ? *oc : 0;
}

}  // namespace

int oc_within(const core::Unit& unit, std::span<const core::Position> positions, const core::ObjectiveArea& area) {
    int total = 0;
    for (std::size_t i = 0; i < unit.models.size() && i < positions.size(); ++i) {
        const core::Model& m = unit.models[i];
        const int oc = model_oc(unit, m);
        if (oc == 0) continue;
        if (core::within_objective(core::BasePose{positions[i], m.base, m.facing_deg}, area)) total += oc;
    }
    return total;
}

int oc_within(const core::Unit& unit, const core::ObjectiveArea& area) {
    int total = 0;
    for (const core::Model& m : unit.models) {
        const int oc = model_oc(unit, m);
        if (oc > 0 && core::model_within_objective(m, area)) total += oc;
    }
    return total;
}

int unit_oc(const core::Unit& unit) {
    int total = 0;
    for (const core::Model& m : unit.models) total += model_oc(unit, m);
    return total;
}

float area_gap_mm(const core::Unit& unit, const core::ObjectiveArea& area) {
    float best = -1.0f;
    for (const core::Model& m : unit.models) {
        const float g = core::objective_gap_mm(core::pose_of(m), area);
        if (best < 0.0f || g < best) best = g;
    }
    return std::max(0.0f, best);
}

float centroid_gap_mm(std::span<const core::Position> positions, const core::ObjectiveArea& area) {
    if (positions.empty()) return 0.0f;
    double x = 0.0;
    double y = 0.0;
    for (const core::Position& p : positions) {
        x += p.x;
        y += p.y;
    }
    const double n = static_cast<double>(positions.size());
    const double dx = x / n - area.centre.x;
    const double dy = y / n - area.centre.y;
    return std::max(0.0f, static_cast<float>(std::sqrt(dx * dx + dy * dy)) - area.diameter_mm * 0.5f);
}

double p_reaches_area(const analysis::ThreatRanges& threat, float gap_mm) {
    if (core::within(gap_mm, 0.0f)) return 1.0;
    if (!(threat.move_mm > 0.0f)) return 0.0;
    if (core::within(gap_mm, threat.move_mm)) return 1.0;
    double advance = 0.0;
    for (std::size_t f = 1; f <= 6; ++f) {
        if (core::within(gap_mm, threat.advance_mm[f])) advance += analysis::d6_pmf()[f];
    }
    return std::max(advance, threat.p_charge_after_move(gap_mm));
}

void one_place(std::vector<double>& p, const std::vector<bool>& in_range_now) {
    if (std::any_of(in_range_now.begin(), in_range_now.end(), [](bool b) { return b; })) {
        for (std::size_t i = 0; i < p.size(); ++i) p[i] = i < in_range_now.size() && in_range_now[i] ? 1.0 : 0.0;
        return;
    }
    double sum = 0.0;
    for (const double x : p) sum += x;
    if (sum > 1.0) {
        for (double& x : p) x /= sum;
    }
}

void LevelPmf::add(int oc, double p) {
    if (oc <= 0 || !(p > 0.0)) return;
    p = std::min(1.0, p);
    std::vector<double> next(pmf_.size() + static_cast<std::size_t>(oc), 0.0);
    for (std::size_t r = 0; r < pmf_.size(); ++r) {
        next[r] += pmf_[r] * (1.0 - p);
        next[r + static_cast<std::size_t>(oc)] += pmf_[r] * p;
    }
    pmf_ = std::move(next);
}

double LevelPmf::p_below(double level) const {
    double p = 0.0;
    for (std::size_t r = 0; r < pmf_.size(); ++r) {
        if (!(static_cast<double>(r) < level - 1e-9)) break;
        p += pmf_[r];
    }
    return std::min(1.0, p);
}

NextScorings next_scorings(const core::GameState& state, core::Side side, int turns) {
    NextScorings n;
    if (!state.mission || core::turn_side(state) != side) return n;
    const int round = state.flow.battle_round;
    const int first = state.mission->first_scoring_round;
    const bool second = state.flow.turn_in_round != 0;
    // The opponent's next Command phase: in this round if we went first, else in the next one.
    n.theirs = turns >= 1 && round + (second ? 1 : 0) >= first;
    // Ours: the next round's (turns_left 2: the opponent's turn and ours are both still to come).
    n.ours = turns >= 2 && round + 1 >= first;
    return n;
}

bool can_still_move(const core::GameState& state, const core::Unit& unit) {
    if (unit.models.empty() || core::turn_side(state) != unit.side) return false;
    const core::Stage s = state.flow.stage;
    if (s == core::Stage::NotStarted || s == core::Stage::Deploy || s > core::Stage::MoveUnits) return false;
    const bool moving = state.flow.move && state.flow.move->unit == unit.id;
    if (s == core::Stage::MoveUnits && unit.phase_flags.selected_to_move && !moving) return false;
    return !core::is_engaged(state, unit);
}

std::optional<ObjectiveOutlook> objective_outlook(const core::GameState& origin, const core::Unit& us,
                                                  const ArmyPlan& plan, int turns, double weight) {
    if (!origin.mission || !(weight > 0.0)) return std::nullopt;
    ObjectiveOutlook o;
    o.next = next_scorings(origin, us.side, turns);
    if (!o.next.ours && !o.next.theirs) return std::nullopt;
    const double rate = vp_share(origin) * weight;
    if (!(rate > 0.0)) return std::nullopt;
    const std::size_t ours = core::side_index(us.side);
    const std::size_t theirs = 1 - ours;
    const std::vector<core::ObjectiveArea>& areas = origin.mission->objectives;
    for (std::size_t i = 0; i < areas.size(); ++i) {
        AreaOutlook a;
        a.index = i;
        a.value_us = rate * static_cast<double>(areas[i].vp[ours]);
        a.value_them = rate * static_cast<double>(areas[i].vp[theirs]);
        o.areas.push_back(std::move(a));
    }
    // Our other units: where they stand, or with all their OC on the area the plan sends them to this turn.
    for (const core::Unit& f : origin.units) {
        if (f.side != us.side || f.id == us.id || f.models.empty()) continue;
        std::optional<std::size_t> promised;
        if (const UnitPlan* up = plan.find(f.id); up != nullptr && up->area && *up->area < plan.areas.size()) {
            const AreaSet& set = plan.areas[*up->area];
            const AreaMember* m = set.member(f.id);
            if (m != nullptr && m->arrival == 0 && can_still_move(origin, f)) promised = set.area;
        }
        for (AreaOutlook& a : o.areas) {
            if (promised) {
                if (a.index == *promised) a.others += unit_oc(f);
            } else {
                a.others += oc_within(f, areas[a.index]);
            }
        }
    }
    // The enemy: its level now, and where it can be at the end of its next turn.
    for (const core::Unit& e : origin.units) {
        if (e.side == us.side || e.models.empty()) continue;
        const int oc = unit_oc(e);
        if (oc <= 0) continue;
        const analysis::ThreatRanges th = analysis::threat_ranges(e);
        const bool engaged = core::is_engaged(origin, e);
        std::vector<double> p(o.areas.size(), 0.0);
        std::vector<bool> in_range(o.areas.size(), false);
        for (AreaOutlook& a : o.areas) {
            const core::ObjectiveArea& area = areas[a.index];
            a.them_now += oc_within(e, area);
            const float gap = area_gap_mm(e, area);
            in_range[a.index] = core::within(gap, 0.0f);
            // An engaged unit stays where it is (it can only Fall Back, 09.07).
            p[a.index] = engaged ? (in_range[a.index] ? 1.0 : 0.0) : p_reaches_area(th, gap);
        }
        one_place(p, in_range);
        for (AreaOutlook& a : o.areas) a.reach.add(oc, p[a.index]);
    }
    return o;
}

ObjectiveTerms objective_terms(const ObjectiveOutlook& outlook, const core::GameState& origin, const core::Unit& us,
                               std::span<const core::Position> ends, double survive) {
    ObjectiveTerms t;
    if (!origin.mission) return t;
    const double s = std::clamp(survive, 0.0, 1.0);
    for (const AreaOutlook& a : outlook.areas) {
        const int oc = oc_within(us, ends, origin.mission->objectives[a.index]);
        if (oc == 0) continue;
        if (outlook.next.theirs && a.them_now > 0) {
            // 14.02 at the end of our turn: they control it while their level is higher than ours.
            const double without = a.them_now > a.others ? 1.0 : 0.0;
            const double with = a.them_now > a.others + oc ? 1.0 : 0.0;
            t.deny += a.value_them * (without - with);
        }
        if (outlook.next.ours) {
            const double others = static_cast<double>(a.others);
            t.score += a.value_us * (a.reach.p_below(others + s * static_cast<double>(oc)) - a.reach.p_below(others));
        }
    }
    return t;
}

}  // namespace wh40k::ai::normal
