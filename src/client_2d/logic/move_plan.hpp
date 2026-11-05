#pragma once

// Building ProposeModelMove actions from board drags (03.01: a path of straight lines). The engine checks
// every proposal; these helpers only assemble paths and measure them for the on-screen ruler.

#include <optional>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

// The model's path in the move in progress: its start position, then each proposed point. Just the start
// if it hasn't been moved yet; empty if the model isn't moving.
[[nodiscard]] std::vector<core::Position> model_path(const core::GameState& state, core::ModelId model);

// The model's path so far plus one more straight line ending at `to`.
[[nodiscard]] core::action::ProposeModelMove extend_path(const core::GameState& state, core::ModelId model,
                                                         core::Position to);

// The "Move unit" handle: every model of `unit` moves by (dx, dy) from where it is now, appended to its
// path. One action per model, furthest along the move first (D-027), so submitting them in order never
// ends a model on one that has yet to move.
[[nodiscard]] std::vector<core::action::ProposeModelMove> translate_unit(const core::GameState& state,
                                                                         core::UnitId unit, float dx_mm,
                                                                         float dy_mm);

// The ruler: distance moved along the path (plus a line to `to`, while dragging) and the move's maximum
// distance (core's max_move_distance_mm).
struct Ruler {
    float moved_mm = 0.0f;
    float max_mm = 0.0f;
    bool over = false;  // "more than" the maximum, with the engine's tolerance
};
[[nodiscard]] std::optional<Ruler> ruler(const core::GameState& state, core::ModelId model,
                                         std::optional<core::Position> to = std::nullopt);

}  // namespace wh40k::client
