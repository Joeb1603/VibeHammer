#pragma once

#include <optional>

#include <raylib.h>

#include "camera.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

[[nodiscard]] Color side_color(core::Side side);

// Draws the battlefield (grid, deployment zones) and every model. Read-only.
void draw_board(const core::GameState& state, const BoardCamera& camera);

// The model whose base is under `screen` (for tooltips), if any.
struct HoveredModel {
    const core::Unit* unit = nullptr;
    const core::Model* model = nullptr;
};
[[nodiscard]] std::optional<HoveredModel> model_at(const core::GameState& state, const BoardCamera& camera,
                                                   Vector2 screen);

}  // namespace wh40k::client
