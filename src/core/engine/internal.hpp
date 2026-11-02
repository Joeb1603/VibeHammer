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
[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::Shoot& d);
[[nodiscard]] std::vector<GameAction> legal_for(const GameState& state, const decision::Charge& d);

[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::StartMove& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::EndShootStep& a);
[[nodiscard]] ValidationResult validate_details(const GameState& state, Side side, const action::EndChargeStep& a);

void apply_details(GameState& state, Side side, const action::StartMove& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::EndShootStep& a, DiceSource& dice, Events& events);
void apply_details(GameState& state, Side side, const action::EndChargeStep& a, DiceSource& dice, Events& events);

// ---- attack_flow.cpp: 04.03 / 05 as a sub-machine ----

// The decision the attack in progress needs (pure), or nullopt if it can progress automatically.
[[nodiscard]] std::optional<PendingDecision> attack_decision(const GameState& state);
// One step of automatic attack work (rolls, damage, D-012, finishing). Precondition: an attack is in
// progress and attack_decision(state) is nullopt.
void attack_progress(GameState& state, DiceSource& dice, Events& events);

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
