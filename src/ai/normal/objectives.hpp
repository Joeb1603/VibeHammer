#pragma once

// M87 (D-086): the bot's view of the D-084 mission's objective areas (14.01, 14.02). Read-only helpers for the army
// plan's area sets and the move score's objective terms: the OC a unit brings into an area, which of the next scorings
// count, and the enemy's level of control at the end of its next turn as a distribution.
//
// Scoring (D-084): each player scores the areas they control at the start of their own Command phase, in rounds
// first_scoring_round .. last, so what counts is control as the opponent's turn ended. Seen from a unit moving in our
// turn: the enemy's next scoring reads control at the end of our turn (our move can deny it), ours reads control at the
// end of the enemy's next turn (we must hold it through that turn).

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "analysis/threat.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai::normal {

struct ArmyPlan;

// The OC of `unit`'s models within range of `area` (14.02, D-009 "within": any part of the base), each model at
// `positions[i]` (unit order) with its own base and facing. A model with OC '-' adds nothing (01.07: every model of a
// battle-shocked unit).
[[nodiscard]] int oc_within(const core::Unit& unit, std::span<const core::Position> positions,
                            const core::ObjectiveArea& area);
// The same where the unit stands.
[[nodiscard]] int oc_within(const core::Unit& unit, const core::ObjectiveArea& area);
// All of the unit's models' OC (what it brings if it moves its whole unit into range).
[[nodiscard]] int unit_oc(const core::Unit& unit);
// Edge to edge from the unit's closest model to the area (0: within range).
[[nodiscard]] float area_gap_mm(const core::Unit& unit, const core::ObjectiveArea& area);
// From the unit's models' centroid to the area's edge (0 inside it): what the approach towards an area measures.
[[nodiscard]] float centroid_gap_mm(std::span<const core::Position> positions, const core::ObjectiveArea& area);

// P(a unit with these threat ranges, `gap_mm` from an area, has a model within range of it after its next move):
// 1 when it is within range or within its Move; else the likelier of its advance roll covering the gap (09.06) and a
// charge after a Normal move (onto a unit of ours standing in the area).
[[nodiscard]] double p_reaches_area(const analysis::ThreatRanges& threat, float gap_mm);

// One enemy unit is in one place: its chances `p` (Mission::objectives order) of being within range of each area after
// its move, as the enemy's level of control counts them. A unit within range of an area now (`in_range_now`) stays
// there: 1 there, 0 at every other area. Otherwise its chances are scaled down to sum to at most 1 (it can't contest
// every area it could reach at once).
void one_place(std::vector<double>& p, const std::vector<bool>& in_range_now);

// A level of control's distribution: the sum of independent units' OC, each within range with its own chance.
class LevelPmf {
public:
    void add(int oc, double p);
    // P(level < `level`): our level `level` beats it (equal levels control nothing, 14.02). 0 for a level of 0 or less.
    [[nodiscard]] double p_below(double level) const;
    [[nodiscard]] const std::vector<double>& pmf() const noexcept { return pmf_; }

private:
    std::vector<double> pmf_{1.0};
};

// Which of the next two scorings count, for `side` moving in its own turn in `state` (`turns`: turns_left(state)).
struct NextScorings {
    bool theirs = false;  // the opponent's next Command phase: control at the end of our turn
    bool ours = false;    // our next one: control at the end of the opponent's next turn
};
[[nodiscard]] NextScorings next_scorings(const core::GameState& state, core::Side side, int turns);

// Whether our unit `unit` can still move this turn: our Movement phase, not yet selected to move (or the unit moving
// now), unengaged. The same rule as the army plan's (a replan from the middle of a move agrees with Move Units).
[[nodiscard]] bool can_still_move(const core::GameState& state, const core::Unit& unit);

// One area, as a move of `us` sees it.
struct AreaOutlook {
    std::size_t index = 0;    // into Mission::objectives
    double value_us = 0.0;    // one scoring of it for us, in shares (VP x vp_share x MoveWeights::objective)
    double value_them = 0.0;  // for them
    int them_now = 0;         // the enemy's level of control now: what it has at the end of our turn
    int others = 0;           // ours without `us`: each unit where it stands, a unit the plan sends here this turn with
                              // all its OC (it hasn't moved yet: the promise that lets an area set's members coordinate)
    LevelPmf reach;           // the enemy's level of control at the end of its next turn
};
struct ObjectiveOutlook {
    NextScorings next;
    std::vector<AreaOutlook> areas;  // in Mission::objectives order
};
// What the areas are worth to a move of `us` (its models at the move's start) in `origin`, our turn; `turns`:
// turns_left(origin); `weight`: MoveWeights::objective. nullopt without a mission, with weight 0, or with no scoring
// ahead.
[[nodiscard]] std::optional<ObjectiveOutlook> objective_outlook(const core::GameState& origin, const core::Unit& us,
                                                                const ArmyPlan& plan, int turns, double weight);

struct ObjectiveTerms {
    double deny = 0.0;   // the enemy's next scoring we deny by being there at the end of our turn
    double score = 0.0;  // our next scoring we gain by holding through its turn
};
// The terms of ending the move with `us`'s models at `ends`; `survive`: the share of `us` left after the enemy's next
// turn on the line, over what is left of it now (0..1).
[[nodiscard]] ObjectiveTerms objective_terms(const ObjectiveOutlook& outlook, const core::GameState& origin,
                                             const core::Unit& us, std::span<const core::Position> ends,
                                             double survive);

}  // namespace wh40k::ai::normal
