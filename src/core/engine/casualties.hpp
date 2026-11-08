#pragma once

// D-051: a read-only preview of the pending casualty decision (05.04 Select Model for an attack batch, or
// 06.02 for mortal wounds: Desperate Escape's, 09.07, or [HAZARDOUS]'s, 24.15), so a client can let the player pick the models one at a time. The answer is
// still one OrderCasualties (D-013): the picks, then the rest of the unit.

#include <optional>
#include <span>
#include <vector>

#include "core/model/dice_expr.hpp"
#include "core/rules/attack_sequence.hpp"  // DamageResult
#include "core/state/game_state.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

// The steps of the pending casualty decision, replayed with the models picked so far. A step that would
// damage a model goes to:
// - the first model already picked (in pick order) that is a legal choice for it. Once picked, a model
//   keeps taking the damage forced onto it (05.04: "a model that has lost one or more wounds if possible");
// - otherwise the next pick, which must be a legal choice (even when it is the only one, D-051: no model
//   takes damage without being picked).
// The preview stops at the first step that needs a pick the player hasn't made yet.
struct CasualtyPreview {
    std::vector<DamageResult> results;  // the damage done so far, in order (forced steps included)
    std::vector<ModelId> options;       // who may be picked next; empty when every step is resolved
    std::optional<int> next_damage;     // the damage the next pick takes (05.04: the attack's D; 06.02: 1)
    // The damage still to be allocated: the next step's and every damaging step after it (each attack's D,
    // or 1 per mortal wound). 0 when done. Attacks lost because the unit would be wiped out don't count.
    int damage_left = 0;
    int attacks_lost = 0;               // 05.04 excess attacks / 06.02 mortal wounds not inflicted (when done)
    // D-069: the attack's D when it is random (e.g. D6+1). It is rolled only when the casualties are applied, so the
    // preview takes its maximum (damage_stand_in): the results' damage, next_damage and damage_left are then "up to".
    std::optional<DiceExpr> random_damage{};
    // D-069: the results from this index on, and the next pick, are provisional: they follow the stand-in of an earlier
    // random roll, and with a lower roll its model survives and takes the next attack instead (05.04). 1 once a random
    // roll is in the results; nullopt when every result is exact.
    std::optional<std::size_t> provisional_from{};
    [[nodiscard]] bool done() const noexcept { return options.empty(); }
    // D-069: whether the next pick is provisional.
    [[nodiscard]] bool next_provisional() const noexcept {
        return provisional_from.has_value() && *provisional_from <= results.size();
    }
    friend bool operator==(const CasualtyPreview&, const CasualtyPreview&) = default;
};

// nullopt if no OrderCasualties decision is pending, a pick isn't a legal choice at its step, or there
// are more picks than steps that need one. For a fixed D, applying OrderCasualties{target, picks + the unit's other
// models} to `state` gives exactly the preview's results once it is done(). D-069: for a random D the preview takes
// the D's maximum and rolls no dice; whatever the rolls, the models the attack damages are then among the picks, in
// pick order (a lower roll only reaches the next pick later, or not at all).
[[nodiscard]] std::optional<CasualtyPreview> preview_casualties(const GameState& state, std::span<const ModelId> picks);

}  // namespace wh40k::core
