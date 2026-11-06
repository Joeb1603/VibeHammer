#pragma once

// D-050 (M34): "Auto move" for pile-ins (12.03) and consolidations (12.08). A legal arrangement of the
// moving unit's models, as ordinary ProposeModelMove actions the player reviews and then commits. Every
// placement is checked by the engine (validate_action, move_end_problems) on a working copy; there are no
// rules here (D-024). Greedy, not optimal: a starting point the player can adjust.

#include <string>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

// D-050: spots whose gap to the closest target differs by this little count as equally close, so the
// formation tie-break (more friends within 2") can choose between them.
inline constexpr float kAutoMoveGapBandMm = 1.0f;

struct AutoFightMove {
    std::vector<core::GameAction> moves;  // one ProposeModelMove (one straight line) per moved model
    std::vector<std::string> problems;    // why there is no arrangement (empty when `moves` is usable)
    [[nodiscard]] bool ok() const noexcept { return problems.empty() && !moves.empty(); }
};

// D-050: a pile-in or consolidation is in progress, it is the pending decision, and no model has been
// moved in it yet (auto move plans the whole unit from its start).
[[nodiscard]] bool auto_fight_move_available(const core::GameState& state);

// D-050: the arrangement. Each model, closest to the target first, goes to the reachable spot closest to
// the closest target unit (1 mm bands), then with the most friendly models within 2" (up to 2), then the
// shortest move; the unit stays in coherency (03.03) after every placement. A model 0.1" or less from the
// target (D-049) only moves straight into base contact. It is tried in that order and in reverse; of the
// results that pass the engine's end-of-move checks, the one with more models in base contact (then less
// total gap) is kept. If neither passes, `problems` says why.
[[nodiscard]] AutoFightMove auto_fight_move(const core::GameState& state);

}  // namespace wh40k::client
