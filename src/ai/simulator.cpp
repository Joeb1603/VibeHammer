#include "simulator.hpp"

#include <exception>
#include <string>
#include <utility>

#include "core/engine/engine.hpp"

namespace wh40k::ai {

Simulator::Simulator(core::GameState state, std::uint64_t dice_seed)
    : state_(std::move(state)), dice_{dice_seed, 0} {}

core::ValidationResult Simulator::step(core::Side side, const core::GameAction& action,
                                       std::vector<core::GameEvent>* events) {
    core::SeededDice dice(dice_);  // D-054: the bot's own dice, never SeededDice(state_.dice)
    return step(side, action, dice, events);
}

core::ValidationResult Simulator::step(core::Side side, const core::GameAction& action, core::DiceSource& dice,
                                       std::vector<core::GameEvent>* events) {
    core::ValidationResult v = core::validate_action(state_, side, action);
    if (!v.ok()) return v;
    try {
        std::vector<core::GameEvent> out = core::apply_action(state_, side, action, dice);
        ++steps_;
        if (events != nullptr) events->insert(events->end(), out.begin(), out.end());
    } catch (const std::exception& e) {
        return core::ValidationResult::failure(std::string("engine error in the bot's simulation: ") + e.what());
    }
    return v;
}

core::ValidationResult Simulator::step(const core::GameAction& action, std::vector<core::GameEvent>* events) {
    if (!state_.pending) return core::ValidationResult::failure("D-054: nothing is pending in the simulation");
    return step(state_.pending->side, action, events);
}

}  // namespace wh40k::ai
