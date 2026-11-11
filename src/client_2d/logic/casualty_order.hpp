#pragma once

// D-058 (M47): the default casualty order, "furthest from the action first, keeping coherency". It is the order
// CasualtyPlan starts in (so "Allocate damage automatically", "Use default order", the bot's defaults and its
// fallback use it), the order CasualtyPicks fills the rest with, and the model the bot's defaults and the fallback
// remove to regain coherency. Read-only core queries and geometry only (D-024): the engine still decides 05.04, 06.02 and
// 03.03; this only ranks the models.
//
// Keys, for each candidate m of the models not yet ordered (S); the first difference decides:
// 1. Fewest extra 03.03 losses after removing m (coherency_losses of S without m): models already out of
//    coherency go first, and a model whose removal would split the unit goes late.
// 2. The odd-one-out model last: the only model of its type_name in a unit with more than one type (the
//    Sergeant), fixed at the decision. Key 1 comes first, so it goes earlier only when that keeps coherency.
// 3. At risk first: not engaged (03.04) and within 2" of at most one other model of S (a leaf of the unit's
//    chain, a straggler, one of a pair, or already isolated). Engaged models are the action: never at risk.
// 4. Furthest from the closest enemy model first (01.04 edge to edge, any enemy unit; compared with
//    core::more_than, so a difference within EPS_MM is a tie). Skipped when there is no enemy model.
// 5. Later in unit order first (the back rank, D-008), so the first-listed model is kept longest.
// Candidates are scanned in unit order and the best is replaced only by a strictly better one.
//
// Two phases: (A) only while OrderCasualties for the unit is pending, the picks so far go through core's
// preview_casualties and the best of the models it offers next is picked, step by step, so a wounded model the
// engine forces (05.04, 06.02), a picked W2 model that keeps taking the damage, the allocation groups (05.03) and
// the 06.02 tiers are followed; (B) then the best of the rest, repeatedly, so every prefix of the order is a good
// set of models to lose.

#include <optional>
#include <span>
#include <vector>

#include "core/geometry/coherency.hpp"
#include "core/state/game_state.hpp"
#include "core/types/ids.hpp"

namespace wh40k::client {

// D-058: what the default order does, for the UI's tooltips ("Use default order", "Allocate damage automatically").
inline constexpr const char* kDefaultCasualtyOrderText =
    "Furthest from the enemy and stragglers first, keeping the unit in coherency; a lone model of its kind (such as "
    "the Sergeant) last (D-058).";

// 03.03 Regaining Coherency, estimated: how many models would have to be removed from `models` for them to be in
// coherency (greedy: each time the first model whose removal leaves the fewest coherency problems, counted as
// check_coherency's isolated plus too-far models). 0 if they are. Moved from the Normal bot (M41) unchanged in
// behaviour (M47); computed on a pairwise table with the same `within` tests as check_coherency.
// D-074 part 3 (M66): each model with its facing (round bases: the same counts as before). The PlacedModel overload
// is the same at facing 0 (round bases).
[[nodiscard]] int coherency_losses(std::vector<core::PosedModel> models);
[[nodiscard]] int coherency_losses(std::vector<core::PlacedModel> models);

// D-058: every model of `target` exactly once, most-preferred casualty first (an OrderCasualties priority,
// D-013). `picks` (models of the unit, in pick order) come first, unchanged; anything in `picks` that is not a
// model of the unit, or repeats, is left out. Empty if the unit doesn't exist.
[[nodiscard]] std::vector<core::ModelId> default_casualty_order(const core::GameState& state, core::UnitId target,
                                                                std::span<const core::ModelId> picks = {});

// D-058: the model to remove for 03.03 Regaining Coherency: the first of the unit's default order (phase B on the
// unit as it stands). nullopt if the unit doesn't exist or has no models.
[[nodiscard]] std::optional<core::ModelId> default_coherency_removal(const core::GameState& state, core::UnitId unit);

}  // namespace wh40k::client
