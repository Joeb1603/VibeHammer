#pragma once

// 11 Charge phase rules as pure queries over GameState: who may declare a charge (11.02), which enemy
// units can be charge targets (11.04 Before Moving) and the charge move's While/After Moving
// conditions (11.04). The engine runs the charge through engine/charge.cpp and the movement
// sub-machine (engine/movement.cpp).

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/state/game_state.hpp"

namespace wh40k::core {

// 11.02: a unit must be within 12" of one or more enemy units to declare a charge; charge targets
// must also be within 12" (11.04 Before Moving).
inline constexpr float kChargeRangeInches = 12.0f;

// 11.02 Declare Charge: why `unit` is not eligible to declare a charge now, or nullopt if it is.
// Rule-cited.
[[nodiscard]] std::optional<std::string> declare_charge_problem(const GameState& state, const Unit& unit);

// 11.04 Before Moving: the enemy units within 12" of `unit` and within `charge_roll` inches (the
// maximum distance), edge to edge (D-009), in GameState order.
[[nodiscard]] std::vector<UnitId> eligible_charge_targets(const GameState& state, const Unit& unit,
                                                          int charge_roll);

// 11.04 While Moving: does a model moved from `start` to `end` end its move closer to one or more
// of `targets` (edge to edge, to the closest model of each target; "closer" means by more than
// EPS_MM)?
[[nodiscard]] bool ends_closer_to_a_charge_target(const GameState& state, const Model& model, const Position& start,
                                                  const Position& end, std::span<const UnitId> targets);

// 11.04 After Moving: the unit is engaged with every charge target and with no other enemy unit.
// Rule-cited problems; empty means both conditions hold.
[[nodiscard]] std::vector<std::string> charge_after_moving_problems(const GameState& state, const Unit& unit,
                                                                   std::span<const UnitId> targets);

// 24.13: a unit is a Fights First unit while every model in it has Fights First. In V1 the only
// source is a charge move this turn (11.04 After Moving), which gives it to every model.
[[nodiscard]] bool is_fights_first_unit(const Unit& unit);

}  // namespace wh40k::core
