#include "core/rules/fight.hpp"

#include <algorithm>
#include <stdexcept>

#include "core/geometry/closest.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/units.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/attacks.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core {

namespace {

std::string unit_label(UnitId id) { return "unit " + std::to_string(id.v); }
std::string model_label(ModelId id) { return "model " + std::to_string(id.v); }

bool contains(std::span<const UnitId> ids, UnitId id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

// Enemy units within `inches` of `unit` (edge to edge, D-009), in GameState order.
std::vector<UnitId> enemy_units_within(const GameState& state, const Unit& unit, float inches) {
    std::vector<UnitId> out;
    for (const Unit& other : state.units) {
        if (other.side == unit.side || is_destroyed(other)) continue;
        if (const auto d = unit_distance(unit, other); d && within(*d, inches_to_mm(inches))) {
            out.push_back(other.id);
        }
    }
    return out;
}

// 01.04 / D-009: edge-to-edge distance from `model` at `pos` to the closest model of `target`.
std::optional<float> distance_to_unit(const Model& model, const Position& pos, const Unit& target) {
    const PlacedModel from{model.id, pos, model.base};
    const auto targets = placed_models(target);
    return closest_distance(std::span<const PlacedModel>(&from, 1), targets);
}

bool has_melee_weapon(const Model& m) {
    return std::any_of(m.weapons.begin(), m.weapons.end(), [](const Weapon& w) {
        return std::any_of(w.profiles.begin(), w.profiles.end(),
                           [](const WeaponProfile& p) { return p.kind == WeaponKind::Melee; });
    });
}

// The checks on one model's declaration (D-021), not counting the "every able model" rule.
std::optional<std::string> declaration_problem(const GameState& state, const Unit& unit, const MeleeDeclaration& d) {
    const Model* model = find_model(unit, d.model);
    if (model == nullptr) {
        return "04.01: " + model_label(d.model) + " is not a model in " + unit_label(unit.id);
    }
    if (d.weapon_index >= model->weapons.size()) {
        return "04.01: " + model_label(d.model) + " has no weapon " + std::to_string(d.weapon_index);
    }
    const Weapon& weapon = model->weapons[d.weapon_index];
    if (d.profile_index >= weapon.profiles.size()) {
        return "04.01: '" + weapon.name + "' has no profile " + std::to_string(d.profile_index);
    }
    const WeaponProfile& profile = weapon.profiles[d.profile_index];
    if (profile.kind != WeaponKind::Melee) {
        return "04.01: '" + profile.name + "' is not a melee weapon; models fight with melee weapons";
    }
    if (!profile.attacks.is_fixed() || !profile.damage.is_fixed()) {
        return "D-002: '" + profile.name + "' has a random characteristic, which V1 does not roll";
    }
    const int a = profile.attacks.fixed_value();
    if (d.targets.empty()) {
        return "04.02: select one or more targets for " + model_label(d.model) + "'s '" + profile.name + "'";
    }
    if (static_cast<int>(d.targets.size()) > a) {
        return "04.02: '" + profile.name + "' (A" + std::to_string(a) + ") cannot select " +
               std::to_string(d.targets.size()) + " targets";
    }
    const std::vector<UnitId> engaged = enemy_units_engaged_with_model(state, unit, *model, model->position);
    int total = 0;
    for (std::size_t i = 0; i < d.targets.size(); ++i) {
        const MeleeTarget& t = d.targets[i];
        for (std::size_t k = 0; k < i; ++k) {
            if (d.targets[k].unit == t.unit) {
                return "04.02: " + unit_label(t.unit) + " is selected more than once for " + model_label(d.model);
            }
        }
        if (!contains(engaged, t.unit)) {
            return "04.02: " + unit_label(t.unit) + " is not an enemy unit engaged with " + model_label(d.model);
        }
        if (d.targets.size() > 1 && t.attacks < 1) {
            return "04.02: declare at least one attack against each target (Splitting Melee Attacks)";
        }
        total += t.attacks;
    }
    if (total != a) {
        return "04.02: " + model_label(d.model) + "'s '" + profile.name + "' has " + std::to_string(a) +
               " attacks but " + std::to_string(total) + " are declared";
    }
    return std::nullopt;
}

}  // namespace

std::string_view fight_type_name(FightType t) { return t == FightType::Normal ? "Normal" : "Overrun"; }
std::string_view fight_type_rule(FightType t) { return t == FightType::Normal ? "12.05" : "12.06"; }

std::optional<FightType> fight_type_from_name(std::string_view name) {
    if (name == "Normal") return FightType::Normal;
    if (name == "Overrun") return FightType::Overrun;
    return std::nullopt;
}

std::vector<UnitId> enemy_units_engaged_with_model(const GameState& state, const Unit& unit, const Model& model,
                                                   const Position& pos) {
    std::vector<UnitId> out;
    const PlacedModel me{model.id, pos, model.base};
    for (const Unit& other : state.units) {
        if (other.side == unit.side) continue;
        for (const PlacedModel& them : placed_models(other)) {
            if (models_engaged(me, them)) {
                out.push_back(other.id);
                break;
            }
        }
    }
    return out;
}

bool in_base_contact_with_enemy(const GameState& state, const Unit& unit, const Model& model, const Position& pos) {
    for (const Unit& other : state.units) {
        if (other.side == unit.side) continue;
        for (const Model& m : other.models) {
            if (in_base_contact(base_distance(pos, model.base, m.position, m.base))) return true;
        }
    }
    return false;
}

bool eligible_to_pile_in(const GameState& state, const Unit& unit) {
    if (is_destroyed(unit) || phase_of(state.flow.stage) != Phase::Fight) return false;
    return is_engaged(state, unit) || unit.turn_flags.made_charge_move || unit.phase_flags.selected_for_overrun;
}

std::vector<UnitId> pile_in_target_options(const GameState& state, const Unit& unit) {
    if (is_engaged(state, unit)) return engaged_enemy_units(state, unit);
    return enemy_units_within(state, unit, kPileInTargetRangeInches);
}

std::optional<std::string> pile_in_targets_problem(const GameState& state, const Unit& unit,
                                                   std::span<const UnitId> targets) {
    if (targets.empty()) return "12.03: select one or more pile-in targets";
    const std::vector<UnitId> options = pile_in_target_options(state, unit);
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (std::find(targets.begin(), targets.begin() + static_cast<std::ptrdiff_t>(i), targets[i]) !=
            targets.begin() + static_cast<std::ptrdiff_t>(i)) {
            return "12.03: " + unit_label(targets[i]) + " is selected as a pile-in target more than once";
        }
        if (!contains(options, targets[i])) {
            return is_engaged(state, unit)
                       ? "12.03: " + unit_label(unit.id) + " is engaged, so its pile-in targets are the enemy "
                                                           "units it is engaged with"
                       : "12.03: " + unit_label(targets[i]) + " is not an enemy unit within 5\" of " +
                             unit_label(unit.id);
        }
    }
    if (is_engaged(state, unit) && targets.size() != options.size()) {
        return "12.03: " + unit_label(unit.id) + " is engaged, so it must select every enemy unit it is engaged "
                                                 "with as a pile-in target";
    }
    return std::nullopt;
}

bool eligible_to_fight(const GameState& state, const Unit& unit) {
    if (is_destroyed(unit) || unit.phase_flags.selected_to_fight) return false;
    return is_engaged(state, unit) || unit.phase_flags.engaged_at_fight_step_start ||
           unit.turn_flags.made_charge_move || unit.phase_flags.forced_to_fight;
}

std::optional<std::string> fight_type_problem(const GameState& state, const Unit& unit, FightType type) {
    const std::string who = unit_label(unit.id);
    if (!eligible_to_fight(state, unit)) {
        return "12.04: " + who + " is not eligible to fight";  // also the Appendix FAQ for overrun fights
    }
    const bool engaged = is_engaged(state, unit);
    if (type == FightType::Normal && !engaged) {
        return "12.05: " + who + " is unengaged; a normal fight needs an engaged unit";
    }
    if (type == FightType::Overrun && engaged && unit.phase_flags.engaged_at_fight_step_start) {
        return "12.06: " + who + " is engaged and was engaged at the start of the Fight step; an overrun fight "
                                 "needs it to be unengaged, or to have become engaged during the Fight phase";
    }
    return std::nullopt;
}

bool may_pass_fight(const GameState& state, Side side) {
    bool any = false;
    for (const Unit& u : state.units) {
        if (u.side != side || !eligible_to_fight(state, u)) continue;
        any = true;
        if (!enemy_units_within(state, u, kPassRangeInches).empty()) return false;
    }
    return any;
}

std::optional<ConsolidationMode> consolidation_mode(const GameState& state, const Unit& unit) {
    if (is_engaged(state, unit)) return ConsolidationMode::Ongoing;
    if (!enemy_units_within(state, unit, kEngagingRangeInches).empty()) return ConsolidationMode::Engaging;
    return std::nullopt;  // Objective Consolidation needs objectives; V1 has none (D-022)
}

std::vector<UnitId> consolidation_target_options(const GameState& state, const Unit& unit) {
    const auto mode = consolidation_mode(state, unit);
    if (!mode) return {};
    if (*mode == ConsolidationMode::Ongoing) return engaged_enemy_units(state, unit);
    return enemy_units_within(state, unit, kEngagingRangeInches);
}

bool eligible_to_consolidate(const GameState& state, const Unit& unit) {
    return !is_destroyed(unit) && unit.phase_flags.eligible_to_fight_this_phase && !unit.phase_flags.consolidated &&
           consolidation_mode(state, unit).has_value();
}

std::optional<std::string> consolidation_targets_problem(const GameState& state, const Unit& unit,
                                                         std::span<const UnitId> targets) {
    const auto mode = consolidation_mode(state, unit);
    if (!mode) return "12.08: " + unit_label(unit.id) + " meets no consolidation mode's conditions";
    if (targets.empty()) return "12.08: select one or more enemy units";
    const std::vector<UnitId> options = consolidation_target_options(state, unit);
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (std::find(targets.begin(), targets.begin() + static_cast<std::ptrdiff_t>(i), targets[i]) !=
            targets.begin() + static_cast<std::ptrdiff_t>(i)) {
            return "12.08: " + unit_label(targets[i]) + " is selected more than once";
        }
        if (!contains(options, targets[i])) {
            return *mode == ConsolidationMode::Ongoing
                       ? "12.08: " + unit_label(unit.id) + " is engaged (Ongoing Consolidation), so it selects the "
                                                           "enemy units it is engaged with"
                       : "12.08: " + unit_label(targets[i]) + " is not an enemy unit within 3\" of " +
                             unit_label(unit.id) + " (Engaging Consolidation)";
        }
    }
    if (*mode == ConsolidationMode::Ongoing && targets.size() != options.size()) {
        return "12.08: Ongoing Consolidation selects every enemy unit " + unit_label(unit.id) + " is engaged with";
    }
    return std::nullopt;
}

bool ends_closer_to_closest_target(const GameState& state, const Model& model, const Position& start,
                                   const Position& end, std::span<const UnitId> targets) {
    std::vector<std::pair<const Unit*, float>> distances;
    float closest = 0.0f;
    for (const UnitId id : targets) {
        const Unit* t = state.find_unit(id);
        if (t == nullptr || is_destroyed(*t)) continue;
        if (const auto d = distance_to_unit(model, start, *t)) {
            if (distances.empty() || *d < closest) closest = *d;
            distances.emplace_back(t, *d);
        }
    }
    for (const auto& [t, d] : distances) {
        if (more_than(d, closest)) continue;  // not (tied for) the closest (D-010)
        const auto after = distance_to_unit(model, end, *t);
        if (after && more_than(d, *after)) return true;
    }
    return false;
}

std::vector<std::string> still_engaged_problems(const GameState& state, const Unit& unit,
                                                std::span<const ModelStart> starts, std::string_view rule) {
    std::vector<std::string> out;
    for (const ModelStart& s : starts) {
        const Model* m = find_model(unit, s.model);
        if (m == nullptr) continue;
        const auto before = enemy_units_engaged_with_model(state, unit, *m, s.position);
        const auto after = enemy_units_engaged_with_model(state, unit, *m, m->position);
        for (const UnitId e : before) {
            if (!contains(after, e)) {
                out.push_back(std::string(rule) + ": " + model_label(m->id) + " started the move engaged with " +
                              unit_label(e) + " and must still be engaged with it");
            }
        }
    }
    return out;
}

std::vector<std::string> melee_problems(const GameState& state, const Unit& unit,
                                        std::span<const MeleeDeclaration> declarations) {
    std::vector<std::string> out;
    if (declarations.empty()) {
        out.push_back("D-021: declare a melee weapon and targets for every model of " + unit_label(unit.id) +
                      " that can fight (an empty Fight action is the template legal_actions returns)");
        return out;
    }
    for (std::size_t i = 0; i < declarations.size(); ++i) {
        const MeleeDeclaration& d = declarations[i];
        bool duplicate = false;
        for (std::size_t k = 0; k < i; ++k) duplicate = duplicate || declarations[k].model == d.model;
        if (duplicate) {
            out.push_back("04.01: " + model_label(d.model) + " must select one melee weapon, not more");
        } else if (auto p = declaration_problem(state, unit, d)) {
            out.push_back(std::move(*p));
        }
    }
    // 04.01 While Fighting: every model that can attack must select one melee weapon (D-021).
    for (const Model& m : unit.models) {
        if (!has_melee_weapon(m) || enemy_units_engaged_with_model(state, unit, m, m.position).empty()) continue;
        const bool declared = std::any_of(declarations.begin(), declarations.end(),
                                          [&m](const MeleeDeclaration& d) { return d.model == m.id; });
        if (!declared) {
            out.push_back("04.01: " + model_label(m.id) + " is engaged with an enemy unit and must select one melee "
                                                          "weapon (D-021)");
        }
    }
    return out;
}

bool can_make_melee_attacks(const GameState& state, const Unit& unit) {
    return std::any_of(unit.models.begin(), unit.models.end(), [&](const Model& m) {
        return has_melee_weapon(m) && !enemy_units_engaged_with_model(state, unit, m, m.position).empty();
    });
}

std::vector<MeleeDeclaration> melee_options(const GameState& state, const Unit& unit) {
    std::vector<MeleeDeclaration> out;
    for (const Model& m : unit.models) {
        const auto engaged = enemy_units_engaged_with_model(state, unit, m, m.position);
        for (std::size_t w = 0; w < m.weapons.size(); ++w) {
            for (std::size_t p = 0; p < m.weapons[w].profiles.size(); ++p) {
                const WeaponProfile& profile = m.weapons[w].profiles[p];
                if (profile.kind != WeaponKind::Melee || !profile.attacks.is_fixed() || !profile.damage.is_fixed()) {
                    continue;
                }
                for (const UnitId target : engaged) {
                    out.push_back(MeleeDeclaration{m.id, w, p, {MeleeTarget{target, profile.attacks.fixed_value()}}});
                }
            }
        }
    }
    return out;
}

std::vector<WeaponSelection> make_melee_selections(const GameState& state,
                                                   std::span<const MeleeDeclaration> declarations) {
    std::vector<WeaponSelection> out;
    for (const MeleeDeclaration& d : declarations) {
        const bool split = d.targets.size() > 1;
        for (const MeleeTarget& t : d.targets) {
            out.push_back(select_weapon_target(state, d.model, d.weapon_index, d.profile_index, t.unit,
                                               split ? std::optional<int>(t.attacks) : std::nullopt));
        }
    }
    return out;
}

}  // namespace wh40k::core
