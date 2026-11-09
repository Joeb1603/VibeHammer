#include "deploy_policy.hpp"

#include <string>
#include <variant>

#include "unit_style.hpp"

namespace wh40k::ai {

std::optional<PolicyResult> deploy_policy(const core::GameState& state, core::Side side,
                                          const std::vector<core::GameAction>& legal) {
    if (!state.pending || !std::holds_alternative<core::decision::DeployUnits>(state.pending->kind)) {
        return std::nullopt;
    }
    // The lowest-id unit of `side` waiting to be set up (GameState::undeployed is in id order).
    std::optional<core::UnitId> lowest;
    for (const core::Unit& u : state.undeployed) {
        if (u.side == side) {
            lowest = u.id;
            break;
        }
    }
    if (!lowest) return std::nullopt;
    const std::string name = client::unit_display_name(state, *lowest);
    for (const core::GameAction& a : legal) {
        if (core::is_template(a)) continue;
        if (const auto* d = std::get_if<core::action::DeployUnit>(&a); d != nullptr && d->unit == *lowest) {
            return PolicyResult{a, "D-081: set up " + name + " where Auto-place puts it", {}};
        }
        if (const auto* c = std::get_if<core::action::CannotSetUp>(&a); c != nullptr && c->unit == *lowest) {
            return PolicyResult{a, "D-081: " + name + " can't be set up: no legal spot is left, so it counts as destroyed",
                                {}};
        }
    }
    return std::nullopt;
}

}  // namespace wh40k::ai
