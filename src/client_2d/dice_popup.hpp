#pragma once

// The dice roll pop-up (M20, D-033): one DiceCard at a time over the board, face-down until the player
// presses Roll, then the real faces (already rolled by the engine), the result and Continue. Display only.

#include <raylib.h>

#include <deque>
#include <optional>
#include <vector>

#include "dice_cards.hpp"
#include "watch_controls.hpp"

namespace wh40k::client {

// D-054 (M39): a bot's own dice, rolled and continued by themselves while the bots aren't paused (M51, D-062):
// the pause before the roll and how long the result stays up. Enter / Space, or a left click on the pop-up or the
// bare board (not on another window: M48, D-059), skips the wait.
struct DicePace {
    double roll_pause_s = 0.0;
    double dwell_s = 0.0;
};

class DicePopup {
public:
    void push(std::vector<DiceCard> cards);
    void clear();
    [[nodiscard]] bool active() const noexcept { return !queue_.empty(); }
    // Show the current card's faces at once (the --dice 2 screenshot flag).
    void reveal() { revealed_ = true; }
    // M45: the watch bar's Step while a card shows: reveal its faces, or close it once they show.
    void advance();

    // The card being shown, if any (the action bar follows its roller while dice show, D-033).
    [[nodiscard]] const DiceCard* current() const noexcept { return queue_.empty() ? nullptr : &queue_.front(); }

    // Draws the current card centred in `area` (screen pixels): Roll, then the result and Continue. With
    // `pace` (the current card is a bot's), it rolls and continues by itself and shows no Roll / Continue button,
    // only a dim line (M49, D-060: pace is given exactly when dice_buttons_shown is false).
    void draw(Rectangle area, float ui_scale, std::optional<DicePace> pace = std::nullopt);

private:
    void next();

    std::deque<DiceCard> queue_;
    int shown_ = 0;   // cards finished in this run of the queue (for "Roll 2 of 5")
    bool revealed_ = false;
    std::optional<double> rolling_since_;  // the tumble after Roll is pressed
    std::optional<double> card_since_;     // when the current card was first drawn (D-054 pacing)
    std::optional<double> revealed_at_;    // when its result was first shown
    bool focus_pending_ = false;           // M48: take the focus once, when a card first appears
};

}  // namespace wh40k::client
