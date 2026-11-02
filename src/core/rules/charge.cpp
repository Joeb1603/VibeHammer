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

// 01.04 / D-009: edge-to-edge distance from one model (at `pos`) to the closest model of `target`.
std::optional<float> distance_to_unit(const Model& model, const Position& pos, const Unit& target) {
    const PlacedModel from{model.id, pos, model.base};
    const auto targets = placed_models(target);
    return closest_distance(std::span<const PlacedModel>(&from, 1), targets);
}

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
    for (const UnitId id : targets) {
        const Unit* target = state.find_unit(id);
        if (target == nullptr || is_destroyed(*target)) {
            continue;
        }
        const auto before = distance_to_unit(model, start, *target);
        const auto after = distance_to_unit(model, end, *target);
        if (before && after && more_than(*before, *after)) {
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
