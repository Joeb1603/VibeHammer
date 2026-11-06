#pragma once

// M38 (D-055): the start screen the exe opens on: ways to play, a saved game, replays, and the options.

#include <filesystem>
#include <string>
#include <vector>

#include "settings.hpp"

namespace wh40k::client {

// What the player chose on the start screen this frame.
struct StartRequest {
    enum class Kind { None, Play, ContinueSaved, WatchReplay, Quit };
    Kind kind = Kind::None;
    GameMode mode = GameMode::Hotseat;  // Play
    std::string replay;                 // WatchReplay: relative to the data root
};

struct StartContext {
    std::filesystem::path data_root;  // holds data/scenarios and tests/replays
    bool save_exists = false;         // "Continue saved game" is offered
    std::string save_label;           // where the save is, for its tooltip
    std::string message;              // e.g. why the last choice failed
    std::string version;
};

class StartScreen {
public:
    // Re-reads the scenario and replay folders (when the screen is shown).
    void refresh(const std::filesystem::path& data_root);
    // Draws the screen centred in the window and edits `settings` in place.
    StartRequest draw(AppSettings& settings, const StartContext& ctx);

private:
    void draw_play(AppSettings& s, const StartContext& ctx, StartRequest& out);
    void draw_options(AppSettings& s, const StartContext& ctx);

    std::vector<std::string> scenarios_;
    std::vector<std::string> replays_;
    std::size_t replay_index_ = 0;
    ScenarioInfo info_;
    std::string info_for_;  // the scenario `info_` was read from
    int scale_percent_ = 100;
    bool scale_active_ = false;
};

}  // namespace wh40k::client
