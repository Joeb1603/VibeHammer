#include "core/model/keywords.hpp"

#include <cctype>
#include <string>

namespace wh40k::core {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool has_keyword(const Unit& unit, std::string_view keyword) {
    for (const std::string& k : unit.keywords) {
        if (iequals(k, keyword)) {
            return true;
        }
    }
    for (const std::string& k : unit.faction_keywords) {
        if (iequals(k, keyword)) {
            return true;
        }
    }
    return false;
}

bool is_character(const Unit& unit, const Model& /*model*/) {
    return has_keyword(unit, "Character");
}

bool is_monster_or_vehicle(const Unit& unit, const Model& /*model*/) {
    return has_keyword(unit, "Monster") || has_keyword(unit, "Vehicle");
}

bool is_explosives_or_grenades(const Unit& unit, const Model& /*model*/) {
    // App 02.05.01: keywords separated by slashes mean any one of them; a keyword's singular and plural are alike.
    for (const std::string_view k : {"Explosives", "Explosive", "Grenades", "Grenade"}) {
        if (has_keyword(unit, k)) return true;
    }
    return false;
}

bool is_wounded(const Model& model) {
    return model.wounds_remaining < model.characteristics.wounds;
}

bool is_destroyed(const Unit& unit) {
    return unit.models.empty();
}

}  // namespace wh40k::core
