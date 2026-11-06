#pragma once

// Level Normal (M41, M42, D-054). Every decision except movement uses the M40 analysis:
// - shooting (normal/shooting.hpp): which unit shoots, shooting type, each model's weapons, profiles and
//   targets with overkill avoidance; the order of attack batches (SelectAttackBatch);
// - defense (normal/defense.hpp): allocation order, casualty priority (also Desperate Escape hazards), and the
//   model removed to regain coherency;
// - fight (normal/fight.hpp): unit and fight type, pass, melee declarations, Overrun and normal pile-ins,
//   consolidations;
// - charges (normal/charge.hpp, M42): whether and which unit declares a charge, the charge targets after the roll
//   (or no charge move), and the charge move itself (the client's Auto move for charges, D-056).
// Movement (MoveUnits and the moves it starts) is the baseline's (easy_policy) until M43 (movement planner)
// replaces it: see `normal_policy`'s dispatch. Any decision the Normal level has no answer of its own for also goes to the
// baseline, and Bot::decide still validates every answer and falls back if one isn't legal.

#include <cstdint>
#include <vector>

#include "bot.hpp"
#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai {

[[nodiscard]] PolicyResult normal_policy(const core::GameState& state, core::Side side,
                                         const std::vector<core::GameAction>& legal, std::uint64_t seed);

}  // namespace wh40k::ai
