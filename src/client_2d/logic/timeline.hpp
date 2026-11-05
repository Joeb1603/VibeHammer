#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "core/engine/events.hpp"
#include "core/engine/replay.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

// One snapshot: the state after a replay step, and the events that step produced.
struct Frame {
    core::GameState state;
    std::vector<core::GameEvent> events;
    std::string label;  // e.g. "Step 4: Side A EndShootStep"
};

// The sequence of states of a replay, for stepping forward and back. Built only through the engine API
// (start_game / validate_action / apply_action) on the timeline's own copies.
class Timeline {
public:
    // Every step of a replay. Stops at the first invalid step and records why in error().
    static Timeline from_replay(const core::Replay& replay, const std::filesystem::path& repo_root,
                                std::string source);

    [[nodiscard]] std::size_t size() const noexcept { return frames_.size(); }
    [[nodiscard]] const Frame& frame(std::size_t i) const { return frames_.at(i); }
    [[nodiscard]] const std::string& source() const noexcept { return source_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    std::vector<Frame> frames_;
    std::string source_;
    std::string error_;
};

}  // namespace wh40k::client
