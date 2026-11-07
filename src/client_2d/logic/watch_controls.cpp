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

bool WatchControls::step_closes_popup(ShownCard card) {
    if (!step_ || card == ShownCard::None) return false;
    step_ = false;
    return card == ShownCard::Bots;  // M48 (D-059): a human's card ignores the step
}

bool click_skips_bot_dice_wait(bool clicked, DiceClickTarget target) noexcept {
    return clicked && (target == DiceClickTarget::BareBoard || target == DiceClickTarget::PopUp);
}

bool bot_dice_paced(bool bot_card, bool paused) noexcept {
    return bot_card && !paused;
}

bool dice_buttons_shown(bool bot_card, bool paused) noexcept {
    return !bot_dice_paced(bot_card, paused);
}

std::string watch_status(const WatchControls& controls, BotSpeed speed) {
    if (controls.paused()) return "Paused";
    return std::string("Playing at ") + bot_speed_name(speed) + " speed";
}

}  // namespace wh40k::client
