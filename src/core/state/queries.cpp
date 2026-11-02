#include "core/state/queries.hpp"

#include "core/geometry/closest.hpp"
#include "core/geometry/engagement.hpp"

namespace wh40k::core {

std::vector<PlacedModel> placed_models(const Unit& unit) {
    std::vector<PlacedModel> out;
    out.reserve(unit.models.size());
    for (const Model& m : unit.models) {
        out.push_back(PlacedModel{m.id, m.position, m.base});
    }
    return out;
}

std::vector<PlacedModel> enemy_models(const GameState& state, Side side) {
    std::vector<PlacedModel> out;
    for (const Unit& u : state.units) {
        if (u.side != side) {
            const auto models = placed_models(u);
            out.insert(out.end(), models.begin(), models.end());
        }
    }
    return out;
}

CoherencyResult unit_coherency(const Unit& unit) {
    const auto models = placed_models(unit);
    return check_coherency(models);
}

std::vector<UnitId> engaged_enemy_units(const GameState& state, const Unit& unit) {
    std::vector<UnitId> out;
    const auto mine = placed_models(unit);
    for (const Unit& other : state.units) {
        if (other.side == unit.side) {
            continue;
        }
        const auto theirs = placed_models(other);
        if (units_engaged(mine, theirs)) {
            out.push_back(other.id);
        }
    }
    return out;
}

bool is_engaged(const GameState& state, const Unit& unit) {
    return !engaged_enemy_units(state, unit).empty();
}

std::optional<float> unit_distance(const Unit& a, const Unit& b) {
    const auto ma = placed_models(a);
    const auto mb = placed_models(b);
    return closest_distance(ma, mb);
}

}  // namespace wh40k::core
