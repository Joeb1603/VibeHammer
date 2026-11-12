#include "rules_help.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <type_traits>
#include <variant>

#include "core/dice/modifiers.hpp"
#include "core/geometry/units.hpp"
#include "core/model/dice_expr.hpp"
#include "core/rules/attacks.hpp"
#include "datasheet_view.hpp"

namespace wh40k::client {

namespace {

namespace ab = core::ability;

std::string inches(float mm) {
    const float in = core::mm_to_inches(mm);
    const float rounded = std::round(in);
    char buf[32];
    if (std::fabs(in - rounded) < 0.05f) {
        std::snprintf(buf, sizeof(buf), "%d\"", static_cast<int>(rounded));
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f\"", static_cast<double>(in));
    }
    return buf;
}

std::string upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string attacks(int n) { return "+" + std::to_string(n) + (n == 1 ? " attack" : " attacks"); }

std::string models(std::size_t n) { return std::to_string(n) + (n == 1 ? " model" : " models"); }

}  // namespace

HelpEntry weapon_ability_help(const core::WeaponAbility& ability) {
    HelpEntry h;
    h.title = weapon_ability_label(ability);
    std::visit(
        [&h](const auto& a) {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, ab::Assault>) {
                h.summary = "Can be fired after the unit Advanced (Assault shooting: only [ASSAULT] weapons fire).";
                h.rule = "24.04";
                h.in_this_game = true;
            } else if constexpr (std::is_same_v<T, ab::Heavy>) {
                h.summary = "+1 to hit in your Shooting phase if the unit is not engaged and no model in it has "
                            "moved more than 3\" this turn.";
                h.rule = "24.16";
                h.in_this_game = true;
            } else if constexpr (std::is_same_v<T, ab::CloseQuarters>) {
                h.summary = "Can be fired while the unit is engaged, at enemy units it is engaged with "
                            "(Close-quarters shooting). Otherwise a model fires either these weapons or its "
                            "other ranged weapons, not both. [PISTOL] is the same ability.";
                h.rule = "24.07";
                h.in_this_game = true;
            } else if constexpr (std::is_same_v<T, ab::RapidFire>) {
                h.summary = "+" + std::to_string(a.x) + (a.x == 1 ? " attack" : " attacks") +
                            " when the target is within half the weapon's range.";
                h.rule = "24.30";
                h.in_this_game = true;
            } else if constexpr (std::is_same_v<T, ab::Blast>) {
                h.summary = "+" + std::to_string(a.x) + (a.x == 1 ? " attack" : " attacks") +
                            " for every 5 models in the target unit (rounding down).";
                h.rule = "24.05";
                h.in_this_game = true;
            } else if constexpr (std::is_same_v<T, ab::TwinLinked>) {
                h.summary = "You can re-roll the wound roll. After the wound rolls, if any of them failed, the "
                            "attacker picks which to re-roll (failed or not, or none); each is re-rolled once, "
                            "before modifiers.";
                h.rule = "24.38";
                h.in_this_game = true;
            } else if constexpr (std::is_same_v<T, ab::Torrent>) {
                h.summary = "Hits automatically: no hit roll is made.";
                h.rule = "24.37";
                h.in_this_game = true;
            } else if constexpr (std::is_same_v<T, ab::Hazardous>) {
                h.summary = "After the unit has made all of its attacks, it makes a hazard roll for each [HAZARDOUS] "
                            "weapon it used: each 1-2 is a mortal wound on the unit (3 if every model in it is a "
                            "MONSTER or VEHICLE), and its player picks the models that take them.";
                h.rule = "24.15";
                h.in_this_game = true;
            } else if constexpr (std::is_same_v<T, ab::DevastatingWounds>) {
                h.summary = "A critical wound (unmodified 6) ends that attack and inflicts mortal wounds equal to "
                            "the weapon's Damage instead. Not in this game yet: weapons with it can't be "
                            "selected.";
                h.rule = "24.10";
            } else if constexpr (std::is_same_v<T, ab::SustainedHits>) {
                h.summary = "Each critical hit (an unmodified 6 to hit) scores " + std::to_string(a.x) +
                            (a.x == 1 ? " extra hit" : " extra hits") + " on the target. The extra hits are not "
                            "critical hits.";
                h.rule = "24.36";
                h.in_this_game = true;
            } else if constexpr (std::is_same_v<T, ab::ExtraAttacks>) {
                h.summary = "The model fights with this weapon in addition to one of its other melee weapons: it "
                            "must select all of its [EXTRA ATTACKS] weapons and one other melee weapon, if it has "
                            "one that can be used.";
                h.rule = "24.11";
                h.in_this_game = true;
            } else {
                h.summary = "Not in this game yet: weapons with this ability can't be selected (D-071).";
                h.rule = "24";
            }
        },
        ability);
    return h;
}

HelpEntry keyword_help(std::string_view keyword, bool faction) {
    HelpEntry h;
    h.title = upper(keyword);
    h.rule = "02.05";
    if (faction) {
        h.summary = "Faction keyword: used when deciding which units can be in an army. Otherwise it works "
                    "like any keyword. Nothing in this game uses it.";
    } else if (h.title == "INFANTRY") {
        h.summary = "Foot soldiers. Rules for terrain refer to it (e.g. moving through dense terrain and "
                    "being hidden in terrain areas, 13), as do some actions (16). There is no terrain in this "
                    "game yet, so it has no effect here.";
        h.rule = "13";
    } else if (h.title == "MONSTER" || h.title == "VEHICLE") {
        // D-073 (M63): 17.01 and 17.03 (also 10.06, 06.03 and [HAZARDOUS], 24.15).
        h.summary = "Moves through enemy models that aren't MONSTER or VEHICLE models in a Normal or Advance move "
                    "(not ending on them, 17.01). Can be shot at while engaged, at -1 to hit (17.03). Can fire "
                    "all its ranged weapons while engaged (close-quarters shooting, at -1 to hit, 10.06), and "
                    "suffers 3 mortal wounds instead of 1 from a failed hazard roll (06.03).";
        h.rule = "17.01";
        h.in_this_game = true;
    } else if (h.title == "EXPLOSIVES" || h.title == "GRENADES") {
        h.summary = "Lets the unit use the Explosives core Stratagem in your Shooting phase (six D6 at an "
                    "enemy within 8\", each 4+ is a mortal wound). Stratagems are not in this game yet.";
        h.rule = "15.05";
    } else {
        h.summary = "A keyword other rules (datasheets, Stratagems, detachments) can refer to. Nothing in this "
                    "game uses it.";
    }
    return h;
}

HelpEntry shooting_type_help(core::ShootingType type) {
    switch (type) {
        case core::ShootingType::Normal:
            return {"Normal shooting",
                    "The unit is not engaged and did not Advance this turn. Shoots with any of its ranged "
                    "weapons.",
                    "10.04", true};
        case core::ShootingType::Assault:
            return {"Assault shooting",
                    "The unit Advanced this turn and is not engaged. Only its [ASSAULT] weapons can fire.",
                    "10.05", true};
        case core::ShootingType::CloseQuarters:
            return {"Close-quarters shooting",
                    "The unit is engaged and did not Advance. Its models can only fire [CLOSE-QUARTERS] "
                    "(pistol) weapons, at enemy units they are engaged with. MONSTER and VEHICLE models can fire "
                    "any ranged weapon at any target (no [BLAST] at units they are engaged with), at -1 to hit "
                    "unless a [CLOSE-QUARTERS] weapon targets a unit they are engaged with.",
                    "10.06", true};
    }
    return {};
}

std::vector<EffectLine> hit_modifier_lines(std::span<const core::WeaponSelection> selections, int net) {
    std::vector<EffectLine> out;
    const auto with = [&](core::HitModifierRule rule) {
        return static_cast<std::size_t>(std::count_if(selections.begin(), selections.end(), [&](const core::WeaponSelection& s) {
            return std::find(s.hit_modifier_rules.begin(), s.hit_modifier_rules.end(), rule) != s.hit_modifier_rules.end();
        }));
    };
    const auto of = [&](std::size_t n) {
        return n == selections.size() ? std::string() : " (" + std::to_string(n) + " of " + models(selections.size()) + ")";
    };
    if (const std::size_t n = with(core::HitModifierRule::CloseQuartersMonster); n > 0) {
        out.push_back({"Close-quarters shooting by a MONSTER/VEHICLE: -1 to hit (10.06)" + of(n), EffectKind::Active});
    }
    if (const std::size_t n = with(core::HitModifierRule::EngagedMonsterTarget); n > 0) {
        out.push_back({"Engaged MONSTER/VEHICLE target: -1 to hit (17.03)" + of(n), EffectKind::Active});
    }
    if (const int capped = core::cap_hit_wound_modifier(net); capped != net) {
        const auto sign = [](int v) { return (v > 0 ? "+" : "") + std::to_string(v); };
        out.push_back({"Net " + sign(net) + " to hit, capped at " + sign(capped) + " (D-011)", EffectKind::Active});
    }
    return out;
}

std::vector<EffectLine> cover_lines(std::span<const core::WeaponSelection> selections, std::optional<int> skill) {
    if (!skill) return {};  // [TORRENT]: no hit roll, no BS (24.37)
    const std::size_t n = static_cast<std::size_t>(std::count_if(
        selections.begin(), selections.end(), [](const core::WeaponSelection& s) { return s.benefit_of_cover; }));
    if (n == 0) return {};
    std::string text = "Benefit of cover: BS " + std::to_string(*skill) + "+ worsened to " + std::to_string(*skill + 1) +
                       "+ (13.08)";
    if (n != selections.size()) text += " (" + std::to_string(n) + " of " + models(selections.size()) + ")";
    return {EffectLine{text, EffectKind::Active}};
}

HelpEntry benefit_of_cover_help() {
    return {"Benefit of cover",
            "A target has the benefit of cover against a model's ranged attacks if every model in it is INFANTRY, "
            "BEASTS or SWARM within a terrain area, or isn't fully visible to the attacker because of terrain in the "
            "way (light or dense features, or obscuring areas). The attacks' BS is worsened by 1 (3+ becomes 4+); an "
            "unmodified 6 still hits (D-089).",
            "13.08", true};
}

HelpEntry monsters_moving_help() {
    return {"Moving MONSTERS and VEHICLES",
            "In a Normal or Advance move, a MONSTER or VEHICLE model can be moved through enemy models that aren't "
            "MONSTER or VEHICLE models (and through friendly models, as any model can). It still can't end on a "
            "model, and the unit must still end unengaged. Fall Back, charge, pile-in and consolidation moves "
            "can't pass enemy models (D-073).",
            "17.01", true};
}

HelpEntry engaged_monster_target_help() {
    return {"Shooting at engaged MONSTERS and VEHICLES",
            "In your Shooting phase an engaged enemy MONSTER or VEHICLE unit can be selected as a target, at -1 to "
            "hit, except for [CLOSE-QUARTERS] weapons fired by a unit engaged with it. [BLAST] weapons can't "
            "target it (Rules Appendix FAQ). The -1 adds to close-quarters shooting's -1, and the net is capped "
            "at -1 (D-011, D-073).",
            "17.03", true};
}

std::vector<EffectLine> effect_lines(const core::WeaponProfile& weapon,
                                     std::span<const core::WeaponSelection> selections,
                                     const core::AttackContext& context, std::optional<core::ShootingType> type) {
    std::vector<EffectLine> out;
    std::vector<std::vector<core::AbilityEffect>> per;
    for (const core::WeaponSelection& s : selections) per.push_back(core::ability_effects(weapon, s, context));
    for (std::size_t i = 0; i < weapon.abilities.size(); ++i) {
        int extra = 0;
        int hit = 0;
        std::size_t active = 0;
        for (const auto& effects : per) {
            if (i >= effects.size()) continue;
            extra += effects[i].extra_attack_dice;
            hit = std::max(hit, effects[i].hit_modifier);
            active += effects[i].active ? 1 : 0;
        }
        const std::size_t n = per.size();
        const core::WeaponAbility& a = weapon.abilities[i];
        EffectLine line;
        if (const auto* rf = std::get_if<ab::RapidFire>(&a)) {
            const std::string half = weapon.range_mm ? inches(*weapon.range_mm / 2.0f) : std::string("?");
            line.text = "Rapid Fire " + std::to_string(rf->x) + ": ";
            if (active == 0) {
                line.text += "no extra attacks (target beyond half range, " + half + ")";
                line.kind = EffectKind::Inactive;
            } else {
                line.text += attacks(extra) + " (";
                line.text += active == n ? "target within half range, " + half
                                         : std::to_string(active) + " of " + models(n) + " within half range, " + half;
                line.text += ")";
                line.kind = EffectKind::Active;
            }
        } else if (const auto* blast = std::get_if<ab::Blast>(&a)) {
            const int target_models = selections.empty() ? 0 : selections.front().target_models_at_selection;
            line.text = "Blast " + std::to_string(blast->x) + ": ";
            if (active == 0) {
                line.text += "no extra attacks (fewer than 5 models in the target)";
                line.kind = EffectKind::Inactive;
            } else {
                line.text += attacks(extra) + " (" + std::to_string(target_models) + " models in the target)";
                line.kind = EffectKind::Active;
            }
        } else if (std::holds_alternative<ab::Heavy>(a)) {
            if (hit > 0) {
                line.text = "Heavy: +1 to hit (not engaged, no model moved more than 3\")";
                line.kind = EffectKind::Active;
            } else if (context.type == core::AttackType::Shooting) {
                line.text = "Heavy: no bonus (the unit moved more than 3\" or is engaged)";
                line.kind = EffectKind::Inactive;
            } else {
                line.text = "Heavy: no effect in the Fight phase";
                line.kind = EffectKind::Inactive;
            }
        } else if (std::holds_alternative<ab::Torrent>(a)) {
            line.text = "Torrent: hits automatically";
            line.kind = EffectKind::Active;
        } else if (const auto* sh = std::get_if<ab::SustainedHits>(&a)) {
            line.text = "Sustained Hits " + std::to_string(sh->x) + ": each critical hit scores " +
                        std::to_string(sh->x) + (sh->x == 1 ? " extra hit" : " extra hits");
            line.kind = EffectKind::Active;
        } else if (std::holds_alternative<ab::Hazardous>(a)) {
            line.text = "Hazardous: a hazard roll for the unit after its attacks (1-2: a mortal wound)";
            line.kind = EffectKind::Active;
        } else if (std::holds_alternative<ab::TwinLinked>(a)) {
            line.text = "Twin-linked: the wound rolls can be re-rolled";  // 24.38 (D-072)
            line.kind = EffectKind::Active;
        } else if (std::holds_alternative<ab::ExtraAttacks>(a)) {
            if (context.type == core::AttackType::Fighting) {
                line.text = "Extra Attacks: made in addition to the model's other melee weapon";
                line.kind = EffectKind::Active;
            } else {
                line.text = "Extra Attacks: fought with in addition to another melee weapon";
                line.kind = EffectKind::Info;
            }
        } else if (std::holds_alternative<ab::Assault>(a)) {
            if (type == core::ShootingType::Assault) {
                line.text = "Assault: firing after Advancing (Assault shooting)";
                line.kind = EffectKind::Active;
            } else {
                line.text = "Assault: can fire after Advancing";
                line.kind = EffectKind::Info;
            }
        } else if (std::holds_alternative<ab::CloseQuarters>(a)) {
            if (type == core::ShootingType::CloseQuarters) {
                line.text = "Close-quarters: firing while engaged (Close-quarters shooting)";
                line.kind = EffectKind::Active;
            } else {
                line.text = "Close-quarters: can fire while engaged";
                line.kind = EffectKind::Info;
            }
        } else {
            line.text = weapon_ability_label(a) + ": not in this game yet";
            line.kind = EffectKind::NotInGame;
        }
        out.push_back(std::move(line));
    }
    return out;
}

std::optional<HelpEntry> random_attacks_help(const core::WeaponProfile& weapon) {
    if (weapon.attacks.is_fixed()) return std::nullopt;
    HelpEntry h;
    h.title = "Random A (" + core::to_string(weapon.attacks) + ")";
    h.rule = "D-069";
    if (weapon.kind == core::WeaponKind::Melee) {
        h.summary = "Random Characteristics: a melee weapon with a random A is not in this game yet, so it can't be "
                    "selected.";
        return h;
    }
    h.summary = "Random Characteristics: rolled for each model and weapon when its attacks are resolved, before the hit "
                "rolls. [BLAST] and [RAPID FIRE] dice are added after the roll.";
    h.in_this_game = true;
    return h;
}

std::optional<HelpEntry> random_damage_help(const core::WeaponProfile& weapon) {
    if (weapon.damage.is_fixed()) return std::nullopt;
    HelpEntry h;
    h.title = "Random D (" + core::to_string(weapon.damage) + ")";
    h.rule = "D-069";
    h.summary = "Random Characteristics: rolled for each attack that inflicts damage, after the model it is allocated to "
                "is selected; excess damage is lost. The defender still orders the casualties once per batch: while "
                "picking, a mark shows the most the roll can do (\"-7?\").";
    h.in_this_game = true;
    return h;
}

std::vector<EffectLine> random_characteristic_lines(const core::WeaponProfile& weapon) {
    std::vector<EffectLine> out;
    if (!weapon.attacks.is_fixed() && weapon.kind == core::WeaponKind::Ranged) {
        out.push_back({"Attacks " + core::to_string(weapon.attacks) +
                           " per model, rolled when the attacks are resolved (D-069)",
                       EffectKind::Info});
    }
    if (!weapon.damage.is_fixed()) {
        out.push_back({"Damage " + core::to_string(weapon.damage) +
                           " per attack, rolled once its model is selected (D-069)",
                       EffectKind::Info});
    }
    return out;
}

std::string profile_stats(const core::WeaponProfile& weapon) {
    std::string out = weapon.range_mm ? inches(*weapon.range_mm) : std::string("Melee");
    out += "  A" + core::to_string(weapon.attacks);
    const char* skill = weapon.kind == core::WeaponKind::Melee ? "WS" : "BS";
    out += std::string("  ") + skill + (weapon.skill ? std::to_string(*weapon.skill) + "+" : std::string(" N/A"));
    out += "  S" + std::to_string(weapon.strength);
    out += "  AP" + std::to_string(weapon.armour_penetration);
    out += "  D" + core::to_string(weapon.damage);
    return out;
}

}  // namespace wh40k::client
