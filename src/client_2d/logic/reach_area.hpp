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

// D-074 part 3 (M66): the shadow of a blocking ReachShape (D-044): the points p whose straight line from `from`
// meets the shape (core::translation_crosses for the moving base at the shape's facing: the line's swept base would
// overlap the model by more than EPS). On each ray from `from` it is everything beyond the ray's first point in the
// shape; the set is convex.
struct ShapeShadow {
    core::Position from;
    core::ReachShape shape;
};

// M88 (D-087): the shadow of a blocking terrain feature (13.06, a Dense feature over 2" tall the model can't move
// through): the points p whose straight line from `from` passes through it (core::translation_crosses_shape for the
// moving base, as move_reach's -EPS threshold). On each ray from `from` it is everything beyond the ray's first point
// in the feature's area; the set is convex.
struct TerrainShadow {
    core::Position from;
    core::ReachTerrain terrain;
};

// A point is in the region when it is inside `bounds`, inside every `inside_all` circle, inside at least
// one circle of every `inside_any` group, outside every `outside_all` circle and in no `outside_shadows`.
// D-074 part 3 (M66): the pairs that aren't round to round (an oval mover, or an oval other model) come as core's
// ReachShapes (D-074 part 2): `inside_any_shapes` is empty (no group has a shape) or holds one group per `inside_any`
// group (group i is inside_any[i] and inside_any_shapes[i] together); a point must be outside every `outside_shapes`
// shape and in no `outside_shape_shadows`. Round-only regions have none, so they are tested exactly as before.
// M88 (D-087): a point must also be outside every `outside_terrain` area (core's ReachTerrain: not ending on a Light or
// Dense feature) and in no `outside_terrain_shadows`. Without terrain they are empty.
struct ReachRegion {
    core::Rect bounds;
    std::vector<core::ReachCircle> inside_all;
    std::vector<std::vector<core::ReachCircle>> inside_any;
    std::vector<core::ReachCircle> outside_all;
    std::vector<Shadow> outside_shadows;
    bool empty = false;  // nothing can be dropped anywhere (e.g. the model can't be moved)
    std::vector<std::vector<core::ReachShape>> inside_any_shapes;
    std::vector<core::ReachShape> outside_shapes;
    std::vector<ShapeShadow> outside_shape_shadows;
    std::vector<core::ReachTerrain> outside_terrain;      // M88
    std::vector<TerrainShadow> outside_terrain_shadows;  // M88
    [[nodiscard]] bool has_shapes() const noexcept {
        return !inside_any_shapes.empty() || !outside_shapes.empty() || !outside_shape_shadows.empty() ||
               !outside_terrain.empty() || !outside_terrain_shadows.empty();
    }
};

// D-074 part 3: the shapes of group `g` of `region.inside_any` (none when the region has no shapes).
[[nodiscard]] const std::vector<core::ReachShape>& group_shapes(const ReachRegion& region, std::size_t g);

// D-074 part 3: whether `p` is in `shape` (core::reach_shape_contains), deciding by its inner and outer discs first.
[[nodiscard]] bool shape_holds(const core::ReachShape& shape, const core::Position& p);

// D-074 part 3: whether `p` is in the shadow (the straight line from its `from` to `p` meets the shape), deciding by
// the shape's inner and outer discs first.
[[nodiscard]] bool shadow_holds(const ShapeShadow& shadow, const core::Position& p);

// M88: whether `p` is in the terrain area (core::reach_terrain_contains), deciding by its inner and outer discs first.
[[nodiscard]] bool terrain_holds(const core::ReachTerrain& terrain, const core::Position& p);

// M88: whether `p` is in the terrain shadow (the straight line from its `from` to `p` passes through the feature),
// deciding by the feature's inner and outer discs first.
[[nodiscard]] bool terrain_shadow_holds(const TerrainShadow& shadow, const core::Position& p);

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
// D-074 part 3: a shape or shape shadow crosses each line in one interval (each is convex), whose ends are found by
// bisection on shape_holds / shadow_holds to within kShapeSpanToleranceMm (or half the step, if smaller), from the
// shape's inner disc (or a sample inside it) out to its outer disc. A sliver of a shadow narrower than the samples
// (at most kShapeSpanToleranceMm apart) on a line may be missed. That is the band at the boundary the shading may be
// off by; in_region is exact.
inline constexpr float kShapeSpanToleranceMm = 0.5f;
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
