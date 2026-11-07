#pragma once

// M45 (D-054): the watch controls for a game with a bot (most of all Watch bot vs bot): Pause / Resume, Step
// (one action) and the bot speed, changed live. Presentation only, client state only (never in GameState).
// raylib-free: the client maps its buttons and keys to WatchCommands and passes the result to the BotDriver
// (DriverInputs::paused / step_once); tested in client tests M45_Watch and M48_Watch.
// M48 (D-059): Pause is the sidebar's "Pause the bots (P)" checkbox (set_paused) and key P (TogglePause); the
// bar above the board keeps Step and the bot speed. Pause is never saved (not in AppSettings).

#include <string>

#include "settings.hpp"

namespace wh40k::client {

enum class WatchCommand { TogglePause, Step, Faster, Slower };

// M48 (D-059): the dice card showing, if any, and whose it is (Step and key N only touch a bot's card).
enum class ShownCard { None, Bots, Humans };

// M48 (D-059): where a left click landed while a bot's paced dice card shows. Only a click on the pop-up itself
// (not on its Roll / Continue button, which acts on its own) or on the bare board skips the card's wait; a click
// on the sidebar, the action bar, a datasheet or any other window doesn't (so Step doesn't also skip).
enum class DiceClickTarget { BareBoard, PopUp, PopUpButton, OtherWindow };
[[nodiscard]] bool click_skips_bot_dice_wait(bool clicked, DiceClickTarget target) noexcept;

// One speed faster / slower (Slow, Normal, Fast, Instant), stopping at the ends.
[[nodiscard]] BotSpeed faster(BotSpeed speed);
[[nodiscard]] BotSpeed slower(BotSpeed speed);

class WatchControls {
public:
    // Applies a button or key; Faster / Slower change `speed` (the settings' bot speed, read by the driver).
    void apply(WatchCommand command, BotSpeed& speed);

    [[nodiscard]] bool paused() const noexcept { return paused_; }
    // M48: the sidebar's "Pause the bots (P)" checkbox.
    void set_paused(bool paused) noexcept { paused_ = paused; }
    // Step was pressed and is waiting for the bot's next action (DriverInputs::step_once).
    [[nodiscard]] bool step_requested() const noexcept { return step_; }

    // Once a frame, before the driver's tick: a Step pressed while a bot's dice card shows reveals or closes
    // that card instead (true: the caller advances the pop-up), and is used up. M48: a Step (key N) while a
    // human's card shows is dropped and leaves the card alone (the human rolls their own dice). With no card,
    // the step waits for the bot.
    [[nodiscard]] bool step_closes_popup(ShownCard card);
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
