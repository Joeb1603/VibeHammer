#pragma once

// M45 (D-054): the watch controls for a game with a bot (most of all Watch bot vs bot): Pause / Resume, Step
// (one action) and the bot speed, changed live. Presentation only, client state only (never in GameState).
// raylib-free: the client maps its buttons and keys to WatchCommands and passes the result to the BotDriver
// (DriverInputs::paused / step_once); tested in client tests M45_Watch.

#include <string>

#include "settings.hpp"

namespace wh40k::client {

enum class WatchCommand { TogglePause, Step, Faster, Slower };

// One speed faster / slower (Slow, Normal, Fast, Instant), stopping at the ends.
[[nodiscard]] BotSpeed faster(BotSpeed speed);
[[nodiscard]] BotSpeed slower(BotSpeed speed);

class WatchControls {
public:
    // Applies a button or key; Faster / Slower change `speed` (the settings' bot speed, read by the driver).
    void apply(WatchCommand command, BotSpeed& speed);

    [[nodiscard]] bool paused() const noexcept { return paused_; }
    // Step was pressed and is waiting for the bot's next action (DriverInputs::step_once).
    [[nodiscard]] bool step_requested() const noexcept { return step_; }

    // Once a frame, before the driver's tick: a Step pressed while a dice pop-up shows closes that card
    // instead (true: the caller advances the pop-up), and is used up. Otherwise the step waits for the bot.
    [[nodiscard]] bool step_closes_popup(bool popup_open);
    // The driver handed an action over: a Step is used up.
    void delivered() noexcept { step_ = false; }

    // The bot's own dice roll and continue by themselves when the setting is on and the game isn't paused
    // (paused, they wait for a click or Step like a human's).
    [[nodiscard]] bool bot_dice_run(bool auto_dice_setting) const noexcept { return auto_dice_setting && !paused_; }

    // A new game, a load or the start screen: playing, no step waiting.
    void reset() noexcept {
        paused_ = false;
        step_ = false;
    }

private:
    bool paused_ = false;
    bool step_ = false;
};

// The watch bar's status: "Paused" or "Playing at <speed> speed".
[[nodiscard]] std::string watch_status(const WatchControls& controls, BotSpeed speed);

}  // namespace wh40k::client
