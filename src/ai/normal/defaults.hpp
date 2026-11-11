#pragma once

// D-085 (M85): the bot's default answers, for every decision the bot's own analysis (normal_bot.hpp) has no answer
// for. They were level Easy's policy (M39, D-054: the M12 test policy as a bot); with one bot they are only the
// defaults, kept exactly as Easy played them (its reasons too):
// - Normal move 2" towards the enemy while it is more than 8" away, otherwise Remain Stationary.
// - D-066 (M56): "the enemy" is the closest enemy unit (D-010), not the first; while it is more than 12" + M away
//   the move is the unit's full Move (then the 2" steps). A charge move aims at the closest of its targets, and
//   the charge targets are the closest eligible unit alone when that is offered.
// - Shoot with the shooting editor's defaults; charge whenever a charge can be declared; fight with the melee
//   editor's defaults; casualties and allocation in their default order (casualties: D-058's order, M47); the
//   model removed to regain coherency (03.03) is the default order's first (default_coherency_removal, M47).
// - Charge moves translate the unit to 1" short of the enemy; Normal moves 2" towards it.
// - D-057 (M46): pile-ins (an Overrun's too) and consolidations are made by Auto move (D-050), the first offered
//   one worth making (D-047, D-049) that stands; otherwise the step is ended. No weighing.
// - M61 (D-072): a [TWIN-LINKED] batch's failed wound rolls are all re-rolled, and no other (24.38).
// It never lets a move be reverted: the whole move is simulated first, and checked with move_end_problems before
// Commit, and a move that wouldn't stand is never started (Remain Stationary, SkipChargeMove, SkipPileIn instead).

#include <cstdint>
#include <optional>
#include <vector>

#include "bot.hpp"
#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai::normal {

// 24.38 (D-072): the answer to a pending RerollWoundRolls: every failed wound roll (nullopt for any other decision).
[[nodiscard]] std::optional<PolicyResult> reroll_failed_wound_rolls(const core::GameState& state);

// The default answer to `side`'s pending decision (see the file comment).
[[nodiscard]] PolicyResult default_answer(const core::GameState& state, core::Side side,
                                          const std::vector<core::GameAction>& legal, std::uint64_t seed);

}  // namespace wh40k::ai::normal
