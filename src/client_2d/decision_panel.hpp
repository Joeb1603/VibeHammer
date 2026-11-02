#pragma once

// The ImGui decision panel, generated from the pending decision and legal_actions (no phase-specific
// buttons): one button per fully specified action, and an editor per template (shooting, melee,
// casualties; moves are made on the board). Returns the action the player chose; the caller submits it
// through GameSession, which validates it first.

#include <optional>
#include <string>
#include <vector>

#include "casualty_plan.hpp"
#include "core/engine/actions.hpp"
#include "melee_plan.hpp"
#include "session.hpp"
#include "shot_plan.hpp"

namespace wh40k::client {

class DecisionPanel {
public:
    // Draws inside the current ImGui window. `selected_model`: the model selected on the board, whose row
    // is highlighted (its override is what the player edits, D-001).
    std::optional<core::GameAction> draw(const GameSession& session, std::optional<core::ModelId> selected_model);

private:
    std::optional<core::GameAction> draw_shot_editor(const GameSession& session, const core::action::Shoot& tmpl,
                                                     std::optional<core::ModelId> selected_model);
    std::optional<core::GameAction> draw_melee_editor(const GameSession& session, const core::action::Fight& tmpl,
                                                      std::optional<core::ModelId> selected_model);
    std::optional<core::GameAction> draw_casualty_editor(const GameSession& session,
                                                         const core::action::OrderCasualties& tmpl);
    void sync(const GameSession& session);  // rebuild the editors when the game moves on

    std::size_t synced_step_ = static_cast<std::size_t>(-1);
    std::vector<ShotPlan> shots_;
    std::optional<MeleePlan> melee_;
    std::optional<CasualtyPlan> casualties_;
};

}  // namespace wh40k::client
