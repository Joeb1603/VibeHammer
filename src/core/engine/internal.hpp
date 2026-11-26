#pragma once

// Engine internals shared between engine/*.cpp. Not part of the public API.

#include <optional>
#include <string>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/engine/actions.hpp"
#include "core/engine/engine.hpp"
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

// 14.02 (D-084, M86; objectives.cpp): determine control of every objective, at the end of a phase (`end_of_phase`)
// or of a turn; an ObjectiveControlChanged event for each change. No-op without a mission.
void update_objective_control(GameState& state, bool end_of_phase, Events& events);
// D-084: the turn's player scores each objective they control, in their Command phase from the first scoring round;
// a VictoryPointsScored event for each. No-op without a mission.
void score_objectives(GameState& state, Side side, Events& events);

[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::MoveUnits& d);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::StartMove& a);
void apply_details(GameState& state, Side side, const action::StartMove& a, DiceSource& dice, Events& events);

// ---- shooting.cpp: 10.02 Shoot ----

[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::Shoot& d);
[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::SnapShoot& d);  // D-097
// D-097 (M96): 15.08 Fire Overwatch's unit shoots using snap shooting: its player's SnapShoot decision.
[[nodiscard]] std::optional<PendingDecision> snap_shoot_decision(const GameState& state);
// D-097: opens a reaction window that follows no roll (the end of a phase, just after a unit has fought), if a side can
// use a stratagem there (never once the battle is over). `unit` is the unit that has just fought (AfterAttacks).
void open_window(GameState& state, WindowTiming timing, std::optional<UnitId> unit);
// charge.cpp: 11.02 Declare Charge for `unit` (D-097: also by 15.11 Heroic Intervention, in its mode).
void declare_charge(GameState& state, UnitId unit, std::optional<StratagemMode> heroic, Events& events);
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

void gain_core_cp(GameState& state, Events& events);  // 08.02
// 08.03: the battle-shock rolls the step owes, one per unit that must make one (FlowState::battle_shock_tests, D-095).
void battle_shock_step(GameState& state);
// D-095 (M94): the battle-shock roll owed first (08.03, or 09.07 after a Desperate Escape), for its unit's player.
[[nodiscard]] std::optional<PendingDecision> battle_shock_decision(const GameState& state);
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
// One step of automatic attack work (the rolls that need no dice, a batch selected when it is the only one, damage
// that needs no roll, lost attacks (04.03.03, D-093), finishing; D-095: every roll with dice is a RollDice decision).
// Precondition: an attack is in progress and attack_decision(state) is nullopt.
void attack_progress(GameState& state, DiceSource& dice, Events& events);
// 04.03: gathers the attack batches and makes `attacker` the selected unit (01.03); the caller then
// advances. Throws std::logic_error on the begin_attacks preconditions.
void start_attacks(GameState& state, UnitId attacker, const AttackContext& context,
                   std::vector<WeaponSelection> selections, Events& events,
                   ShootingType shooting_type = ShootingType::Normal);

[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::SelectAttackBatch& d);
[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::DeclareAllocationOrder& d);
[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::OrderCasualties& d);
[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::RerollWoundRolls& d);
[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::RetargetAttacks& d);

[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::SelectAttackBatch& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side,
                                                const action::DeclareAllocationOrder& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::OrderCasualties& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::RerollWoundRolls& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::RetargetAttacks& a);

void apply_details(GameState& state, Side side, const action::SelectAttackBatch& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::DeclareAllocationOrder& a, DiceSource& dice,
                   Events& events);
void apply_details(GameState& state, Side side, const action::OrderCasualties& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::RerollWoundRolls& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::RetargetAttacks& a, DiceSource& dice, Events& events);

// ---- deployment.cpp: D-081 deployment by hand (03.02) ----

// Starts battle round 1: battle round 1, its first turn, StartOfBattleRound (D-003's start; also the end of the
// deployment step, D-081). The caller then advances.
void start_first_battle_round(GameState& state, DiceSource& dice, Events& events);
// The decision the Deploy stage needs (pure): the next side's DeployUnits, or nullopt once every unit is handled.
[[nodiscard]] std::optional<PendingDecision> deploy_decision(const GameState& state);

[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::DeployUnits& d);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::DeployUnit& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::CannotSetUp& a);
void apply_details(GameState& state, Side side, const action::DeployUnit& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::CannotSetUp& a, DiceSource& dice, Events& events);

// ---- rolls.cpp: D-095 (M94) engine-paced dice rolls ----

[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::RollDice& d);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::RollDice& a);
void apply_details(GameState& state, Side side, const action::RollDice& a, DiceSource& dice, Events& events);
// Answers every pending RollDice for its roller from `dice`, advancing after each (the roll closure): what
// apply_action does after the action itself.
void roll_through(GameState& state, DiceSource& dice, Events& events);

// The rolls, each made where the action that led to it made it before M94 (with the same dice and events):
void make_advance_roll(GameState& state, DiceSource& dice, Events& events);                  // 09.06, movement.cpp
void make_desperate_escape_hazard_rolls(GameState& state, DiceSource& dice, Events& events);  // 09.07, movement.cpp
void make_charge_roll(GameState& state, DiceSource& dice, Events& events);                   // 11.02, charge.cpp
void make_owed_battle_shock_roll(GameState& state, DiceSource& dice, Events& events);        // 01.07, command.cpp
// Random A (D-069), hit, wound, save and damage rolls, and [HAZARDOUS] hazard rolls (24.15); attack_flow.cpp.
void make_attack_roll(GameState& state, RollKind roll, DiceSource& dice, Events& events);
// The dice of the attack in progress's pending roll (RollDiceInfo::dice, ::expression, ::target); attack_flow.cpp.
void describe_attack_roll(const GameState& state, RollKind roll, RollDiceInfo& info);
// rolls.cpp: what roll `d` by `roller` rolls (roll_dice_info for a pending RollDice; D-096: window_info's next roll).
[[nodiscard]] RollDiceInfo describe_roll(const GameState& state, const decision::RollDice& d, Side roller);
// stratagems.cpp (D-096): the open AfterRoll window's roll as its event, judged from its dice now.
[[nodiscard]] event::RerolledRoll window_roll_result(const GameState& state);

// ---- stratagems.cpp: D-096 (M95) reaction windows (01.03.02) and stratagems (15) ----

// Called by each roll point once its dice are rolled and its event emitted: opens a window just after the roll if a
// side can use a stratagem there (the roll then waits in FlowState::window, `faces` parked in it), or applies the roll
// at once (as in M94). `batch`: the attack batch of an attack roll. `rerolled`: dice already re-rolled (Twin-linked's,
// 01.05.02).
void after_roll(GameState& state, RollKind roll, UnitId unit, std::size_t batch, std::vector<int> faces,
                std::vector<std::size_t> rerolled, Events& events);
// 08.03: opens the window just before the front battle-shock test's roll if Insane Bravery can be used there (15.04).
void offer_battle_shock_window(GameState& state);
// The open window's decision (the side whose turn it is in the window, 01.03.02), or nullopt.
[[nodiscard]] std::optional<PendingDecision> window_decision(const GameState& state);
// 15.01: the end of a phase ends both sides' per-phase records.
void end_phase_stratagems(GameState& state);

[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::UseStratagems& d);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::UseStratagem& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side,
                                                const action::DeclineStratagems& a);
void apply_details(GameState& state, Side side, const action::UseStratagem& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::DeclineStratagems& a, DiceSource& dice,
                   Events& events);

// Each roll module: a roll's consequence (M94's code) once its window closes, or at once; and its event rebuilt from its
// dice after a re-roll.
void apply_move_roll(GameState& state, RollKind roll, const std::vector<int>& faces);  // Advance, Hazard (09.07)
void apply_charge_roll(GameState& state, const std::vector<int>& faces, Events& events);  // 11.02, charge.cpp
[[nodiscard]] event::ChargeRolled charge_roll_result(const GameState& state, UnitId unit,
                                                     const std::vector<int>& faces);
// RandomAttacks, Hit, Wound, Save, Damage and [HAZARDOUS]'s Hazard rolls; attack_flow.cpp.
void apply_attack_roll(GameState& state, RollKind roll, std::size_t batch, const std::vector<int>& faces,
                       Events& events);
[[nodiscard]] event::RerolledRoll attack_roll_result(const GameState& state, RollKind roll, std::size_t batch,
                                                     const std::vector<int>& faces);
// 15.04: the front battle-shock test (08.03) passes automatically (01.05.01): no dice; command.cpp.
void pass_battle_shock_automatically(GameState& state, UnitId unit, Events& events);
// D-099 (M97): 15.05 Explosives at the Shoot decision (10.02): why `side` can't use it on `target`, or nullopt; and the
// units it can target, in unit order (legal_for(Shoot) lists them after the Shoot templates).
[[nodiscard]] std::optional<std::string> explosives_problem(const GameState& state, Side side, UnitId target);
[[nodiscard]] std::vector<UnitId> explosives_targets(const GameState& state, Side side);

// ---- effects.cpp: D-099 (M97) the EFFECT of 15.05 Explosives and 15.06 Crushing Impact (FlowState::effect) ----

// 15.01 step 3: the stratagem's effect starts once its CP are paid (the selection, the roll, the mortal wounds follow).
void start_effect(GameState& state, Stratagem stratagem, UnitId unit);
// The decision the effect in progress needs (pure): the selection, the roll, then the enemy unit's and the unit's
// casualties (06.02); nullopt once there is nothing left to do (effect_progress then ends it).
[[nodiscard]] std::optional<PendingDecision> effect_decision(const GameState& state);
// Ends the effect in progress (its decisions are all answered, or the 1s are not inflicted: D-099 section 6).
void effect_progress(GameState& state, Events& events);
// 15.05 / 15.06 step 3: the effect's dice (RollDice{Explosives} or RollDice{CrushingImpact}): no window follows.
void make_effect_roll(GameState& state, DiceSource& dice, Events& events);
// The effect's roll for roll_dice_info (the dice, their number and the result each needs).
void describe_effect_roll(const GameState& state, RollDiceInfo& info);
// 06.02 for the effect's mortal wounds on `target` (the enemy unit or the stratagem's unit), with its player's
// casualty priority (D-013).
void resolve_effect_casualties(GameState& state, UnitId target, const std::vector<ModelId>& priority, Events& events);

[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::SelectModelAndEnemy& d);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side,
                                                const action::SelectModelAndEnemy& a);
void apply_details(GameState& state, Side side, const action::SelectModelAndEnemy& a, DiceSource& dice,
                   Events& events);

// ---- helpers ----
// Every non-empty subset of `ids` (at most 2^10 - 1), in bit order: the target sets legal_actions lists
// for charges (11.04), pile-ins (12.03) and consolidations (12.08). Defined in fight.cpp.
[[nodiscard]] std::vector<std::vector<UnitId>> nonempty_subsets(const std::vector<UnitId>& ids);
[[nodiscard]] std::string side_label(Side side);  // "Side A"
[[nodiscard]] std::string unit_label(UnitId id);  // "unit 3"

}  // namespace wh40k::core::detail
