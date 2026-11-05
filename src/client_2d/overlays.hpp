#pragma once

// Ring overlays (D-028). Which rings to draw comes from plan_overlays (client logic); this file only
// draws them. Rings are drawn at base radius + distance because distances are edge to edge (01.04).

#include <raylib.h>

#include "camera.hpp"
#include "core/state/game_state.hpp"
#include "overlay_plan.hpp"

namespace wh40k::client {

[[nodiscard]] Color ring_color(RingKind kind);

// `ui_scale`: the line width multiplier (D-028).
void draw_overlays(const core::GameState& state, const BoardCamera& camera, const std::vector<Ring>& rings,
                   float ui_scale);

}  // namespace wh40k::client
