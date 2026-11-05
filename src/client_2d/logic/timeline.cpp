#include "timeline.hpp"

#include <utility>

#include "core/engine/engine.hpp"

namespace wh40k::client {

namespace {

Frame first_frame(core::GameState state) {
    Frame f;
    if (state.flow.stage == core::Stage::NotStarted) {
        f.events = core::start_game(state);
    }
    f.state = std::move(state);
    f.label = "Start";
    return f;
}

std::string side_name(core::Side s) { return s == core::Side::A ? "Side A" : "Side B"; }

}  // namespace

Timeline Timeline::from_replay(const core::Replay& replay, const std::filesystem::path& repo_root,
                               std::string source) {
    Timeline t;
    t.source_ = std::move(source);
    t.frames_.push_back(first_frame(core::replay_initial_state(replay, repo_root)));
    for (std::size_t i = 0; i < replay.steps.size(); ++i) {
        const core::ReplayStep& step = replay.steps[i];
        Frame next;
        next.state = t.frames_.back().state;
        const core::ValidationResult v = core::validate_action(next.state, step.side, step.action);
        if (!v.ok()) {
            t.error_ = "Step " + std::to_string(i + 1) + " is invalid: " + v.reasons.front();
            break;
        }
        next.events = core::apply_action(next.state, step.side, step.action);
        next.label = "Step " + std::to_string(i + 1) + ": " + side_name(step.side) + " " +
                     std::string(core::action_name(step.action));
        t.frames_.push_back(std::move(next));
    }
    return t;
}

}  // namespace wh40k::client
