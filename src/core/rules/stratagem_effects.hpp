#pragma once

// D-099 (M97): the pure queries of the EFFECT of 15.05 Explosives and 15.06 Crushing Impact: the (model, enemy unit)
// pairs its steps 1-2 can select, why a pair can't be selected (rule-cited), and the mortal wounds step 3's dice
// inflict. Read-only: the engine, the client and the bots use the same functions (D-024).

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/state/game_state.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

inline constexpr float kExplosivesRangeInches = 8.0f;  // 15.05 step 2: within 8" of the model
inline constexpr int kExplosivesDice = 6;              // 15.05 step 3: roll six D6
inline constexpr int kExplosivesSuccess = 4;           // 15.05: for each 4+, 1 mortal wound
inline constexpr int kCrushingImpactSuccess = 5;       // 15.06: for each 5+, 1 mortal wound on the enemy unit
inline constexpr int kCrushingImpactFailure = 1;       // 15.06: for each 1, 1 mortal wound on your unit
inline constexpr int kCrushingImpactCap = 6;           // 15.06: a maximum of 6 mortal wounds per unit (D-099)

// One selection of EFFECT steps 1-2: a model of the stratagem's unit and an enemy unit.
struct EffectPair {
    ModelId model;
    UnitId enemy;
    friend bool operator==(const EffectPair&, const EffectPair&) = default;
};

// 15.05 steps 1-2: why `model` of `unit` and `enemy` can't be selected: the model must be an EXPLOSIVES/GRENADES model
// of the unit; the enemy unit must be unengaged, within 8" of the model (01.04 edge to edge, D-009) and visible to it
// (06.01, 13.07 with terrain). nullopt if they can.
[[nodiscard]] std::optional<std::string> explosives_pair_problem(const GameState& state, const Unit& unit, ModelId model,
                                                                 UnitId enemy);
// Every pair Explosives can select for `unit`, in model order then unit order (`first_only`: at most one).
[[nodiscard]] std::vector<EffectPair> explosives_pairs(const GameState& state, const Unit& unit,
                                                       bool first_only = false);

// 15.06 steps 1-2: why `model` of `unit` and `enemy` can't be selected: the enemy unit must be engaged with the unit,
// and the model engaged with that enemy unit (03.04). nullopt if they can.
[[nodiscard]] std::optional<std::string> crushing_impact_pair_problem(const GameState& state, const Unit& unit,
                                                                      ModelId model, UnitId enemy);
// Every pair Crushing Impact can select for `unit`, in model order then unit order (`first_only`: at most one).
[[nodiscard]] std::vector<EffectPair> crushing_impact_pairs(const GameState& state, const Unit& unit,
                                                            bool first_only = false);

// The same by stratagem (Explosives or Crushing Impact; any other has no pairs, and every pair is refused).
[[nodiscard]] std::vector<EffectPair> effect_pairs(const GameState& state, Stratagem s, const Unit& unit,
                                                   bool first_only = false);
[[nodiscard]] std::optional<std::string> effect_pair_problem(const GameState& state, Stratagem s, const Unit& unit,
                                                             ModelId model, UnitId enemy);

// Step 3: the mortal wounds a set of dice inflicts, on the enemy unit and on the stratagem's own unit.
struct EffectMortalWounds {
    int enemy = 0;
    int unit = 0;
    friend bool operator==(const EffectMortalWounds&, const EffectMortalWounds&) = default;
};
// 15.05: each 4+ is 1 mortal wound on the enemy unit.
[[nodiscard]] EffectMortalWounds judge_explosives(std::span<const int> dice);
// 15.06: each 5+ is 1 mortal wound on the enemy unit and each 1 is 1 on the unit, each at most 6.
[[nodiscard]] EffectMortalWounds judge_crushing_impact(std::span<const int> dice);
[[nodiscard]] EffectMortalWounds judge_effect_roll(Stratagem s, std::span<const int> dice);
// Step 3's number of D6: six for Explosives; the model's T for Crushing Impact.
[[nodiscard]] int effect_dice(Stratagem s, const Model& model);

}  // namespace wh40k::core
