#pragma once

// Engine internals shared between engine/*.cpp. Not part of the public API.

#include <optional>
#include <string>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/engine/actions.hpp"
#include "core/engine/events.hpp"
#include "core/engine/validation.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::core::detail {

using Events = std::vector<GameEvent>;

// ---- advance.cpp: the stage machine ----

// Runs automatic work until a decision is needed or the game is over; sets state.pending.
void advance(GameState& state, DiceSource& dice, Events& events);
// Leaves the current stage and enters the next one (with its entry work and events).
void next_stage(GameState& state, Events& events);
// Enters `stage` (sets it and runs its entry work).
void enter_stage(GameState& state, Stage stage, Events& events);

// ---- turn_phases.cpp: stage decisions and the turn-structure actions ----

// The decision the current stage needs (pure), or nullopt if the stage has nothing to ask.
[[nodiscard]] std::optional<PendingDecision> stage_decision(const GameState& state);
// Entry work for a stage, after state.flow.stage is set to it.
void on_enter_stage(GameState& state, Events& events);
// Exit work for the current stage, before the next stage is set.
void on_leave_stage(GameState& state, Events& events);

[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::MoveUnits& d);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::StartMove& a);
void apply_details(GameState& state, Side side, const action::StartMove& a, DiceSource& dice, Events& events);

// ---- shooting.cpp: 10.02 Shoot ----

[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::Shoot& d);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::Shoot& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::EndShootStep& a);
void apply_details(GameState& state, Side side, const action::Shoot& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::EndShootStep& a, DiceSource& dice, Events& events);

// ---- charge.cpp: 11.02 Charge and 11.04 Before Moving ----

// The decision a declared charge needs while no charge move is in progress (select targets or skip).
[[nodiscard]] std::optional<PendingDecision> charge_decision(const GameState& state);
// 11.02 Attempt Charge: the charge in progress is resolved; clears it and the activation.
void resolve_charge(GameState& state, bool charge_move_made, std::string reason, Events& events);

[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::Charge& d);
[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::SelectChargeTargets& d);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::DeclareCharge& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::StartChargeMove& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::SkipChargeMove& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::EndChargeStep& a);
void apply_details(GameState& state, Side side, const action::DeclareCharge& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::StartChargeMove& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::SkipChargeMove& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::EndChargeStep& a, DiceSource& dice, Events& events);

// ---- movement.cpp: 09.04-09.07 / 03.01 as a sub-machine ----

// The StartMove actions `unit` may take now (Remain Stationary first).
[[nodiscard]] std::vector<GameAction> legal_moves_for_unit(const GameState& state, const Unit& unit);
// 09.04-09.07 eligibility and fall-back mode for a StartMove.
[[nodiscard]] ValidationResult validate_move_type(const GameState& state, const Unit& unit, const action::StartMove& a);
// Starts a Normal/Advance/Fall-back move (Before Moving rolls, start positions, activation).
void begin_move(GameState& state, const action::StartMove& a, DiceSource& dice, Events& events);
// The decision the move in progress needs (pure), or nullopt if it can progress automatically.
[[nodiscard]] std::optional<PendingDecision> move_decision(const GameState& state);
// One step of automatic move work. Precondition: a move is in progress and move_decision is nullopt.
void move_progress(GameState& state, DiceSource& dice, Events& events);
// 06.02 for the moving unit's pending Desperate Escape mortal wounds, with a casualty priority (D-013).
void resolve_move_mortal_wounds(GameState& state, const std::vector<ModelId>& priority, Events& events);

[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::MoveModels& d);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::ProposeModelMove& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::CommitUnitMove& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::ResetUnitMove& a);
void apply_details(GameState& state, Side side, const action::ProposeModelMove& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::CommitUnitMove& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::ResetUnitMove& a, DiceSource& dice, Events& events);

// ---- attack_flow.cpp: 04.03 / 05 as a sub-machine ----

// The decision the attack in progress needs (pure), or nullopt if it can progress automatically.
[[nodiscard]] std::optional<PendingDecision> attack_decision(const GameState& state);
// One step of automatic attack work (rolls, damage, D-012, finishing). Precondition: an attack is in
// progress and attack_decision(state) is nullopt.
void attack_progress(GameState& state, DiceSource& dice, Events& events);
// 04.03: gathers the attack batches and makes `attacker` the selected unit (01.03); the caller then
// advances. Throws std::logic_error on the begin_attacks preconditions.
void start_attacks(GameState& state, UnitId attacker, const AttackContext& context,
                   std::vector<WeaponSelection> selections, Events& events);

[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::SelectAttackBatch& d);
[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::DeclareAllocationOrder& d);
[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::OrderCasualties& d);

[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::SelectAttackBatch& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side,
                                                const action::DeclareAllocationOrder& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::OrderCasualties& a);

void apply_details(GameState& state, Side side, const action::SelectAttackBatch& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::DeclareAllocationOrder& a, DiceSource& dice,
                   Events& events);
void apply_details(GameState& state, Side side, const action::OrderCasualties& a, DiceSource& dice, Events& events);

// ---- helpers ----
[[nodiscard]] std::string side_label(Side side);  // "Side A"
[[nodiscard]] std::string unit_label(UnitId id);  // "unit 3"

}  // namespace wh40k::core::detail
