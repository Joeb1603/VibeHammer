#pragma once

// Movement queries for clients and tests (09.05-09.07, 03.01). The move itself runs through the
// engine actions StartMove, ProposeModelMove, CommitUnitMove and ResetUnitMove.

#include <optional>
#include <string>
#include <vector>

#include "core/state/game_state.hpp"

namespace wh40k::core {

// The maximum distance `model` may move in the move in progress (its M, plus the advance roll for an
// Advance move; Rules Appendix: Different Move Characteristics). nullopt if the model isn't moving.
[[nodiscard]] std::optional<float> max_move_distance_mm(const GameState& state, ModelId model);

// The position `model` started the move in progress from. nullopt if the model isn't moving.
[[nodiscard]] std::optional<Position> move_start_position(const GameState& state, ModelId model);

// What would make CommitUnitMove return the models to their start positions right now (03.01 Ending a
// Move, 03.03 coherency, the move type's After Moving conditions). Rule-cited; empty means the move
// can end. Empty if no move is in progress.
[[nodiscard]] std::vector<std::string> move_end_problems(const GameState& state);

}  // namespace wh40k::core
