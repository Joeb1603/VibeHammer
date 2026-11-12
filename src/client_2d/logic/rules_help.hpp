#pragma once

// Plain-English help for weapon abilities, keywords and shooting types (D-037, M24), and the lines that
// say what a weapon's abilities do to a particular volley. Text is paraphrased from docs/rules (with the
// rule number); what applies comes from core's read-only ability_effects (D-024). Plain ASCII only (the
// font has no other glyphs, D-034).

#include <optional>
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
    Inactive,   // it could, but its condition isn't met here (shown red)
    Info,       // it acts elsewhere (e.g. [ASSAULT] lets the unit shoot after Advancing)
    NotInGame,  // the engine doesn't implement it yet
};

struct EffectLine {
    std::string text;  // "Rapid Fire 1: +5 attacks (target within half range, 12\")"
    EffectKind kind = EffectKind::Info;
    friend bool operator==(const EffectLine&, const EffectLine&) = default;
};

// One line per ability on `weapon`, summed over `selections` (every selection uses `weapon`). With the
// shooting type, [ASSAULT] / [CLOSE-QUARTERS] are Active when they are what lets the unit shoot that way
// (10.05 / 10.06), and Info otherwise.
[[nodiscard]] std::vector<EffectLine> effect_lines(const core::WeaponProfile& weapon,
                                                   std::span<const core::WeaponSelection> selections,
                                                   const core::AttackContext& context,
                                                   std::optional<core::ShootingType> type = std::nullopt);

// D-073: what the shooting type and the target did to these selections' hit rolls: one line per rule that gave them
// -1 when their targets were selected (10.06: a MONSTER/VEHICLE in close-quarters shooting; 17.03: an engaged
// MONSTER/VEHICLE target), and one when D-011 caps `net`, the attacks' whole hit roll modifier ([HEAVY] included).
[[nodiscard]] std::vector<EffectLine> hit_modifier_lines(std::span<const core::WeaponSelection> selections, int net);

// M88 (D-089): 13.08 Benefit of Cover on these selections (one weapon profile with skill `skill`, before cover): one
// line when any of them has it, "Benefit of cover: BS 3+ worsened to 4+ (13.08)" (with "(n of N models)" when only some
// do). Nothing for [TORRENT] (no BS) or when none has it.
[[nodiscard]] std::vector<EffectLine> cover_lines(std::span<const core::WeaponSelection> selections,
                                                  std::optional<int> skill);
[[nodiscard]] HelpEntry benefit_of_cover_help();

// D-073: help for 17.01 / 17.03 (the MONSTER and VEHICLE keywords' rules in this game).
[[nodiscard]] HelpEntry monsters_moving_help();
[[nodiscard]] HelpEntry engaged_monster_target_help();

// "24\"  A2  BS3+  S4  AP-1  D1" (WS for melee).
[[nodiscard]] std::string profile_stats(const core::WeaponProfile& weapon);

// D-069 Random Characteristics: help for a random A ("Random A (D6+3)") or D ("Random D (D6+1)"); nullopt when the
// characteristic is fixed.
[[nodiscard]] std::optional<HelpEntry> random_attacks_help(const core::WeaponProfile& weapon);
[[nodiscard]] std::optional<HelpEntry> random_damage_help(const core::WeaponProfile& weapon);
// D-069: one Info line per random characteristic of `weapon`, for the weapon menus: "Attacks D6+3 per model, rolled
// when the attacks are resolved (D-069)".
[[nodiscard]] std::vector<EffectLine> random_characteristic_lines(const core::WeaponProfile& weapon);

}  // namespace wh40k::client
