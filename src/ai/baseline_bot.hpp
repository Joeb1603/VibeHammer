#pragma once

// Level Easy (M39, D-054): the M12 test policy (tests/client/client_logic_test.cpp, choose()), as a bot.
// - Normal move 2" towards the enemy while it is more than 8" away, otherwise Remain Stationary.
// - Shoot with the shooting editor's defaults; charge whenever a charge can be declared; fight with the melee
//   editor's defaults; casualties and allocation in their default order.
// - Charge moves translate the unit to 1" short of the enemy; other moves (an Overrun's pile-in) 2" towards it.
// - Pile-ins and consolidations are ended (the editors' first button).
// Unlike the test policy it never lets a move be reverted: the whole move is simulated first, and checked
// with move_end_problems before Commit, and a move that wouldn't stand is never started (Remain Stationary,
// SkipChargeMove, SkipPileIn instead).

#include <cstdint>
#include <vector>

#include "bot.hpp"
#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai {

[[nodiscard]] PolicyResult easy_policy(const core::GameState& state, core::Side side,
                                       const std::vector<core::GameAction>& legal, std::uint64_t seed);

}  // namespace wh40k::ai
