#include "core/data/scenario_loader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/formation.hpp"
#include "core/data/load_error.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/oval.hpp"
#include "core/geometry/pose.hpp"
#include "core/geometry/units.hpp"
#include "core/state/points_destroyed.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core {

using Json = nlohmann::json;

namespace {

[[noreturn]] void fail(const std::string& what) {
    throw DataError("scenario: " + what);
}

// `where` is a by-value string_view so a string literal argument doesn't bind a temporary
// std::string to a reference parameter (GCC 13 -Wdangling-reference false positive).
const Json& require(const Json& j, const char* key, std::string_view where) {
    if (!j.is_object() || !j.contains(key)) {
        fail(std::string(where) + ": missing field '" + key + "'");
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

// 03.02 Set up: unit in coherency (03.03); D-009: wholly within its deployment zone.
void check_set_up(const Unit& unit, const Rect& zone, const std::string& where) {
    const CoherencyResult coh = unit_coherency(unit);
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
        if (!wholly_within(zone, pose_of(m))) {  // D-074: by the base's extents at its facing
            fail(where + ": D-009: model " + std::to_string(m.id.v) + " of '" + unit.name +
                 "' is not wholly within its deployment zone");
        }
    }
}

// 03.02 "All other requirements and restrictions are met" / D-064 (as D-027 for moves, 03.01): no
// model is set up on another model. Bases in contact are fine (the D-008 formations touch); any pair,
// friendly or enemy, that intersects by more than EPS_MM is corrupt data.
void check_no_overlap(const GameState& gs) {
    struct Placed {
        const Unit* unit;
        const Model* model;
    };
    std::vector<Placed> all;
    for (const Unit& u : gs.units) {
        for (const Model& m : u.models) {
            all.push_back({&u, &m});
        }
    }
    for (std::size_t i = 0; i < all.size(); ++i) {
        for (std::size_t j = i + 1; j < all.size(); ++j) {
            const Model& a = *all[i].model;
            const Model& b = *all[j].model;
            if (bases_overlap(pose_of(a), pose_of(b))) {  // D-074: each base with its facing
                fail("03.02 / D-064: model " + std::to_string(a.id.v) + " of unit " +
                     std::to_string(all[i].unit->id.v) + " '" + all[i].unit->name + "' overlaps model " +
                     std::to_string(b.id.v) + " of unit " + std::to_string(all[j].unit->id.v) + " '" +
                     all[j].unit->name + "'");
            }
        }
    }
}

// 03.02: a set-up unit must be unengaged (03.04).
void check_unengaged(const GameState& gs) {
    for (const Unit& u : gs.units) {
        const std::vector<UnitId> engaged = engaged_enemy_units(gs, u);
        if (!engaged.empty()) {
            fail("03.02: unit '" + u.name + "' is set up engaged (03.04) with unit " +
                 std::to_string(engaged.front().v));
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
    // Optional PRNG seed; GameState::dice starts at position 0 (M13 adds a seed override).
    if (scenario.contains("seed")) {
        gs.dice.seed = scenario.at("seed").get<std::uint64_t>();
    }
    // D-076: optional; absent means D-007 (a draw after the last battle round).
    if (scenario.contains("result")) {
        const Json& r = scenario.at("result");
        if (!r.is_string() || r.get<std::string>() != "points_destroyed") {
            fail("root: D-076: unknown result " + r.dump() + " (the only value is \"points_destroyed\")");
        }
        gs.result_rule = ResultRule::PointsDestroyed;
    }

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
            // D-076: the unit's points (required with result "points_destroyed") and its starting wounds.
            if (uj.contains("points")) {
                const Json& p = uj.at("points");
                if (!p.is_number_integer() || p.get<std::int64_t>() < 1 || p.get<std::int64_t>() > kMaxUnitPoints) {
                    fail(uwhere + ": D-076: points must be a positive integer of at most " +
                         std::to_string(kMaxUnitPoints) + ", not " + p.dump());
                }
                const std::int64_t wounds = sum_of_wounds_characteristics(unit);
                if (wounds < 1 || wounds > std::numeric_limits<int>::max()) {
                    fail(uwhere + ": D-076: the unit's starting wounds (" + std::to_string(wounds) + ") must be a positive int");
                }
                unit.points = p.get<int>();
                unit.starting_wounds = static_cast<int>(wounds);
            } else if (gs.result_rule == ResultRule::PointsDestroyed) {
                fail(uwhere + ": D-076: missing field 'points' (result \"points_destroyed\" needs every unit's points)");
            }

            place_formation(unit.models, parse_formation(require(uj, "formation", uwhere), uwhere),
                            gs.battlefield, info.deployment_zone, uwhere);
            check_set_up(unit, zone, uwhere);
            gs.units.push_back(std::move(unit));
        }
    }
    check_no_overlap(gs);
    check_unengaged(gs);
    check_points_data(gs, "scenario");  // D-076: the bound on the exact count
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
