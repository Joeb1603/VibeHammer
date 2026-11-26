#include "timeline.hpp"

#include <utility>

#include "core/data/load_error.hpp"
#include "core/engine/engine.hpp"

namespace wh40k::client {

namespace {

// D-095 (M94): a format-2 replay records each roll as a step (the engine paced); format 1 makes them inside the steps.
Frame first_frame(core::GameState state, bool paced) {
    Frame f;
    if (state.flow.stage == core::Stage::NotStarted) {
        f.events = paced ? core::start_game_paced(state) : core::start_game(state);
    }
    f.state = std::move(state);
    f.label = "Start";
    return f;
}

std::string side_name(core::Side s) { return s == core::Side::A ? "Side A" : "Side B"; }

}  // namespace

Timeline Timeline::from_replay(const core::Replay& given, const std::filesystem::path& repo_root,
                               std::string source) {
    Timeline t;
    t.source_ = std::move(source);
    // D-096: a format-2 replay is shown as its format-5 form (its windows declined, its damage rolled per attack), and
    // D-097: a format-3 one too (the windows that follow no roll declined), and D-099: a format-4 one (the windows just
    // after a charge move declined); format 1 still plays inline.
    core::Replay replay = given;
    if (replay.format == core::kReplayFormatRollsRecorded || replay.format == core::kReplayFormatWindowsRecorded ||
        replay.format == core::kReplayFormatReactionsRecorded) {
        try {
            replay = core::with_roll_steps(given, repo_root);
        } catch (const core::DataError& e) {
            t.error_ = e.what();
            t.frames_.push_back(first_frame(core::replay_initial_state(given, repo_root), true));
            return t;
        }
    }
    const bool paced = replay.format == core::kReplayFormatOwnTurnStratagems;
    t.frames_.push_back(first_frame(core::replay_initial_state(replay, repo_root), paced));
    for (std::size_t i = 0; i < replay.steps.size(); ++i) {
        const core::ReplayStep& step = replay.steps[i];
        Frame next;
        next.state = t.frames_.back().state;
        const core::ValidationResult v = core::validate_action(next.state, step.side, step.action);
        if (!v.ok()) {
            t.error_ = "Step " + std::to_string(i + 1) + " is invalid: " + v.reasons.front();
            break;
        }
        next.events = paced ? core::apply_action_paced(next.state, step.side, step.action)
                            : core::apply_action(next.state, step.side, step.action);
        next.label = "Step " + std::to_string(i + 1) + ": " + side_name(step.side) + " " +
                     std::string(core::action_name(step.action));
        t.frames_.push_back(std::move(next));
    }
    return t;
}

}  // namespace wh40k::client
