#include "decision_view.hpp"

#include <variant>

namespace wh40k::client {

DecisionView make_decision_view(const std::vector<core::GameAction>& legal) {
    DecisionView v;
    for (const core::GameAction& a : legal) {
        (core::is_template(a) ? v.templates : v.buttons).push_back(a);
    }
    return v;
}

std::optional<EditorKind> editor_kind(const core::GameAction& action) {
    if (!core::is_template(action)) return std::nullopt;
    if (std::holds_alternative<core::action::ProposeModelMove>(action)) return EditorKind::Path;
    if (std::holds_alternative<core::action::Shoot>(action)) return EditorKind::Shot;
    if (std::holds_alternative<core::action::Fight>(action)) return EditorKind::Melee;
    if (std::holds_alternative<core::action::OrderCasualties>(action)) return EditorKind::Casualties;
    return std::nullopt;
}

}  // namespace wh40k::client
