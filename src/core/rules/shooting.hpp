#pragma once

// 10 Shooting phase rules as pure queries over GameState: which units may shoot (10.02, 09.06,
// 09.07), which shooting types they may make (10.04-10.06), which weapons and targets each model
// may select (04.01, 04.02, 06.01, 24.04, 24.07, 24.27) and the attack facts for 24.16 [HEAVY].
// The engine's Shoot action (engine/shooting.cpp) validates with these and then resolves the attacks
// through engine/attack_flow.hpp.

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/rules/attack_types.hpp"
#include "core/rules/shooting_types.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::core {

// 10.02: why `unit` cannot be selected to shoot now (on the battlefield, not already selected to
// shoot this phase, not fallen back this turn, 09.07), or nullopt if it can. Rule-cited.
[[nodiscard]] std::optional<std::string> unit_shoot_problem(const GameState& state, const Unit& unit);

// 10.04-10.06 Eligible If: why `unit` cannot make `type`, or nullopt if it can. Rule-cited.
[[nodiscard]] std::optional<std::string> shooting_type_problem(const GameState& state, const Unit& unit,
                                                               ShootingType type);

// The shooting types `unit` is eligible to make, in 10.02 order. Empty if the unit can't shoot.
[[nodiscard]] std::vector<ShootingType> eligible_shooting_types(const GameState& state, const Unit& unit);

// 06.01: is any model of `target` visible to `observer` (Unit Visible)? Models in the observer's unit
// and in `target` are ignored; every other model can block.
[[nodiscard]] bool unit_visible_to_model(const GameState& state, const Model& observer, const Unit& target);

// 04.02: is `target` within the profile's range of `model` (some model of the target within range,
// edge to edge, D-009)?
[[nodiscard]] bool target_within_range(const Model& model, const WeaponProfile& profile, const Unit& target);

// Every rule problem with `shots` for `unit` shooting with `type` (04.01, 04.02, 06.01, 10.05, 10.06,
// 24.07, D-016; D-069: a random A or D is rolled). Rule-cited; empty means the declarations are valid. Does not check the unit or
// shooting type themselves (see unit_shoot_problem / shooting_type_problem).
[[nodiscard]] std::vector<std::string> shot_problems(const GameState& state, const Unit& unit, ShootingType type,
                                                     std::span<const ShotDeclaration> shots);

// Every single declaration that would be valid on its own for `unit` making `type` (one per model,
// weapon, profile and target), in model, weapon, profile, target order. For clients and random play:
// combining options can still break 24.07 or select one weapon twice.
[[nodiscard]] std::vector<ShotDeclaration> shot_options(const GameState& state, const Unit& unit,
                                                        ShootingType type);

// M54: !shot_options(state, unit, type).empty(), stopping at the first valid declaration (D-004 / D-016:
// legal_actions offers a Shoot template only for a unit and type with one).
[[nodiscard]] bool has_shot_option(const GameState& state, const Unit& unit, ShootingType type);

// 24.16 [HEAVY] and the other attack facts for `unit` shooting now.
[[nodiscard]] AttackContext shooting_context(const GameState& state, const Unit& unit);

// 04.02 Select Targets: turns valid declarations into WeaponSelections, recording the Select Targets
// facts (24.05, 24.30) and the 10.06 MONSTER/VEHICLE hit roll penalty. Precondition: shot_problems
// is empty.
[[nodiscard]] std::vector<WeaponSelection> make_weapon_selections(const GameState& state, const Unit& unit,
                                                                  ShootingType type,
                                                                  std::span<const ShotDeclaration> shots);

}  // namespace wh40k::core
