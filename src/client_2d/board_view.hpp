#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <raylib.h>

#include "camera.hpp"
#include "core/geometry/pose.hpp"
#include "core/state/game_state.hpp"
#include "unit_style.hpp"

namespace wh40k::client {

[[nodiscard]] Color to_color(Rgb rgb);
[[nodiscard]] Color side_color(core::Side side);
// D-065: the unit's own shade of its side colour (side_color when its side has one unit).
[[nodiscard]] Color unit_color(const core::GameState& state, core::UnitId id);

// Draws the battlefield (grid, deployment zones) and every model. Read-only. Units for which `dimmed`
// returns true are drawn grey: they can't be clicked now. `emphasised`: the clickable unit under the cursor,
// whose models get a bold outline (D-029). `ui_scale` scales that outline. `model_dimmed` greys single
// models of other units too (D-051: models that can't be picked as the next casualty).
void draw_board(const core::GameState& state, const BoardCamera& camera,
                const std::function<bool(core::UnitId)>& dimmed = {},
                std::optional<core::UnitId> emphasised = std::nullopt, float ui_scale = 1.0f,
                const std::function<bool(core::UnitId, core::ModelId)>& model_dimmed = {});

// D-074 part 3 (M66): a base drawn at its pose (an oval at its facing; a round base as a circle). `grow_px` moves the
// outline out from the base's edge (a selection ring round it).
void fill_base(const BoardCamera& camera, const core::BasePose& pose, Color colour, float grow_px = 0.0f);
void draw_base_outline(const BoardCamera& camera, const core::BasePose& pose, float grow_px, float thick_px,
                       Color colour);
// The cross over a base that marks it for removal (along an oval's axes, turned with it).
void draw_base_cross(const BoardCamera& camera, const core::BasePose& pose, float thick_px, Color colour);
// A closed polyline of world points (an outline), `thick_px` wide.
void draw_world_loop(const BoardCamera& camera, const std::vector<core::Position>& points, float thick_px, Color colour);

// The model whose base is under `screen` (for tooltips), if any. D-074 part 3: inside the base at its facing.
struct HoveredModel {
    const core::Unit* unit = nullptr;
    const core::Model* model = nullptr;
};
[[nodiscard]] std::optional<HoveredModel> model_at(const core::GameState& state, const BoardCamera& camera,
                                                   Vector2 screen);

}  // namespace wh40k::client
