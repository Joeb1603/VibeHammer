#pragma once

#include <functional>
#include <optional>

#include <raylib.h>

#include "camera.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

[[nodiscard]] Color side_color(core::Side side);

// Draws the battlefield (grid, deployment zones) and every model. Read-only. Units for which `dimmed`
// returns true are drawn grey: they can't be clicked now. `emphasised`: the clickable unit under the cursor,
// whose models get a bold outline (D-029). `ui_scale` scales that outline. `model_dimmed` greys single
// models of other units too (D-051: models that can't be picked as the next casualty).
void draw_board(const core::GameState& state, const BoardCamera& camera,
                const std::function<bool(core::UnitId)>& dimmed = {},
                std::optional<core::UnitId> emphasised = std::nullopt, float ui_scale = 1.0f,
                const std::function<bool(core::UnitId, core::ModelId)>& model_dimmed = {});

// The model whose base is under `screen` (for tooltips), if any.
struct HoveredModel {
    const core::Unit* unit = nullptr;
    const core::Model* model = nullptr;
};
[[nodiscard]] std::optional<HoveredModel> model_at(const core::GameState& state, const BoardCamera& camera,
                                                   Vector2 screen);

}  // namespace wh40k::client
