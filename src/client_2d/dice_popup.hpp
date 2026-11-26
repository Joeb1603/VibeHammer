#pragma once

// The dice roll pop-up (M20, D-033): one DiceCard at a time over the board, face-down until Roll is pressed, then the
// real faces, the result and Continue. These cards show rolls the engine has already made (a bot's, or any roll made
// with the pop-ups off), so their Roll only reveals them. D-095 (M94): a human's roll that waits to be made is shown by
// draw_prompt instead, and its Roll makes it (the caller submits the RollDice), after which its card tumbles at once.

#include <imgui.h>
#include <raylib.h>

#include <deque>
#include <optional>
#include <vector>

#include "core/engine/actions.hpp"
#include "dice_cards.hpp"
#include "stratagem_picks.hpp"
#include "watch_controls.hpp"

namespace wh40k::client {

// One die at `p` (top-left), `size` pixels square: face down, tumbling (showing `tumble_face`), or its face, green
// when it passed (M61: shared with the [TWIN-LINKED] re-roll pop-up). M95: a shown critical die has a gold border, and
// a die Command Re-roll replaced a blue "R" in its corner.
enum class DieLook { FaceDown, Tumbling, Shown };
void draw_die_face(ImDrawList* dl, ImVec2 p, float size, const DieFace& die, DieLook look = DieLook::Shown,
                   int tumble_face = 1);

// D-054 (M39): a bot's own dice, rolled and continued by themselves while the bots aren't paused (M51, D-062):
// the pause before the roll and how long the result stays up. Enter / Space, or a left click on the pop-up or the
// bare board (not on another window: M48, D-059), skips the wait.
struct DicePace {
    double roll_pause_s = 0.0;
    double dwell_s = 0.0;
};

// D-096 (M95): how a human's pre-roll card with Insane Bravery's window before it was answered.
enum class WindowPromptChoice { None, Roll, Stratagem };

class DicePopup {
public:
    void push(std::vector<DiceCard> cards);
    void clear();
    [[nodiscard]] bool active() const noexcept { return !queue_.empty(); }
    // Show the current card's faces at once (the --dice 2 screenshot flag).
    void reveal() { revealed_ = true; }
    // M45: the watch bar's Step while a card shows: reveal its faces, or close it once they show.
    void advance();
    // D-095 (M94): the current card's faces tumble at once, as if Roll had been pressed on it (the player pressed Roll
    // on the pre-roll card that made these dice). Nothing if no card shows, or it is already rolling or rolled.
    void start_rolling();

    // D-095 (M94): a human's roll before it is made (core's RollDice decision): the card face down with a Roll button
    // (Enter / Space too). Drawn while no card is queued; true when Roll is pressed, and the caller then makes the
    // roll (submits the RollDice) and calls start_rolling.
    bool draw_prompt(Rectangle area, float ui_scale, const DiceCard& card);

    // The card being shown, if any (the action bar follows its roller while dice show, D-033).
    [[nodiscard]] const DiceCard* current() const noexcept { return queue_.empty() ? nullptr : &queue_.front(); }
    // D-096: whether the card being shown is the last one queued (a window waits just after the last roll).
    [[nodiscard]] bool showing_last() const noexcept { return queue_.size() == 1; }

    // Draws the current card centred in `area` (screen pixels): Roll, then the result and Continue. With
    // `pace` (the current card is a bot's), it rolls and continues by itself and shows no Roll / Continue button,
    // only a dim line (M49, D-060: pace is given exactly when dice_buttons_shown is false).
    // D-096 (M95): with `picks` (the card is the roll a human's window waits after, is_window_card), once its faces
    // show a click picks a die and the buttons answer the window: Command Re-roll, and "Keep the roll" (Enter / Space,
    // the decline) in place of Continue. Returns that answer, for the caller to submit; the card closes with it.
    std::optional<core::GameAction> draw(Rectangle area, float ui_scale, std::optional<DicePace> pace = std::nullopt,
                                         StratagemPicks* picks = nullptr);

    // D-096: the window's pop-up when its roll's card isn't showing (the dice pop-ups off, a loaded game): `card`
    // (window_card) with its dice to pick and the same buttons. Drawn while no card is queued; returns the answer.
    std::optional<core::GameAction> draw_window(Rectangle area, float ui_scale, const DiceCard& card,
                                                StratagemPicks& picks);
    // D-096: a human's roll with a window before it (Insane Bravery's, before a battle-shock roll): its card face down
    // (window_prompt) with Roll (Enter / Space: the caller declines, then makes the roll) and the stratagem.
    WindowPromptChoice draw_window_prompt(Rectangle area, float ui_scale, const DiceCard& card,
                                          const StratagemPicks& picks);

private:
    void next();

    std::deque<DiceCard> queue_;
    int shown_ = 0;   // cards finished in this run of the queue (for "Roll 2 of 5")
    bool revealed_ = false;
    std::optional<double> rolling_since_;  // the tumble after Roll is pressed
    std::optional<double> card_since_;     // when the current card was first drawn (D-054 pacing)
    std::optional<double> revealed_at_;    // when its result was first shown
    bool focus_pending_ = false;           // M48: take the focus once, when a card first appears
    bool prompt_shown_ = false;            // D-095: a pre-roll card is up (it takes the focus once)
    bool prompt_focus_ = false;
    bool window_shown_ = false;            // D-096: a window's pop-up is up (it takes the focus once)
    bool window_focus_ = false;
};

}  // namespace wh40k::client
