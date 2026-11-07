#include "core/state/queries.hpp"

#include "core/geometry/closest.hpp"
#include "core/geometry/distance.hpp"
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

namespace {

std::vector<PlacedModel> models_where(const GameState& state, bool (*keep)(Side, Side), Side side) {
    std::vector<PlacedModel> out;
    for (const Unit& u : state.units) {
        if (keep(u.side, side)) {
            const auto models = placed_models(u);
            out.insert(out.end(), models.begin(), models.end());
        }
    }
    return out;
}

}  // namespace

std::vector<PlacedModel> enemy_models(const GameState& state, Side side) {
    return models_where(state, [](Side u, Side s) { return u != s; }, side);
}

std::vector<PlacedModel> friendly_models(const GameState& state, Side side) {
    return models_where(state, [](Side u, Side s) { return u == s; }, side);
}

CoherencyResult unit_coherency(const Unit& unit) {
    const auto models = placed_models(unit);
    return check_coherency(models);
}

namespace {

// 03.04: units_engaged(placed_models(a), placed_models(b)) without building the lists (M54).
bool engaged_units(const Unit& a, const Unit& b) {
    for (const Model& ma : a.models) {
        const PlacedModel pa{ma.id, ma.position, ma.base};
        for (const Model& mb : b.models) {
            if (models_engaged(pa, PlacedModel{mb.id, mb.position, mb.base})) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

std::vector<UnitId> engaged_enemy_units(const GameState& state, const Unit& unit) {
    std::vector<UnitId> out;
    for (const Unit& other : state.units) {
        if (other.side == unit.side) {
            continue;
        }
        if (engaged_units(unit, other)) {
            out.push_back(other.id);
        }
    }
    return out;
}

bool is_engaged(const GameState& state, const Unit& unit) {
    // M54: the first engaged enemy unit is enough (the same tests as engaged_enemy_units).
    for (const Unit& other : state.units) {
        if (other.side != unit.side && engaged_units(unit, other)) {
            return true;
        }
    }
    return false;
}

std::optional<float> model_distance_to_unit(const Model& model, const Position& pos, const Unit& target) {
    // closest_distance({model at pos}, placed_models(target)) without building the list (M54): the least
    // base_distance (the same value whatever the order).
    std::optional<float> best;
    for (const Model& t : target.models) {
        const float d = base_distance(pos, model.base, t.position, t.base);
        if (!best || d < *best) {
            best = d;
        }
    }
    return best;
}

std::optional<float> unit_distance(const Unit& a, const Unit& b) {
    // closest_distance(placed_models(a), placed_models(b)) without building the lists (M54).
    std::optional<float> best;
    for (const Model& ma : a.models) {
        for (const Model& mb : b.models) {
            const float d = base_distance(ma.position, ma.base, mb.position, mb.base);
            if (!best || d < *best) {
                best = d;
            }
        }
    }
    return best;
}

}  // namespace wh40k::core
