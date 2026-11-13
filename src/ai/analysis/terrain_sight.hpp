#pragma once

// M89 (D-090): what terrain does to a hypothetical shot, for the move score (the engine's own paths, best_shooting and
// the Shooting phase, judge the real positions themselves). Read-only, over core's terrain queries:
// - 13.08 Benefit of Cover (D-089): a unit that takes cover (INFANTRY, BEASTS, SWARM) with every model within a terrain
//   area has it against every attacker ("area cover"); otherwise a target model must not be fully visible due to the
//   intervening terrain (D-088 Fully Visible past the cover blockers).
// - 13.09 Hidden: such a model within an area holding a Dense feature, its unit having made no ranged attacks this turn
//   or the previous turn (the caller's business), is visible only within 15".
// - 13.10 / 13.11 (D-088): a line of sight past the sight blockers of the two models' areas.
// Lines are estimated, not searched as core's has_line_of_sight / fully_visible_past do (they were most of the bot's
// time): three parallel 1 mm lines between the two bases (the centres and near either side), and for Fully Visible
// the target's two sides and its nearest point each reached from one of the observer's three points. The engine still
// decides every real shot (and best_shooting refines the bot's own best moves).
// Unlike core::TerrainView, which caches a model's areas by its id, every query takes a pose, so candidate end
// positions can be tested. Model blockers (06.01) are not counted (D-090). Without terrain every query answers as if
// there were none (visible, fully visible, no cover).

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/geometry/pose.hpp"
#include "core/geometry/primitives.hpp"
#include "core/rules/terrain.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai::analysis {

// D-090: models sampled on each side of a unit-to-unit sight test.
inline constexpr std::size_t kSightSamples = 3;

// One model's view of another.
struct SightLine {
    bool visible = true;        // 06.01 with 13.09-13.11 (no model blockers)
    bool fully_visible = true;  // D-088 Fully Visible past the intervening terrain (only when asked for)
};

class TerrainSight {
public:
    // `state` must outlive the sight and not change.
    explicit TerrainSight(const core::GameState& state);
    [[nodiscard]] bool empty() const noexcept { return empty_; }
    [[nodiscard]] const core::GameState& state() const noexcept { return state_; }

    // Bit i: a base at `pose` is within terrain area i (D-009).
    [[nodiscard]] std::uint32_t mask_at(const core::BasePose& pose) const;
    // The mask holds an area with a Dense feature (13.09).
    [[nodiscard]] bool dense(std::uint32_t mask) const noexcept { return (mask & dense_) != 0; }
    // 13.09 for a real model of `unit` where it stands (its record included).
    [[nodiscard]] bool hidden_now(const core::Unit& unit, const core::Model& model);

    // `from` looking at `to`; `to_hidden`: the observed model is hidden (13.09: visible only within 15").
    // `need_full`: also work out Fully Visible.
    [[nodiscard]] SightLine line(const core::BasePose& from, std::uint32_t from_mask, const core::BasePose& to,
                                 std::uint32_t to_mask, bool to_hidden, bool need_full);

private:
    // The shapes of `shapes` whose box meets `box`.
    [[nodiscard]] static std::vector<const core::TerrainShape*> near(std::span<const core::TerrainShape* const> shapes,
                                                                     const core::Rect& box);

    const core::GameState& state_;
    core::TerrainView view_;
    bool empty_ = true;
    std::uint32_t dense_ = 0;
    std::vector<core::Rect> bounds_;  // each area's box
};

// The box round a base (its circumscribed circle).
[[nodiscard]] core::Rect pose_box(const core::BasePose& pose);

// D-090: up to `n` of `positions` for a sight test against something at `towards`: the closest to it, then the two
// extremes across the line from the positions' centroid to it (left, then right), each once. Deterministic: ties by
// index. All of them when there are no more than `n`.
[[nodiscard]] std::vector<std::size_t> sight_samples(const std::vector<core::Position>& positions,
                                                     const core::Position& towards, std::size_t n = kSightSamples);

}  // namespace wh40k::ai::analysis
