#pragma once

// M41 (D-054): the Normal level's Fight phase decisions (12.02-12.08, 04.01, 04.02 While Fighting).
//
// The measure is the melee exchange (value.hpp's unit shares): what the units that will still fight this phase
// are expected to destroy, ours minus theirs, from the engine's melee_options in the state as it is (only
// engaged models fight).
// - Pile-ins (12.02): each offered pile-in (unit and targets) is simulated in full (auto_fight_move, D-050,
//   then the engine's end-of-move checks and the commit) and made only if it improves the exchange. Ending the
//   step is always a candidate, so a pile-in that would bring more enemy attackers than ours is skipped.
// - Selecting a unit to fight (12.04): each offered unit and fight type is simulated up to its attacks; an
//   Overrun's additional pile-in (12.06) is planned as above, and the enemy units it newly engages (which then
//   fight back) count against it. A pass (Appendix) only when no unit can make an attack.
// - Melee declarations (04.01, 04.02): each model's weapon, profile and targets, attacks split across targets
//   when that is worth more, by coordinate descent on the exact outcome (overkill across targets falls out).
// - Consolidations (12.07): each offered move is simulated; the standing exchange afterwards (engaged: the next
//   fight and close-quarters shooting; unengaged: shooting at the gap) minus the free fight an Engaging
//   Consolidation gives each enemy unit that hasn't fought (12.08), against not moving.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "analysis/odds.hpp"
#include "bot.hpp"
#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai::normal {

struct MeleeChoice {
    core::UnitId unit;
    std::vector<core::MeleeDeclaration> declarations;
    std::vector<analysis::TargetOutcome> outcomes;
    double value = 0.0;  // unit shares
};

// The best declarations for `unit` fighting now (every model that can fight declares, D-021). nullopt if no
// model of the unit can make a melee attack.
[[nodiscard]] std::optional<MeleeChoice> plan_melee(analysis::OddsCache& cache, const core::GameState& state,
                                                    core::UnitId unit);

// Will `unit` still fight this phase (not yet selected and eligible or engaged; or selected and its attacks not
// yet made)?
[[nodiscard]] bool will_fight(const core::GameState& state, const core::Unit& unit);

// The melee exchange for `side`: plan_melee values of its units that will still fight, minus those of the enemy
// units that will still fight and are engaged with one of its units.
[[nodiscard]] double melee_exchange(analysis::OddsCache& cache, const core::GameState& state, core::Side side);

// The standing position after a consolidation, for `side` (see the file comment).
[[nodiscard]] double consolidation_score(analysis::OddsCache& cache, const core::GameState& state, core::Side side);

// The model positions and paths of the move in progress put back to its start (a copy, for planning only): the
// state just after the move was started, so a plan is a function of that state (D-054). Precondition: a move is
// in progress.
[[nodiscard]] core::GameState move_origin(const core::GameState& state);

// A pile-in or consolidation simulated in full from `state`: `start` (StartPileIn / StartConsolidation), the
// auto_fight_move placements and the commit, each step with the hash of the state it expects. nullopt if any
// step is refused, the move would be reverted, or auto_fight_move finds no arrangement.
struct FightMovePlan {
    std::vector<PlannedStep> steps;
    core::GameState after;
};
[[nodiscard]] std::optional<FightMovePlan> simulate_fight_move(const core::GameState& state, core::Side side,
                                                               const core::GameAction& start, std::uint64_t seed,
                                                               const std::string& reason);

// The decisions (each answers the pending decision; nullopt when the policy has no answer of its own, and the
// caller uses the baseline's).
[[nodiscard]] std::optional<PolicyResult> decide_fight_selection(analysis::OddsCache& cache,
                                                                 const core::GameState& state, core::Side side,
                                                                 const std::vector<core::GameAction>& legal,
                                                                 std::uint64_t seed);
[[nodiscard]] std::optional<PolicyResult> decide_melee(analysis::OddsCache& cache, const core::GameState& state,
                                                       core::UnitId unit);
[[nodiscard]] std::optional<PolicyResult> decide_pile_in(analysis::OddsCache& cache, const core::GameState& state,
                                                         core::Side side, const std::vector<core::GameAction>& legal,
                                                         std::uint64_t seed);
[[nodiscard]] std::optional<PolicyResult> decide_overrun_pile_in(analysis::OddsCache& cache,
                                                                 const core::GameState& state, core::Side side,
                                                                 const std::vector<core::GameAction>& legal,
                                                                 std::uint64_t seed);
[[nodiscard]] std::optional<PolicyResult> decide_consolidation(analysis::OddsCache& cache,
                                                               const core::GameState& state, core::Side side,
                                                               const std::vector<core::GameAction>& legal,
                                                               std::uint64_t seed);
// A pile-in or consolidation in progress (MoveModels): the rest of the auto_fight_move arrangement planned from
// the move's start, so a replan (after a load or undo, or a fresh bot) gives the same steps as the memo.
[[nodiscard]] std::optional<PolicyResult> continue_fight_move(const core::GameState& state, core::Side side,
                                                              std::uint64_t seed);

}  // namespace wh40k::ai::normal
