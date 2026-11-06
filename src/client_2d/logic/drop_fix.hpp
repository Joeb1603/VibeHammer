#pragma once

// D-052 (M36): "Fix illegal moves". A drag the engine would refuse goes to the nearest spot it can go:
// geometry on core's move_reach area (D-040), as a ReachRegion without its Engagement Range parts, which are
// left for the commit to check (the user's choice). No rules here (D-024): the engine still checks every
// fixed drop.

#include <optional>
#include <string>

#include "core/geometry/primitives.hpp"
#include "core/geometry/units.hpp"
#include "reach_area.hpp"

namespace wh40k::client {

// The nearest spot may be at most this far from the drop; further, the model goes along the drag instead.
inline constexpr float kNearestSpotMaxMm = 1.0f * core::MM_PER_INCH;
// Fixed spots sit this far inside the area: below EPS_MM, so a spot against a model is in base contact with
// it (01.04) but not on it (as auto_fight_move's contact nudge).
inline constexpr float kFixNudgeMm = 0.005f;

// Why a drop is outside the region (the first that applies, in this order).
enum class DropProblem {
    CannotMove,         // the region is empty: no distance left, or the model can't be moved
    ThroughEnemyModel,  // its straight line meets an enemy model's base (03.01, D-044)
    OnModel,            // it ends on another model (03.01, D-027)
    TooFar,             // further than the distance left (03.01 and the move's limit)
    OffBattlefield,     // the base is not wholly on the battlefield (03.01)
    NotCloser,          // a charge, pile-in or consolidation that doesn't end closer (11.04, 12.03, 12.08)
};

// The region a drop is fixed into: `region` (model_region or unit_region) without its Engagement Range
// parts. An enemy's engagement circle becomes a plain "not on it" circle (both base radii), and the
// "still engaged" groups (12.03 / 12.08) are dropped. The "closer" groups, distances, edges, models and
// shadows stay.
[[nodiscard]] ReachRegion fix_region(const ReachRegion& region);

// Why `p` is outside `region`; nullopt when in_region(region, p).
[[nodiscard]] std::optional<DropProblem> drop_problem(const ReachRegion& region, const core::Position& p);

struct FixedDrop {
    enum class How { Nearest, AlongDrag };
    core::Position to;
    DropProblem problem = DropProblem::TooFar;  // what was wrong with the drop
    How how = How::Nearest;
};

// Where a drop outside `region` (a fix_region) goes, for a straight line from `from` (the model's position
// now, or a whole-unit drag's grab point):
// - a drop whose line meets an enemy model: the furthest point of the line in the region (along the drag);
// - otherwise the nearest point of the region to the drop, if within kNearestSpotMaxMm, else along the drag.
// nullopt when the drop is in the region (use it as is) or no spot was found (drop_problem says why).
// Exact and deterministic: projections onto the region's circles and lines and their crossings.
[[nodiscard]] std::optional<FixedDrop> fix_drop(const ReachRegion& region, const core::Position& from,
                                                const core::Position& drop);

// What was wrong, for the status line: "it was further than the model can move".
[[nodiscard]] std::string drop_problem_text(DropProblem problem);

// The status line after a fixed drop: "Moved to the nearest spot it can go: it was on another model (03.01)".
[[nodiscard]] std::string fix_message(const FixedDrop& fixed);

}  // namespace wh40k::client
