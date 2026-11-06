#pragma once

// D-054: the bot's scratch game. A bare GameState copy (never a GameSession, for speed) stepped through the
// engine with the bot's own dice, so a simulation never uses (or advances) the game's real rolls. The
// matching .cpp is the only ai file that applies actions; every other ai file only reads states.

#include <cstdint>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/engine/actions.hpp"
#include "core/engine/events.hpp"
#include "core/engine/validation.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai {

class Simulator {
public:
    // `dice_seed` seeds the bot's own dice (normally from state_hash, so it is a function of the state alone).
    Simulator(core::GameState state, std::uint64_t dice_seed);

    [[nodiscard]] const core::GameState& state() const noexcept { return state_; }

    // validate_action for `side`, then (if valid) the engine step with the bot's dice. The events of an
    // accepted step are appended to `events` when given. Nothing changes when the action is invalid.
    core::ValidationResult step(core::Side side, const core::GameAction& action,
                                std::vector<core::GameEvent>* events = nullptr);
    // The same with dice the caller supplies (M40: a ScriptedDice face or 2D6 pair, to enumerate a roll's
    // outcomes exactly). The bot's own dice are not advanced.
    core::ValidationResult step(core::Side side, const core::GameAction& action, core::DiceSource& dice,
                                std::vector<core::GameEvent>* events = nullptr);
    // The same for the side that owns the pending decision (a failure when nothing is pending).
    core::ValidationResult step(const core::GameAction& action, std::vector<core::GameEvent>* events = nullptr);

    // Simulated actions so far (the work budget counts these, never wall-clock time; D-054).
    [[nodiscard]] std::size_t steps() const noexcept { return steps_; }

private:
    core::GameState state_;
    core::DiceState dice_;  // the bot's own dice, not state_.dice
    std::size_t steps_ = 0;
};

}  // namespace wh40k::ai
