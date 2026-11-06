#pragma once

// M41 (D-054): the Normal level's answers when its own units lose models: the allocation order (05.03), the
// casualty priority (05.04 Select Model, 06.02 mortal wounds, so also Desperate Escape hazards, 09.07) and the
// model removed to regain coherency (03.03).
//
// Casualty priority, most important first (each a term of one score per candidate, lower removed first):
// 1. Coherency (03.03): the models left must still be in coherency, else the End of Turn step removes more
//    (estimated by coherency_losses). Weighted far above the rest, so it is kept wherever it can be.
// 2. Position, by stance (casualty_stance): away from an enemy the unit would rather not fight, remove the
//    models closest to it (its charge gets longer); towards one it wants to fight, the furthest; engaged, the
//    unengaged models while the unit will still fight this phase (its engaged models fight back) or is winning,
//    else (losing, or falling back) the engaged models (to break the combat).
// 3. Weapons: keep the models worth more (model_worth: a sergeant's special weapon).
// 4. A model that has already lost wounds goes first when the engine leaves a choice.
// The engine's own rules decide what is legal: every pick is one preview_casualties offers.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "analysis/odds.hpp"
#include "core/engine/actions.hpp"
#include "core/geometry/coherency.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai::normal {

enum class CasualtyStance : std::uint8_t {
    AvoidContact,  // unengaged; the enemy gains more from a combat than we do: remove the front models
    SeekContact,   // unengaged; we gain more from a combat: remove the rear models
    BreakCombat,   // engaged, already fought and losing the combat, or falling back: remove the engaged models
    HoldCombat,    // engaged and still to fight this phase, or winning: remove the unengaged models
};

struct StanceInfo {
    CasualtyStance stance = CasualtyStance::AvoidContact;
    // How much position matters against weapons: 1 while an enemy may charge (or is engaged), less otherwise.
    double position_weight = 1.0;
    std::string why;
};

[[nodiscard]] StanceInfo casualty_stance(analysis::OddsCache& cache, const core::GameState& state,
                                         const core::Unit& unit);

// 03.03 Regaining Coherency, estimated: how many models would have to be removed from `models` for them to be
// in coherency (greedy: each time the model whose removal leaves the fewest coherency problems). 0 if they are.
[[nodiscard]] int coherency_losses(std::vector<core::PlacedModel> models);

struct CasualtyOrder {
    std::vector<core::ModelId> priority;  // every model of the unit, most-preferred casualty first (D-013)
    std::string reason;
};

// The pending OrderCasualties decision (05.04 / 06.02) answered for the unit losing models. nullopt if none is
// pending or the preview refuses (the caller then uses the default order).
[[nodiscard]] std::optional<CasualtyOrder> casualty_order(analysis::OddsCache& cache, const core::GameState& state);

// 03.03 RestoreCoherency for `unit`: the model whose removal restores coherency with the fewest further
// removals, then the least valuable by position and weapons. nullopt if the unit has no models.
[[nodiscard]] std::optional<core::ModelId> coherency_removal(analysis::OddsCache& cache, const core::GameState& state,
                                                             core::UnitId unit, std::string* reason = nullptr);

// 05.03 DeclareAllocationOrder: of the legal orders (in `legal`), the one whose groups go from the least to the
// most valuable models per wound (model_worth). The first legal order if nothing tells them apart.
[[nodiscard]] std::optional<core::action::DeclareAllocationOrder> allocation_order(
    analysis::OddsCache& cache, const core::GameState& state, const std::vector<core::GameAction>& legal);

}  // namespace wh40k::ai::normal
