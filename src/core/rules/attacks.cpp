#include "core/rules/attacks.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

#include "core/geometry/closest.hpp"
#include "core/geometry/distance.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core {

namespace {

// D-006: abilities that still affect the attack from the hit roll onward, other than those already
// captured by AttackProfile fields ([HEAVY] -> hit_modifier, [TORRENT] -> torrent).
bool is_attack_time_ability(const WeaponAbility& a) {
    return std::visit(
        [](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            return !(std::is_same_v<T, ability::Assault> || std::is_same_v<T, ability::CloseQuarters> ||
                     std::is_same_v<T, ability::RapidFire> || std::is_same_v<T, ability::Blast> ||
                     std::is_same_v<T, ability::Heavy> || std::is_same_v<T, ability::Torrent>);
        },
        a);
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
        // 24.30 / D-009: the target unit is within half range if any of its models is.
        const PlacedModel from{model.id, model.position, model.base};
        const auto targets = placed_models(target_unit);
        const auto d = closest_distance(std::span<const PlacedModel>(&from, 1), targets);
        sel.target_within_half_range = d.has_value() && within(*d, *wp.range_mm * 0.5f);
    }
    return sel;
}

AttackProfile make_attack_profile(const WeaponProfile& weapon, const AttackContext& context) {
    AttackProfile p;
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
    for (const WeaponAbility& a : weapon.abilities) {
        if (is_attack_time_ability(a)) {
            p.attack_time_abilities.push_back(a);
        }
    }
    std::stable_sort(p.attack_time_abilities.begin(), p.attack_time_abilities.end(), ability_less);
    return p;
}

int gather_attack_dice(const WeaponProfile& weapon, const WeaponSelection& selection,
                       const AttackContext& context) {
    // 04.01: ranged attacks with ranged weapons while shooting, melee attacks with melee weapons while
    // fighting.
    const WeaponKind expected = context.type == AttackType::Shooting ? WeaponKind::Ranged : WeaponKind::Melee;
    if (weapon.kind != expected) {
        throw std::logic_error("04.01: profile '" + weapon.name + "' cannot be used for this attack type");
    }
    const int a = weapon.attacks.fixed_value();  // D-002: random A is not rolled in V1

    if (selection.declared_attacks) {
        // 04.02 Splitting Melee Attacks: gather only the attacks declared against this target.
        if (context.type != AttackType::Fighting) {
            throw std::logic_error("04.02: only melee attacks can be split between targets");
        }
        if (*selection.declared_attacks < 1 || *selection.declared_attacks > a) {
            throw std::logic_error("04.02: declared attacks must be between 1 and the weapon's A");
        }
        return *selection.declared_attacks;
    }

    int dice = a;
    if (const auto* rf = weapon.get<ability::RapidFire>(); rf != nullptr && selection.target_within_half_range) {
        dice += rf->x;  // 24.30
    }
    if (const auto* blast = weapon.get<ability::Blast>(); blast != nullptr) {
        dice += blast->x * (selection.target_models_at_selection / 5);  // 24.05, rounding down
    }
    return dice;
}

std::vector<AttackBatch> gather_attack_batches(const GameState& state,
                                               std::span<const WeaponSelection> selections,
                                               const AttackContext& context) {
    std::vector<AttackBatch> batches;
    std::vector<UnitId> target_order;  // 04.03 Select Enemy Unit: batches grouped per target
    for (std::size_t i = 0; i < selections.size(); ++i) {
        const WeaponSelection& sel = selections[i];
        const WeaponProfile& wp = selected_profile(state, sel);
        const AttackProfile profile = make_attack_profile(wp, context);
        const int dice = gather_attack_dice(wp, sel, context);

        if (std::find(target_order.begin(), target_order.end(), sel.target) == target_order.end()) {
            target_order.push_back(sel.target);
        }
        auto it = std::find_if(batches.begin(), batches.end(), [&](const AttackBatch& b) {
            return b.target == sel.target && b.profile == profile;
        });
        if (it == batches.end()) {
            batches.push_back(AttackBatch{sel.target, profile, {}, 0});
            it = std::prev(batches.end());
        }
        it->selections.push_back(i);
        it->attack_dice += dice;
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
