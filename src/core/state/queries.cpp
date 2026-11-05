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

const Model* find_model(const Unit& unit, ModelId id) {
    for (const Model& m : unit.models) {
        if (m.id == id) {
            return &m;
        }
    }
    return nullptr;
}

Model* find_model(Unit& unit, ModelId id) {
    for (Model& m : unit.models) {
        if (m.id == id) {
            return &m;
        }
    }
    return nullptr;
}

const Unit* find_unit_of_model(const GameState& state, ModelId id) {
    for (const Unit& u : state.units) {
        if (find_model(u, id) != nullptr) {
            return &u;
        }
    }
    return nullptr;
}

const Model* find_model(const GameState& state, ModelId id) {
    const Unit* u = find_unit_of_model(state, id);
    return u == nullptr ? nullptr : find_model(*u, id);
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

std::optional<float> model_distance_to_unit(const Model& model, const Position& pos, const Unit& target) {
    const PlacedModel from{model.id, pos, model.base};
    const auto targets = placed_models(target);
    return closest_distance(std::span<const PlacedModel>(&from, 1), targets);
}

std::optional<float> unit_distance(const Unit& a, const Unit& b) {
    const auto ma = placed_models(a);
    const auto mb = placed_models(b);
    return closest_distance(ma, mb);
}

}  // namespace wh40k::core
