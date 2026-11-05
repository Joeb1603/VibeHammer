#pragma once

// Ring overlays (D-028). Which rings to draw comes from plan_overlays and ring_circles, merged into
// outlines by outline_arcs (client logic); this file only draws the arcs.

#include <vector>

#include <raylib.h>

#include "camera.hpp"
#include "overlay_plan.hpp"
#include "ring_outline.hpp"

namespace wh40k::client {

[[nodiscard]] Color ring_color(RingKind kind);

// `ui_scale`: the line width multiplier (D-028).
void draw_overlays(const BoardCamera& camera, const std::vector<Arc>& arcs, float ui_scale);

}  // namespace wh40k::client
