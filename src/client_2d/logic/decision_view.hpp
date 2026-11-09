#pragma once

// Splits the legal actions for the pending decision into fully specified actions (one button each) and
// templates (D-004), each handled by an editor. Nothing here is keyed on the phase: the panel is
// generated from PendingDecision and legal_actions.

#include <optional>
#include <vector>

#include "core/engine/actions.hpp"

namespace wh40k::client {

enum class EditorKind {
    Path,        // ProposeModelMove: drag on the board
    Shot,        // Shoot: weapons, profiles and targets per model (D-001, D-016)
    Melee,       // Fight: melee weapons and targets per model (D-021)
    Casualties,  // OrderCasualties: casualty priority (D-013)
    Deploy,      // DeployUnit: every model's place when the unit is set up by hand (D-081; the board editor is M76's)
};

struct DecisionView {
    std::vector<core::GameAction> buttons;    // fully specified: submit as they are
    std::vector<core::GameAction> templates;  // open parameters: an editor fills them
};

[[nodiscard]] DecisionView make_decision_view(const std::vector<core::GameAction>& legal);

// The editor for a template (nullopt if `action` is not a template).
[[nodiscard]] std::optional<EditorKind> editor_kind(const core::GameAction& action);

}  // namespace wh40k::client
