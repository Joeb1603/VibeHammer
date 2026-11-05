#pragma once

// Click-on-the-board interaction (M16, D-029): for the pending decision, which units can be clicked, what
// clicking one does (submit an action, or offer a menu of them), and the step-level actions that stay as
// sidebar buttons. Everything offered is one of legal_actions (D-024): the board only groups the legal
// actions by unit. No rendering; the client draws the result and submits through GameSession.

#include <optional>
#include <string>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"
#include "shot_plan.hpp"

namespace wh40k::client {

struct MenuEntry {
    std::string label;
    core::GameAction action;
};

// What a click led to: an action to submit now, a menu to choose from, or neither (the selection changed).
struct ClickResult {
    std::optional<core::GameAction> submit;
    std::vector<MenuEntry> menu;
};

class BoardInteraction {
public:
    BoardInteraction() = default;  // inactive (e.g. replay mode)
    BoardInteraction(const core::GameState& state, std::vector<core::GameAction> legal);

    // Whether the board handles this decision (otherwise the sidebar works as before and nothing is grey).
    [[nodiscard]] bool active() const noexcept { return mode_ != Mode::Inactive; }
    [[nodiscard]] std::optional<core::UnitId> actor() const noexcept { return actor_; }
    [[nodiscard]] const std::vector<core::UnitId>& chosen_targets() const noexcept { return targets_; }
    [[nodiscard]] bool clickable(core::UnitId unit) const;
    [[nodiscard]] std::string hint(const core::GameState& state) const;

    // A click on one of `unit`'s models. Units that aren't clickable do nothing.
    ClickResult click(const core::GameState& state, core::UnitId unit);
    // A menu entry was picked (a Shoot template selects that shooter; anything else is submitted).
    ClickResult choose(const core::GameState& state, const core::GameAction& action);
    // Esc or a click on empty board: forget the selection (a fixed actor stays).
    void cancel();

    // The legal actions that don't belong to a unit on the board: End shooting, Pass, Commit move, ...
    [[nodiscard]] std::vector<core::GameAction> step_buttons() const;
    // In target selection: the option whose targets are exactly the chosen ones, if any.
    [[nodiscard]] std::optional<core::GameAction> confirm() const;
    // While choosing a target to shoot: the shooter's plan (the sidebar edits per-model overrides on it).
    [[nodiscard]] ShotPlan* shot_plan() { return shot_ ? &*shot_ : nullptr; }

private:
    enum class Mode {
        Inactive,   // the sidebar handles the decision
        PickActor,  // click a friendly unit; its actions are submitted or offered as a menu
        Shoot,      // the shooter is picked: click a target, then a weapon from the menu
        Targets,    // the actor is picked: click enemy units to choose targets (charge, pile-in, consolidate)
        Moving,     // MoveModels: the moving unit is dragged on the board
    };

    [[nodiscard]] std::vector<core::GameAction> actions_of(core::UnitId unit) const;
    [[nodiscard]] std::vector<core::UnitId> target_options() const;
    ClickResult pick_actor(const core::GameState& state, core::UnitId unit);
    ClickResult start_targets(core::UnitId unit);

    Mode mode_ = Mode::Inactive;
    Mode base_mode_ = Mode::Inactive;  // what cancel() returns to
    std::vector<core::GameAction> legal_;
    std::optional<core::UnitId> actor_;
    bool actor_fixed_ = false;  // given by the decision (charge targets, overrun pile-in)
    std::vector<core::UnitId> targets_;
    std::optional<ShotPlan> shot_;
    std::string verb_;         // picking a unit: "move", "shoot with", ... (for the hint)
    std::string target_verb_;  // picking targets: "charge", "pile in towards", ...
};

// The unit an action is for, if it is one the board picks by clicking that unit.
[[nodiscard]] std::optional<core::UnitId> board_unit(const core::GameAction& action);

}  // namespace wh40k::client
