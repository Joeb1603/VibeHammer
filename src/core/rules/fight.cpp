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


// A melee profile V1 can resolve: fixed A and D (D-002) and no unimplemented ability (D-026). A model
// whose only melee weapons fail this cannot make melee attacks in V1, so it is not required to (D-021).
bool usable_melee_profile(const WeaponProfile& p) {
    return p.kind == WeaponKind::Melee && p.attacks.is_fixed() && p.damage.is_fixed() &&
           !unimplemented_ability_problem(p);
}

bool usable_melee_weapon(const Weapon& w) {
    return std::any_of(w.profiles.begin(), w.profiles.end(), usable_melee_profile);
}

bool has_melee_weapon(const Model& m) { return std::any_of(m.weapons.begin(), m.weapons.end(), usable_melee_weapon); }

// 24.11 / D-071: what a model must select. Weapons it can't use (D-002, D-026, D-071) count as absent, so a
// model whose only other melee weapon is refused selects its [EXTRA ATTACKS] weapons alone.
struct MeleeSelectionRule {
    std::vector<std::size_t> extra_attacks;  // usable [EXTRA ATTACKS] weapons: all must be selected
    bool has_other = false;                  // a usable other melee weapon: exactly one must be selected
};

MeleeSelectionRule selection_rule(const Model& m) {
    MeleeSelectionRule r;
    for (std::size_t w = 0; w < m.weapons.size(); ++w) {
        if (!usable_melee_weapon(m.weapons[w])) continue;
        if (is_extra_attacks_weapon(m.weapons[w])) {
            r.extra_attacks.push_back(w);
        } else {
            r.has_other = true;
        }
    }
    return r;
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
    if (auto p = unimplemented_ability_problem(profile)) return p;  // D-026
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
    std::vector<const Unit*> units;
    std::vector<std::vector<PlacedModel>> candidates;
    for (const UnitId id : targets) {
        const Unit* t = state.find_unit(id);
        if (t == nullptr || is_destroyed(*t)) continue;
        units.push_back(t);
        candidates.push_back(placed_models(*t));
    }
    // D-010: every unit tied for closest to the model's start position counts (geometry's tie tolerance).
    const PlacedModel from{model.id, start, model.base};
    for (const std::size_t i : closest_units(std::span<const PlacedModel>(&from, 1), candidates)) {
        const auto before = model_distance_to_unit(model, start, *units[i]);
        const auto after = model_distance_to_unit(model, end, *units[i]);
        if (before && after && more_than(*before, *after)) return true;
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
        for (std::size_t k = 0; k < i && !duplicate; ++k) duplicate = declarations[k].model == d.model;
        // 24.11: a model with [EXTRA ATTACKS] weapons selects them in addition to one other melee weapon.
        const Model* model = duplicate ? find_model(unit, d.model) : nullptr;
        const bool extra = model != nullptr && !selection_rule(*model).extra_attacks.empty();
        bool same_weapon = false;
        bool two_others = false;
        for (std::size_t k = 0; k < i && extra; ++k) {
            const MeleeDeclaration& e = declarations[k];
            if (e.model != d.model) continue;
            same_weapon = same_weapon || e.weapon_index == d.weapon_index;
            const auto is_other = [&](const MeleeDeclaration& x) {
                return x.weapon_index < model->weapons.size() && !is_extra_attacks_weapon(model->weapons[x.weapon_index]);
            };
            two_others = two_others || (is_other(e) && is_other(d));
        }
        if (duplicate && !extra) {
            out.push_back("04.01: " + model_label(d.model) + " must select one melee weapon, not more");
        } else if (same_weapon) {
            out.push_back("04.01: " + model_label(d.model) + " selected weapon " + std::to_string(d.weapon_index) +
                          " more than once (to attack several units with it, split its attacks, 04.02)");
        } else if (two_others) {
            out.push_back("24.11: " + model_label(d.model) + " must select its [EXTRA ATTACKS] weapons and only one "
                                                              "of its other melee weapons");
        } else if (auto p = declaration_problem(state, unit, d)) {
            out.push_back(std::move(*p));
        }
    }
    // 04.01 While Fighting: every model that can attack must select one melee weapon (D-021), and 24.11 all of
    // its [EXTRA ATTACKS] weapons plus one other melee weapon if it has one it can use (D-071).
    for (const Model& m : unit.models) {
        if (!has_melee_weapon(m) || enemy_units_engaged_with_model(state, unit, m, m.position).empty()) continue;
        std::vector<std::size_t> declared;
        for (const MeleeDeclaration& d : declarations) {
            if (d.model == m.id) declared.push_back(d.weapon_index);
        }
        if (declared.empty()) {
            out.push_back("04.01: " + model_label(m.id) + " is engaged with an enemy unit and must select one melee "
                                                          "weapon (D-021)");
            continue;
        }
        if (std::none_of(m.weapons.begin(), m.weapons.end(), is_extra_attacks_weapon)) continue;  // 04.01 alone
        const MeleeSelectionRule rule = selection_rule(m);
        if (rule.extra_attacks.empty()) continue;  // its [EXTRA ATTACKS] weapons are refused: 04.01 alone
        for (const std::size_t w : rule.extra_attacks) {
            if (std::find(declared.begin(), declared.end(), w) == declared.end()) {
                out.push_back("24.11: " + model_label(m.id) + " must select all of its [EXTRA ATTACKS] weapons ('" +
                              m.weapons[w].name + "' is not selected)");
            }
        }
        const bool other_declared = std::any_of(declared.begin(), declared.end(), [&m](std::size_t w) {
            return w < m.weapons.size() && !is_extra_attacks_weapon(m.weapons[w]);
        });
        if (rule.has_other && !other_declared) {
            out.push_back("24.11: " + model_label(m.id) + " must also select one of its melee weapons other than its "
                                                          "[EXTRA ATTACKS] weapons");
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
                if (!usable_melee_profile(profile)) {
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
