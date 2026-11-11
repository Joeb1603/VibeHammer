#include "deploy_policy.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>
#include <variant>

#include "analysis/threat.hpp"
#include "bounds.hpp"
#include "core/geometry/units.hpp"
#include "core/state/deployment.hpp"
#include "simulator.hpp"
#include "unit_style.hpp"

namespace wh40k::ai {

std::optional<PolicyResult> deploy_policy(const core::GameState& state, core::Side side,
                                          const std::vector<core::GameAction>& legal) {
    if (!state.pending || !std::holds_alternative<core::decision::DeployUnits>(state.pending->kind)) {
        return std::nullopt;
    }
    // The lowest-id unit of `side` waiting to be set up (GameState::undeployed is in id order).
    std::optional<core::UnitId> lowest;
    for (const core::Unit& u : state.undeployed) {
        if (u.side == side) {
            lowest = u.id;
            break;
        }
    }
    if (!lowest) return std::nullopt;
    const std::string name = client::unit_display_name(state, *lowest);
    for (const core::GameAction& a : legal) {
        if (core::is_template(a)) continue;
        if (const auto* d = std::get_if<core::action::DeployUnit>(&a); d != nullptr && d->unit == *lowest) {
            return PolicyResult{a, "D-081: set up " + name + " where Auto-place puts it", {}};
        }
        if (const auto* c = std::get_if<core::action::CannotSetUp>(&a); c != nullptr && c->unit == *lowest) {
            return PolicyResult{a, "D-081: " + name + " can't be set up: no legal spot is left, so it counts as destroyed",
                                {}};
        }
    }
    return std::nullopt;
}

// ---- M77: the heuristic ----

namespace {

core::Side other(core::Side s) { return s == core::Side::A ? core::Side::B : core::Side::A; }

std::optional<core::Position> centroid(const std::vector<core::Model>& models) {
    if (models.empty()) return std::nullopt;
    double x = 0.0;
    double y = 0.0;
    for (const core::Model& m : models) {
        x += m.position.x;
        y += m.position.y;
    }
    const double n = static_cast<double>(models.size());
    return core::Position{static_cast<float>(x / n), static_cast<float>(y / n), 0.0f};
}

core::Position centroid(const core::action::DeployUnit& a) {
    double x = 0.0;
    double y = 0.0;
    for (const core::ModelPlacement& p : a.models) {
        x += p.position.x;
        y += p.position.y;
    }
    const double n = static_cast<double>(std::max<std::size_t>(a.models.size(), 1));
    return core::Position{static_cast<float>(x / n), static_cast<float>(y / n), 0.0f};
}

const core::Model* model_of(const core::Unit& unit, core::ModelId id) {
    for (const core::Model& m : unit.models) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

// Depth steps: 0, step, 2 step, ... up to `room` (mm, >= 0), and `room` itself when `exact` and not already a step.
std::vector<float> depth_steps(float room, float max_mm, bool exact) {
    std::vector<float> out{0.0f};
    const float step = core::inches_to_mm(kDeployStepIn);
    const float limit = std::min(room, max_mm);
    for (int k = 1; static_cast<float>(k) * step <= limit + core::EPS_MM; ++k) out.push_back(static_cast<float>(k) * step);
    if (exact && room > core::EPS_MM && room - out.back() > core::EPS_MM) out.push_back(room);
    return out;
}

const char* role_text(DeployRole r) {
    switch (r) {
        case DeployRole::LongRangeShooter: return "a long-range shooter: set up early and back";
        case DeployRole::Melee: return "melee-oriented: set up late and forward";
        case DeployRole::Other: return "neither a long-range shooter nor melee-oriented";
    }
    return "";
}

std::string inches_text(float mm) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g\"", std::round(static_cast<double>(core::mm_to_inches(mm)) * 100.0) / 100.0);
    return buf;
}

// "2" east and 1" forward of" / "at": how `chosen` stands relative to `proposal` (the same models, translated).
std::string shift_text(const core::GameState& state, core::Side side, const core::action::DeployUnit& proposal,
                       const core::action::DeployUnit& chosen) {
    if (chosen == proposal || chosen.models.empty()) return "at";
    const float dx = chosen.models.front().position.x - proposal.models.front().position.x;
    float dy = chosen.models.front().position.y - proposal.models.front().position.y;
    if (state.side(side).deployment_zone.edge == core::BattlefieldEdge::North) dy = -dy;  // forward: toward the enemy
    std::string out;
    if (std::abs(dx) > core::EPS_MM) out += inches_text(std::abs(dx)) + (dx < 0.0f ? " west" : " east");
    if (std::abs(dy) > core::EPS_MM) {
        if (!out.empty()) out += " and ";
        out += inches_text(std::abs(dy)) + (dy < 0.0f ? " back" : " forward");
    }
    return out + " of";
}

}  // namespace

DeployAssessment deploy_assessment(analysis::OddsCache& cache, const core::GameState& state, const core::Unit& unit) {
    DeployAssessment out;
    out.max_range_mm = analysis::threat_ranges(unit).max_range_mm;
    double ranged = 0.0;
    double melee = 0.0;
    const core::Side enemy_side = other(unit.side);
    const auto add = [&](const core::Unit& e) {
        if (e.side != enemy_side || e.models.empty()) return;
        const analysis::UnitRole r = analysis::unit_role(cache, unit, e);
        // Each enemy model destroyed counts its share of the unit's points (D-076), so a lascannon's monster kill
        // weighs what it is worth; without points every model counts 1.
        const double weight = e.points > 0 ? static_cast<double>(e.points) / static_cast<double>(e.models.size()) : 1.0;
        ranged += r.ranged * weight;
        melee += r.melee * weight;
    };
    for (const core::Unit& e : state.units) add(e);
    for (const core::Unit& e : state.undeployed) add(e);
    out.ranged_share = ranged + melee > 0.0 ? ranged / (ranged + melee) : 0.5;
    if (out.ranged_share < 0.5 - kDeployLeanMargin) {
        out.role = DeployRole::Melee;
    } else if (out.ranged_share > 0.5 + kDeployLeanMargin &&
               out.max_range_mm >= core::inches_to_mm(kLongRangeIn) - core::EPS_MM) {
        out.role = DeployRole::LongRangeShooter;
    }
    return out;
}

std::vector<core::UnitId> deploy_order(const core::GameState& state, core::Side side) {
    analysis::OddsCache cache;
    std::vector<std::pair<DeployRole, core::UnitId>> keyed;
    for (const core::Unit& u : state.undeployed) {
        if (u.side == side) keyed.emplace_back(deploy_assessment(cache, state, u).role, u.id);
    }
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<core::UnitId> out;
    out.reserve(keyed.size());
    for (const auto& k : keyed) out.push_back(k.second);
    return out;
}

std::optional<core::Position> enemy_deployed_centre(const core::GameState& state, core::Side side) {
    double x = 0.0;
    double y = 0.0;
    int n = 0;
    for (const core::Unit& u : state.units) {
        if (u.side == side) continue;
        if (const std::optional<core::Position> c = centroid(u.models)) {
            x += c->x;
            y += c->y;
            ++n;
        }
    }
    if (n == 0) return std::nullopt;
    return core::Position{static_cast<float>(x / n), static_cast<float>(y / n), 0.0f};
}

std::vector<core::action::DeployUnit> deploy_candidates(const core::GameState& state,
                                                        const core::action::DeployUnit& proposal, DeployRole role,
                                                        float max_range_mm, const core::Position& enemy) {
    const core::Unit* unit = core::find_undeployed(state, proposal.unit);
    if (unit == nullptr || proposal.models.empty()) return {proposal};
    const core::DeploymentZone& zone = state.side(unit->side).deployment_zone;
    if (zone.edge != core::BattlefieldEdge::North && zone.edge != core::BattlefieldEdge::South) return {proposal};
    const core::Rect zr = core::zone_rect(state.battlefield, zone);

    // The formation's extents (each base by its half extents at its facing).
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_y = 0.0f;
    float max_y = 0.0f;
    bool first = true;
    for (const core::ModelPlacement& p : proposal.models) {
        const core::Model* m = model_of(*unit, p.model);
        if (m == nullptr) return {proposal};
        const core::BaseExtents e = edge_extents(m->base, p.facing_deg);
        const float lo_x = p.position.x - e.half_x_mm;
        const float hi_x = p.position.x + e.half_x_mm;
        const float lo_y = p.position.y - e.half_y_mm;
        const float hi_y = p.position.y + e.half_y_mm;
        min_x = first ? lo_x : std::min(min_x, lo_x);
        max_x = first ? hi_x : std::max(max_x, hi_x);
        min_y = first ? lo_y : std::min(min_y, lo_y);
        max_y = first ? hi_y : std::max(max_y, hi_y);
        first = false;
    }
    const bool south = zone.edge == core::BattlefieldEdge::South;
    const float forward_sign = south ? 1.0f : -1.0f;  // toward the enemy
    const float forward_room = std::max(0.0f, south ? zr.max_y - max_y : min_y - zr.min_y);
    const float back_room = std::max(0.0f, south ? min_y - zr.min_y : zr.max_y - max_y);
    const float west_room = std::max(0.0f, min_x);
    const float east_room = std::max(0.0f, state.battlefield.width_mm - max_x);

    // Depth changes, forward positive.
    std::vector<float> depths;
    switch (role) {
        case DeployRole::LongRangeShooter:
            for (const float d : depth_steps(back_room, core::inches_to_mm(kDeployBackIn), false)) depths.push_back(-d);
            break;
        case DeployRole::Melee:
            depths = depth_steps(forward_room, forward_room, true);
            break;
        case DeployRole::Other:
            depths = {0.0f};
            break;
    }
    std::vector<float> shifts;
    const float step = core::inches_to_mm(kDeployStepIn);
    const int steps = static_cast<int>(std::lround(kDeployNudgeIn / kDeployStepIn));
    for (int k = -steps; k <= steps; ++k) {
        const float dx = static_cast<float>(k) * step;
        if (dx < -west_room - core::EPS_MM || dx > east_room + core::EPS_MM) continue;
        shifts.push_back(dx);
    }

    const core::Position c0 = centroid(proposal);
    struct Candidate {
        std::tuple<int, float, float, float, float, float> key;
        float dx = 0.0f;
        float depth = 0.0f;
    };
    std::vector<Candidate> cands;
    cands.reserve(shifts.size() * depths.size());
    for (const float depth : depths) {
        for (const float dx : shifts) {
            const core::Position c{c0.x + dx, c0.y + forward_sign * depth, 0.0f};
            const float x_gap = std::abs(c.x - enemy.x);
            const float dist = core::centre_distance(c, enemy);
            Candidate cand;
            cand.dx = dx;
            cand.depth = depth;
            switch (role) {
                case DeployRole::Melee:
                    cand.key = {0, dist, std::abs(dx), dx, -depth, 0.0f};
                    break;
                case DeployRole::LongRangeShooter: {
                    const bool in_range = dist <= max_range_mm + core::EPS_MM;
                    // In range: the furthest back first (depth is <= 0, so ascending); out of range: the least far back.
                    cand.key = {in_range ? 0 : 1, in_range ? depth : -depth, x_gap, std::abs(dx), dx, 0.0f};
                    break;
                }
                case DeployRole::Other:
                    cand.key = {0, x_gap, std::abs(dx), dx, std::abs(depth), 0.0f};
                    break;
            }
            cands.push_back(cand);
        }
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Candidate& a, const Candidate& b) { return a.key < b.key; });
    std::vector<core::action::DeployUnit> out;
    out.reserve(cands.size());
    for (const Candidate& cand : cands) {
        core::action::DeployUnit a = proposal;
        if (cand.dx != 0.0f || cand.depth != 0.0f) {
            for (core::ModelPlacement& p : a.models) {
                p.position.x += cand.dx;
                p.position.y += forward_sign * cand.depth;
            }
        }
        out.push_back(std::move(a));
    }
    return out;
}

std::optional<PolicyResult> heuristic_deploy_policy(const core::GameState& state, core::Side side,
                                                    const std::vector<core::GameAction>& legal) {
    if (!state.pending || !std::holds_alternative<core::decision::DeployUnits>(state.pending->kind)) {
        return std::nullopt;
    }
    analysis::OddsCache cache;
    // The order: by role, then by id (deploy_order, with the assessments kept for the chosen unit).
    std::optional<std::pair<DeployRole, core::UnitId>> next;
    DeployAssessment chosen_role;
    for (const core::Unit& u : state.undeployed) {
        if (u.side != side) continue;
        const DeployAssessment a = deploy_assessment(cache, state, u);
        if (!next || a.role < next->first) {
            next = std::pair{a.role, u.id};
            chosen_role = a;
        }
    }
    if (!next) return std::nullopt;
    const core::UnitId unit = next->second;
    const std::string name = client::unit_display_name(state, unit);
    const std::string role = role_text(chosen_role.role);

    const core::action::DeployUnit* proposal = nullptr;
    for (const core::GameAction& a : legal) {
        if (core::is_template(a)) continue;
        if (const auto* d = std::get_if<core::action::DeployUnit>(&a); d != nullptr && d->unit == unit) {
            proposal = d;
            break;
        }
        if (const auto* c = std::get_if<core::action::CannotSetUp>(&a); c != nullptr && c->unit == unit) {
            // The engine finds no legal spot for it, so no translation of a spot can be tried.
            return PolicyResult{a,
                                "D-081 (M77): " + name + " (" + role +
                                    ") can't be set up: no legal spot is left, so it counts as destroyed",
                                {}};
        }
    }
    if (proposal == nullptr) return std::nullopt;

    const std::optional<core::Position> enemy = enemy_deployed_centre(state, side);
    if (!enemy) {
        return PolicyResult{*proposal,
                            "D-081 (M77): set up " + name + " (" + role +
                                ") where Auto-place puts it: no enemy unit is set up yet",
                            {}};
    }

    // The side's other units that have a legal spot now: a nudge must leave each of them one.
    std::vector<core::UnitId> placeable;
    for (const core::GameAction& a : legal) {
        if (core::is_template(a)) continue;
        if (const auto* d = std::get_if<core::action::DeployUnit>(&a); d != nullptr && d->unit != unit) {
            placeable.push_back(d->unit);
        }
    }
    const auto leaves_room = [&](const core::action::DeployUnit& a) {
        if (placeable.empty()) return true;
        Simulator sim(state, decision_seed(state));
        if (!sim.step(side, a).ok()) return false;
        const std::vector<core::SetUpProposal> after = core::deployment_proposals(sim.state(), side);
        for (const core::UnitId id : placeable) {
            const auto it = std::find_if(after.begin(), after.end(),
                                         [&](const core::SetUpProposal& p) { return p.unit == id; });
            if (it == after.end() || !it->action) return false;
        }
        return true;
    };

    constexpr int kMaxRoomChecks = 3;  // nudges simulated before settling for the proposal
    int room_checks = 0;
    for (const core::action::DeployUnit& cand :
         deploy_candidates(state, *proposal, chosen_role.role, chosen_role.max_range_mm, *enemy)) {
        if (cand == *proposal) break;  // the proposal: legal by construction (below)
        if (!core::set_up_problems(state, side, cand).empty()) continue;
        if (room_checks >= kMaxRoomChecks) break;
        ++room_checks;
        if (!leaves_room(cand)) continue;
        return PolicyResult{core::GameAction{cand},
                            "D-081 (M77): set up " + name + " (" + role + ") " + shift_text(state, side, *proposal, cand) +
                                " where Auto-place puts it, to face the enemy's set-up units",
                            {}};
    }
    return PolicyResult{*proposal,
                        "D-081 (M77): set up " + name + " (" + role +
                            ") where Auto-place puts it: no better legal spot nearby",
                        {}};
}

}  // namespace wh40k::ai
