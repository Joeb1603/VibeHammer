#include "core/data/scenario_loader.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "core/data/load_error.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/units.hpp"

namespace wh40k::core {

using Json = nlohmann::json;

namespace {

constexpr float kEngagementRangeMm = 2.0f * MM_PER_INCH;  // 03.04

[[noreturn]] void fail(const std::string& what) {
    throw DataError("scenario: " + what);
}

const Json& require(const Json& j, const char* key, const std::string& where) {
    if (!j.is_object() || !j.contains(key)) {
        fail(where + ": missing field '" + key + "'");
    }
    return j.at(key);
}

float require_inches(const Json& j, const char* key, const std::string& where) {
    const Json& e = require(j, key, where);
    if (!e.is_number()) {
        fail(where + ": field '" + key + "' must be a number of inches");
    }
    return e.get<float>();
}

Side parse_side(const std::string& s) {
    if (s == "A") return Side::A;
    if (s == "B") return Side::B;
    fail("unknown side '" + s + "'");
}

BattlefieldEdge parse_edge(const std::string& s) {
    if (s == "north") return BattlefieldEdge::North;
    if (s == "south") return BattlefieldEdge::South;
    if (s == "east") return BattlefieldEdge::East;
    if (s == "west") return BattlefieldEdge::West;
    fail("unknown battlefield edge '" + s + "'");
}

std::string side_name(Side s) { return s == Side::A ? "A" : "B"; }

struct Formation {
    int ranks = 1;
    float gap_mm = 0.0f;
    float centre_x_mm = 0.0f;
};

Formation parse_formation(const Json& j, const std::string& where) {
    Formation f;
    const Json& r = require(j, "ranks", where);
    if (!r.is_number_integer() || r.get<int>() < 1) {
        fail(where + ": formation.ranks must be a positive integer");
    }
    f.ranks = r.get<int>();
    f.gap_mm = inches_to_mm(require_inches(j, "gap_in", where));
    f.centre_x_mm = inches_to_mm(require_inches(j, "centre_x_in", where));
    const Json& anchor = require(j, "anchor", where);
    if (anchor != "zone_front") {
        fail(where + ": unsupported formation anchor " + anchor.dump());
    }
    return f;
}

// 02.06 / 02.07: build the unit's models from the scenario's model groups.
std::vector<Model> build_models(const Datasheet& ds, const Json& groups, const std::string& where) {
    std::vector<Model> models;
    std::map<std::string, int> per_type;
    for (const Json& g : groups) {
        const std::string type = require(g, "type", where).get<std::string>();
        const ModelTypeSheet* mt = ds.find_model_type(type);
        if (mt == nullptr) {
            fail(where + ": 02.06: datasheet '" + ds.name + "' has no model type '" + type + "'");
        }
        const Json& count_j = require(g, "count", where);
        if (!count_j.is_number_integer() || count_j.get<int>() < 1) {
            fail(where + ": model group '" + type + "' count must be a positive integer");
        }
        const int count = count_j.get<int>();
        per_type[type] += count;

        std::vector<Weapon> weapons;
        for (const Json& wn : require(g, "weapons", where)) {
            const std::string weapon_name = wn.get<std::string>();
            const Weapon* w = ds.find_weapon(weapon_name);
            if (w == nullptr) {
                fail(where + ": 02.04: datasheet '" + ds.name + "' has no weapon '" + weapon_name + "'");
            }
            if (std::find(mt->weapon_options.begin(), mt->weapon_options.end(), weapon_name) ==
                mt->weapon_options.end()) {
                fail(where + ": 02.07: '" + type + "' cannot be equipped with '" + weapon_name + "'");
            }
            weapons.push_back(*w);
        }

        for (int i = 0; i < count; ++i) {
            Model m;
            m.type_name = type;
            m.characteristics = mt->characteristics;
            m.base = mt->base;
            m.wounds_remaining = mt->characteristics.wounds;
            m.weapons = weapons;
            models.push_back(std::move(m));
        }
    }

    // 02.06 unit composition.
    for (const ModelTypeSheet& mt : ds.model_types) {
        const auto it = per_type.find(mt.name);
        const int n = it == per_type.end() ? 0 : it->second;
        if (n < mt.min || n > mt.max) {
            fail(where + ": 02.06: '" + mt.name + "' count " + std::to_string(n) + " outside " +
                 std::to_string(mt.min) + "-" + std::to_string(mt.max));
        }
    }
    const int total = static_cast<int>(models.size());
    if (total < ds.min_models || total > ds.max_models) {
        fail(where + ": 02.06: unit size " + std::to_string(total) + " outside " +
             std::to_string(ds.min_models) + "-" + std::to_string(ds.max_models));
    }
    return models;
}

// D-008: ranks centred on centre_x, front rank's bases touching the zone's front line, further
// ranks behind it; models fill each rank left to right (low x to high x) in listed order.
void place_formation(std::vector<Model>& models, const Formation& f, const Battlefield& field,
                     const DeploymentZone& zone, const std::string& where) {
    float front_line = 0.0f;
    float inward = 0.0f;  // direction from the front line towards the owning side's edge
    switch (zone.edge) {
        case BattlefieldEdge::South:
            front_line = zone.depth_mm;
            inward = -1.0f;
            break;
        case BattlefieldEdge::North:
            front_line = field.depth_mm - zone.depth_mm;
            inward = 1.0f;
            break;
        case BattlefieldEdge::East:
        case BattlefieldEdge::West:
            fail(where + ": D-008 formation supports north/south deployment zones only");
    }

    const std::size_t n = models.size();
    const std::size_t ranks = static_cast<std::size_t>(f.ranks);
    const std::size_t per_rank = (n + ranks - 1) / ranks;
    float max_diameter = 0.0f;
    for (const Model& m : models) {
        max_diameter = std::max(max_diameter, m.base.diameter_mm);
    }

    for (std::size_t rank = 0; rank * per_rank < n; ++rank) {
        const std::size_t first = rank * per_rank;
        const std::size_t last = std::min(n, first + per_rank);
        float width = f.gap_mm * static_cast<float>(last - first - 1);
        for (std::size_t i = first; i < last; ++i) {
            width += models[i].base.diameter_mm;
        }
        const float rank_centre_y =
            front_line + inward * (max_diameter * 0.5f +
                                   static_cast<float>(rank) * (max_diameter + f.gap_mm));
        float x = f.centre_x_mm - width * 0.5f;
        for (std::size_t i = first; i < last; ++i) {
            const float r = models[i].base.diameter_mm * 0.5f;
            models[i].position = Position{x + r, rank_centre_y, 0.0f};
            x += models[i].base.diameter_mm + f.gap_mm;
        }
    }
}

// 03.02 Set up: unit in coherency (03.03); D-009: wholly within its deployment zone.
void check_set_up(const Unit& unit, const Rect& zone, const std::string& where) {
    std::vector<PlacedModel> placed;
    for (const Model& m : unit.models) {
        placed.push_back(PlacedModel{m.id, m.position, m.base});
    }
    const CoherencyResult coh = check_coherency(placed);
    if (!coh.ok()) {
        std::string msg = where + ": 03.03: unit '" + unit.name + "' is not in coherency:";
        for (const ModelId id : coh.isolated) {
            msg += " model " + std::to_string(id.v) + " is more than 2\" from every other model;";
        }
        for (const ModelId id : coh.too_far) {
            msg += " model " + std::to_string(id.v) + " is more than 9\" from another model;";
        }
        fail(msg);
    }
    for (const Model& m : unit.models) {
        if (!wholly_within(zone, m.position, m.base)) {
            fail(where + ": D-009: model " + std::to_string(m.id.v) + " of '" + unit.name +
                 "' is not wholly within its deployment zone");
        }
    }
}

// 03.02: a set-up unit must be unengaged (03.04: no enemy model within 2").
void check_unengaged(const std::vector<Unit>& units) {
    for (const Unit& a : units) {
        for (const Unit& b : units) {
            if (a.side == b.side || a.side != Side::A) {
                continue;
            }
            for (const Model& ma : a.models) {
                for (const Model& mb : b.models) {
                    if (within(base_distance(ma.position, ma.base, mb.position, mb.base),
                               kEngagementRangeMm)) {
                        fail("03.02: units '" + a.name + "' and '" + b.name +
                             "' are set up engaged (03.04)");
                    }
                }
            }
        }
    }
}

GameState build_game_state_impl(const Json& scenario, const DatasheetResolver& resolve);

}  // namespace

GameState build_game_state(const Json& scenario, const DatasheetResolver& resolve) {
    try {
        return build_game_state_impl(scenario, resolve);
    } catch (const Json::exception& e) {
        // Wrong JSON types etc. are corrupt data too.
        throw DataError(std::string("scenario: ") + e.what());
    }
}

namespace {

GameState build_game_state_impl(const Json& scenario, const DatasheetResolver& resolve) {
    GameState gs;
    gs.scenario_name = require(scenario, "name", "root").get<std::string>();

    const Json& bf = require(scenario, "battlefield", "root");
    gs.battlefield.width_mm = inches_to_mm(require_inches(bf, "width_in", "battlefield"));
    gs.battlefield.depth_mm = inches_to_mm(require_inches(bf, "depth_in", "battlefield"));
    gs.battle_rounds = require(scenario, "battle_rounds", "root").get<int>();
    gs.first_turn = parse_side(require(scenario, "first_turn", "root").get<std::string>());

    const Json& sides = require(scenario, "sides", "root");
    std::uint32_t next_unit = 1;
    std::uint32_t next_model = 1;
    for (const Side side : {Side::A, Side::B}) {
        const std::string sname = side_name(side);
        const Json& sj = require(sides, sname.c_str(), "sides");
        const std::string where = "side " + sname;

        SideInfo& info = gs.sides[side_index(side)];
        info.name = require(sj, "name", where).get<std::string>();
        const Json& dz = require(sj, "deployment_zone", where);
        info.deployment_zone.edge = parse_edge(require(dz, "edge", where).get<std::string>());
        info.deployment_zone.depth_mm = inches_to_mm(require_inches(dz, "depth_in", where));
        const Rect zone = zone_rect(gs.battlefield, info.deployment_zone);

        for (const Json& uj : require(sj, "units", where)) {
            const std::string unit_file = require(uj, "unit_file", where).get<std::string>();
            const Datasheet ds = resolve(unit_file);
            const std::string uwhere = where + " unit '" + ds.name + "'";

            Unit unit;
            unit.id = UnitId{next_unit++};
            unit.side = side;
            unit.name = ds.name;
            unit.faction_keywords = ds.faction_keywords;
            unit.keywords = ds.keywords;
            unit.abilities = ds.abilities;
            unit.models = build_models(ds, require(uj, "models", uwhere), uwhere);
            for (Model& m : unit.models) {
                m.id = ModelId{next_model++};
            }
            unit.starting_strength = static_cast<int>(unit.models.size());

            place_formation(unit.models, parse_formation(require(uj, "formation", uwhere), uwhere),
                            gs.battlefield, info.deployment_zone, uwhere);
            check_set_up(unit, zone, uwhere);
            gs.units.push_back(std::move(unit));
        }
    }
    check_unengaged(gs.units);
    return gs;
}

}  // namespace

GameState load_scenario(const std::filesystem::path& scenario_path,
                        const std::filesystem::path& repo_root) {
    const Json scenario = read_json_file(scenario_path);
    return build_game_state(scenario, [&repo_root](const std::string& unit_file) {
        return load_datasheet(repo_root / unit_file);
    });
}

}  // namespace wh40k::core
