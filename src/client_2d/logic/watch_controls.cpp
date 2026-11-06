#include "watch_controls.hpp"

namespace wh40k::client {

BotSpeed faster(BotSpeed speed) {
    switch (speed) {
        case BotSpeed::Slow: return BotSpeed::Normal;
        case BotSpeed::Normal: return BotSpeed::Fast;
        case BotSpeed::Fast:
        case BotSpeed::Instant: return BotSpeed::Instant;
    }
    return speed;
}

BotSpeed slower(BotSpeed speed) {
    switch (speed) {
        case BotSpeed::Instant: return BotSpeed::Fast;
        case BotSpeed::Fast: return BotSpeed::Normal;
        case BotSpeed::Normal:
        case BotSpeed::Slow: return BotSpeed::Slow;
    }
    return speed;
}

void WatchControls::apply(WatchCommand command, BotSpeed& speed) {
    switch (command) {
        case WatchCommand::TogglePause: paused_ = !paused_; break;
        case WatchCommand::Step: step_ = true; break;
        case WatchCommand::Faster: speed = faster(speed); break;
        case WatchCommand::Slower: speed = slower(speed); break;
    }
}

bool WatchControls::step_closes_popup(bool popup_open) {
    if (!step_ || !popup_open) return false;
    step_ = false;
    return true;
}

std::string watch_status(const WatchControls& controls, BotSpeed speed) {
    if (controls.paused()) return "Paused";
    return std::string("Playing at ") + bot_speed_name(speed) + " speed";
}

}  // namespace wh40k::client
