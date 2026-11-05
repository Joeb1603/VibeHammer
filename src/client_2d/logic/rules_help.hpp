#pragma once

// Plain-English help for weapon abilities, keywords and shooting types (D-037, M24), and the lines that
// say what a weapon's abilities do to a particular volley. Text is paraphrased from docs/rules (with the
// rule number); what applies comes from core's read-only ability_effects (D-024). Plain ASCII only (the
// font has no other glyphs, D-034).

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/model/weapon.hpp"
#include "core/rules/attack_types.hpp"
#include "core/rules/shooting_types.hpp"

namespace wh40k::client {

struct HelpEntry {
    std::string title;    // "[RAPID FIRE 1]", "INFANTRY", "Assault shooting"
    std::string summary;  // what it does, in plain words
    std::string rule;     // "24.30"
    bool in_this_game = false;  // the engine applies it
    friend bool operator==(const HelpEntry&, const HelpEntry&) = default;
};

[[nodiscard]] HelpEntry weapon_ability_help(const core::WeaponAbility& ability);
// `faction`: a faction keyword (02.05: used when building armies).
[[nodiscard]] HelpEntry keyword_help(std::string_view keyword, bool faction = false);
[[nodiscard]] HelpEntry shooting_type_help(core::ShootingType type);

// What one of a weapon's abilities does to a volley.
enum class EffectKind {
    Active,     // it changes these attacks (extra attacks, +1 to hit, hits automatically)
    Inactive,   // it could, but its condition isn't met here
    Info,       // it acts elsewhere (e.g. [ASSAULT] lets the unit shoot after Advancing)
    NotInGame,  // the engine doesn't implement it yet
};

struct EffectLine {
    std::string text;  // "Rapid Fire 1: +5 attacks (target within half range, 12\")"
    EffectKind kind = EffectKind::Info;
    friend bool operator==(const EffectLine&, const EffectLine&) = default;
};

// One line per ability on `weapon`, summed over `selections` (every selection uses `weapon`).
[[nodiscard]] std::vector<EffectLine> effect_lines(const core::WeaponProfile& weapon,
                                                   std::span<const core::WeaponSelection> selections,
                                                   const core::AttackContext& context);

// "24\"  A2  BS3+  S4  AP-1  D1" (WS for melee).
[[nodiscard]] std::string profile_stats(const core::WeaponProfile& weapon);

}  // namespace wh40k::client
