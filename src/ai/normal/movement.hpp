#pragma once

// M43 (D-054): the Normal level's Movement phase (09.02-09.07, 03.01-03.04).
//
// - MoveUnits: for the unit to move, each move type offered is valued with its best planned move
//   (move_score.hpp): Remain Stationary; a Normal move; an Advance, valued before its roll as the mean over the six
//   advance results (each one's best move on a scratch state with that roll scripted, D-054: the real roll is never
//   looked at first); a Fall-back move when engaged (Ordered Retreat, or Desperate Escape when it is the only
//   mode, less its expected hazard losses: decided before the hazard rolls). The best is made. The bot never resets
//   a move to switch type after seeing a roll.
// - The move itself (MoveModels): `plan_unit_move` is a pure function of the state just after the move started
//   (with the actual advance roll, after any Desperate Escape casualties), so the plan made in MoveUnits and a
//   replan from any state along the move give the same steps.
// - Candidates (move_candidates): the unit translated along the 16 fixed directions (directions.hpp, no sin/cos)
//   by fixed distance steps up to its maximum (and as far as it can go where a step is refused), line-abreast and
//   two-rank formations facing the enemy at fixed offsets towards and away from it, and staying put. Cheap
//   geometric checks prune them (battlefield edges, enemy bases on the path, engagement range, bases on bases,
//   coherency); the best few by the cheap score are simulated in full on a Simulator (every ProposeModelMove
//   validated, move_end_problems empty, the commit not reverted) and refined from the engine's answers.
// - Formations assign models to slots in order along the line (no crossings), propose them in an order where no
//   model ends on one still to move (D-027), and break a cycle by parking one model next to its start first and
//   proposing it again later (a proposal replaces the earlier path).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "analysis/odds.hpp"
#include "bot.hpp"
#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"
#include "generation.hpp"
#include "normal/move_score.hpp"

namespace wh40k::ai::normal {

// One way to make the move in progress.
struct MoveCandidate {
    std::string label;                                      // "3\" back", "line abreast 3\" towards"
    std::vector<core::Position> ends;                       // every model's end, in unit order
    std::vector<float> lengths;                             // every model's path length, in unit order
    std::vector<core::action::ProposeModelMove> proposals;  // in submission order (D-027)
    float longest_mm = 0.0f;
};

// Work budgets (D-054: counts, never time).
inline constexpr std::size_t kPreciseMoves = 3;        // candidates simulated in full and refined per plan
inline constexpr std::size_t kMaxSimulatedMoves = 12;  // simulation attempts per plan
inline constexpr std::size_t kChargeProspects = 2;     // refined candidates that also get charge_prospect
inline constexpr std::size_t kAdvanceEstimateMoves =
    1;  // per advance roll, for the value of an Advance before the roll

// The candidates for the move just started in `origin` (MoveModels pending, no model moved yet), each passing the
// cheap geometric checks; the engine still decides (they are simulated before one is chosen). Deterministic.
[[nodiscard]] std::vector<MoveCandidate> move_candidates(const core::GameState& origin);

// D-075 (M66): the proposals that move each model of the move in progress in `origin` (no model moved yet) straight to
// `ends` (unit order), as a formation's are made: an order in which no model ends on one still to move (D-027), a cycle
// broken by parking one model beside its start (clear of it at every facing, so with room for a 120 mm oval) and
// proposing it again later. nullopt if no order is found. Exposed for the tests.
[[nodiscard]] std::optional<std::vector<core::action::ProposeModelMove>> slot_moves(
    const core::GameState& origin, const std::vector<core::Position>& ends);

struct PlannedMove {
    MoveCandidate candidate;
    MoveScore score;
    std::vector<PlannedStep> steps;  // the proposals and the commit, each with the hash of the state it expects
    core::GameState after;           // after the commit
};

// The best move for the move just started in `origin`. A pure function of `origin` (its seed is
// decision_seed(origin)). nullopt if no candidate stands.
[[nodiscard]] std::optional<PlannedMove> plan_unit_move(analysis::OddsCache& cache, const core::GameState& origin,
                                                        Generation generation = Generation::Current);

// M44's seam (the Hard level): the candidates of the move just started in `origin` that stand when simulated in full,
// in the cheap score's order (with `weights`), up to `count` of them after at most `attempts` simulations. Each
// keeps its cheap score (not refined). A pure function of its arguments; Normal's plan_unit_move doesn't use it.
[[nodiscard]] std::vector<PlannedMove> standing_moves(analysis::OddsCache& cache, const core::GameState& origin,
                                                      const MoveWeights& weights, std::size_t count,
                                                      std::size_t attempts);

// D-067 (M57, the Hard level's army path): the candidate of the move just started in `origin` whose end is furthest
// from the closest enemy model (the most edge-to-edge room left to every enemy unit; a tie to the earlier
// candidate), simulated in full as standing_moves simulates (nullopt if it doesn't stand). Its score is not set.
// A pure function of `origin`; Normal doesn't use it.
[[nodiscard]] std::optional<PlannedMove> furthest_standing_move(const core::GameState& origin);

// The value of each move type for `unit` at `side`'s MoveUnits decision (for tests and reasons): nullopt where the
// type isn't offered. Advance is the mean over the six advance results.
struct MoveTypeValues {
    std::optional<double> remain_stationary;
    std::optional<double> normal;
    std::optional<double> advance;
    std::optional<double> fall_back;  // Ordered Retreat, or Desperate Escape less its hazard losses
};

// D-066 (M56): which of the units offered a StartMove at MoveUnits moves next: the one closest to an enemy unit
// (edge to edge, 01.04; so an engaged unit, which chooses between holding and falling back, goes first): its move
// matters most, and the units further back then move round where it ended. A tie keeps the engine's (unit)
// order. M55: the first unit offered. nullopt if none is offered.
[[nodiscard]] std::optional<core::UnitId> unit_to_move(const core::GameState& state,
                                                       const std::vector<core::GameAction>& legal,
                                                       Generation generation = Generation::Current);

// MoveUnits: the move type and (for moves without dice) the whole planned move, for unit_to_move's unit. nullopt:
// no answer of its own.
[[nodiscard]] std::optional<PolicyResult> decide_move_units(analysis::OddsCache& cache, const core::GameState& state,
                                                            core::Side side, const std::vector<core::GameAction>& legal,
                                                            std::uint64_t seed, MoveTypeValues* values = nullptr,
                                                            Generation generation = Generation::Current);

// MoveModels of a Normal, Advance or Fall-back move: the rest of plan_unit_move planned from the move's start.
[[nodiscard]] std::optional<PolicyResult> continue_unit_move(analysis::OddsCache& cache, const core::GameState& state,
                                                             core::Side side,
                                                             Generation generation = Generation::Current);

}  // namespace wh40k::ai::normal
