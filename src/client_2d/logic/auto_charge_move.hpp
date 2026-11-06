#pragma once

// D-056 (M42): "Auto move" for charge moves (11.04), the sibling of auto_fight_move (D-050). A legal
// arrangement of the charging unit's models, as ordinary ProposeModelMove actions (one straight line per moved
// model) that the player reviews and commits, or that the bot (M42) submits. Geometry on core's move_reach
// area (D-040, D-046) plus the engine's own checks: every proposal is submitted to a scratch GameSession and
// the arrangement is only offered if move_end_problems is empty. No rules here (D-024): the charge targets come
// from the move in progress, legality from validate_action and move_end_problems.
//
// The arrangement:
// - Start points: the unit translated as a block towards the charge targets (towards their centre, and along
//   the line between the closest pair of models), as far as every model's spot stays in its area (up to the
//   charge roll, closer to a charge target, not on or through a model, not within Engagement Range of a unit
//   that isn't a target); and the M39 baseline translation (1" short of the targets).
// - Each start is refined model by model: a model moves to the best spot it can reach in one straight line
//   (base contact with a target model, else within 1", else within Engagement Range, else as close as it can
//   get), as long as the whole unit stays in coherency (03.03) and the models can still be moved one at a
//   time without ending on one not yet moved (D-027). Several model orders and two aims are tried.
// - The arrangements are ranked by the most models engaged with a charge target (they fight), then the most
//   within 1", then the most in base contact, then the least total gap; the best one the engine accepts wins.
//   TODO(if-possible): 11.04 While Moving "each model that can end within 1" / engaged must": the engine
//   doesn't enforce it; the ranking tries to satisfy as many models as it can, not every one.

#include <string>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

// How well a charging unit ended up (each model measured to the closest model of the charge targets).
struct ChargeArrangementScore {
    int engaged = 0;            // models within Engagement Range (03.04) of a charge target model
    int within_1 = 0;           // models within 1" of one (11.04 While Moving)
    int contacts = 0;           // models in base contact with one
    float total_gap_mm = 0.0f;  // the sum of the models' gaps
    friend bool operator==(const ChargeArrangementScore&, const ChargeArrangementScore&) = default;
};

// More engaged, then more within 1", then more in base contact, then less total gap (by more than EPS_MM).
[[nodiscard]] bool better_charge_score(const ChargeArrangementScore& a, const ChargeArrangementScore& b);

// The score of the moving unit as it stands, against the move's targets (zero if no move is in progress).
[[nodiscard]] ChargeArrangementScore charge_arrangement_score(const core::GameState& state);

struct AutoChargeMove {
    std::vector<core::GameAction> moves;  // one ProposeModelMove (one straight line) per moved model, in order
    std::vector<std::string> problems;    // why there is no arrangement (empty when `moves` is usable)
    ChargeArrangementScore score;         // of the arrangement after `moves`
    [[nodiscard]] bool ok() const noexcept { return problems.empty() && !moves.empty(); }
};

// D-056: a charge move is in progress, it is the pending decision, and no model has been moved in it yet.
[[nodiscard]] bool auto_charge_move_available(const core::GameState& state);

// D-056: the arrangement (see the file comment). Deterministic: a pure function of the state.
[[nodiscard]] AutoChargeMove auto_charge_move(const core::GameState& state);

}  // namespace wh40k::client
