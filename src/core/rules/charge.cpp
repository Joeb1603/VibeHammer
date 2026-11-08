#include "core/rules/charge.hpp"

#include <algorithm>

#include "core/geometry/closest.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/units.hpp"
#include "core/model/keywords.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core {

namespace {

std::string unit_label(UnitId id) { return "unit " + std::to_string(id.v); }


bool contains(std::span<const UnitId> ids, UnitId id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

}  // namespace

std::optional<std::string> declare_charge_problem(const GameState& state, const Unit& unit) {
    const std::string who = unit_label(unit.id);
    if (is_destroyed(unit)) {
        return "11.02: " + who + " is not on the battlefield";
    }
    if (unit.phase_flags.declared_charge) {
        return "11.02: " + who + " has already declared a charge this phase";
    }
    if (is_engaged(state, unit)) {
        return "11.02: " + who + " is engaged and cannot declare a charge";
    }
    if (unit.turn_flags.advanced) {
        return "11.02: " + who + " made an advance move this turn and cannot declare a charge";
    }
    if (unit.turn_flags.fell_back) {
        return "11.02: " + who + " made a fall-back move this turn and cannot declare a charge";
    }
    for (const Unit& other : state.units) {
        if (other.side == unit.side || is_destroyed(other)) {
            continue;
        }
        if (const auto d = unit_distance(unit, other); d && within(*d, inches_to_mm(kChargeRangeInches))) {
            return std::nullopt;
        }
    }
    return "11.02: " + who + " is not within 12\" of any enemy unit";
}

std::vector<UnitId> eligible_charge_targets(const GameState& state, const Unit& unit, int charge_roll) {
    std::vector<UnitId> out;
    const float limit = inches_to_mm(std::min(kChargeRangeInches, static_cast<float>(charge_roll)));
    for (const Unit& other : state.units) {
        if (other.side == unit.side || is_destroyed(other)) {
            continue;
        }
        // 11.04 Before Moving: within 12" and within the maximum distance (the charge roll).
        if (const auto d = unit_distance(unit, other); d && within(*d, limit)) {
            out.push_back(other.id);
        }
    }
    return out;
}

bool ends_closer_to_a_charge_target(const GameState& state, const Model& model, const Position& start,
                                    const Position& end, std::span<const UnitId> targets) {
    return ends_closer_to_a_charge_target(state, BasePose{start, model.base, model.facing_deg},
                                          BasePose{end, model.base, model.facing_deg}, targets);
}

bool ends_closer_to_unit(const BasePose& start, const BasePose& end, const Unit& target) {
    const auto before = model_distance_to_unit(start, target);
    const auto after = model_distance_to_unit(end, target);
    if (!before || !after || !more_than(*before, *after)) {
        return false;
    }
    // D-074 part 2: a turn does not count as moving closer, so the start is also measured at the end facing (only an
    // oval's distance depends on its facing).
    if (start.base.shape == BaseShape::Round || footprint_turn_deg(start.facing_deg, end.facing_deg) == 0.0) {
        return true;
    }
    const auto turned = model_distance_to_unit(BasePose{start.position, start.base, end.facing_deg}, target);
    return turned && more_than(*turned, *after);
}

bool ends_closer_to_a_charge_target(const GameState& state, const BasePose& start, const BasePose& end,
                                    std::span<const UnitId> targets) {
    for (const UnitId id : targets) {
        const Unit* target = state.find_unit(id);
        if (target == nullptr || is_destroyed(*target)) {
            continue;
        }
        if (ends_closer_to_unit(start, end, *target)) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> charge_after_moving_problems(const GameState& state, const Unit& unit,
                                                      std::span<const UnitId> targets) {
    std::vector<std::string> out;
    const std::vector<UnitId> engaged = engaged_enemy_units(state, unit);
    for (const UnitId t : targets) {
        if (!contains(engaged, t)) {
            out.push_back("11.04: " + unit_label(unit.id) + " must end its charge move engaged with charge target " +
                          unit_label(t));
        }
    }
    for (const UnitId e : engaged) {
        if (!contains(targets, e)) {
            out.push_back("11.04: " + unit_label(unit.id) + " cannot end its charge move engaged with " +
                          unit_label(e) + ", which is not a charge target");
        }
    }
    return out;
}

bool is_fights_first_unit(const Unit& unit) { return !is_destroyed(unit) && unit.turn_flags.fights_first; }

}  // namespace wh40k::core
