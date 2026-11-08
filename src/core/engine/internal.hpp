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
void next_stage(GameState& state, DiceSource& dice, Events& events);
// Enters `stage` (sets it and runs its entry work).
void enter_stage(GameState& state, Stage stage, DiceSource& dice, Events& events);

// ---- turn_phases.cpp: stage decisions and the turn-structure actions ----

// The decision the current stage needs (pure), or nullopt if the stage has nothing to ask.
[[nodiscard]] std::optional<PendingDecision> stage_decision(const GameState& state);
// Entry work for a stage, after state.flow.stage is set to it.
void on_enter_stage(GameState& state, DiceSource& dice, Events& events);
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

// ---- command.cpp: 08 Command phase, 03.03 Regaining Coherency ----

void gain_core_cp(GameState& state, Events& events);                           // 08.02
void battle_shock_step(GameState& state, DiceSource& dice, Events& events);    // 08.03
// 03.03: the End of Turn step's decision while a unit on the battlefield is not in coherency.
[[nodiscard]] std::optional<PendingDecision> coherency_decision(const GameState& state);
[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::RestoreCoherency& d);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side,
                                                const action::RemoveModelForCoherency& a);
void apply_details(GameState& state, Side side, const action::RemoveModelForCoherency& a, DiceSource& dice,
                   Events& events);

// ---- fight.cpp: 12 Fight phase ----

// Entry work for StartOfFightPhase, PileIn, Fight and Consolidate.
void on_enter_fight_stage(GameState& state, Events& events);
// The decision the PileIn / Fight / Consolidate stage needs (pure), or nullopt.
[[nodiscard]] std::optional<PendingDecision> fight_stage_decision(const GameState& state);
// 12.04 "if this is not possible" branches and the end of the Fight step (D-020). True if it did
// something; false if the stage has nothing more to do.
bool fight_stage_progress(GameState& state, Events& events);
// A unit selected to fight: its overrun pile-in or melee declaration (pure), or nullopt.
[[nodiscard]] std::optional<PendingDecision> fight_decision(const GameState& state);
void fight_progress(GameState& state, Events& events);
// 12.08: the player who must select the next forced unit to fight.
[[nodiscard]] std::optional<PendingDecision> forced_fight_decision(const GameState& state);
// Called by the movement sub-machine when a pile-in or consolidation move ends (After Moving).
void fight_move_committed(GameState& state, const MoveInProgress& mv, Events& events);

[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::PileIn& d);
[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::OverrunPileIn& d);
[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::SelectUnitToFight& d);
[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::DeclareMeleeAttacks& d);
[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::Consolidate& d);
[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::ForcedFight& d);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::StartPileIn& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::SkipPileIn& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::EndPileIns& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::SelectToFight& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::PassFight& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::Fight& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side,
                                                const action::StartConsolidation& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side,
                                                const action::EndConsolidations& a);
void apply_details(GameState& state, Side side, const action::StartPileIn& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::SkipPileIn& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::EndPileIns& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::SelectToFight& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::PassFight& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::Fight& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::StartConsolidation& a, DiceSource& dice,
                   Events& events);
void apply_details(GameState& state, Side side, const action::EndConsolidations& a, DiceSource& dice,
                   Events& events);

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
// 06.02: `count` mortal wounds on `unit`, each Select Model step taking the first legal model in `priority`
// (D-013), with the MortalWoundsInflicted / ModelDestroyed / UnitDestroyed events. Shared by Desperate Escape
// (09.07) and [HAZARDOUS] (24.15).
void inflict_mortal_wounds_by_priority(GameState& state, Unit& unit, int count, const std::vector<ModelId>& priority,
                                       Events& events);

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
// Every non-empty subset of `ids` (at most 2^10 - 1), in bit order: the target sets legal_actions lists
// for charges (11.04), pile-ins (12.03) and consolidations (12.08). Defined in fight.cpp.
[[nodiscard]] std::vector<std::vector<UnitId>> nonempty_subsets(const std::vector<UnitId>& ids);
[[nodiscard]] std::string side_label(Side side);  // "Side A"
[[nodiscard]] std::string unit_label(UnitId id);  // "unit 3"

}  // namespace wh40k::core::detail
