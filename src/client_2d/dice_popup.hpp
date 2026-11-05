#pragma once

// The dice roll pop-up (M20, D-033): one DiceCard at a time over the board, face-down until the player
// presses Roll, then the real faces (already rolled by the engine), the result and Continue. Display only.

#include <raylib.h>

#include <deque>
#include <optional>
#include <vector>

#include "dice_cards.hpp"

namespace wh40k::client {

class DicePopup {
public:
    void push(std::vector<DiceCard> cards);
    void clear();
    [[nodiscard]] bool active() const noexcept { return !queue_.empty(); }
    // Show the current card's faces at once (the --dice 2 screenshot flag).
    void reveal() { revealed_ = true; }

    // The card being shown, if any (the action bar follows its roller while dice show, D-033).
    [[nodiscard]] const DiceCard* current() const noexcept { return queue_.empty() ? nullptr : &queue_.front(); }

    // Draws the current card centred in `area` (screen pixels): Roll, then the result and Continue.
    void draw(Rectangle area, float ui_scale);

private:
    void next();

    std::deque<DiceCard> queue_;
    int shown_ = 0;   // cards finished in this run of the queue (for "Roll 2 of 5")
    bool revealed_ = false;
    std::optional<double> rolling_since_;  // the tumble after Roll is pressed
};

}  // namespace wh40k::client
