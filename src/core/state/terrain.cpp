#include "core/state/terrain.hpp"

#include <cmath>
#include <string>

#include "core/data/load_error.hpp"
#include "core/state/game_state.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core {

namespace {

[[noreturn]] void fail(std::string_view where, const std::string& what) {
    throw DataError(std::string(where) + ": D-087: " + what);
}

bool finite(const Position& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

void check_shape(const TerrainShape& shape, std::string_view where, const std::string& label) {
    if (shape.is_circle()) {
        if (!finite(shape.centre) || shape.centre.z != 0.0f) fail(where, label + " has a circle centre that isn't on the battlefield plane");
        if (!(shape.diameter_mm > 0.0f) || !std::isfinite(shape.diameter_mm)) {
            fail(where, label + " needs a circle diameter greater than 0");
        }
        return;
    }
    if (shape.diameter_mm != 0.0f) fail(where, label + " has both a polygon and a circle diameter");
    if (shape.vertices.size() < kMinPolygonVertices || shape.vertices.size() > kMaxPolygonVertices) {
        fail(where, label + " has " + std::to_string(shape.vertices.size()) + " vertices; a polygon has " +
                        std::to_string(kMinPolygonVertices) + ".." + std::to_string(kMaxPolygonVertices));
    }
    for (const Position& p : shape.vertices) {
        if (!finite(p) || p.z != 0.0f) fail(where, label + " has a vertex that isn't on the battlefield plane");
    }
    if (!is_convex_ccw(shape.vertices)) {
        fail(where, label + " is not a convex polygon with its vertices counter-clockwise (non-convex pieces are two "
                            "features or areas)");
    }
}

}  // namespace

std::string_view terrain_category_name(TerrainCategory c) {
    switch (c) {
        case TerrainCategory::Exposed:
            return "exposed";
        case TerrainCategory::Light:
            return "light";
        case TerrainCategory::Dense:
            return "dense";
    }
    return "exposed";
}

std::optional<TerrainCategory> terrain_category_from_name(std::string_view name) {
    for (const TerrainCategory c : {TerrainCategory::Exposed, TerrainCategory::Light, TerrainCategory::Dense}) {
        if (terrain_category_name(c) == name) return c;
    }
    return std::nullopt;
}

void check_terrain_data(const GameState& state, std::string_view where) {
    if (state.terrain.size() > kMaxTerrainAreas) {
        fail(where, "at most " + std::to_string(kMaxTerrainAreas) + " terrain areas, not " +
                        std::to_string(state.terrain.size()));
    }
    const Rect battlefield{0.0f, 0.0f, state.battlefield.width_mm, state.battlefield.depth_mm};
    std::vector<std::string> linked;
    for (const TerrainArea& area : state.terrain) {
        if (area.name.empty()) fail(where, "a terrain area needs a name");
        const std::string label = "terrain area '" + area.name + "'";
        check_shape(area.shape, where, label);
        if (!shape_wholly_within(area.shape, battlefield)) fail(where, label + " is not wholly on the battlefield");
        if (area.features.empty() || area.features.size() > kMaxTerrainFeatures) {
            fail(where, label + " has " + std::to_string(area.features.size()) + " terrain features; an area has 1.." +
                            std::to_string(kMaxTerrainFeatures));
        }
        if (area.objective) {
            // 14.01: the terrain area is the objective.
            if (!state.mission) fail(where, label + " is linked to objective '" + *area.objective + "' without a mission");
            const ObjectiveArea* objective = nullptr;
            for (const ObjectiveArea& o : state.mission->objectives) {
                if (o.name == *area.objective) objective = &o;
            }
            if (objective == nullptr) fail(where, label + " is linked to an unknown objective '" + *area.objective + "'");
            for (const std::string& l : linked) {
                if (l == *area.objective) fail(where, "objective '" + l + "' is linked to two terrain areas");
            }
            linked.push_back(*area.objective);
            if (!(area.shape == circle_shape(objective->centre, objective->diameter_mm))) {
                fail(where, label + " must be objective '" + *area.objective + "''s circle");
            }
        }
        for (const TerrainFeature& f : area.features) {
            if (f.name.empty()) fail(where, "a terrain feature of " + label + " needs a name");
            const std::string flabel = "terrain feature '" + f.name + "' of " + label;
            check_shape(f.shape, where, flabel);
            if (!shape_wholly_within(f.shape, area.shape)) fail(where, flabel + " is not wholly within its area");
            if (f.category == TerrainCategory::Dense) {
                if (!f.height_mm || !(*f.height_mm > 0.0f) || !std::isfinite(*f.height_mm)) {
                    fail(where, flabel + " is dense and needs a height greater than 0");
                }
            } else if (f.height_mm) {
                fail(where, flabel + " has a height; only dense terrain features have one (13.06)");
            }
        }
    }
    // D-087: no model is set up on a Light or Dense feature (03.01 Ending a Move, 13.06: ground level only in 2D).
    for (const Unit& u : state.units) {
        for (const Model& m : u.models) {
            for (const TerrainArea& area : state.terrain) {
                for (const TerrainFeature& f : area.features) {
                    if (is_solid(f.category) && overlaps_shape(pose_of(m), f.shape)) {
                        fail(where, "model " + std::to_string(m.id.v) + " of unit " + std::to_string(u.id.v) + " '" +
                                        u.name + "' is set up on terrain feature '" + f.name + "' (" +
                                        std::string(terrain_category_name(f.category)) + ")");
                    }
                }
            }
        }
    }
}

}  // namespace wh40k::core
