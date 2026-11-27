#include "core/rules/attacks.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "core/geometry/closest.hpp"
#include "core/geometry/distance.hpp"
#include "core/model/keywords.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core {

namespace {

// 02.02.01 Modifiers (app-only, D-093, M92): "BS cannot be 1+ (or better) or 7+ (or worse)" after all modifiers.
constexpr int kWorstSkill = 6;

// D-006: abilities that still affect the attack from the hit roll onward, other than those already
// captured by AttackProfile fields ([HEAVY] -> hit_modifier, [TORRENT] -> torrent). [EXTRA ATTACKS] is used
// up in the Select Weapons step (24.11, D-071), like [ASSAULT]. [IGNORES COVER] is used up in the Select Targets step,
// where the selection's benefit of cover is decided (24.18, 13.08; M90, D-091). M99 (D-101): [LANCE] is the profile's
// wound modifier, [MELTA X] its D and [ANTI-X Y+] its critical wound threshold, so they are not kept either.
bool is_attack_time_ability(const WeaponAbility& a) {
    return std::visit(
        [](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            return !(std::is_same_v<T, ability::Assault> || std::is_same_v<T, ability::CloseQuarters> ||
                     std::is_same_v<T, ability::RapidFire> || std::is_same_v<T, ability::Blast> ||
                     std::is_same_v<T, ability::Heavy> || std::is_same_v<T, ability::Torrent> ||
                     std::is_same_v<T, ability::ExtraAttacks> || std::is_same_v<T, ability::IgnoresCover> ||
                     std::is_same_v<T, ability::Anti> || std::is_same_v<T, ability::Melta> ||
                     std::is_same_v<T, ability::Lance> ||
                     // M100 (D-102): [CLEAVE] is gathered dice, [ONE SHOT] is used up when the weapon is selected.
                     std::is_same_v<T, ability::Cleave> || std::is_same_v<T, ability::OneShot>);
        },
        a);
}

std::string upper_ascii(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return s;
}

// Canonical order so that ability order in the data file doesn't split identical attacks.
bool ability_less(const WeaponAbility& a, const WeaponAbility& b) {
    if (a.index() != b.index()) {
        return a.index() < b.index();
    }
    const auto* ua = std::get_if<ability::Unknown>(&a);
    const auto* ub = std::get_if<ability::Unknown>(&b);
    return ua != nullptr && ub != nullptr && ua->text < ub->text;
}

const Unit& require_unit(const GameState& state, UnitId id) {
    const Unit* u = state.find_unit(id);
    if (u == nullptr) {
        throw std::logic_error("attacks: unknown unit " + std::to_string(id.v));
    }
    return *u;
}

const Model& require_model(const GameState& state, ModelId id) {
    const Model* m = find_model(state, id);
    if (m == nullptr) {
        throw std::logic_error("attacks: unknown model " + std::to_string(id.v));
    }
    return *m;
}

const WeaponProfile& profile_of(const Model& model, std::size_t weapon_index, std::size_t profile_index) {
    if (weapon_index >= model.weapons.size() ||
        profile_index >= model.weapons[weapon_index].profiles.size()) {
        throw std::logic_error("attacks: model " + std::to_string(model.id.v) +
                               " has no weapon/profile at that index");
    }
    return model.weapons[weapon_index].profiles[profile_index];
}

}  // namespace

const WeaponProfile& selected_profile(const GameState& state, const WeaponSelection& sel) {
    return profile_of(require_model(state, sel.attacker), sel.weapon_index, sel.profile_index);
}

WeaponSelection select_weapon_target(const GameState& state, ModelId attacker, std::size_t weapon_index,
                                     std::size_t profile_index, UnitId target,
                                     std::optional<int> declared_attacks) {
    const Model& model = require_model(state, attacker);
    const WeaponProfile& wp = profile_of(model, weapon_index, profile_index);
    const Unit& target_unit = require_unit(state, target);

    WeaponSelection sel;
    sel.attacker = attacker;
    sel.weapon_index = weapon_index;
    sel.profile_index = profile_index;
    sel.target = target;
    sel.declared_attacks = declared_attacks;
    sel.target_models_at_selection = static_cast<int>(target_unit.models.size());
    if (wp.range_mm) {
        // 24.30 / D-009: the target unit is within half range if any of its models is (D-074: each base with its
        // facing).
        const PosedModel from = posed_of(model);
        const auto targets = posed_models(target_unit);
        const auto d = closest_distance(std::span<const PosedModel>(&from, 1), std::span<const PosedModel>(targets));
        sel.target_within_half_range = d.has_value() && within(*d, *wp.range_mm * 0.5f);
    }
    return sel;
}

std::optional<std::string> unimplemented_ability_problem(const WeaponProfile& weapon) {
    // D-069: a random A on a melee weapon is not implemented (04.02 splits a known number of attacks; no melee profile
    // in the data has one). A random A or D is rolled otherwise.
    if (weapon.kind == WeaponKind::Melee && !weapon.attacks.is_fixed()) {
        return "D-069: '" + weapon.name + "' has a random A (" + to_string(weapon.attacks) +
               "), which the engine does not implement for melee weapons (none in the data); it can't be selected";
    }
    // [TWIN-LINKED] (24.38) is implemented since M61 (D-072); [DEVASTATING WOUNDS] (24.10), [ANTI-X Y+] (24.03),
    // [MELTA X] (24.25) and [LANCE] (24.21) since M99 (D-101).
    // 24.23 (M100, D-102): "you can choose for that attack to automatically wound": the choice only matters when a
    // critical wound does more than wound, i.e. with [DEVASTATING WOUNDS]. The engine takes the automatic wound without
    // asking, so a profile with both is refused rather than resolved without the choice.
    if (weapon.has<ability::LethalHits>() && weapon.has<ability::DevastatingWounds>()) {
        return "24.23: '" + weapon.name + "' has both [LETHAL HITS] and [DEVASTATING WOUNDS]; the choice between an "
               "automatic wound and a wound roll is not implemented yet (D-102), so it can't be selected";
    }
    // D-071: an ability the engine does not recognise ([PRECISION], [INDIRECT FIRE], ...) is refused, rather than
    // ignored.
    if (const auto* unknown = weapon.get<ability::Unknown>()) {
        return "D-071: '" + weapon.name + "' has [" + upper_ascii(unknown->text) +
               "], which the engine does not implement yet; weapons with it can't be selected";
    }
    return std::nullopt;
}

int sustained_hits(const AttackProfile& profile) {
    int x = 0;
    for (const WeaponAbility& a : profile.attack_time_abilities) {
        if (const auto* s = std::get_if<ability::SustainedHits>(&a)) x = std::max(x, s->x);  // D-071 (24.02)
    }
    return x;
}

bool lethal_hits(const AttackProfile& profile) {
    return std::any_of(profile.attack_time_abilities.begin(), profile.attack_time_abilities.end(),
                       [](const WeaponAbility& a) { return std::holds_alternative<ability::LethalHits>(a); });
}

bool condition_met(const ability::TargetCondition& condition, const Unit& target) {
    if (condition.always()) return true;
    return has_any_keyword(target, condition.keywords) != condition.non;  // app 02.05.01: "non-X"
}

bool ignores_hit_penalties(const WeaponProfile& weapon) { return weapon.has<ability::Psychic>(); }

std::optional<std::string> one_shot_problem(const Weapon& weapon, const WeaponProfile& profile) {
    if (weapon.one_shot_spent && profile.has<ability::OneShot>()) {
        return "24.26: '" + profile.name + "' is a [ONE SHOT] weapon that has already been selected to make attacks "
               "with this battle";
    }
    return std::nullopt;
}

void spend_one_shot_weapons(GameState& state, std::span<const WeaponSelection> selections) {
    for (const WeaponSelection& sel : selections) {
        if (!selected_profile(state, sel).has<ability::OneShot>()) continue;
        for (Unit& u : state.units) {
            if (Model* m = find_model(u, sel.attacker)) m->weapons[sel.weapon_index].one_shot_spent = true;
        }
    }
}

bool devastating_wounds(const AttackProfile& profile) {
    return std::any_of(profile.attack_time_abilities.begin(), profile.attack_time_abilities.end(),
                       [](const WeaponAbility& a) { return std::holds_alternative<ability::DevastatingWounds>(a); });
}

bool anti_applies(const ability::Anti& anti, const Unit& target) {
    return has_any_keyword(target, anti.keywords) != anti.non;  // app 02.05.01: "non-X" is a unit without the keyword
}

int anti_critical_wound_on(const WeaponProfile& weapon, const Unit& target) {
    int on = 6;  // 05.02: an unmodified 6
    for (const WeaponAbility& a : weapon.abilities) {
        const auto* anti = std::get_if<ability::Anti>(&a);
        if (anti != nullptr && anti_applies(*anti, target)) on = std::min(on, anti->threshold);  // D-101 (24.02)
    }
    return on;
}

AttackProfile selection_attack_profile(const WeaponProfile& weapon, const WeaponSelection& selection,
                                       const AttackContext& context, const Unit& target) {
    AttackProfile profile = make_attack_profile(weapon, context);
    // (24.29, D-102: a [PSYCHIC] weapon's selection records no -1 and no cover, make_weapon_selections.)
    profile.hit_modifier += selection.hit_modifier;  // e.g. 10.06; part of the profile, so D-006 compares it
    // 13.08 (D-089): worsen the BS characteristic of the attack by 1 (3+ becomes 4+). The characteristic, not the
    // hit roll, so D-011's cap doesn't apply; [TORRENT] has no BS. Part of the profile, so D-006 keeps attacks with
    // and without cover in separate batches.
    // 02.02.01 (app-only, D-093, M92): after all modifiers a BS cannot be 7+ or worse, so 6+ stays 6+.
    if (selection.benefit_of_cover && profile.skill) *profile.skill = std::min(*profile.skill + 1, kWorstSkill);
    apply_target_abilities(profile, weapon, selection, target);
    return profile;
}

void apply_target_abilities(AttackProfile& profile, const WeaponProfile& weapon, const WeaponSelection& selection,
                            const Unit& target) {
    // 24.25 [MELTA X] (D-101): "if the target unit was within half range of that weapon in the Select Targets step ...
    // add X to that weapon's D characteristic". With two instances (24.02) the highest X is the player's best choice.
    if (selection.target_within_half_range) {
        int x = 0;
        for (const WeaponAbility& a : weapon.abilities) {
            if (const auto* melta = std::get_if<ability::Melta>(&a)) x = std::max(x, melta->x);
        }
        profile.damage.modifier = static_cast<std::int16_t>(profile.damage.modifier + x);
    }
    profile.critical_wound_on = anti_critical_wound_on(weapon, target);  // 24.03
    // 24.01 (M100, D-102): an ability followed by keywords "only applies if the target unit has one or more of those
    // keywords". One the target doesn't meet is left out; one it meets is kept without its condition, so attacks that
    // are identical from here on compare as identical (D-006).
    auto& abilities = profile.attack_time_abilities;
    for (auto it = abilities.begin(); it != abilities.end();) {
        const bool keep = std::visit(
            [&target](auto& a) {
                using T = std::decay_t<decltype(a)>;
                if constexpr (std::is_same_v<T, ability::LethalHits> || std::is_same_v<T, ability::DevastatingWounds> ||
                              std::is_same_v<T, ability::SustainedHits>) {
                    if (!condition_met(a.only_against, target)) return false;
                    a.only_against = {};
                }
                return true;
            },
            *it);
        it = keep ? std::next(it) : abilities.erase(it);
    }
}

AttackContext fighting_context(const Unit& unit) {
    AttackContext context;
    context.type = AttackType::Fighting;
    context.charged = unit.turn_flags.made_charge_move;  // 24.21 [LANCE] (11.04)
    return context;
}

bool is_extra_attacks_weapon(const Weapon& weapon) {
    return std::any_of(weapon.profiles.begin(), weapon.profiles.end(), [](const WeaponProfile& p) {
        return p.kind == WeaponKind::Melee && p.has<ability::ExtraAttacks>();
    });
}

int hazardous_weapons_selected(const GameState& state, std::span<const WeaponSelection> selections) {
    std::vector<std::pair<std::uint32_t, std::size_t>> seen;  // (model, weapon): a split weapon counts once
    for (const WeaponSelection& sel : selections) {
        if (!selected_profile(state, sel).has<ability::Hazardous>()) continue;
        const std::pair<std::uint32_t, std::size_t> key{sel.attacker.v, sel.weapon_index};
        if (std::find(seen.begin(), seen.end(), key) == seen.end()) seen.push_back(key);
    }
    return static_cast<int>(seen.size());
}

AttackProfile make_attack_profile(const WeaponProfile& weapon, const AttackContext& context) {
    AttackProfile p;
    p.snap_shooting = context.snap_shooting;  // D-097: 15.09 (part of D-006's identical attacks)
    p.torrent = weapon.has<ability::Torrent>();
    if (!p.torrent) {
        if (!weapon.skill) {
            throw std::logic_error("05.01: weapon profile '" + weapon.name + "' has no BS/WS");
        }
        p.skill = weapon.skill;
    }
    p.strength = weapon.strength;
    p.armour_penetration = weapon.armour_penetration;
    p.damage = weapon.damage;
    // 24.16 [HEAVY]: +1 to the hit roll in your Shooting phase when the conditions apply.
    if (context.type == AttackType::Shooting && context.heavy_conditions_met &&
        weapon.has<ability::Heavy>()) {
        p.hit_modifier += 1;
    }
    // 24.21 [LANCE] (M99, D-101): +1 to the wound roll if the attacking model's unit made a charge move this turn.
    if (context.charged && weapon.has<ability::Lance>()) p.wound_modifier += 1;
    for (const WeaponAbility& a : weapon.abilities) {
        if (is_attack_time_ability(a)) {
            p.attack_time_abilities.push_back(a);
        }
    }
    std::stable_sort(p.attack_time_abilities.begin(), p.attack_time_abilities.end(), ability_less);
    return p;
}

SelectionDice selection_dice(const WeaponProfile& weapon, const WeaponSelection& selection,
                             const AttackContext& context) {
    // 04.01: ranged attacks with ranged weapons while shooting, melee attacks with melee weapons while
    // fighting.
    const WeaponKind expected = context.type == AttackType::Shooting ? WeaponKind::Ranged : WeaponKind::Melee;
    if (weapon.kind != expected) {
        throw std::logic_error("04.01: profile '" + weapon.name + "' cannot be used for this attack type");
    }
    const bool random = !weapon.attacks.is_fixed();
    if (random && weapon.kind == WeaponKind::Melee) {
        // D-069: refused when selected (unimplemented_ability_problem); this is the safety net.
        throw std::logic_error("D-069: '" + weapon.name + "' has a random A, which is not implemented for melee weapons");
    }
    SelectionDice out;
    if (selection.declared_attacks) {
        const int a = weapon.attacks.fixed_value();
        // 04.02 Splitting Melee Attacks: gather only the attacks declared against this target.
        if (context.type != AttackType::Fighting) {
            throw std::logic_error("04.02: only melee attacks can be split between targets");
        }
        if (*selection.declared_attacks < 1 || *selection.declared_attacks > a) {
            throw std::logic_error("04.02: declared attacks must be between 1 and the weapon's A");
        }
        out.dice = *selection.declared_attacks;
        return out;
    }

    int bonus = 0;
    if (const auto* rf = weapon.get<ability::RapidFire>(); rf != nullptr && selection.target_within_half_range) {
        bonus += rf->x;  // 24.30
    }
    if (const auto* blast = weapon.get<ability::Blast>(); blast != nullptr) {
        bonus += blast->x * (selection.target_models_at_selection / 5);  // 24.05, rounding down
    }
    // 24.06 [CLEAVE X] (M100, D-102): "if you only selected one target for all of that weapon's attacks" (a split
    // weapon returned above), X dice per five models in the target at Select Targets.
    if (const auto* cleave = weapon.get<ability::Cleave>(); cleave != nullptr) {
        bonus += cleave->x * (selection.target_models_at_selection / 5);
    }
    if (random) {
        out.random = weapon.attacks;  // D-069: rolled for this model and weapon when its batch is resolved
        out.bonus = bonus;            // added after the roll
    } else {
        out.dice = weapon.attacks.fixed_value() + bonus;
    }
    return out;
}

int gather_attack_dice(const WeaponProfile& weapon, const WeaponSelection& selection, const AttackContext& context) {
    const SelectionDice d = selection_dice(weapon, selection, context);
    if (d.random) {
        throw std::logic_error("D-069: '" + weapon.name + "' has a random A (" + to_string(*d.random) +
                               "), which is rolled when its batch is resolved (selection_dice)");
    }
    return d.dice;
}

std::vector<AbilityEffect> ability_effects(const WeaponProfile& weapon, const WeaponSelection& selection,
                                           const AttackContext& context, const Unit* target) {
    std::vector<AbilityEffect> out;
    const bool split = selection.declared_attacks.has_value();  // 04.02: exactly the declared attacks
    for (const WeaponAbility& a : weapon.abilities) {
        AbilityEffect e;
        e.ability = a;
        if (const auto* rf = std::get_if<ability::RapidFire>(&a)) {
            if (!split && selection.target_within_half_range) e.extra_attack_dice = rf->x;  // 24.30
        } else if (const auto* blast = std::get_if<ability::Blast>(&a)) {
            if (!split) e.extra_attack_dice = blast->x * (selection.target_models_at_selection / 5);  // 24.05
        } else if (std::holds_alternative<ability::Heavy>(a)) {
            if (context.type == AttackType::Shooting && context.heavy_conditions_met) e.hit_modifier = 1;  // 24.16
        } else if (std::holds_alternative<ability::Torrent>(a)) {
            e.active = true;  // 24.37
        } else if (std::holds_alternative<ability::SustainedHits>(a) || std::holds_alternative<ability::Hazardous>(a) ||
                   std::holds_alternative<ability::TwinLinked>(a)) {
            // 24.36 on each critical hit; 24.15 a hazard roll after the unit's attacks (D-071); 24.38 the wound rolls
            // can be re-rolled (D-072)
            e.active = true;
            if (const auto* sustained = std::get_if<ability::SustainedHits>(&a)) {
                e.condition_unmet = target != nullptr && !condition_met(sustained->only_against, *target);  // 24.01
            }
        } else if (std::holds_alternative<ability::ExtraAttacks>(a)) {
            e.active = context.type == AttackType::Fighting;  // 24.11: made in addition to another melee weapon
        } else if (std::holds_alternative<ability::IgnoresCover>(a)) {
            e.active = context.type == AttackType::Shooting;  // 24.18: the target can't have the benefit of cover
        } else if (const auto* dw = std::get_if<ability::DevastatingWounds>(&a)) {
            e.active = true;  // 24.10: on each critical wound (M99, D-101)
            e.condition_unmet = target != nullptr && !condition_met(dw->only_against, *target);  // 24.01 (M100)
        } else if (const auto* lethal = std::get_if<ability::LethalHits>(&a)) {
            e.active = true;  // 24.23: each critical hit wounds automatically (M100, D-102)
            e.condition_unmet = target != nullptr && !condition_met(lethal->only_against, *target);
        } else if (std::holds_alternative<ability::Psychic>(a)) {
            e.active = true;  // 24.29: hit penalties ignored
        } else if (const auto* cleave = std::get_if<ability::Cleave>(&a)) {
            if (!split) e.extra_attack_dice = cleave->x * (selection.target_models_at_selection / 5);  // 24.06
        } else if (std::holds_alternative<ability::OneShot>(a)) {
            e.active = true;  // 24.26: this is its one use
        } else if (std::holds_alternative<ability::Lance>(a)) {
            if (context.charged) e.wound_modifier = 1;  // 24.21
        } else if (const auto* melta = std::get_if<ability::Melta>(&a)) {
            if (selection.target_within_half_range) e.damage_modifier = melta->x;  // 24.25
        } else if (const auto* anti = std::get_if<ability::Anti>(&a)) {
            if (target != nullptr && anti_applies(*anti, *target)) e.critical_wound_on = anti->threshold;  // 24.03
        }
        e.active = (e.active || e.extra_attack_dice > 0 || e.hit_modifier != 0 || e.wound_modifier != 0 ||
                    e.damage_modifier != 0 || e.critical_wound_on != 0) &&
                   !e.condition_unmet;
        out.push_back(e);
    }
    return out;
}

AttackContext batch_context(const AttackBatch& batch, const WeaponSelection& selection, AttackType type) {
    AttackContext context;
    context.type = type;
    context.heavy_conditions_met = type == AttackType::Shooting && batch.profile.hit_modifier > selection.hit_modifier;
    context.charged = batch.profile.wound_modifier > 0;  // 24.21 [LANCE] (M99)
    return context;
}

std::vector<AttackBatch> gather_attack_batches(const GameState& state,
                                               std::span<const WeaponSelection> selections,
                                               const AttackContext& context) {
    std::vector<AttackBatch> batches;
    std::vector<UnitId> target_order;  // 04.03 Select Enemy Unit: batches grouped per target
    for (std::size_t i = 0; i < selections.size(); ++i) {
        const WeaponSelection& sel = selections[i];
        const WeaponProfile& wp = selected_profile(state, sel);
        // The hit modifier, cover, [MELTA] and [ANTI] of this selection against its target (D-089, D-101).
        const AttackProfile profile = selection_attack_profile(wp, sel, context, require_unit(state, sel.target));
        const SelectionDice dice = selection_dice(wp, sel, context);

        if (std::find(target_order.begin(), target_order.end(), sel.target) == target_order.end()) {
            target_order.push_back(sel.target);
        }
        auto it = std::find_if(batches.begin(), batches.end(), [&](const AttackBatch& b) {
            return b.target == sel.target && b.profile == profile;
        });
        if (it == batches.end()) {
            batches.push_back(AttackBatch{sel.target, profile, {}, 0, {}});
            it = std::prev(batches.end());
        }
        it->selections.push_back(i);
        if (dice.random) {
            // D-069: rolled for this model and weapon when the batch is gathered for resolution (04.03).
            it->random_attacks.push_back(RandomAttackDice{i, sel.attacker, *dice.random, dice.bonus});
        } else {
            it->attack_dice += dice.dice;
        }
    }
    // Keep every target's batches together, targets in first-appearance order.
    std::stable_sort(batches.begin(), batches.end(), [&](const AttackBatch& a, const AttackBatch& b) {
        const auto ia = std::find(target_order.begin(), target_order.end(), a.target);
        const auto ib = std::find(target_order.begin(), target_order.end(), b.target);
        return ia < ib;
    });
    return batches;
}

}  // namespace wh40k::core
