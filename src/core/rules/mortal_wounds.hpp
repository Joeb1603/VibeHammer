#pragma once

#include <vector>

#include "core/dice/dice.hpp"
#include "core/model/unit.hpp"
#include "core/rules/attack_sequence.hpp"  // DamageResult, ModelChooser
#include "core/types/ids.hpp"

namespace wh40k::core {

// ---- 06.02 Mortal Wounds ----

// 06.02 Select Model, first instruction that applies:
//  1. non-CHARACTER models that have lost wounds;  2. otherwise non-CHARACTER models;
//  3. otherwise wounded CHARACTER models;          4. otherwise CHARACTER models.
// The controlling player chooses among the returned models. Empty if the unit has no models.
[[nodiscard]] std::vector<ModelId> mortal_wound_candidates(const Unit& unit);

struct MortalWoundsResult {
    std::vector<DamageResult> steps;  // one per mortal wound inflicted
    int models_destroyed = 0;
    int not_inflicted = 0;  // mortal wounds left when the unit was destroyed
    friend bool operator==(const MortalWoundsResult&, const MortalWoundsResult&) = default;
};

// 06.02: resolves `count` mortal wounds one at a time (each: select a model, it loses 1 wound) until
// all are inflicted or the unit is destroyed. `choose` is called only when there is more than one
// candidate (the controlling player's choice; M5 makes it a PendingDecision), or for every wound when
// `ask_when_forced` (D-051's preview, which follows each step).
[[nodiscard]] MortalWoundsResult inflict_mortal_wounds(Unit& unit, int count, const ModelChooser& choose,
                                                       bool ask_when_forced = false);

// ---- 06.03 Hazard Rolls ----

struct HazardRolls {
    std::vector<int> rolls;
    int failures = 0;       // results of 1-2
    int mortal_wounds = 0;  // 1 per failure, or 3 if every model in the unit is a MONSTER/VEHICLE
    friend bool operator==(const HazardRolls&, const HazardRolls&) = default;
};

// 06.03: makes `count` hazard rolls simultaneously. The caller inflicts the mortal wounds (06.02).
[[nodiscard]] HazardRolls make_hazard_rolls(DiceSource& dice, const Unit& unit, int count);

}  // namespace wh40k::core
