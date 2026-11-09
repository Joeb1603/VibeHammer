#pragma once

// The engine API: the only way in or out of the rules (CLAUDE.md, Engine API).
// GameState is a plain value; these are free functions. Every action answers state.pending.

#include <string>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/engine/actions.hpp"
#include "core/engine/events.hpp"
#include "core/engine/validation.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::core {

// Starts the first battle round (D-003) and advances to the first decision. D-081 (M74): when units wait to be set up
// (GameState::undeployed, a deployment by hand), the deployment step comes first (Stage::Deploy) and battle round 1
// starts after the last unit is handled.
// Precondition: state.flow.stage == Stage::NotStarted (std::logic_error otherwise).
std::vector<GameEvent> start_game(GameState& state);
std::vector<GameEvent> start_game(GameState& state, DiceSource& dice);

// Every action `side` may take now. Empty unless `side` owns the pending decision. Discrete
// decisions list every legal action; continuous/ordering decisions return templates (D-004, D-013;
// see is_template).
[[nodiscard]] std::vector<GameAction> legal_actions(const GameState& state, Side side);

// Whether `side` may take `action` now; reasons cite rules.
[[nodiscard]] ValidationResult validate_action(const GameState& state, Side side, const GameAction& action);

// Applies a validated action (std::logic_error if it is not valid), then advances the game to the
// next decision. Returns everything that happened. The first overload rolls dice with
// SeededDice(state.dice); the second lets tests script them.
std::vector<GameEvent> apply_action(GameState& state, Side side, const GameAction& action);
std::vector<GameEvent> apply_action(GameState& state, Side side, const GameAction& action, DiceSource& dice);
// M54: the same as the DiceSource overload, with the copy kept for the rollback (D-026) made in `backup` by
// copy assignment, so a caller applying many actions (the bot's simulator) reuses its allocations instead
// of copying the whole state each time. `backup` is scratch: afterwards it holds no particular state.
std::vector<GameEvent> apply_action(GameState& state, Side side, const GameAction& action, DiceSource& dice,
                                    GameState& backup);

// 01.03: the active player. Between turns (and during the deployment step, D-081), the player who takes the first
// turn; while a unit is selected to move, shoot or fight, that unit's controlling player; otherwise the player whose
// turn it is. Not the same as state.pending->side (e.g. 05.03/05.04 belong to the opposing player).
[[nodiscard]] Side active_side(const GameState& state);

// The player whose turn it is (07.02). Meaningful once the game has started.
[[nodiscard]] Side turn_side(const GameState& state);

// What the engine would wait for in this state; after every apply_action this equals
// state.pending (an invariant the tests check).
[[nodiscard]] std::optional<PendingDecision> compute_decision(const GameState& state);

}  // namespace wh40k::core
