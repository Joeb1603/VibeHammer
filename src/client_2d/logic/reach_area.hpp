#pragma once

// The shaded area a moving model, or a whole unit's drag handle, can be dropped in (D-040, D-041): core's
// move_reach as a region, and the region as horizontal spans to fill. Interval maths on what core reports;
// no rules here (D-024).

#include <span>
#include <vector>

#include "core/engine/movement.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

// D-044: the points p whose straight line from `from` passes within `disc.radius_mm` of `disc.centre`
// (the disc, and its shadow from `from`).
struct Shadow {
    core::Position from;
    core::ReachCircle disc;
};

// A point is in the region when it is inside `bounds`, inside every `inside_all` circle, inside at least
// one circle of every `inside_any` group, outside every `outside_all` circle and in no `outside_shadows`.
struct ReachRegion {
    core::Rect bounds;
    std::vector<core::ReachCircle> inside_all;
    std::vector<std::vector<core::ReachCircle>> inside_any;
    std::vector<core::ReachCircle> outside_all;
    std::vector<Shadow> outside_shadows;
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

// M54: the same region without the circles and shadows no point of it can be in: those further than its
// smallest `inside_all` circle reaches (with a 1 mm margin for rounding). in_region gives the same answer for
// every point, only faster when most of the battlefield's models are far away. Unchanged without an
// `inside_all` circle.
[[nodiscard]] ReachRegion trimmed_region(const ReachRegion& region);

// The distance from `p` to the segment from `a` to `b` (the shadow test: a shadow holds the points whose
// line from `from` passes within the disc's radius of its centre).
[[nodiscard]] float point_segment_distance(const core::Position& p, const core::Position& a, const core::Position& b);

// M54: for spots on rays from `origin` (ray i along (cosines[i], sines[i]); on each ray the spots at `distances`,
// ascending), the ones in_region certainly refuses, worked out per ray from where the region's circles cross it:
// inside a forbidden (`outside_all`) circle by more than a 0.05 mm margin, or further than that outside every
// circle of an `inside_any` group. The margin is far beyond the rounding of a spot computed in float as
// origin + distance * direction (about 1e-4 mm on the battlefield), so in_region refuses each of them; every
// other spot needs in_region itself. A shortcut for the auto moves' samples, never a different answer.
class RayRefusals {
public:
    RayRefusals(const ReachRegion& region, const core::Position& origin, std::span<const float> cosines,
                std::span<const float> sines, std::span<const float> distances);
    [[nodiscard]] bool refused(std::size_t ray, std::size_t distance_index) const {
        return ray < rays_ && distance_index < spots_ && refused_[ray * spots_ + distance_index] != 0;
    }

private:
    std::size_t rays_ = 0;
    std::size_t spots_ = 0;
    std::vector<char> refused_;
};

// M54: std::hypot(dx, dy) < r, decided without the (slow) hypot when the squares in double precision are clearly
// on one side: hypot is accurate to well under the 1e-6 relative margin, so the answer is the same; within the
// margin (or for non-finite values) it is std::hypot(dx, dy) < r itself.
[[nodiscard]] bool hypot_less(float dx, float dy, float r);

// M54: point_segment_distance(p, a, b) < r, with hypot_less (the same answer).
[[nodiscard]] bool point_segment_closer_than(const core::Position& p, const core::Position& a, const core::Position& b,
                                             float r);

}  // namespace wh40k::client
