#pragma once

// D-096 (M95): a human's answer to a reaction window, picked in the client before it is submitted: the die Command
// Re-roll re-rolls (15.02; none to pick for a charge roll, re-rolled in full), or Insane Bravery (15.04). Built from
// core's window_info, so the client works none of it out (D-024). Board-free and raylib-free: the rolled card and the
// pop-up draw these dice and pick them; the action bar offers the answers. Client state only.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/engine/engine.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

// D-097 (M96): one answer to a window that follows no roll (15.08, 15.11, 15.12): a stratagem, its mode and the friendly
// unit it targets, with its button label ("Fire Overwatch (1 CP): Termagants", "Into the Fray (2 CP): Hive Tyrant").
struct ReactionChoice {
    core::GameAction action;
    std::string label;
    core::UnitId unit;
    friend bool operator==(const ReactionChoice&, const ReactionChoice&) = default;
};

class StratagemPicks {
public:
    StratagemPicks() = default;
    // The pending window's dice and stratagems, with no die picked (the only pickable die, when there is one).
    // Inactive when no window is pending.
    explicit StratagemPicks(const core::GameState& state);

    [[nodiscard]] bool active() const noexcept { return info_.has_value(); }
    // The window (only while active).
    [[nodiscard]] const core::WindowInfo& info() const { return info_.value(); }
    [[nodiscard]] bool offers(core::Stratagem s) const;
    [[nodiscard]] int cost(core::Stratagem s) const;  // its CP (0 when not offered)

    // Command Re-roll's die: a click picks a pickable die, or drops it again; others are ignored.
    void pick(std::size_t die);
    [[nodiscard]] std::optional<std::size_t> picked() const noexcept { return picked_; }
    [[nodiscard]] bool pickable(std::size_t die) const;
    // Whether Command Re-roll can be submitted now: it is offered, and a die is picked (a charge roll needs none).
    [[nodiscard]] bool reroll_ready() const;

    // D-097 (M96): the window follows no roll (Fire Overwatch, Heroic Intervention, Counteroffensive), and its answers
    // other than the decline, in the engine's order (by stratagem, then mode, then unit).
    [[nodiscard]] bool reaction_window() const noexcept { return info_ && !info_->roll; }
    [[nodiscard]] std::vector<ReactionChoice> reactions(const core::GameState& state) const;

    // The answers: decline (the window's first answer, D-096), Command Re-roll of the picked die, Insane Bravery.
    [[nodiscard]] static core::GameAction decline() { return core::action::DeclineStratagems{}; }
    [[nodiscard]] std::optional<core::GameAction> command_reroll() const;
    [[nodiscard]] std::optional<core::GameAction> insane_bravery() const;

    // Button labels: "Command Re-roll (1 CP)", "Insane Bravery (1 CP)", and the decline's ("Keep the roll" after a
    // roll, "Roll normally" before one, "No stratagem" for a window that follows no roll). The hint says what each
    // stratagem does.
    [[nodiscard]] std::string reroll_label() const;
    [[nodiscard]] std::string insane_bravery_label() const;
    [[nodiscard]] std::string decline_label() const;
    // What the window is about, for the pop-up and the action bar: "Command Re-roll? (15.02)".
    [[nodiscard]] std::string headline() const;
    // One line on what to do: the CP, and "click a die to pick it" for a die to pick.
    [[nodiscard]] std::string hint() const;

private:
    std::optional<core::WindowInfo> info_;
    std::optional<std::size_t> picked_;
};

}  // namespace wh40k::client
