#include "core/rules/shooting.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

#include "core/geometry/closest.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/units.hpp"
#include "core/geometry/visibility.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/terrain.hpp"
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

// 06.01: the models that can block a line of sight from a model of `own` to `target`: every model on the
// battlefield except those in the observing and observed units. D-074: each with its facing.
std::vector<PosedModel> visibility_blockers(const GameState& state, const Unit* own, const Unit& target) {
    std::vector<PosedModel> blockers;
    for (const Unit& u : state.units) {
        if (u.id == target.id || (own != nullptr && u.id == own->id)) {
            continue;  // 06.01: models in the observing and observed units are ignored
        }
        for (const Model& m : u.models) blockers.push_back(posed_of(m));
    }
    return blockers;
}

// 06.01 Unit Visible: one or more of the target's models are visible to `observer` past `blockers` (D-074: the
// posed overload; with only round bases it is D-017's answer, bit-identical).
bool visible_past(const Model& observer, const Unit& target, std::span<const PosedModel> blockers) {
    const PosedModel from = posed_of(observer);
    for (const Model& m : target.models) {
        if (has_line_of_sight(from, posed_of(m), blockers)) {
            return true;
        }
    }
    return false;
}

// M88 (D-088): the same with terrain: 13.09 Hidden, 13.10 Obscuring and 13.11 Solid (model_visible).
bool visible_past(TerrainView& view, const Model& observer, const Unit& target, std::span<const PosedModel> blockers) {
    for (const Model& m : target.models) {
        if (model_visible(view, observer, target, m, blockers)) return true;
    }
    return false;
}

// M54: the facts the declarations of one unit share, each worked out the first time a declaration needs it
// (the same queries a declaration made on its own before): the enemy units the shooting unit is engaged with
// (04.02, 10.06), whether a target unit is engaged (04.02) and whether a target unit is visible to a model
// (06.01). Only for one state and one shooting unit.
class TargetFacts {
public:
    TargetFacts(const GameState& state, const Unit& unit) : state_(state), unit_(unit) {}

    // engaged_with(state, unit, target)
    bool engaged_with_us(UnitId target) {
        if (!engaged_) engaged_ = engaged_enemy_units(state_, unit_);
        return std::find(engaged_->begin(), engaged_->end(), target) != engaged_->end();
    }
    // is_engaged(state, target)
    bool target_engaged(const Unit& target) {
        for (const auto& [id, engaged] : target_engaged_) {
            if (id == target.id) return engaged;
        }
        const bool engaged = is_engaged(state_, target);
        target_engaged_.emplace_back(target.id, engaged);
        return engaged;
    }
    // unit_visible_to_model(state, model, target) (the blockers built once per observing and target unit)
    bool visible(const Model& model, const Unit& target) {
        const auto key = std::make_pair(model.id.v, target.id.v);
        const auto it = visible_.find(key);
        if (it != visible_.end()) return it->second;
        const Unit* own = find_unit_of_model(state_, model.id);
        const auto bkey = std::make_pair(own != nullptr ? std::uint64_t{own->id.v} + 1 : 0, target.id.v);
        auto b = blockers_.find(bkey);
        if (b == blockers_.end()) b = blockers_.emplace(bkey, visibility_blockers(state_, own, target)).first;
        bool v = false;
        if (state_.terrain.empty()) {
            v = visible_past(model, target, b->second);
        } else {
            if (!terrain_) terrain_.emplace(state_);  // M88: shared by every declaration of this unit
            v = visible_past(*terrain_, model, target, b->second);
        }
        visible_.emplace(key, v);
        return v;
    }
    // The 06.01 reason `target` can't be selected by `model` (only when it isn't visible). M88 (D-088): with terrain,
    // visibility_problem's (13.09, 13.10 / 13.11 or 06.01).
    std::string not_visible_reason(const Model& model, const Unit& target) const {
        if (state_.terrain.empty()) {
            return "06.01: " + unit_label(target.id) + " is not visible to " + model_label(model.id);
        }
        return visibility_problem(state_, model, target).text;
    }

private:
    const GameState& state_;
    const Unit& unit_;
    std::optional<std::vector<UnitId>> engaged_;
    std::vector<std::pair<UnitId, bool>> target_engaged_;
    std::map<std::pair<std::uint32_t, std::uint32_t>, bool> visible_;  // (model, target unit)
    // by (observing unit id + 1, or 0 for none; target unit)
    std::map<std::pair<std::uint64_t, std::uint32_t>, std::vector<PosedModel>> blockers_;
    std::optional<TerrainView> terrain_;  // M88: only with terrain
};

// 04.02 / 10.06: the target rules for one declaration (the target unit exists and is an enemy).
std::optional<std::string> target_problem(const Unit& unit, const Model& model, const WeaponProfile& profile,
                                          ShootingType type, const Unit& target, TargetFacts& facts) {
    const bool close_quarters = type == ShootingType::CloseQuarters;
    const bool monster_vehicle = is_monster_or_vehicle(unit, model);
    const bool target_engaged_with_us = facts.engaged_with_us(target.id);
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
    } else if (facts.target_engaged(target) && !unit_is_monster_or_vehicle(target)) {
        // 17.03: in your Shooting phase, enemy MONSTER/VEHICLE units that are engaged can be selected as targets.
        return "04.02: " + unit_label(target.id) + " is engaged; targets must be unengaged";
    }
    if (profile.has<ability::Blast>() && facts.target_engaged(target) && unit_is_monster_or_vehicle(target)) {
        // Rules Appendix FAQ and the 17.03 diagram: [BLAST] weapons cannot target an engaged MONSTER/VEHICLE unit
        // (D-073).
        return "17.03: '" + profile.name + "' is a [BLAST] weapon and cannot target " + unit_label(target.id) +
               ", an engaged MONSTER/VEHICLE unit (Rules Appendix FAQ)";
    }
    if (!facts.visible(model, target)) {
        return facts.not_visible_reason(model, target);
    }
    if (!target_within_range(model, profile, target)) {
        return "04.02: " + unit_label(target.id) + " is not within range (" + format_inches(*profile.range_mm) + ") of " +
               model_label(model.id) + "'s '" + profile.name + "'";
    }
    return std::nullopt;
}

// M54: !target_problem(...), with the same checks, range before visibility (only the answer is needed, not
// which rule it breaks first, so the cheap test goes first).
bool target_ok(const Unit& unit, const Model& model, const WeaponProfile& profile, ShootingType type,
               const Unit& target, TargetFacts& facts) {
    const bool close_quarters = type == ShootingType::CloseQuarters;
    const bool monster_vehicle = is_monster_or_vehicle(unit, model);
    const bool target_engaged_with_us = facts.engaged_with_us(target.id);
    if (close_quarters && !monster_vehicle) {
        if (!target_engaged_with_us) return false;  // 10.06
    } else if (close_quarters && target_engaged_with_us) {
        if (profile.has<ability::Blast>()) return false;  // 10.06
    } else if (facts.target_engaged(target) && !unit_is_monster_or_vehicle(target)) {
        return false;  // 04.02 (17.03: an engaged MONSTER/VEHICLE unit can be targeted)
    }
    if (profile.has<ability::Blast>() && facts.target_engaged(target) && unit_is_monster_or_vehicle(target)) {
        return false;  // 17.03, Rules Appendix FAQ (D-073)
    }
    return target_within_range(model, profile, target) && facts.visible(model, target);  // 04.02, 06.01
}

// M54: the checks of declaration_problem that don't depend on the target, as a yes/no (04.01, D-026, D-071,
// 10.05, 10.06). D-069: a random A or D is rolled, so it is no reason to refuse (D-002 is replaced).
bool profile_selectable(const Unit& unit, const Model& model, const WeaponProfile& profile, ShootingType type) {
    if (profile.kind != WeaponKind::Ranged || !profile.range_mm) return false;
    if (unimplemented_ability_problem(profile)) return false;
    if (type == ShootingType::Assault && !profile.has<ability::Assault>()) return false;
    if (type == ShootingType::CloseQuarters && !is_monster_or_vehicle(unit, model) && !is_close_quarters(profile)) {
        return false;
    }
    return true;
}

// M54: shot_options' walk (model, weapon, profile, target order), calling `found` with each valid declaration
// until it returns false. A declaration is valid exactly when declaration_problem finds nothing.
template <typename Found>
void for_each_shot_option(const GameState& state, const Unit& unit, ShootingType type, Found found) {
    TargetFacts facts(state, unit);
    for (const Model& model : unit.models) {
        for (std::size_t w = 0; w < model.weapons.size(); ++w) {
            for (std::size_t p = 0; p < model.weapons[w].profiles.size(); ++p) {
                const WeaponProfile& profile = model.weapons[w].profiles[p];
                if (!profile_selectable(unit, model, profile, type)) continue;
                for (const Unit& target : state.units) {
                    if (target.side == unit.side || is_destroyed(target)) {
                        continue;
                    }
                    if (target_ok(unit, model, profile, type, target, facts) &&
                        !found(ShotDeclaration{model.id, w, p, target.id})) {
                        return;
                    }
                }
            }
        }
    }
}

// The checks on one declaration on its own (not 24.07 or duplicates, which need all of them).
std::optional<std::string> declaration_problem(const GameState& state, const Unit& unit, ShootingType type,
                                               const ShotDeclaration& shot, TargetFacts& facts) {
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
    if (auto p = unimplemented_ability_problem(profile)) return p;  // D-026, D-071 (D-069: random A / D are rolled)
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
    return target_problem(unit, *model, profile, type, *target, facts);
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
    const std::vector<PosedModel> blockers = visibility_blockers(state, find_unit_of_model(state, observer.id), target);
    if (state.terrain.empty()) return visible_past(observer, target, blockers);
    TerrainView view(state);  // M88 (D-088)
    return visible_past(view, observer, target, blockers);
}

bool target_within_range(const Model& model, const WeaponProfile& profile, const Unit& target) {
    if (!profile.range_mm) {
        return false;
    }
    const PosedModel from = posed_of(model);  // D-074: each base with its facing
    const auto targets = posed_models(target);
    const auto d = closest_distance(std::span<const PosedModel>(&from, 1), std::span<const PosedModel>(targets));
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
    TargetFacts facts(state, unit);  // M54: shared by the declarations (the same queries, asked once)
    for (std::size_t i = 0; i < shots.size(); ++i) {
        if (auto p = declaration_problem(state, unit, type, shots[i], facts)) {
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
    for_each_shot_option(state, unit, type, [&](const ShotDeclaration& shot) {
        out.push_back(shot);
        return true;
    });
    return out;
}

bool has_shot_option(const GameState& state, const Unit& unit, ShootingType type) {
    bool any = false;
    for_each_shot_option(state, unit, type, [&](const ShotDeclaration&) {
        any = true;
        return false;  // the first one is enough
    });
    return any;
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
    // M88 (D-089): 13.08 per attacking model and target unit, worked out once each (only with terrain).
    std::optional<TerrainView> view;
    std::map<std::pair<std::uint32_t, std::uint32_t>, bool> cover;
    for (const ShotDeclaration& s : shots) {
        WeaponSelection sel = select_weapon_target(state, s.model, s.weapon_index, s.profile_index, s.target);
        const Model& model = *find_model(unit, s.model);
        const WeaponProfile& profile = model.weapons[s.weapon_index].profiles[s.profile_index];
        // Both exceptions: a [CLOSE-QUARTERS] weapon at a unit this unit is engaged with.
        std::optional<bool> cq_at_engaged;
        const auto close_quarters_at_engaged = [&] {
            if (!cq_at_engaged) cq_at_engaged = is_close_quarters(profile) && engaged_with(state, unit, s.target);
            return *cq_at_engaged;
        };
        if (type == ShootingType::CloseQuarters && is_monster_or_vehicle(unit, model) && !close_quarters_at_engaged()) {
            // 10.06 MONSTER/VEHICLE Models: -1 to hit unless a [CLOSE-QUARTERS] weapon targets a unit this
            // unit is engaged with.
            sel.hit_modifier -= 1;
            sel.hit_modifier_rules.push_back(HitModifierRule::CloseQuartersMonster);
        }
        const Unit* target = state.find_unit(s.target);
        if (target != nullptr && unit_is_monster_or_vehicle(*target) && is_engaged(state, *target) &&
            !close_quarters_at_engaged()) {
            // 17.03: -1 to hit a ranged attack at an engaged enemy MONSTER/VEHICLE unit, excluding attacks made with
            // [CLOSE-QUARTERS] weapons by models in a unit engaged with the target. Both modifiers count; D-011 caps
            // the net at the hit roll (D-073).
            sel.hit_modifier -= 1;
            sel.hit_modifier_rules.push_back(HitModifierRule::EngagedMonsterTarget);
        }
        if (!state.terrain.empty() && target != nullptr) {
            // 13.08 (D-089): decided when the targets are selected; models lost later don't change it.
            if (!view) view.emplace(state);
            const auto key = std::make_pair(s.model.v, s.target.v);
            auto it = cover.find(key);
            if (it == cover.end()) it = cover.emplace(key, benefit_of_cover(*view, model, *target)).first;
            sel.benefit_of_cover = it->second;
        }
        out.push_back(sel);
    }
    return out;
}

}  // namespace wh40k::core
