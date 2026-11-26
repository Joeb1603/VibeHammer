#pragma once

// The engine API: the only way in or out of the rules (CLAUDE.md, Engine API).
// GameState is a plain value; these are free functions. Every action answers state.pending.

#include <optional>
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
// starts after the last unit is handled. D-095 (M94): every dice roll on the way is made at once (see apply_action);
// start_game_paced stops at the first roll instead.
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
// D-095 (M94): the engine stops at every roll point with a RollDice decision; apply_action answers each one at once,
// for its roller (the decision's side), from the same dice and inside the same rollback, so the next decision is never
// a RollDice and every roll is made where the action made it before M94 (the roll closure). apply_action_paced stops at
// each RollDice instead.
std::vector<GameEvent> apply_action(GameState& state, Side side, const GameAction& action);
std::vector<GameEvent> apply_action(GameState& state, Side side, const GameAction& action, DiceSource& dice);
// M54: the same as the DiceSource overload, with the copy kept for the rollback (D-026) made in `backup` by
// copy assignment, so a caller applying many actions (the bot's simulator) reuses its allocations instead
// of copying the whole state each time. `backup` is scratch: afterwards it holds no particular state.
std::vector<GameEvent> apply_action(GameState& state, Side side, const GameAction& action, DiceSource& dice,
                                    GameState& backup);

// ---- D-095 (M94): engine-paced dice rolls ----
// The same as start_game / apply_action, but stopping at the next decision even when it is a RollDice, so a player can
// make each roll (the client's Roll button, GameSession; format-2 replays record those answers). Answering every
// RollDice that follows, each from the same dice, gives exactly what start_game / apply_action give.
std::vector<GameEvent> start_game_paced(GameState& state);
std::vector<GameEvent> start_game_paced(GameState& state, DiceSource& dice);
std::vector<GameEvent> apply_action_paced(GameState& state, Side side, const GameAction& action);
std::vector<GameEvent> apply_action_paced(GameState& state, Side side, const GameAction& action, DiceSource& dice);
std::vector<GameEvent> apply_action_paced(GameState& state, Side side, const GameAction& action, DiceSource& dice,
                                          GameState& backup);

// Whether the pending decision is a RollDice.
[[nodiscard]] bool roll_pending(const GameState& state);

// What the pending RollDice rolls, for showing it before it is made (the client's pre-roll card, D-024: the client
// works none of it out itself). nullopt unless a RollDice is pending.
struct RollDiceInfo {
    RollKind roll = RollKind::Hit;
    UnitId unit;                   // the unit the roll is for (decision::RollDice::unit)
    Side roller = Side::A;         // its controlling player, who makes the roll
    std::optional<UnitId> target;  // an attack's target: random A, hit, wound, save and damage rolls
    // How many dice are rolled. D-096: a damage roll is one attack's D (D-069).
    int dice = 0;
    // D-096: a damage roll's model (05.04 Select Model: the one the casualty order selects for the attack).
    std::optional<ModelId> model;
    std::string expression;  // each roll: "D6", "2D6", or a random characteristic such as "D3+1" (random A, damage)
    // The result each die (or the 2D6 of a battle-shock roll) needs, where one number says it: a hit roll's BS/WS
    // (05.01; none for [TORRENT]), a wound roll's required result (05.02), the unit's Leadership (01.06).
    std::optional<int> need;
    friend bool operator==(const RollDiceInfo&, const RollDiceInfo&) = default;
};
[[nodiscard]] std::optional<RollDiceInfo> roll_dice_info(const GameState& state);

// D-096 (M95): whether the pending decision is a reaction window (UseStratagems).
[[nodiscard]] bool window_pending(const GameState& state);

// D-096: a stratagem the deciding side can use in the open window.
struct StratagemOption {
    Stratagem stratagem = Stratagem::CommandReroll;
    std::string name;  // "Command Re-roll"
    std::string rule;  // "15.02"
    int cost = 0;      // CP (15.01)
    friend bool operator==(const StratagemOption&, const StratagemOption&) = default;
};

// D-096: what the open window offers, for the client and the bot (D-024: they work none of it out themselves). nullopt
// unless a window is pending.
struct WindowInfo {
    WindowTiming timing = WindowTiming::AfterRoll;
    RollKind roll = RollKind::Hit;  // the roll the window comes after (or before)
    UnitId unit;                    // the unit the roll is for
    Side side = Side::A;            // the deciding side
    int command_points = 0;         // its CP
    std::vector<StratagemOption> options;  // what it can use here (none: it can only decline)
    std::vector<int> faces;                // the roll's dice (AfterRoll)
    std::vector<bool> pickable;            // per die: Command Re-roll can pick it (01.05.02: not re-rolled before)
    bool whole_roll = false;               // a charge roll: Command Re-roll re-rolls it in full, no die is picked
    friend bool operator==(const WindowInfo&, const WindowInfo&) = default;
};
[[nodiscard]] std::optional<WindowInfo> window_info(const GameState& state);

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
