#pragma once

// What a unit's datasheet window shows (D-034, M21): built from the live unit, so it follows casualties.
// Display only (D-024).

#include <string>
#include <string_view>
#include <vector>

#include "core/model/unit.hpp"
#include "core/model/weapon.hpp"
#include "rules_help.hpp"

namespace wh40k::client {

// One row of the profile table: a model type and its characteristics as text (02.02).
struct DatasheetStatRow {
    std::string model_type;  // e.g. "Intercessor"
    int count = 0;           // models of this type and profile on the table
    std::string move, toughness, save, invuln_save, wounds, leadership, objective_control;
};

// One weapon profile (02.04) carried by models in the unit.
struct DatasheetWeaponRow {
    std::string name;  // the profile name, e.g. "Bolt Rifle - Saturation"
    std::string range, attacks, skill, strength, armour_penetration, damage;
    std::string abilities;  // e.g. "[ASSAULT] [RAPID FIRE 1]"; empty when none
    std::vector<HelpEntry> ability_help;  // D-037: one per ability, in the same order
    int carried_by = 0;     // models carrying the weapon
};

struct DatasheetAbility {
    std::string name;
    std::string text;  // cleaned (clean_ability_text)
};

struct DatasheetView {
    std::string name;
    std::string side;        // "Side A"
    std::string models;      // "5/5 models"
    bool battle_shocked = false;
    std::vector<DatasheetStatRow> stats;
    std::vector<DatasheetWeaponRow> ranged;
    std::vector<DatasheetWeaponRow> melee;
    std::vector<DatasheetAbility> abilities;
    std::string keywords;          // comma-separated
    std::string faction_keywords;  // comma-separated
    std::vector<HelpEntry> keyword_help;          // D-037: one per keyword
    std::vector<HelpEntry> faction_keyword_help;
};

[[nodiscard]] DatasheetView datasheet_view(const core::Unit& unit);

// "M 6\"  T 5  Sv 3+  W 2  Ld 6+  OC 2" (InSv only when the model has one), for the hover tooltip.
[[nodiscard]] std::string stat_line(const core::Characteristics& c);

// "[ASSAULT]", "[RAPID FIRE 1]", "[BLAST 1]", ...; unrecognised abilities show their text in brackets.
[[nodiscard]] std::string weapon_ability_label(const core::WeaponAbility& ability);

// BSData text as the default font can show it: U+00A0 -> space, U+25AA / U+25A0 bullets -> "-",
// and the "**" / "^^" markup removed.
[[nodiscard]] std::string clean_ability_text(std::string_view text);

}  // namespace wh40k::client
