// 10.02 Shoot: select a unit and its shooting type, declare every model's weapons, profiles and
// targets (04.01, 04.02), then resolve the attacks through the attack flow (04.03, 05).

#include <string>
#include <utility>
#include <vector>

#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/shooting.hpp"

namespace wh40k::core::detail {

std::vector<GameAction> legal_for(const GameState& state, const decision::Shoot&) {
    std::vector<GameAction> out{action::EndShootStep{}};  // 10.02: shooting is optional
    const Side side = state.pending ? state.pending->side : turn_side(state);
    for (const Unit& u : state.units) {
        if (u.side != side) {
            continue;
        }
        for (const ShootingType type : eligible_shooting_types(state, u)) {
            // D-004 / D-016: a template per unit and shooting type that has at least one valid shot.
            if (!shot_options(state, u, type).empty()) {
                out.push_back(action::Shoot{u.id, type, {}});
            }
        }
    }
    return out;
}

ValidationResult validate_details(const GameState& state, Side side, const action::Shoot& a) {
    const Unit* u = state.find_unit(a.unit);
    if (u == nullptr) {
        return ValidationResult::failure("10.02: " + unit_label(a.unit) + " does not exist");
    }
    if (u->side != side) {
        return ValidationResult::failure("10.02: " + unit_label(a.unit) + " is not a friendly unit");
    }
    if (auto problem = unit_shoot_problem(state, *u)) {
        return ValidationResult::failure(std::move(*problem));
    }
    if (auto problem = shooting_type_problem(state, *u, a.type)) {
        return ValidationResult::failure(std::move(*problem));
    }
    ValidationResult r;
    r.reasons = shot_problems(state, *u, a.type, a.shots);
    return r;
}

void apply_details(GameState& state, Side, const action::Shoot& a, DiceSource&, Events& events) {
    Unit& u = *state.find_unit(a.unit);
    // 04.02: the Select Targets facts are recorded before anything is rolled.
    std::vector<WeaponSelection> selections = make_weapon_selections(state, u, a.type, a.shots);
    const AttackContext context = shooting_context(state, u);
    u.phase_flags.selected_to_shoot = true;  // 10.02 Select Unit
    // 10.04-10.06 After Shooting (not eligible to start an action) is not tracked: actions (16) are not
    // in V1.
    events.push_back(event::ShootingStarted{a.unit, a.type, selections});
    start_attacks(state, a.unit, context, std::move(selections), events);  // 04.03; apply_action advances
}

ValidationResult validate_details(const GameState&, Side, const action::EndShootStep&) {
    return ValidationResult::success();
}

void apply_details(GameState& state, Side, const action::EndShootStep&, DiceSource&, Events& events) {
    next_stage(state, events);  // 10.03 End of Shooting Phase
}

}  // namespace wh40k::core::detail
