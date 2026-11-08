#include <fstream>
#include <optional>
#include <string>

#include "core/data/datasheet.hpp"
#include "core/data/load_error.hpp"
#include "core/geometry/units.hpp"
#include "core/model/dice_expr.hpp"

namespace wh40k::core {

using Json = nlohmann::json;

namespace {

[[noreturn]] void fail(const std::string& where, const std::string& what) {
    throw DataError("datasheet " + where + ": " + what);
}

const Json& require(const Json& j, const char* key, const std::string& where) {
    if (!j.is_object() || !j.contains(key)) {
        fail(where, std::string("missing field '") + key + "'");
    }
    return j.at(key);
}

int require_int(const Json& j, const char* key, const std::string& where) {
    const Json& e = require(j, key, where);
    if (!e.is_number_integer()) {
        fail(where, std::string("field '") + key + "' must be an integer, got " + e.dump());
    }
    return e.get<int>();
}

std::string require_string(const Json& j, const char* key, const std::string& where) {
    const Json& e = require(j, key, where);
    if (!e.is_string()) {
        fail(where, std::string("field '") + key + "' must be a string, got " + e.dump());
    }
    return e.get<std::string>();
}

std::vector<std::string> string_list(const Json& j, const char* key, const std::string& where) {
    std::vector<std::string> out;
    if (!j.contains(key)) {
        return out;
    }
    for (const Json& e : j.at(key)) {
        if (!e.is_string()) {
            fail(where, std::string("'") + key + "' entries must be strings");
        }
        out.push_back(e.get<std::string>());
    }
    return out;
}

// 02.02: a characteristic shown as '-' (null or "-" in data) is absent.
std::optional<int> optional_int(const Json& j, const char* key, const std::string& where) {
    if (!j.contains(key)) {
        return std::nullopt;
    }
    const Json& e = j.at(key);
    if (e.is_null() || (e.is_string() && (e == "-" || e == "N/A"))) {
        return std::nullopt;
    }
    if (!e.is_number_integer()) {
        fail(where, std::string("field '") + key + "' must be an integer, null, \"-\" or \"N/A\", got " +
                        e.dump());
    }
    return e.get<int>();
}

DiceExpr dice_value(const Json& j, const char* key, const std::string& where) {
    const Json& e = require(j, key, where);
    if (e.is_number_integer()) {
        return DiceExpr::fixed(e.get<int>());
    }
    if (e.is_string()) {
        return parse_dice_expr(e.get<std::string>());  // D-002: parsed, never rolled
    }
    fail(where, std::string("field '") + key + "' must be an integer or dice string, got " + e.dump());
}

WeaponKind weapon_kind(const Json& j, const std::string& where) {
    const std::string s = require_string(j, "type", where);
    if (s == "ranged") return WeaponKind::Ranged;
    if (s == "melee") return WeaponKind::Melee;
    fail(where, "unknown weapon type '" + s + "'");
}

// D-070: a profile may carry its own "type" (a weapon with ranged and melee profiles); without one it
// takes the weapon's.
WeaponProfile parse_profile(const Json& j, WeaponKind weapon_kind_default, const std::string& weapon_where) {
    WeaponProfile p;
    p.name = require_string(j, "name", weapon_where);
    const std::string where = weapon_where + " profile '" + p.name + "'";
    p.kind = j.contains("type") ? weapon_kind(j, where) : weapon_kind_default;
    const WeaponKind kind = p.kind;

    const Json& range = require(j, "range_in", where);
    if (kind == WeaponKind::Ranged) {
        if (!range.is_number()) {
            fail(where, "ranged weapon needs a numeric range_in");
        }
        p.range_mm = inches_to_mm(range.get<float>());
    } else if (!range.is_null()) {
        fail(where, "melee weapon must have range_in null");  // 02.04: R is 'Melee'
    }

    p.attacks = dice_value(j, "A", where);
    p.skill = optional_int(j, "skill", where);  // int, null or "N/A" ([TORRENT])
    p.strength = require_int(j, "S", where);
    p.armour_penetration = require_int(j, "AP", where);
    p.damage = dice_value(j, "D", where);
    for (const std::string& a : string_list(j, "abilities", where)) {
        p.abilities.push_back(parse_weapon_ability(a));
    }
    return p;
}

Characteristics parse_stats(const Json& j, const std::string& where) {
    Characteristics c;
    if (const auto m = optional_int(j, "M", where)) {
        c.move_mm = inches_to_mm(static_cast<float>(*m));
    }
    c.toughness = require_int(j, "T", where);
    c.save = optional_int(j, "Sv", where);
    c.invuln_save = optional_int(j, "InSv", where);
    c.wounds = require_int(j, "W", where);
    c.leadership = optional_int(j, "Ld", where);
    c.objective_control = optional_int(j, "OC", where);
    return c;
}

// D-070: a round base has diameter_mm; an oval ("120x92mm") has diameter_mm = its width (the short
// axis, 92) and length_mm = its length (the long axis, 120). A null base is one the fetch tool could
// not read ("Hull", "~40mm", ...).
Base parse_base(const Json& j, const std::string& where) {
    if (j.is_null()) {
        fail(where, "D-070: base is null (the fetch tool could not read the datasheet's base size)");
    }
    const std::string shape = require_string(j, "shape", where);
    Base b;
    const Json& d = require(j, "diameter_mm", where);
    if (!d.is_number() || d.get<float>() <= 0.0f) {
        fail(where, "diameter_mm must be a positive number");
    }
    b.diameter_mm = d.get<float>();
    if (shape == "round") {
        b.shape = BaseShape::Round;
    } else if (shape == "oval") {
        b.shape = BaseShape::Oval;
        const Json& len = require(j, "length_mm", where);
        if (!len.is_number() || len.get<float>() < b.diameter_mm) {
            fail(where, "D-070: an oval base's length_mm must be a number no smaller than its diameter_mm (width)");
        }
        b.length_mm = len.get<float>();
    } else {
        fail(where, "unknown base shape '" + shape + "'");
    }
    return b;
}

}  // namespace

const ModelTypeSheet* Datasheet::find_model_type(std::string_view type_name) const {
    for (const auto& t : model_types) {
        if (t.name == type_name) {
            return &t;
        }
    }
    return nullptr;
}

const Weapon* Datasheet::find_weapon(std::string_view weapon_name) const {
    for (const auto& w : weapons) {
        if (w.name == weapon_name) {
            return &w;
        }
    }
    return nullptr;
}

namespace {
Datasheet parse_datasheet_impl(const Json& j);
}  // namespace

Datasheet parse_datasheet(const Json& j) {
    try {
        return parse_datasheet_impl(j);
    } catch (const Json::exception& e) {
        // Wrong JSON types etc. are corrupt data too.
        throw DataError(std::string("datasheet: ") + e.what());
    }
}

namespace {
Datasheet parse_datasheet_impl(const Json& j) {
    Datasheet ds;
    ds.name = require_string(j, "name", "<unnamed>");
    const std::string where = "'" + ds.name + "'";

    if (j.contains("source") && j.at("source").contains("commit")) {
        ds.source_commit = j.at("source").at("commit").get<std::string>();
    }
    ds.faction_keywords = string_list(j, "faction_keywords", where);
    ds.keywords = string_list(j, "keywords", where);

    if (j.contains("abilities")) {
        for (const Json& a : j.at("abilities")) {
            ds.abilities.push_back(
                AbilityText{require_string(a, "name", where), require_string(a, "text", where)});
        }
    }

    const Json& comp = require(j, "composition", where);
    ds.min_models = require_int(comp, "min_models", where);
    ds.max_models = require_int(comp, "max_models", where);

    for (const Json& t : require(j, "model_types", where)) {
        ModelTypeSheet mt;
        mt.name = require_string(t, "name", where);
        const std::string type_where = where + " model type '" + mt.name + "'";
        mt.min = require_int(t, "min", type_where);
        mt.max = require_int(t, "max", type_where);
        mt.characteristics = parse_stats(require(t, "stats", type_where), type_where);
        mt.base = parse_base(require(t, "base", type_where), type_where);
        mt.default_weapons = string_list(t, "default_weapons", type_where);
        mt.weapon_options = string_list(t, "weapon_options", type_where);
        ds.model_types.push_back(std::move(mt));
    }

    // nlohmann::json objects iterate in sorted key order: deterministic.
    for (const auto& [weapon_name, wj] : require(j, "weapons", where).items()) {
        const std::string weapon_where = where + " weapon '" + weapon_name + "'";
        Weapon w;
        w.name = weapon_name;
        w.kind = weapon_kind(wj, weapon_where);
        const Json& profiles = require(wj, "profiles", weapon_where);
        if (profiles.empty()) {
            fail(weapon_where, "weapon has no profiles");
        }
        for (const Json& pj : profiles) {
            if (pj.contains("condition")) {
                // D-070: a profile that applies only against some targets ("Hunter: Monster/Vehicle")
                // is not implemented; it is left out so it can never be selected, and recorded.
                ds.conditional_profiles.push_back(
                    ConditionalProfile{weapon_name, require_string(pj, "name", weapon_where),
                                       require_string(pj, "condition", weapon_where)});
                continue;
            }
            w.profiles.push_back(parse_profile(pj, w.kind, weapon_where));
        }
        if (w.profiles.empty()) {
            continue;  // D-070: only conditional profiles; the weapon can't be equipped (02.04 at load)
        }
        ds.weapons.push_back(std::move(w));
    }
    return ds;
}
}  // namespace

Json read_json_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw DataError("cannot open data file '" + path.string() + "'");
    }
    try {
        return Json::parse(in);
    } catch (const Json::parse_error& e) {
        throw DataError("malformed JSON in '" + path.string() + "': " + e.what());
    }
}

Datasheet load_datasheet(const std::filesystem::path& path) {
    return parse_datasheet(read_json_file(path));
}

}  // namespace wh40k::core
