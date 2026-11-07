#pragma once

// Level Normal (M41-M43, D-054). Every decision uses the M40 analysis:
// - shooting (normal/shooting.hpp): which unit shoots, shooting type, each model's weapons, profiles and
//   targets with overkill avoidance; the order of attack batches (SelectAttackBatch);
// - defense (normal/defense.hpp): allocation order, casualty priority (also Desperate Escape hazards), and the
//   model removed to regain coherency;
// - fight (normal/fight.hpp): unit and fight type, pass, melee declarations, Overrun and normal pile-ins,
//   consolidations;
// - charges (normal/charge.hpp, M42): whether and which unit declares a charge, the charge targets after the roll
//   (or no charge move), and the charge move itself (the client's Auto move for charges, D-056).
// - movement (normal/movement.hpp, M43): the move type (Remain Stationary, Normal, Advance decided from its odds
//   before the roll, Fall Back) and the planned move, scored for the whole turn (normal/move_score.hpp).
// Any decision the Normal level has no answer of its own for goes to the
// baseline, and Bot::decide still validates every answer and falls back if one isn't legal.

#include <cstdint>
#include <vector>

#include "bot.hpp"
#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai {

// `generation` (D-066): Current, or M55 (the regression switch: Normal, and the baseline it falls back to, exactly
// as at M55).
[[nodiscard]] PolicyResult normal_policy(const core::GameState& state, core::Side side,
                                         const std::vector<core::GameAction>& legal, std::uint64_t seed,
                                         Generation generation = Generation::Current);

}  // namespace wh40k::ai
