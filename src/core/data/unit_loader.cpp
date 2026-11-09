#include <algorithm>
#include <fstream>
#include <optional>
#include <set>
#include <string>

#include "core/data/datasheet.hpp"
#include "core/data/load_error.hpp"
#include "core/geometry/units.hpp"
#include "core/model/dice_expr.hpp"
#include "core/state/points_destroyed.hpp"

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
        return parse_dice_expr(e.get<std::string>());  // D-069: rolled when needed (A and D only)
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

const WargearOption* WargearSlot::find_option(std::string_view option_name) const {
    for (const auto& o : options) {
        if (o.name == option_name) {
            return &o;
        }
    }
    return nullptr;
}

const ModelWargear* ListBuilding::find_wargear(std::string_view model_type) const {
    for (const auto& w : wargear) {
        if (w.model_type == model_type) {
            return &w;
        }
    }
    return nullptr;
}

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

// ---- D-079: list-building data (tools/fetch_bsdata.py writes it from BSData) ----

[[noreturn]] void list_fail(const std::string& where, const std::string& what) {
    fail(where, "D-079: " + what);
}

int int_at_least(const Json& j, const char* key, int lowest, const std::string& where) {
    const int v = require_int(j, key, where);
    if (v < lowest) {
        list_fail(where, std::string("'") + key + "' must be at least " + std::to_string(lowest) + ", got " +
                             std::to_string(v));
    }
    return v;
}

bool has(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

// A limit per model (no size terms, never a model type) or per unit.
WargearLimit parse_limit(const Json& j, bool per_model, const std::string& where) {
    WargearLimit l;
    const int kinds = static_cast<int>(j.contains("weapons")) + static_cast<int>(j.contains("option")) +
                      static_cast<int>(j.contains("model_type"));
    if (kinds != 1) {
        list_fail(where, "a limit counts exactly one of 'weapons', 'option' and 'model_type'");
    }
    if (j.contains("weapons")) {
        l.counts = LimitCounts::Weapons;
        l.names = string_list(j, "weapons", where);
        if (l.names.empty()) {
            list_fail(where, "a limit's 'weapons' list is empty");
        }
    } else if (j.contains("option")) {
        l.counts = LimitCounts::Option;
        l.names = {require_string(j, "option", where)};
    } else {
        if (per_model) {
            list_fail(where, "a per-model limit can't count a model type");
        }
        l.counts = LimitCounts::ModelType;
        l.names = {require_string(j, "model_type", where)};
    }
    l.max = int_at_least(j, "max", 0, where);
    if (j.contains("below_models")) {
        const Json& b = j.at("below_models");
        l.below_models = BelowModels{int_at_least(b, "models", 1, where), int_at_least(b, "max", 0, where)};
    }
    if (j.contains("per_models")) {
        const Json& p = j.at("per_models");
        l.per_models = PerModels{int_at_least(p, "models", 1, where), int_at_least(p, "add", 1, where)};
    }
    if (per_model && (l.below_models || l.per_models)) {
        list_fail(where, "a per-model limit can't depend on the unit's size");
    }
    if (l.below_models && l.per_models) {
        list_fail(where, "a limit has both 'below_models' and 'per_models'");
    }
    return l;
}

bool any_slot_has_option(const ModelWargear& w, const std::string& option) {
    return std::any_of(w.slots.begin(), w.slots.end(),
                       [&](const WargearSlot& s) { return s.find_option(option) != nullptr; });
}

ListBuilding parse_list_building(const Json& j, const Datasheet& ds, const std::set<std::string>& weapon_names,
                                 const std::string& sheet_where) {
    const std::string where = sheet_where + " list_building";
    if (!j.is_object()) {
        list_fail(where, "must be an object");
    }
    ListBuilding lb;

    const Json& points = require(j, "points", where);
    if (!points.is_array() || points.empty()) {
        list_fail(where, "'points' must be a non-empty list of size brackets");
    }
    for (const Json& b : points) {
        const PointsBracket pb{require_int(b, "min_models", where), require_int(b, "points", where)};
        if (pb.points < 1 || pb.points > kMaxUnitPoints) {
            list_fail(where, "a size bracket's points must be 1.." + std::to_string(kMaxUnitPoints) + ", got " +
                                 std::to_string(pb.points));
        }
        if (!lb.points.empty() && pb.min_models <= lb.points.back().min_models) {
            list_fail(where, "size brackets must have strictly increasing min_models");
        }
        lb.points.push_back(pb);
    }
    if (lb.points.front().min_models != ds.min_models || lb.points.back().min_models > ds.max_models) {
        list_fail(where, "size brackets must start at the composition's minimum (" + std::to_string(ds.min_models) +
                             " models) and stay within its maximum (" + std::to_string(ds.max_models) + ")");
    }

    if (j.contains("copy_surcharge")) {
        const Json& sc = j.at("copy_surcharge");
        lb.surcharge = CopySurcharge{int_at_least(sc, "earlier_copies", 1, where), int_at_least(sc, "points", 1, where)};
    }
    const Json& copies = require(j, "max_copies", where);
    lb.max_copies_default = int_at_least(copies, "default", 1, where);
    lb.max_copies_incursion = int_at_least(copies, "incursion", 1, where);

    const Json& wargear = require(j, "wargear", where);
    if (!wargear.is_array() || wargear.size() != ds.model_types.size()) {
        list_fail(where, "'wargear' must list every model type once, in model_types order");
    }
    for (std::size_t i = 0; i < ds.model_types.size(); ++i) {
        const ModelTypeSheet& mt = ds.model_types[i];
        const Json& wj = wargear[i];
        ModelWargear mw;
        mw.model_type = require_string(wj, "model_type", where);
        if (mw.model_type != mt.name) {
            list_fail(where, "'wargear' entry " + std::to_string(i) + " is for '" + mw.model_type +
                                 "', expected the model type '" + mt.name + "' (every model type once, in order)");
        }
        const std::string mw_where = where + " model type '" + mt.name + "'";
        const auto check_weapon = [&](const std::string& weapon, const std::string& at) {
            if (weapon_names.count(weapon) == 0) {
                list_fail(at, "unknown weapon '" + weapon + "'");
            }
            if (!has(mt.weapon_options, weapon)) {
                list_fail(at, "'" + weapon + "' is not one of the model type's weapon_options");
            }
        };
        mw.fixed = string_list(wj, "fixed", mw_where);
        for (const std::string& w : mw.fixed) {
            check_weapon(w, mw_where);
        }
        if (wj.contains("slots")) {
            for (const Json& sj : wj.at("slots")) {
                WargearSlot slot;
                slot.name = require_string(sj, "name", mw_where);
                const std::string slot_where = mw_where + " slot '" + slot.name + "'";
                const Json& options = require(sj, "options", slot_where);
                if (!options.is_array() || options.empty()) {
                    list_fail(slot_where, "a slot needs at least one option");
                }
                for (const Json& oj : options) {
                    WargearOption o;
                    o.name = require_string(oj, "name", slot_where);
                    if (slot.find_option(o.name) != nullptr) {
                        list_fail(slot_where, "two options are named '" + o.name + "'");
                    }
                    o.weapons = string_list(oj, "weapons", slot_where);
                    if (o.weapons.empty()) {
                        list_fail(slot_where, "option '" + o.name + "' gives no weapon");
                    }
                    for (const std::string& w : o.weapons) {
                        check_weapon(w, slot_where + " option '" + o.name + "'");
                    }
                    if (oj.contains("points")) {
                        o.points = int_at_least(oj, "points", 0, slot_where);
                        if (o.points > kMaxUnitPoints) {
                            list_fail(slot_where, "option '" + o.name + "' costs more than " +
                                                      std::to_string(kMaxUnitPoints) + " points");
                        }
                    }
                    slot.options.push_back(std::move(o));
                }
                if (sj.contains("default")) {
                    const std::string d = require_string(sj, "default", slot_where);
                    if (slot.find_option(d) == nullptr) {
                        list_fail(slot_where, "its default '" + d + "' is not one of its options");
                    }
                    slot.default_option = d;
                }
                mw.slots.push_back(std::move(slot));
            }
        }
        if (wj.contains("limits")) {
            for (const Json& lj : wj.at("limits")) {
                WargearLimit l = parse_limit(lj, true, mw_where);
                for (const std::string& n : l.names) {
                    if (l.counts == LimitCounts::Weapons && weapon_names.count(n) == 0) {
                        list_fail(mw_where, "a limit counts the unknown weapon '" + n + "'");
                    }
                    if (l.counts == LimitCounts::Option && !any_slot_has_option(mw, n)) {
                        list_fail(mw_where, "a limit counts the unknown option '" + n + "'");
                    }
                }
                mw.limits.push_back(std::move(l));
            }
        }
        lb.wargear.push_back(std::move(mw));
    }

    if (j.contains("unit_limits")) {
        for (const Json& lj : j.at("unit_limits")) {
            WargearLimit l = parse_limit(lj, false, where);
            for (const std::string& n : l.names) {
                const bool known =
                    l.counts == LimitCounts::Weapons
                        ? weapon_names.count(n) > 0
                        : l.counts == LimitCounts::Option
                              ? std::any_of(lb.wargear.begin(), lb.wargear.end(),
                                            [&](const ModelWargear& w) { return any_slot_has_option(w, n); })
                              : ds.find_model_type(n) != nullptr;
                if (!known) {
                    list_fail(where, "a unit limit counts the unknown " +
                                         std::string(l.counts == LimitCounts::Weapons  ? "weapon"
                                                     : l.counts == LimitCounts::Option ? "option"
                                                                                       : "model type") +
                                         " '" + n + "'");
                }
            }
            lb.unit_limits.push_back(std::move(l));
        }
    }
    return lb;
}

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

    if (j.contains("list_building")) {
        // D-079: weapon names are the file's, including a weapon whose profiles are all conditional (D-070).
        std::set<std::string> weapon_names;
        for (const auto& item : j.at("weapons").items()) {
            weapon_names.insert(item.key());
        }
        ds.list_building = parse_list_building(j.at("list_building"), ds, weapon_names, where);
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
