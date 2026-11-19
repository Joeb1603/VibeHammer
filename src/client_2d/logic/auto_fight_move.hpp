#pragma once

// D-050 (M34): "Auto move" for pile-ins (12.03) and consolidations (12.08). A legal arrangement of the
// moving unit's models, as ordinary ProposeModelMove actions the player reviews and then commits. Every
// placement is checked by the engine (validate_action, move_end_problems) on a working copy; there are no
// rules here (D-024). Greedy, not optimal: a starting point the player can adjust.

#include <string>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"
#include "core/state/mission.hpp"

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
// M90 (D-091): in an Objective Consolidation the target is the objective: "closest" and "base contact" are the gap to
// the area and being within range of it (gap 0), the direct line goes towards the area's centre, and a third
// arrangement is tried, the whole unit moved straight towards the area by what its closest model needs (it keeps its
// formation); the best of the three that pass is kept.
[[nodiscard]] AutoFightMove auto_fight_move(const core::GameState& state);

// D-057 (M46): how close a unit stands to the target units of a pile-in or consolidation, and how tightly
// together, for the bot to tell a tidy fight move from not moving. Each model is measured to its closest
// target unit (edge to edge, 01.04). Read-only; no rules here (D-024).
struct FightMoveShape {
    int engaged = 0;         // models within Engagement Range (2") of a target unit
    int in_contact = 0;      // models in base contact with a target model
    float total_gap = 0.0f;  // the models' gaps to their closest target unit, summed (mm)
    int links = 0;           // friendly models within 2" (03.03), capped at 2 per model, summed
};

// D-057: the shape of `unit` against `targets` in `state`. All zero if the unit or every target is gone.
// M90 (D-091): with `objective` (an Objective Consolidation's area) the targets are ignored and each model is measured
// to the area instead: a model within range of it counts as engaged and in base contact.
[[nodiscard]] FightMoveShape fight_move_shape(const core::GameState& state, core::UnitId unit,
                                              const std::vector<core::UnitId>& targets,
                                              const core::ObjectiveArea* objective = nullptr);

// D-057: `after` minus `before`, field by field (a negative total_gap means the unit got closer).
[[nodiscard]] FightMoveShape shape_change(const FightMoveShape& before, const FightMoveShape& after);

// D-057: whether change `a` is tidier than change `b`: more models engaged, then more in base contact, then less
// total gap (by more than kAutoMoveGapBandMm), then more friendly links. Closeness first, then coherency, as
// auto_fight_move places each model. `tidier(c, {})` means better than not moving.
[[nodiscard]] bool tidier(const FightMoveShape& a, const FightMoveShape& b);

// D-057: a short description of a change for a bot reason, e.g. "3 more in base contact, 1.2\" closer in
// total". "no closer" when nothing improved.
[[nodiscard]] std::string describe_shape_change(const FightMoveShape& change);

}  // namespace wh40k::client
