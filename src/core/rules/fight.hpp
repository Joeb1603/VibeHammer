#pragma once

// 12 Fight phase rules as pure queries over GameState: pile-in moves (12.03), eligibility to fight and
// fight types (12.04-12.06), passing (Appendix: Eligible to Fight, but Unable to Fight), consolidation
// modes (12.08) and melee weapon selection (04.01, 04.02 While Fighting, D-021). The engine runs the
// phase in engine/fight.cpp and the moves through engine/movement.cpp.

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/rules/attack_types.hpp"
#include "core/rules/fight_types.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::core {

inline constexpr float kPileInDistanceInches = 3.0f;         // 12.03 Maximum Distance
inline constexpr float kPileInTargetRangeInches = 5.0f;      // 12.03 Before Moving
inline constexpr float kConsolidationDistanceInches = 3.0f;  // 12.08 Maximum Distance
inline constexpr float kEngagingRangeInches = 3.0f;          // 12.08 Engaging Consolidation
inline constexpr float kPassRangeInches = 5.0f;              // Appendix: Eligible to Fight, but Unable

// ---- model-level engagement (03.04) ----

// The enemy units (of `unit`'s opponent) that `model` of `unit`, at `pos`, is engaged with.
[[nodiscard]] std::vector<UnitId> enemy_units_engaged_with_model(const GameState& state, const Unit& unit,
                                                                 const Model& model, const Position& pos);

// D-022: is `model` of `unit`, at `pos`, in base contact with one or more enemy models?
[[nodiscard]] bool in_base_contact_with_enemy(const GameState& state, const Unit& unit, const Model& model,
                                              const Position& pos);

// ---- 12.03 Pile-in move ----

// 12.03 Eligible If (it is the Fight phase): engaged, made a charge move this turn, or selected to make
// an overrun fight this phase.
[[nodiscard]] bool eligible_to_pile_in(const GameState& state, const Unit& unit);

// 12.03 Before Moving: the enemy units that can be pile-in targets. If the unit is engaged, exactly the
// units it is engaged with (all of them must be selected); otherwise those within 5".
[[nodiscard]] std::vector<UnitId> pile_in_target_options(const GameState& state, const Unit& unit);

// 12.03 Before Moving: why `targets` is not a valid pile-in target selection, or nullopt.
[[nodiscard]] std::optional<std::string> pile_in_targets_problem(const GameState& state, const Unit& unit,
                                                                 std::span<const UnitId> targets);

// ---- 12.04-12.06 Fight ----

// 12.04: not already selected to fight this phase, and engaged, engaged at the start of the Fight step,
// made a charge move this turn, or became eligible through an Engaging Consolidation (12.08).
[[nodiscard]] bool eligible_to_fight(const GameState& state, const Unit& unit);

// 12.05 / 12.06 Eligible If, and the Appendix FAQ (an overrun fight needs the unit to be eligible to
// fight). Rule-cited problem, or nullopt.
[[nodiscard]] std::optional<std::string> fight_type_problem(const GameState& state, const Unit& unit,
                                                            FightType type);

// Appendix: `side` may pass if it has units eligible to fight and all of them are more than 5" from
// every enemy unit.
[[nodiscard]] bool may_pass_fight(const GameState& state, Side side);

// ---- 12.08 Consolidation move ----

// 12.08 Before Moving: the first mode that applies (Ongoing if engaged, Engaging if within 3" of an
// enemy unit; V1 has no objectives), or nullopt if none does (09 sidebar: it cannot make the move).
[[nodiscard]] std::optional<ConsolidationMode> consolidation_mode(const GameState& state, const Unit& unit);

// The enemy units the mode selects from: Ongoing, every engaged unit (all must be selected); Engaging,
// those within 3" (one or more).
[[nodiscard]] std::vector<UnitId> consolidation_target_options(const GameState& state, const Unit& unit);

// 12.08 Eligible If (was eligible to fight this phase), not consolidated this step, and a mode applies.
[[nodiscard]] bool eligible_to_consolidate(const GameState& state, const Unit& unit);

[[nodiscard]] std::optional<std::string> consolidation_targets_problem(const GameState& state, const Unit& unit,
                                                                       std::span<const UnitId> targets);

// ---- shared While / After Moving checks (12.03, 12.08; D-022) ----

// Each model that is moved must end its move closer to the closest of `targets` (measured from its
// start position, edge to edge; any unit tied for closest counts, D-010).
[[nodiscard]] bool ends_closer_to_closest_target(const GameState& state, const Model& model, const Position& start,
                                                 const Position& end, std::span<const UnitId> targets);

// 12.03 / 12.08 Ongoing After Moving: each model that started the move engaged with an enemy unit is
// still engaged with that unit. `starts` are the models' start positions. Rule-cited problems.
[[nodiscard]] std::vector<std::string> still_engaged_problems(const GameState& state, const Unit& unit,
                                                              std::span<const ModelStart> starts,
                                                              std::string_view rule);

// ---- 04.01 / 04.02 While Fighting (D-021), 24.11 [EXTRA ATTACKS] (D-071) ----

// Every rule problem with `declarations` for `unit`. Rule-cited; empty means valid. Each engaged model that
// can make melee attacks declares one melee weapon (04.01), or, if it has [EXTRA ATTACKS] weapons, each of
// those plus one other melee weapon if it has one it can use (24.11; weapons refused under D-069 / D-026 /
// D-071 count as absent). A weapon is declared once; to attack several units it splits its attacks (04.02).
[[nodiscard]] std::vector<std::string> melee_problems(const GameState& state, const Unit& unit,
                                                      std::span<const MeleeDeclaration> declarations);

// Can any model of `unit` make a melee attack now (has a melee weapon and an enemy unit engaged with it)?
[[nodiscard]] bool can_make_melee_attacks(const GameState& state, const Unit& unit);

// Every single-target declaration a model may make (each melee weapon, profile and engaged enemy unit,
// with all of the weapon's attacks), in model order. For clients and random play. A model offered an
// [EXTRA ATTACKS] weapon (is_extra_attacks_weapon, attacks.hpp) must declare one option of each of those
// weapons, plus one option of one other weapon if it is offered one (24.11).
[[nodiscard]] std::vector<MeleeDeclaration> melee_options(const GameState& state, const Unit& unit);

// 04.02 Select Targets: the WeaponSelections for valid declarations (one per target; split attacks
// recorded as declared_attacks). Precondition: melee_problems is empty.
[[nodiscard]] std::vector<WeaponSelection> make_melee_selections(const GameState& state,
                                                                 std::span<const MeleeDeclaration> declarations);

}  // namespace wh40k::core
