#pragma once

// D-047 (M31): pile-ins and consolidations too small to matter, for "Skip steps with nothing to do"
// (D-036). Moving is the player's choice (12.02 / 12.07: "units they choose to move"), so skipping a move
// changes no rule. Read-only: built on core's preview_fight_move and move_reach; no rules here (D-024).

#include <optional>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/geometry/units.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

// D-047: a move is worth making if some model can end more than this far from where it started.
inline constexpr float kWorthwhileMoveMm = 2.0f;

// D-048 (house rule, client only): for skipping, a model whose base is this close to an enemy model is
// treated as in base contact in a pile-in (12.03) or an Ongoing Consolidation (12.08), so as not moving.
// A mouse drag can't reach exact contact the way models are pushed together on a table. The engine still
// lets such a model move closer.
inline constexpr float kNearBaseContactMm = 0.05f * core::MM_PER_INCH;

// D-049: a move is worth making only if some model could end more than this much closer to the closest
// target unit than it starts. A model can't gain more than its starting gap, so the gap is the bound.
inline constexpr float kWorthwhileGainMm = 0.1f * core::MM_PER_INCH;

// D-049: the edge distance from `model`, placed at `pos`, to the closest target unit of the move in
// progress (D-022). nullopt if no move is in progress or no target has models.
[[nodiscard]] std::optional<float> gap_to_closest_target(const core::GameState& state, const core::Model& model,
                                                         const core::Position& pos);

// D-047: whether the pile-in (12.03) or consolidation (12.08) `start` could do anything worth doing: some
// model of the unit starts more than kWorthwhileGainMm from the closest target unit (D-049) and can end
// more than kWorthwhileMoveMm from its start, or could reach the engagement range of an enemy unit it
// isn't engaged with now. Per model, ignoring coherency and the unit-wide After
// Moving conditions, so it errs towards true. True for an action that isn't a valid start (nothing to judge).
[[nodiscard]] bool fight_move_worthwhile(const core::GameState& state, core::Side side, const core::GameAction& start);

// D-047: in a Pile In, Consolidate or overrun pile-in decision whose only choices are moves not worth
// making, the action that skips them: EndPileIns, EndConsolidations or SkipPileIn. Otherwise nullopt.
[[nodiscard]] std::optional<core::GameAction> pointless_fight_step_skip(const core::GameState& state,
                                                                        const std::vector<core::GameAction>& legal);

}  // namespace wh40k::client
