#pragma once

// The ImGui decision panel, generated from the pending decision and legal_actions (no phase-specific
// buttons): one button per fully specified action, and an editor per template (shooting, melee,
// casualties; moves are made on the board). Returns the action the player chose; the caller submits it
// through GameSession, which validates it first. It is laid out for the wide, short action bar under the
// phase bar (D-030): the decision on the left, its buttons on the right, per-model editors in popups.

#include <optional>
#include <string>
#include <vector>

#include "board_interaction.hpp"
#include "casualty_plan.hpp"
#include "decision_view.hpp"
#include "core/engine/actions.hpp"
#include "melee_plan.hpp"
#include "session.hpp"
#include "shot_plan.hpp"

namespace wh40k::client {

class DecisionPanel {
public:
    // Draws inside the current ImGui window. `selected_model`: the model selected on the board, whose row
    // is highlighted (its override is what the player edits, D-001). When `board` handles the decision
    // (D-029), units are picked on the board: the panel shows the hint, the step buttons and the selected
    // shooter's editor instead of a button per unit.
    // Returns the actions the player chose, to submit in order (the end-phase button can be several).
    std::vector<core::GameAction> draw(const GameSession& session, std::optional<core::ModelId> selected_model,
                                       BoardInteraction* board = nullptr);
    // Forget the editors: call when the session is replaced (new game, load) or rebuilt (undo), so the next
    // draw builds them for the new state even if its step count is the same.
    void reset() { synced_step_.reset(); }

private:
    std::optional<core::GameAction> draw_shot_editor(const GameSession& session, const core::action::Shoot& tmpl,
                                                     std::optional<core::ModelId> selected_model);
    std::optional<core::GameAction> draw_shot_plan(const core::GameState& gs, ShotPlan& plan, const std::string& title,
                                                   std::optional<core::ModelId> selected_model);
    std::vector<core::GameAction> draw_board_mode(const GameSession& session, BoardInteraction& board,
                                                  std::optional<core::ModelId> selected_model);
    std::optional<core::GameAction> draw_sidebar_mode(const GameSession& session,
                                                      std::optional<core::ModelId> selected_model);
    std::optional<core::GameAction> draw_melee_editor(const GameSession& session, const core::action::Fight& tmpl,
                                                      std::optional<core::ModelId> selected_model);
    std::optional<core::GameAction> draw_casualty_editor(const GameSession& session,
                                                         const core::action::OrderCasualties& tmpl);
    void sync(const GameSession& session);  // rebuild the editors when the game moves on

    std::optional<std::size_t> synced_step_;  // the session step the editors were built for
    DecisionView view_;                        // legal actions for that step, split (not rebuilt per frame)
    std::vector<ShotPlan> shots_;
    std::optional<MeleePlan> melee_;
    std::optional<CasualtyPlan> casualties_;
};

}  // namespace wh40k::client
