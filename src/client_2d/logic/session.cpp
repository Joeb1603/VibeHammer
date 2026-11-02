#include "session.hpp"

#include <utility>

namespace wh40k::client {

namespace {

std::string side_name(core::Side s) { return s == core::Side::A ? "Side A" : "Side B"; }

}  // namespace

GameSession::GameSession(core::GameState initial) : initial_(initial), state_(std::move(initial)) {
    LogEntry start{"Start", {}};
    if (state_.flow.stage == core::Stage::NotStarted) {
        start.events = core::start_game(state_);
    }
    log_.push_back(std::move(start));
}

std::vector<core::GameAction> GameSession::legal() const {
    if (!state_.pending) return {};
    return core::legal_actions(state_, state_.pending->side);
}

core::Side GameSession::deciding_side() const {
    return state_.pending ? state_.pending->side : core::active_side(state_);
}

core::ValidationResult GameSession::check(const core::GameAction& action) const {
    return core::validate_action(state_, deciding_side(), action);
}

core::ValidationResult GameSession::submit(const core::GameAction& action) {
    const core::Side side = deciding_side();
    core::ValidationResult v = core::validate_action(state_, side, action);
    if (!v.ok()) return v;
    LogEntry entry;
    entry.label = "Step " + std::to_string(history_.size() + 1) + ": " + side_name(side) + " " +
                  std::string(core::action_name(action));
    entry.events = core::apply_action(state_, side, action);
    history_.push_back(core::ReplayStep{side, action});
    log_.push_back(std::move(entry));
    return v;
}

}  // namespace wh40k::client
