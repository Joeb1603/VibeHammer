#pragma once

// The shaded area a moving model, or a whole unit's drag handle, can be dropped in (D-040, D-041): core's
// move_reach as a region, and the region as horizontal spans to fill. Interval maths on what core reports;
// no rules here (D-024).

#include <vector>

#include "core/engine/movement.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

// A point is in the region when it is inside `bounds`, inside every `inside_all` circle, inside at least
// one circle of every `inside_any` group, and outside every `outside_all` circle.
struct ReachRegion {
    core::Rect bounds;
    std::vector<core::ReachCircle> inside_all;
    std::vector<std::vector<core::ReachCircle>> inside_any;
    std::vector<core::ReachCircle> outside_all;
    bool empty = false;  // nothing can be dropped anywhere (e.g. the model can't be moved)
};

// Where one model's next drag can be dropped: its next straight line (from where it is now, up to the
// distance it has left), within core's limits. Empty if it can't be moved.
[[nodiscard]] ReachRegion model_region(const core::MoveReach& reach);

// Where the mouse can be dropped for a whole-unit drag of `unit` grabbed at `grab` (translate_unit moves
// every model by the mouse's offset from `grab`): every movable model's region, shifted to the mouse, all
// at once. The unit's own models don't block each other (they move together). Empty if no model can move.
[[nodiscard]] ReachRegion unit_region(const core::GameState& state, core::UnitId unit, const core::Position& grab);

// Where the region is on one horizontal line of the battlefield (mm).
struct ReachSpan {
    float y = 0.0f;
    float x0 = 0.0f;
    float x1 = 0.0f;
    friend bool operator==(const ReachSpan&, const ReachSpan&) = default;
};

// One line every `y_step_mm` across the region, bottom to top, spans on a line left to right. Nothing for
// an empty region or a step that isn't positive.
[[nodiscard]] std::vector<ReachSpan> reach_spans(const ReachRegion& region, float y_step_mm);

// Whether a point is in the region (the same test the spans use).
[[nodiscard]] bool in_region(const ReachRegion& region, const core::Position& p);

}  // namespace wh40k::client
