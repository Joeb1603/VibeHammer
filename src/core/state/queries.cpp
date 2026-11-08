#include "core/state/queries.hpp"

#include "core/geometry/closest.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/oval.hpp"

namespace wh40k::core {

std::vector<PlacedModel> placed_models(const Unit& unit) {
    std::vector<PlacedModel> out;
    out.reserve(unit.models.size());
    for (const Model& m : unit.models) {
        out.push_back(PlacedModel{m.id, m.position, m.base});
    }
    return out;
}

std::vector<PosedModel> posed_models(const Unit& unit) {
    std::vector<PosedModel> out;
    out.reserve(unit.models.size());
    for (const Model& m : unit.models) {
        out.push_back(posed_of(m));
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

namespace {

std::vector<PosedModel> posed_where(const GameState& state, bool enemies, Side side) {
    std::vector<PosedModel> out;
    for (const Unit& u : state.units) {
        if ((u.side != side) == enemies) {
            for (const Model& m : u.models) out.push_back(posed_of(m));
        }
    }
    return out;
}

}  // namespace

std::vector<PosedModel> posed_enemy_models(const GameState& state, Side side) { return posed_where(state, true, side); }

std::vector<PosedModel> posed_friendly_models(const GameState& state, Side side) {
    return posed_where(state, false, side);
}

CoherencyResult unit_coherency(const Unit& unit) {
    // D-074: measured with each model's facing (round bases: check_coherency's round result, bit-identical).
    const auto models = posed_models(unit);
    return check_coherency(std::span<const PosedModel>(models));
}

namespace {

// 03.04: units_engaged(posed_models(a), posed_models(b)) without building the lists (M54). D-074: each model with its
// facing; a round pair is the old within(base_distance(...), 2") test (inline, bit-identical).
bool engaged_units(const Unit& a, const Unit& b) {
    for (const Model& ma : a.models) {
        const BasePose pa = pose_of(ma);
        for (const Model& mb : b.models) {
            if (within(base_distance(pa, pose_of(mb)), kEngagementRangeMm)) {
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
    return model_distance_to_unit(BasePose{pos, model.base, model.facing_deg}, target);
}

std::optional<float> model_distance_to_unit(const BasePose& from, const Unit& target) {
    // closest_distance({from}, posed_models(target)) without building the list (M54): the least base_distance (the
    // same value whatever the order). D-074: with each base's facing.
    std::optional<float> best;
    for (const Model& t : target.models) {
        const float d = base_distance(from, pose_of(t));
        if (!best || d < *best) {
            best = d;
        }
    }
    return best;
}

std::optional<float> unit_distance(const Unit& a, const Unit& b) {
    // closest_distance(posed_models(a), posed_models(b)) without building the lists (M54).
    std::optional<float> best;
    for (const Model& ma : a.models) {
        const BasePose pa = pose_of(ma);
        for (const Model& mb : b.models) {
            const float d = base_distance(pa, pose_of(mb));
            if (!best || d < *best) {
                best = d;
            }
        }
    }
    return best;
}

}  // namespace wh40k::core
