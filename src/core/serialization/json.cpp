#include "core/serialization/json.hpp"

#include "core/data/load_error.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace wh40k::core {

namespace {

template <typename T>
void put_opt(Json& j, const char* key, const std::optional<T>& v) {
    if (v) {
        j[key] = *v;
    } else {
        j[key] = nullptr;
    }
}

template <typename T>
void get_opt(const Json& j, const char* key, std::optional<T>& v) {
    const Json& e = j.at(key);
    if (e.is_null()) {
        v.reset();
    } else {
        v = e.get<T>();
    }
}

[[noreturn]] void bad_enum(std::string_view type, const Json& j) {
    throw DataError("state JSON: invalid " + std::string(type) + ": " + j.dump());
}

}  // namespace

// ---- ids ----
void to_json(Json& j, Side v) { j = (v == Side::A) ? "A" : "B"; }
void from_json(const Json& j, Side& v) {
    const auto s = j.get<std::string>();
    if (s == "A") v = Side::A;
    else if (s == "B") v = Side::B;
    else bad_enum("Side", j);
}
void to_json(Json& j, const UnitId& v) { j = v.v; }
void from_json(const Json& j, UnitId& v) { v.v = j.get<std::uint32_t>(); }
void to_json(Json& j, const ModelId& v) { j = v.v; }
void from_json(const Json& j, ModelId& v) { v.v = j.get<std::uint32_t>(); }

// ---- geometry ----
void to_json(Json& j, const Position& v) { j = Json{{"x", v.x}, {"y", v.y}, {"z", v.z}}; }
void from_json(const Json& j, Position& v) {
    j.at("x").get_to(v.x);
    j.at("y").get_to(v.y);
    j.at("z").get_to(v.z);
}
void to_json(Json& j, BaseShape v) { j = (v == BaseShape::Round) ? "round" : "oval"; }
void from_json(const Json& j, BaseShape& v) {
    const auto s = j.get<std::string>();
    if (s == "round") v = BaseShape::Round;
    else if (s == "oval") v = BaseShape::Oval;
    else bad_enum("BaseShape", j);
}
void to_json(Json& j, const Base& v) {
    j = Json{{"shape", v.shape}, {"diameter_mm", v.diameter_mm}, {"length_mm", v.length_mm}};
}
void from_json(const Json& j, Base& v) {
    j.at("shape").get_to(v.shape);
    j.at("diameter_mm").get_to(v.diameter_mm);
    j.at("length_mm").get_to(v.length_mm);
}
void to_json(Json& j, const Rect& v) {
    j = Json{{"min_x", v.min_x}, {"min_y", v.min_y}, {"max_x", v.max_x}, {"max_y", v.max_y}};
}
void from_json(const Json& j, Rect& v) {
    j.at("min_x").get_to(v.min_x);
    j.at("min_y").get_to(v.min_y);
    j.at("max_x").get_to(v.max_x);
    j.at("max_y").get_to(v.max_y);
}

// ---- model ----
void to_json(Json& j, const DiceExpr& v) {
    if (v.is_fixed()) {
        j = static_cast<int>(v.modifier);
    } else {
        j = to_string(v);
    }
}
void from_json(const Json& j, DiceExpr& v) {
    if (j.is_number_integer()) {
        v = DiceExpr::fixed(j.get<int>());
    } else {
        v = parse_dice_expr(j.get<std::string>());
    }
}

void to_json(Json& j, const Characteristics& v) {
    j = Json::object();
    put_opt(j, "M_mm", v.move_mm);
    j["T"] = v.toughness;
    put_opt(j, "Sv", v.save);
    put_opt(j, "InSv", v.invuln_save);
    j["W"] = v.wounds;
    put_opt(j, "Ld", v.leadership);
    put_opt(j, "OC", v.objective_control);
}
void from_json(const Json& j, Characteristics& v) {
    get_opt(j, "M_mm", v.move_mm);
    j.at("T").get_to(v.toughness);
    get_opt(j, "Sv", v.save);
    get_opt(j, "InSv", v.invuln_save);
    j.at("W").get_to(v.wounds);
    get_opt(j, "Ld", v.leadership);
    get_opt(j, "OC", v.objective_control);
}

void to_json(Json& j, WeaponKind v) { j = (v == WeaponKind::Ranged) ? "ranged" : "melee"; }
void from_json(const Json& j, WeaponKind& v) {
    const auto s = j.get<std::string>();
    if (s == "ranged") v = WeaponKind::Ranged;
    else if (s == "melee") v = WeaponKind::Melee;
    else bad_enum("WeaponKind", j);
}

namespace ability {

void to_json(Json& j, const WeaponAbility& v) {
    std::visit(
        [&j](const auto& a) {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, Assault>) j = Json{{"kind", "Assault"}};
            else if constexpr (std::is_same_v<T, Heavy>) j = Json{{"kind", "Heavy"}};
            else if constexpr (std::is_same_v<T, CloseQuarters>) j = Json{{"kind", "CloseQuarters"}};
            else if constexpr (std::is_same_v<T, RapidFire>) j = Json{{"kind", "RapidFire"}, {"x", a.x}};
            else if constexpr (std::is_same_v<T, Blast>) j = Json{{"kind", "Blast"}, {"x", a.x}};
            else if constexpr (std::is_same_v<T, TwinLinked>) j = Json{{"kind", "TwinLinked"}};
            else if constexpr (std::is_same_v<T, Torrent>) j = Json{{"kind", "Torrent"}};
            else if constexpr (std::is_same_v<T, Hazardous>) j = Json{{"kind", "Hazardous"}};
            else if constexpr (std::is_same_v<T, DevastatingWounds>) j = Json{{"kind", "DevastatingWounds"}};
            else if constexpr (std::is_same_v<T, Unknown>) j = Json{{"kind", "Unknown"}, {"text", a.text}};
            else static_assert(sizeof(T) == 0, "unhandled WeaponAbility alternative");
        },
        v);
}

void from_json(const Json& j, WeaponAbility& v) {
    const auto kind = j.at("kind").get<std::string>();
    if (kind == "Assault") v = Assault{};
    else if (kind == "Heavy") v = Heavy{};
    else if (kind == "CloseQuarters") v = CloseQuarters{};
    else if (kind == "RapidFire") v = RapidFire{j.at("x").get<int>()};
    else if (kind == "Blast") v = Blast{j.at("x").get<int>()};
    else if (kind == "TwinLinked") v = TwinLinked{};
    else if (kind == "Torrent") v = Torrent{};
    else if (kind == "Hazardous") v = Hazardous{};
    else if (kind == "DevastatingWounds") v = DevastatingWounds{};
    else if (kind == "Unknown") v = Unknown{j.at("text").get<std::string>()};
    else bad_enum("WeaponAbility", j);
}

}  // namespace ability

void to_json(Json& j, const WeaponProfile& v) {
    j = Json::object();
    j["name"] = v.name;
    j["kind"] = v.kind;
    put_opt(j, "range_mm", v.range_mm);
    j["A"] = v.attacks;
    put_opt(j, "skill", v.skill);
    j["S"] = v.strength;
    j["AP"] = v.armour_penetration;
    j["D"] = v.damage;
    Json abilities = Json::array();
    for (const auto& a : v.abilities) {
        Json e;
        ability::to_json(e, a);
        abilities.push_back(std::move(e));
    }
    j["abilities"] = std::move(abilities);
}
void from_json(const Json& j, WeaponProfile& v) {
    j.at("name").get_to(v.name);
    j.at("kind").get_to(v.kind);
    get_opt(j, "range_mm", v.range_mm);
    j.at("A").get_to(v.attacks);
    get_opt(j, "skill", v.skill);
    j.at("S").get_to(v.strength);
    j.at("AP").get_to(v.armour_penetration);
    j.at("D").get_to(v.damage);
    v.abilities.clear();
    for (const Json& e : j.at("abilities")) {
        WeaponAbility a;
        ability::from_json(e, a);
        v.abilities.push_back(std::move(a));
    }
}

void to_json(Json& j, const Weapon& v) {
    j = Json{{"name", v.name}, {"kind", v.kind}, {"profiles", v.profiles}};
}
void from_json(const Json& j, Weapon& v) {
    j.at("name").get_to(v.name);
    j.at("kind").get_to(v.kind);
    j.at("profiles").get_to(v.profiles);
}

void to_json(Json& j, const Model& v) {
    j = Json{{"id", v.id},
             {"type_name", v.type_name},
             {"characteristics", v.characteristics},
             {"base", v.base},
             {"position", v.position},
             {"wounds_remaining", v.wounds_remaining},
             {"weapons", v.weapons}};
}
void from_json(const Json& j, Model& v) {
    j.at("id").get_to(v.id);
    j.at("type_name").get_to(v.type_name);
    j.at("characteristics").get_to(v.characteristics);
    j.at("base").get_to(v.base);
    j.at("position").get_to(v.position);
    j.at("wounds_remaining").get_to(v.wounds_remaining);
    j.at("weapons").get_to(v.weapons);
}

void to_json(Json& j, const AbilityText& v) { j = Json{{"name", v.name}, {"text", v.text}}; }
void from_json(const Json& j, AbilityText& v) {
    j.at("name").get_to(v.name);
    j.at("text").get_to(v.text);
}

void to_json(Json& j, const UnitTurnFlags& v) {
    j = Json{{"advanced", v.advanced}, {"fell_back", v.fell_back}, {"moved_over_3in", v.moved_over_3in}};
}
void from_json(const Json& j, UnitTurnFlags& v) {
    j.at("advanced").get_to(v.advanced);
    j.at("fell_back").get_to(v.fell_back);
    j.at("moved_over_3in").get_to(v.moved_over_3in);
}

void to_json(Json& j, const Unit& v) {
    j = Json{{"id", v.id},
             {"side", v.side},
             {"name", v.name},
             {"faction_keywords", v.faction_keywords},
             {"keywords", v.keywords},
             {"abilities", v.abilities},
             {"models", v.models},
             {"starting_strength", v.starting_strength},
             {"battle_shocked", v.battle_shocked},
             {"turn_flags", v.turn_flags}};
}
void from_json(const Json& j, Unit& v) {
    j.at("id").get_to(v.id);
    j.at("side").get_to(v.side);
    j.at("name").get_to(v.name);
    j.at("faction_keywords").get_to(v.faction_keywords);
    j.at("keywords").get_to(v.keywords);
    j.at("abilities").get_to(v.abilities);
    j.at("models").get_to(v.models);
    j.at("starting_strength").get_to(v.starting_strength);
    j.at("battle_shocked").get_to(v.battle_shocked);
    j.at("turn_flags").get_to(v.turn_flags);
}

// ---- state ----
void to_json(Json& j, BattlefieldEdge v) {
    switch (v) {
        case BattlefieldEdge::North: j = "north"; return;
        case BattlefieldEdge::South: j = "south"; return;
        case BattlefieldEdge::East: j = "east"; return;
        case BattlefieldEdge::West: j = "west"; return;
    }
    throw std::logic_error("to_json: invalid BattlefieldEdge");
}
void from_json(const Json& j, BattlefieldEdge& v) {
    const auto s = j.get<std::string>();
    if (s == "north") v = BattlefieldEdge::North;
    else if (s == "south") v = BattlefieldEdge::South;
    else if (s == "east") v = BattlefieldEdge::East;
    else if (s == "west") v = BattlefieldEdge::West;
    else bad_enum("BattlefieldEdge", j);
}
void to_json(Json& j, const Battlefield& v) {
    j = Json{{"width_mm", v.width_mm}, {"depth_mm", v.depth_mm}};
}
void from_json(const Json& j, Battlefield& v) {
    j.at("width_mm").get_to(v.width_mm);
    j.at("depth_mm").get_to(v.depth_mm);
}
void to_json(Json& j, const DeploymentZone& v) {
    j = Json{{"edge", v.edge}, {"depth_mm", v.depth_mm}};
}
void from_json(const Json& j, DeploymentZone& v) {
    j.at("edge").get_to(v.edge);
    j.at("depth_mm").get_to(v.depth_mm);
}
void to_json(Json& j, const SideInfo& v) {
    j = Json{{"name", v.name}, {"deployment_zone", v.deployment_zone}};
}
void from_json(const Json& j, SideInfo& v) {
    j.at("name").get_to(v.name);
    j.at("deployment_zone").get_to(v.deployment_zone);
}
void to_json(Json& j, const GameState& v) {
    j = Json{{"scenario_name", v.scenario_name},
             {"battlefield", v.battlefield},
             {"battle_rounds", v.battle_rounds},
             {"first_turn", v.first_turn},
             {"sides", v.sides},
             {"units", v.units}};
}
void from_json(const Json& j, GameState& v) {
    j.at("scenario_name").get_to(v.scenario_name);
    j.at("battlefield").get_to(v.battlefield);
    j.at("battle_rounds").get_to(v.battle_rounds);
    j.at("first_turn").get_to(v.first_turn);
    j.at("sides").get_to(v.sides);
    j.at("units").get_to(v.units);
}

}  // namespace wh40k::core
