#pragma once

#include <optional>
#include <vector>

#include "core/geometry/coherency.hpp"
#include "core/geometry/pose.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::core {

// Geometry views of GameState units. Geometry functions work on PlacedModel / PosedModel spans; these
// adapt units and the game state to them.
//
// D-074 part 2 (M65): the engine measures every model with its facing (PosedModel, BasePose). The PlacedModel
// views below are round-only (their geometry throws for an oval base) and stay for the client and the bots until
// M66; nothing in wh40k_core uses them any more.

[[nodiscard]] std::vector<PlacedModel> placed_models(const Unit& unit);

// D-074: a model's base where it stands, with its facing.
[[nodiscard]] inline BasePose pose_of(const Model& m) noexcept { return BasePose{m.position, m.base, m.facing_deg}; }
[[nodiscard]] inline PosedModel posed_of(const Model& m) noexcept {
    return PosedModel{m.id, m.position, m.base, m.facing_deg};
}
[[nodiscard]] std::vector<PosedModel> posed_models(const Unit& unit);

// The unit containing model `id`, or nullptr if no (surviving) model has that id.
[[nodiscard]] const Unit* find_unit_of_model(const GameState& state, ModelId id);
[[nodiscard]] const Model* find_model(const GameState& state, ModelId id);
[[nodiscard]] const Model* find_model(const Unit& unit, ModelId id);
[[nodiscard]] Model* find_model(Unit& unit, ModelId id);

// Every model of `side`'s opponent, in unit then model order.
[[nodiscard]] std::vector<PlacedModel> enemy_models(const GameState& state, Side side);

// Every model of `side` itself, in unit then model order (03.01: models a move may pass through).
[[nodiscard]] std::vector<PlacedModel> friendly_models(const GameState& state, Side side);

// D-074: the same with each model's facing.
[[nodiscard]] std::vector<PosedModel> posed_enemy_models(const GameState& state, Side side);
[[nodiscard]] std::vector<PosedModel> posed_friendly_models(const GameState& state, Side side);

// 03.03: is the unit in coherency? (Offending models in the result.)
[[nodiscard]] CoherencyResult unit_coherency(const Unit& unit);

// 03.04: the enemy units `unit` is engaged with, in GameState order.
[[nodiscard]] std::vector<UnitId> engaged_enemy_units(const GameState& state, const Unit& unit);

// 03.04: a unit is unengaged while it contains no engaged models.
[[nodiscard]] bool is_engaged(const GameState& state, const Unit& unit);

// D-009: closest base-to-base distance between two units; nullopt if either has no models.
[[nodiscard]] std::optional<float> unit_distance(const Unit& a, const Unit& b);

// 01.04 / D-009: edge-to-edge distance from `model`, placed at `pos` with its current facing, to the closest model of
// `target`; nullopt if `target` has no models.
[[nodiscard]] std::optional<float> model_distance_to_unit(const Model& model, const Position& pos, const Unit& target);

// D-074: the same from a base at a given pose (position, base and facing).
[[nodiscard]] std::optional<float> model_distance_to_unit(const BasePose& from, const Unit& target);

}  // namespace wh40k::core
