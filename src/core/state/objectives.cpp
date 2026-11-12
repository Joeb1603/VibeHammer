#include "core/state/objectives.hpp"

#include <cstdint>
#include <string>

#include "core/data/load_error.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/oval.hpp"
#include "core/geometry/pose.hpp"
#include "core/rules/leadership.hpp"
#include "core/state/points_destroyed.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core {

namespace {

[[noreturn]] void fail(std::string_view where, const std::string& what) {
    throw DataError(std::string(where) + ": D-084: " + what);
}

BasePose area_pose(const ObjectiveArea& area) {
    return BasePose{Position{area.centre.x, area.centre.y, 0.0f}, Base{BaseShape::Round, area.diameter_mm, 0.0f}, 0.0f};
}

}  // namespace

float objective_gap_mm(const BasePose& pose, const ObjectiveArea& area) {
    return base_distance(pose, area_pose(area));
}

bool within_objective(const BasePose& pose, const ObjectiveArea& area) {
    // D-009: any part of the base within the area, i.e. the closest parts of the base and the area at most 0 apart.
    return within(objective_gap_mm(pose, area), 0.0f);
}

bool model_within_objective(const Model& model, const ObjectiveArea& area) {
    return within_objective(pose_of(model), area);
}

std::array<int, 2> level_of_control(const GameState& state, const ObjectiveArea& area) {
    std::array<int, 2> levels{};
    for (const Unit& u : state.units) {
        for (const Model& m : u.models) {
            const std::optional<int> oc = objective_control(u, m);  // 01.07: '-' while battle-shocked
            if (!oc || *oc <= 0) continue;                         // 02.02: '-' cannot control objectives
            if (model_within_objective(m, area)) levels[side_index(u.side)] += *oc;
        }
    }
    return levels;
}

ControlOutcome determine_control(const ObjectiveArea& area, const std::array<int, 2>& levels, bool end_of_phase) {
    if (area.secured_by) {
        // 14.03: it remains under their control until their opponent's level is greater at the end of a phase.
        const Side owner = *area.secured_by;
        const Side other = opponent(owner);
        if (end_of_phase && levels[side_index(other)] > levels[side_index(owner)]) {
            return ControlOutcome{other, std::nullopt};
        }
        return ControlOutcome{owner, owner};
    }
    // 14.02: the higher level controls it; equal levels (0 and 0 included): neither player.
    const int a = levels[side_index(Side::A)];
    const int b = levels[side_index(Side::B)];
    if (a > b) return ControlOutcome{Side::A, std::nullopt};
    if (b > a) return ControlOutcome{Side::B, std::nullopt};
    return ControlOutcome{std::nullopt, std::nullopt};
}

std::vector<UnitId> controlling_units(const GameState& state, std::size_t index) {
    std::vector<UnitId> out;
    if (!state.mission || index >= state.mission->objectives.size()) return out;
    const ObjectiveArea& area = state.mission->objectives[index];
    if (!area.controller) return out;
    for (const Unit& u : state.units) {
        if (u.side != *area.controller) continue;
        bool in_range = false;
        bool has_oc = false;
        for (const Model& m : u.models) {
            if (model_within_objective(m, area)) in_range = true;
            const std::optional<int> oc = objective_control(u, m);
            if (oc && *oc >= 1) has_oc = true;
        }
        if (in_range && has_oc) out.push_back(u.id);
    }
    return out;
}

std::array<int, 2> kill_vp(const GameState& state) {
    std::array<int, 2> out{};
    if (!state.mission) return out;
    const Mission& m = *state.mission;
    const PointsTally tally = points_destroyed(state);
    for (std::size_t i = 0; i < 2; ++i) {
        // floor(floor(x / D) / k) == floor(x / (D k)) for positive integers, without forming D x k.
        const std::int64_t whole_points = tally.destroyed[i] / tally.denominator;
        const std::int64_t steps = whole_points / m.kill_points;
        out[i] = static_cast<int>(steps * m.kill_vp);  // bounded by check_mission_data
    }
    return out;
}

VpTally vp_tally(const GameState& state) {
    VpTally t;
    t.objectives = state.mission->objective_vp;
    t.kills = kill_vp(state);
    for (std::size_t i = 0; i < 2; ++i) t.total[i] = t.objectives[i] + t.kills[i];
    return t;
}

void check_mission_data(const GameState& state, std::string_view where) {
    const bool rule = state.result_rule == ResultRule::Mission;
    if (rule != state.mission.has_value()) {
        fail(where, rule ? "result \"mission\" needs a \"mission\" block"
                         : "a \"mission\" block needs result \"mission\"");
    }
    if (!state.mission) return;
    const Mission& m = *state.mission;
    if (m.first_scoring_round < 1 || m.first_scoring_round > state.battle_rounds) {
        fail(where, "first_scoring_round " + std::to_string(m.first_scoring_round) + " is not a battle round (1.." +
                        std::to_string(state.battle_rounds) + ")");
    }
    if (m.kill_points < 1 || m.kill_points > kMaxKillPoints) {
        fail(where, "kill_vp points " + std::to_string(m.kill_points) + " must be 1.." + std::to_string(kMaxKillPoints));
    }
    if (m.kill_vp < 0 || m.kill_vp > kMaxKillVp) {
        fail(where, "kill_vp vp " + std::to_string(m.kill_vp) + " must be 0.." + std::to_string(kMaxKillVp));
    }
    if (m.objectives.empty() || m.objectives.size() > kMaxObjectives) {
        fail(where, "a mission has 1.." + std::to_string(kMaxObjectives) + " objectives, not " +
                        std::to_string(m.objectives.size()));
    }
    for (const ObjectiveArea& a : m.objectives) {
        const std::string label = "objective '" + a.name + "'";
        if (a.name.empty()) fail(where, "an objective needs a name");
        if (!(a.diameter_mm > 0.0f)) fail(where, label + " needs a diameter greater than 0");
        const float r = a.diameter_mm * 0.5f;
        if (a.centre.x - r < -EPS_MM || a.centre.y - r < -EPS_MM || a.centre.x + r > state.battlefield.width_mm + EPS_MM ||
            a.centre.y + r > state.battlefield.depth_mm + EPS_MM) {
            fail(where, label + " is not wholly within the battlefield");
        }
        for (const int vp : a.vp) {
            if (vp < 0 || vp > kMaxObjectiveVp) {
                fail(where, label + " has VP " + std::to_string(vp) + "; VP must be 0.." + std::to_string(kMaxObjectiveVp));
            }
        }
    }
    for (const int vp : m.objective_vp) {
        if (vp < 0) fail(where, "objective VP scored can't be negative (" + std::to_string(vp) + ")");
    }
    // Every total stays far below INT_MAX: at most kMaxObjectives x kMaxObjectiveVp per scoring (32,000) over the
    // battle rounds, and kill VP at most (the sum of every unit's points / kill_points) x kill_vp.
    std::int64_t points = 0;
    for (const Unit& u : state.units) points += u.points;
    for (const Unit& u : state.undeployed) points += u.points;
    const std::int64_t kill_max = (points / m.kill_points) * m.kill_vp;
    const std::int64_t scoring_max = std::int64_t{state.battle_rounds} * static_cast<std::int64_t>(m.objectives.size()) *
                                     kMaxObjectiveVp;
    if (kill_max + scoring_max + m.objective_vp[0] + m.objective_vp[1] > std::int64_t{1} << 30) {
        fail(where, "the mission's VP could pass 2^30");
    }
}

}  // namespace wh40k::core
