#pragma once

// The shaded area a moving model can end in (D-040, M27): core's move_reach as horizontal spans to fill.
// Pure interval maths on what core reports; no rules here (D-024).

#include <vector>

#include "core/engine/movement.hpp"

namespace wh40k::client {

// Where the model's centre may be on one horizontal line of the battlefield (mm).
struct ReachSpan {
    float y = 0.0f;
    float x0 = 0.0f;
    float x1 = 0.0f;
    friend bool operator==(const ReachSpan&, const ReachSpan&) = default;
};

// One line every `y_step_mm` across the reach circle: the circle inside the centre bounds, minus every
// forbidden circle. Lines are bottom to top, spans on a line left to right. `y_step_mm` must be positive.
[[nodiscard]] std::vector<ReachSpan> reach_spans(const core::MoveReach& reach, float y_step_mm);

// Whether a centre position is in the area (the same test the spans use).
[[nodiscard]] bool in_reach(const core::MoveReach& reach, const core::Position& centre);

}  // namespace wh40k::client
