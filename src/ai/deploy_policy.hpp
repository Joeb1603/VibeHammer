#pragma once

// D-081 (M74, M77): how a bot sets its units up in the deployment step (DeployUnits), before any level's own policy runs
// (so Hard never builds chain tables from a half-deployed army).
//
// - deploy_policy (M74; level Easy, Normal as at M55, and the fallback): the lowest-id unit waiting to be set up, at
//   the engine's proposal (Auto-place's spot, core::propose_set_up), or CannotSetUp when the engine finds no legal spot
//   for it. A hand-deployed Easy army so ends exactly where Auto-place puts it.
// - heuristic_deploy_policy (M77; levels Normal and Hard, Hard using Normal's): a simple, deterministic heuristic.
//   - Order: long-range shooters first, melee-oriented units last (so they can answer the enemy's placement), the rest
//     between; ties by id. A unit's role comes from its weapons and the existing odds helpers (deploy_assessment).
//   - Spot: core's proposal, nudged. A small, bounded set of translations of the proposed formation (x within +/-6" in
//     1" steps; a shooter up to 6" further back, a melee unit forward to the zone's front, in 1" steps) is ordered by
//     how well it faces the centre of the enemy units already set up (deploy_candidates), and the first that core
//     accepts (set_up_problems empty) and that leaves every other unit of the side that has a legal spot one (checked
//     on the bot's scratch game) is taken. With no enemy unit set up yet, the proposal. The proposal is always a
//     candidate, so CannotSetUp is answered only when the engine finds no legal spot at all.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "analysis/odds.hpp"
#include "analysis/threat.hpp"
#include "bot.hpp"
#include "core/engine/actions.hpp"
#include "core/geometry/primitives.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai {

// The answer to `side`'s pending DeployUnits, taken from `legal` (legal_actions): nullopt for any other decision, or if
// `legal` has no fully specified answer for the side's lowest-id unit waiting to be set up.
[[nodiscard]] std::optional<PolicyResult> deploy_policy(const core::GameState& state, core::Side side,
                                                        const std::vector<core::GameAction>& legal);

// ---- M77: the Normal and Hard heuristic ----

// The heuristic's roles, in set-up order.
enum class DeployRole : std::uint8_t { LongRangeShooter, Other, Melee };

inline constexpr float kLongRangeIn = 24.0f;       // a long-range shooter's longest usable ranged weapon: at least this
inline constexpr double kDeployLeanMargin = analysis::kLeanMargin;  // shares beyond 0.5 +/- this lean one way
inline constexpr float kDeployNudgeIn = 6.0f;      // x shifts within +/- this
inline constexpr float kDeployBackIn = 6.0f;       // a shooter's further depth back, at most
inline constexpr float kDeployStepIn = 1.0f;       // the step of both

// A unit's role for deployment.
// - ranged_share: its expected damage at range over its expected damage in all, summed over every enemy unit with
//   models (set up or not): analysis::unit_role's model equivalents (its best shooting within half range of its longest
//   weapon, and every model fighting with its best melee weapon), each weighted by the enemy unit's points per model
//   (1 without points), so the points it is expected to destroy; 0.5 with no damage either way.
// - LongRangeShooter: its longest usable ranged weapon (analysis::threat_ranges) is at least kLongRangeIn and its
//   ranged share is above 0.5 + kDeployLeanMargin. Melee: its ranged share is below 0.5 - kDeployLeanMargin. Other:
//   the rest.
struct DeployAssessment {
    DeployRole role = DeployRole::Other;
    float max_range_mm = 0.0f;
    double ranged_share = 0.5;
};
[[nodiscard]] DeployAssessment deploy_assessment(analysis::OddsCache& cache, const core::GameState& state,
                                                 const core::Unit& unit);

// `side`'s units waiting to be set up, in the heuristic's order: by role (long-range shooters, the rest, melee), then
// by id.
[[nodiscard]] std::vector<core::UnitId> deploy_order(const core::GameState& state, core::Side side);

// The centre of the enemy's deployed mass: the mean of the centroids of `side`'s enemy units on the battlefield (each
// unit counts once); nullopt when none is set up.
[[nodiscard]] std::optional<core::Position> enemy_deployed_centre(const core::GameState& state, core::Side side);

// The translations of `proposal` (a legal DeployUnit of a north or south zone) the heuristic considers, best first,
// the proposal itself included; none is checked against the rules here. `enemy` is enemy_deployed_centre.
// - x: dx = -6" .. +6" in 1" steps, keeping the formation's extents on the battlefield.
// - depth: LongRangeShooter 0 .. 6" further back in 1" steps (within the zone); Melee 0 .. the distance to the zone's
//   front in 1" steps, and that distance itself; Other 0.
// - order, measured from the formation's centroid (the mean of its models' positions):
//   - Melee: nearest to `enemy`;
//   - LongRangeShooter: those within `max_range_mm` of `enemy` first, the furthest back of them first; then the rest,
//     the least far back first; then nearest to `enemy` in x;
//   - Other: nearest to `enemy` in x;
//   ties: the smaller |dx|, west before east, the smaller depth change.
// An east or west zone (never with hand deployment, D-081): the proposal alone.
[[nodiscard]] std::vector<core::action::DeployUnit> deploy_candidates(const core::GameState& state,
                                                                      const core::action::DeployUnit& proposal,
                                                                      DeployRole role, float max_range_mm,
                                                                      const core::Position& enemy);

// The answer to `side`'s pending DeployUnits by the M77 heuristic (see the file comment), from `legal`
// (legal_actions); nullopt for any other decision.
[[nodiscard]] std::optional<PolicyResult> heuristic_deploy_policy(const core::GameState& state, core::Side side,
                                                                  const std::vector<core::GameAction>& legal);

// Whether a bot of this configuration deploys by the heuristic: levels Normal and Hard (Normal as at M55, D-066,
// keeps M74's policy).
[[nodiscard]] bool uses_deploy_heuristic(const BotConfig& config);

}  // namespace wh40k::ai
