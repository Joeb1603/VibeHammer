#include "core/rules/shooting.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>

#include "core/geometry/closest.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/units.hpp"
#include "core/geometry/visibility.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/attacks.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core {

namespace {

struct ShootingTypeInfo {
    ShootingType type;
    std::string_view name;
    std::string_view rule;
};

constexpr std::array<ShootingTypeInfo, 3> kShootingTypes{{
    {ShootingType::Normal, "Normal", "10.04"},
    {ShootingType::Assault, "Assault", "10.05"},
    {ShootingType::CloseQuarters, "CloseQuarters", "10.06"},
}};

const ShootingTypeInfo& info(ShootingType t) {
    for (const auto& i : kShootingTypes) {
        if (i.type == t) return i;
    }
    throw std::logic_error("invalid ShootingType");
}

std::string unit_label(UnitId id) { return "unit " + std::to_string(id.v); }
std::string model_label(ModelId id) { return "model " + std::to_string(id.v); }

// [PISTOL] and [CLOSE-QUARTERS] are identical for all rules purposes (24.27); the loader parses both
// to ability::CloseQuarters.
bool is_close_quarters(const WeaponProfile& p) { return p.has<ability::CloseQuarters>(); }

// The unit has a ranged weapon with a profile that satisfies `pred`.
template <typename Pred>
bool unit_has_ranged_profile(const Unit& unit, Pred pred) {
    for (const Model& m : unit.models) {
        for (const Weapon& w : m.weapons) {
            for (const WeaponProfile& p : w.profiles) {
                if (p.kind == WeaponKind::Ranged && pred(p)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool unit_is_monster_or_vehicle(const Unit& unit) {
    return std::any_of(unit.models.begin(), unit.models.end(),
                       [&unit](const Model& m) { return is_monster_or_vehicle(unit, m); });
}

bool engaged_with(const GameState& state, const Unit& unit, UnitId other) {
    const auto engaged = engaged_enemy_units(state, unit);
    return std::find(engaged.begin(), engaged.end(), other) != engaged.end();
}

// 04.02 / 10.06: the target rules for one declaration (the target unit exists and is an enemy).
std::optional<std::string> target_problem(const GameState& state, const Unit& unit, const Model& model,
                                          const WeaponProfile& profile, ShootingType type, const Unit& target) {
    const bool close_quarters = type == ShootingType::CloseQuarters;
    const bool monster_vehicle = is_monster_or_vehicle(unit, model);
    const bool target_engaged_with_us = engaged_with(state, unit, target.id);
    if (close_quarters && !monster_vehicle) {
        // 10.06 Non-MONSTER/Non-VEHICLE Models: only enemy units engaged with your unit.
        if (!target_engaged_with_us) {
            return "10.06: " + model_label(model.id) + " can only target enemy units engaged with " +
                   unit_label(unit.id) + " in close-quarters shooting";
        }
    } else if (close_quarters && target_engaged_with_us) {
        // 10.06 MONSTER/VEHICLE Models: may target units they are engaged with, except with [BLAST].
        if (profile.has<ability::Blast>()) {
            return "10.06: '" + profile.name + "' is a [BLAST] weapon and cannot target " + unit_label(target.id) +
                   ", which " + unit_label(unit.id) + " is engaged with";
        }
    } else if (is_engaged(state, target)) {
        return "04.02: " + unit_label(target.id) + " is engaged; targets must be unengaged";
    }
    if (!unit_visible_to_model(state, model, target)) {
        return "06.01: " + unit_label(target.id) + " is not visible to " + model_label(model.id);
    }
    if (!target_within_range(model, profile, target)) {
        return "04.02: " + unit_label(target.id) + " is not within range (" + format_inches(*profile.range_mm) + ") of " +
               model_label(model.id) + "'s '" + profile.name + "'";
    }
    return std::nullopt;
}

// The checks on one declaration on its own (not 24.07 or duplicates, which need all of them).
std::optional<std::string> declaration_problem(const GameState& state, const Unit& unit, ShootingType type,
                                               const ShotDeclaration& shot) {
    const Model* model = find_model(unit, shot.model);
    if (model == nullptr) {
        return "04.01: " + model_label(shot.model) + " is not a model in " + unit_label(unit.id);
    }
    if (shot.weapon_index >= model->weapons.size()) {
        return "04.01: " + model_label(shot.model) + " has no weapon " + std::to_string(shot.weapon_index);
    }
    const Weapon& weapon = model->weapons[shot.weapon_index];
    if (shot.profile_index >= weapon.profiles.size()) {
        return "04.01: '" + weapon.name + "' has no profile " + std::to_string(shot.profile_index);
    }
    const WeaponProfile& profile = weapon.profiles[shot.profile_index];
    if (profile.kind != WeaponKind::Ranged || !profile.range_mm) {
        return "04.01: '" + profile.name + "' is not a ranged weapon; models shoot with ranged weapons";
    }
    if (!profile.attacks.is_fixed() || !profile.damage.is_fixed()) {
        return "D-002: '" + profile.name + "' has a random characteristic, which V1 does not roll";
    }
    if (auto p = unimplemented_ability_problem(profile)) return p;  // D-026
    if (type == ShootingType::Assault && !profile.has<ability::Assault>()) {
        return "10.05: '" + profile.name + "' is not an [ASSAULT] weapon; assault shooting can only select "
                                           "[ASSAULT] weapons";
    }
    if (type == ShootingType::CloseQuarters && !is_monster_or_vehicle(unit, *model) && !is_close_quarters(profile)) {
        return "10.06: '" + profile.name + "' is not a [CLOSE-QUARTERS] weapon; non-MONSTER/VEHICLE models can "
                                           "only select [CLOSE-QUARTERS] weapons";
    }
    const Unit* target = state.find_unit(shot.target);
    if (target == nullptr || target->side == unit.side) {
        return "04.02: the target of '" + profile.name + "' must be an enemy unit";
    }
    if (is_destroyed(*target)) {
        return "04.02: " + unit_label(shot.target) + " is not on the battlefield";
    }
    return target_problem(state, unit, *model, profile, type, *target);
}

}  // namespace

std::string_view shooting_type_name(ShootingType t) { return info(t).name; }
std::string_view shooting_type_rule(ShootingType t) { return info(t).rule; }

std::optional<ShootingType> shooting_type_from_name(std::string_view name) {
    for (const auto& i : kShootingTypes) {
        if (i.name == name) return i.type;
    }
    return std::nullopt;
}

std::optional<std::string> unit_shoot_problem(const GameState&, const Unit& unit) {
    if (is_destroyed(unit)) {
        return "10.02: " + unit_label(unit.id) + " is not on the battlefield";
    }
    if (unit.phase_flags.selected_to_shoot) {
        return "10.02: " + unit_label(unit.id) + " has already been selected to shoot this phase";
    }
    if (unit.turn_flags.fell_back) {
        return "09.07: " + unit_label(unit.id) + " made a fall-back move this turn and is not eligible to shoot";
    }
    return std::nullopt;
}

std::optional<std::string> shooting_type_problem(const GameState& state, const Unit& unit, ShootingType type) {
    const std::string rule(shooting_type_rule(type));
    const std::string who = unit_label(unit.id);
    const bool engaged = is_engaged(state, unit);
    const bool advanced = unit.turn_flags.advanced;
    switch (type) {
        case ShootingType::Normal:
            if (engaged) return rule + ": " + who + " is engaged; normal shooting needs an unengaged unit";
            if (advanced) return rule + ": " + who + " made an advance move this turn";
            return std::nullopt;
        case ShootingType::Assault:
            if (engaged) return rule + ": " + who + " is engaged; assault shooting needs an unengaged unit";
            if (!advanced) return rule + ": " + who + " did not make an advance move this turn";
            if (!unit_has_ranged_profile(unit, [](const WeaponProfile& p) { return p.has<ability::Assault>(); })) {
                return rule + ": " + who + " has no [ASSAULT] weapons";  // 24.04
            }
            return std::nullopt;
        case ShootingType::CloseQuarters:
            if (!engaged) return rule + ": " + who + " is unengaged; close-quarters shooting needs an engaged unit";
            if (advanced) return rule + ": " + who + " made an advance move this turn";
            if (!unit_is_monster_or_vehicle(unit) && !unit_has_ranged_profile(unit, is_close_quarters)) {
                return rule + ": " + who + " has no [CLOSE-QUARTERS] weapons and is not a MONSTER/VEHICLE unit";
            }
            return std::nullopt;
    }
    throw std::logic_error("invalid ShootingType");
}

std::vector<ShootingType> eligible_shooting_types(const GameState& state, const Unit& unit) {
    std::vector<ShootingType> out;
    if (unit_shoot_problem(state, unit)) {
        return out;
    }
    for (const auto& i : kShootingTypes) {
        if (!shooting_type_problem(state, unit, i.type)) {
            out.push_back(i.type);
        }
    }
    return out;
}

bool unit_visible_to_model(const GameState& state, const Model& observer, const Unit& target) {
    const Unit* own = find_unit_of_model(state, observer.id);
    std::vector<PlacedModel> blockers;
    for (const Unit& u : state.units) {
        if (u.id == target.id || (own != nullptr && u.id == own->id)) {
            continue;  // 06.01: models in the observing and observed units are ignored
        }
        const auto models = placed_models(u);
        blockers.insert(blockers.end(), models.begin(), models.end());
    }
    const PlacedModel from{observer.id, observer.position, observer.base};
    for (const PlacedModel& to : placed_models(target)) {
        if (has_line_of_sight(from, to, blockers)) {
            return true;  // 06.01 Unit Visible: one or more of its models are visible
        }
    }
    return false;
}

bool target_within_range(const Model& model, const WeaponProfile& profile, const Unit& target) {
    if (!profile.range_mm) {
        return false;
    }
    const PlacedModel from{model.id, model.position, model.base};
    const auto targets = placed_models(target);
    const auto d = closest_distance(std::span<const PlacedModel>(&from, 1), targets);
    return d.has_value() && within(*d, *profile.range_mm);  // D-009 Within, 01.04 edge to edge
}

std::vector<std::string> shot_problems(const GameState& state, const Unit& unit, ShootingType type,
                                       std::span<const ShotDeclaration> shots) {
    std::vector<std::string> out;
    if (shots.empty()) {
        out.push_back("D-016: select at least one weapon and target for " + unit_label(unit.id) +
                      " (an empty Shoot action is the template legal_actions returns)");
        return out;
    }
    for (std::size_t i = 0; i < shots.size(); ++i) {
        if (auto p = declaration_problem(state, unit, type, shots[i])) {
            out.push_back(std::move(*p));
            continue;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (shots[j].model == shots[i].model && shots[j].weapon_index == shots[i].weapon_index) {
                // 04.01 selects weapons, and each fires one profile (D-001, D-016).
                out.push_back("04.01: " + model_label(shots[i].model) + " selected weapon " +
                              std::to_string(shots[i].weapon_index) + " more than once (D-016: one profile and "
                              "one target per weapon)");
                break;
            }
        }
    }
    if (!out.empty() || type == ShootingType::CloseQuarters) {
        return out;
    }
    // 24.07: in another shooting type, each non-MONSTER/VEHICLE model selects either its
    // [CLOSE-QUARTERS] weapons or its other ranged weapons, not both.
    for (const Model& model : unit.models) {
        if (is_monster_or_vehicle(unit, model)) {
            continue;
        }
        bool cq = false;
        bool other = false;
        for (const ShotDeclaration& s : shots) {
            if (s.model == model.id) {
                (is_close_quarters(model.weapons[s.weapon_index].profiles[s.profile_index]) ? cq : other) = true;
            }
        }
        if (cq && other) {
            out.push_back("24.07: " + model_label(model.id) + " can select either its [CLOSE-QUARTERS] weapons or "
                          "its other ranged weapons, not both");
        }
    }
    return out;
}

std::vector<ShotDeclaration> shot_options(const GameState& state, const Unit& unit, ShootingType type) {
    std::vector<ShotDeclaration> out;
    for (const Model& model : unit.models) {
        for (std::size_t w = 0; w < model.weapons.size(); ++w) {
            for (std::size_t p = 0; p < model.weapons[w].profiles.size(); ++p) {
                for (const Unit& target : state.units) {
                    if (target.side == unit.side || is_destroyed(target)) {
                        continue;
                    }
                    const ShotDeclaration shot{model.id, w, p, target.id};
                    if (!declaration_problem(state, unit, type, shot)) {
                        out.push_back(shot);
                    }
                }
            }
        }
    }
    return out;
}

AttackContext shooting_context(const GameState& state, const Unit& unit) {
    AttackContext context;
    context.type = AttackType::Shooting;
    // 24.16: unengaged, not set up on the battlefield this turn (V1 has no reserves or deployment
    // during the battle, D-003), and no model has moved more than 3" this turn.
    context.heavy_conditions_met = !is_engaged(state, unit) && !unit.turn_flags.moved_over_3in;
    return context;
}

std::vector<WeaponSelection> make_weapon_selections(const GameState& state, const Unit& unit, ShootingType type,
                                                    std::span<const ShotDeclaration> shots) {
    std::vector<WeaponSelection> out;
    for (const ShotDeclaration& s : shots) {
        WeaponSelection sel = select_weapon_target(state, s.model, s.weapon_index, s.profile_index, s.target);
        const Model& model = *find_model(unit, s.model);
        if (type == ShootingType::CloseQuarters && is_monster_or_vehicle(unit, model)) {
            // 10.06 MONSTER/VEHICLE Models: -1 to hit unless a [CLOSE-QUARTERS] weapon targets a unit this
            // unit is engaged with.
            const WeaponProfile& profile = model.weapons[s.weapon_index].profiles[s.profile_index];
            if (!(is_close_quarters(profile) && engaged_with(state, unit, s.target))) {
                sel.hit_modifier = -1;
            }
        }
        out.push_back(sel);
    }
    return out;
}

}  // namespace wh40k::core
