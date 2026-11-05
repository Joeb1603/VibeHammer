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
    // The target menu's "Custom..." entry (D-031): opens the per-model shot editor instead of submitting;
    // `action` is the Shoot it starts from (the first weapon choice at that target).
    bool custom = false;
};

// The panel's "End ... phase" button: the actions to submit, in order (D-029).
struct EndPhase {
    std::string label;
    std::vector<core::GameAction> actions;
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
    // Whether the board is waiting for a click now: only then is what can't be clicked drawn grey.
    [[nodiscard]] bool waiting_for_click() const;
    // Whether `unit` is drawn grey: while waiting for a click, every unit that can't be clicked; when no
    // unit is left to act (only the end-phase button), every unit (D-029).
    [[nodiscard]] bool greyed(core::UnitId unit) const;
    // Picking targets (shooting, charge, pile-in): a line follows the cursor from the actor.
    [[nodiscard]] bool picking_target() const noexcept { return mode_ == Mode::Shoot || mode_ == Mode::Targets; }
    [[nodiscard]] std::string hint(const core::GameState& state) const;
    // The panel's headline while picking a target ("Pick a target for the Intercessor Squad"), else empty.
    [[nodiscard]] std::string prompt(const core::GameState& state) const;

    // A click on one of `unit`'s models. Units that aren't clickable do nothing.
    ClickResult click(const core::GameState& state, core::UnitId unit);
    // A menu entry was picked (a Shoot template selects that shooter; anything else is submitted).
    ClickResult choose(const core::GameState& state, const core::GameAction& action);
    // The same for a whole entry: "Custom..." opens the custom-shot editor (D-031).
    ClickResult choose(const core::GameState& state, const MenuEntry& entry);
    // Esc or a click on empty board: forget the selection (a fixed actor stays). In the custom-shot editor:
    // back to picking a target for the same shooter.
    void cancel();
    // Editing a custom shot (D-031): each model's weapon and target are set in the panel, on shot_plan().
    // A click on empty board doesn't cancel it.
    [[nodiscard]] bool custom_shot() const noexcept { return mode_ == Mode::CustomShot; }

    // Ending the phase (or step): every remaining unit remains stationary, End Shooting phase, Pass, ...
    [[nodiscard]] std::optional<EndPhase> end_phase() const;
    // The other legal actions that don't belong to a unit on the board: Commit move, Make no charge move, ...
    [[nodiscard]] std::vector<core::GameAction> step_buttons() const;
    // In target selection: the option whose targets are exactly the chosen ones, if any.
    [[nodiscard]] std::optional<core::GameAction> confirm() const;
    // While shooting: the shooter's plan (in the custom-shot editor the panel edits per-model choices on it).
    [[nodiscard]] ShotPlan* shot_plan() {
        if (mode_ == Mode::CustomShot) return custom_ ? &*custom_ : nullptr;
        return shot_ ? &*shot_ : nullptr;
    }

private:
    enum class Mode {
        Inactive,   // the sidebar handles the decision
        PickActor,  // click a friendly unit; its actions are submitted or offered as a menu
        Shoot,      // the shooter is picked: click a target, then a weapon from the menu
        CustomShot, // "Custom..." was picked on a target: each model's choice is edited in the panel (D-031)
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
    std::optional<ShotPlan> custom_;             // CustomShot: the plan being edited (a copy of shot_)
    std::optional<core::UnitId> custom_target_;  // CustomShot: the target "Custom..." was picked on
    std::string verb_;         // picking a unit: "move", "shoot with", ... (for the hint)
    std::string target_verb_;  // picking targets: "charge", "pile in towards", ...
    std::string end_label_;    // the end-phase button, if this decision has one
};

// The unit an action is for, if it is one the board picks by clicking that unit. Remain stationary is
// not: it comes from the end-phase button.
[[nodiscard]] std::optional<core::UnitId> board_unit(const core::GameAction& action);

}  // namespace wh40k::client
