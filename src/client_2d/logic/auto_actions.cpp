#include "auto_actions.hpp"

#include <algorithm>
#include <variant>

#include "casualty_plan.hpp"
#include "fight_move_skip.hpp"

namespace wh40k::client {

bool is_skip(const core::GameAction& action) {
    using namespace core::action;
    return std::holds_alternative<EndShootStep>(action) || std::holds_alternative<EndChargeStep>(action) ||
           std::holds_alternative<EndPileIns>(action) || std::holds_alternative<EndConsolidations>(action) ||
           std::holds_alternative<PassFight>(action) || std::holds_alternative<SkipPileIn>(action);
}

std::optional<core::GameAction> lone_skip(const std::vector<core::GameAction>& legal) {
    if (legal.size() != 1 || !is_skip(legal.front())) return std::nullopt;
    return legal.front();
}

std::optional<core::GameAction> next_auto_action(const core::GameState& state,
                                                 const std::vector<core::GameAction>& legal,
                                                 AutoSettings settings) {
    if (settings.skip_empty_steps) {
        if (auto skip = lone_skip(legal)) return skip;
        if (auto skip = pointless_fight_step_skip(state, legal)) return skip;  // D-047
    }
    // D-082 part 2 (M76): "Auto-place the rest": the first fully specified DeployUnit, which D-081 lists first for the
    // lowest-id unit with a legal spot (a unit without one has CannotSetUp instead, which this never picks).
    if (state.pending && std::holds_alternative<core::decision::DeployUnits>(state.pending->kind)) {
        const std::size_t side = core::side_index(state.pending->side);
        if (settings.auto_deploy[side] && !settings.bot_controlled[side]) {
            for (const core::GameAction& a : legal) {
                const auto* d = std::get_if<core::action::DeployUnit>(&a);
                if (d != nullptr && !d->models.empty()) return a;
            }
        }
        return std::nullopt;
    }
    if (settings.auto_allocate && state.pending && !settings.bot_controlled[core::side_index(state.pending->side)]) {
        if (std::holds_alternative<core::decision::DeclareAllocationOrder>(state.pending->kind) && !legal.empty()) {
            return legal.front();  // 05.03: the first legal order, as the panel offers first
        }
        if (const auto* d = std::get_if<core::decision::OrderCasualties>(&state.pending->kind)) {
            return CasualtyPlan(state, d->target).action();  // 05.04: the default casualty order (D-058)
        }
    }
    return std::nullopt;
}

std::vector<core::GameAction> run_auto_actions(GameSession& session, AutoSettings settings,
                                               const std::function<bool(const core::GameAction&)>& submit,
                                               std::size_t limit) {
    std::vector<core::GameAction> done;
    while (done.size() < limit && session.pending()) {
        const std::optional<core::GameAction> next = next_auto_action(session.state(), session.legal(), settings);
        if (!next) break;
        const bool ok = submit ? submit(*next) : session.submit(*next).ok();
        if (!ok) break;
        done.push_back(*next);
    }
    return done;
}

bool is_set_up(const core::GameAction& action) {
    return std::holds_alternative<core::action::DeployUnit>(action) || std::holds_alternative<core::action::CannotSetUp>(action);
}

std::vector<core::GameAction> run_auto_actions_grouped(GameSession& session, AutoSettings settings, UndoGroups& groups,
                                                       const std::function<bool(const core::GameAction&)>& submit,
                                                       std::size_t limit) {
    return run_auto_actions(
        session, settings,
        [&](const core::GameAction& a) {
            if (is_set_up(a)) {
                groups.end(session.history().size());  // the group so far (dropped if it added nothing)
                groups.begin(session.history().size(), InputOwner::Human);
            }
            return submit ? submit(a) : session.submit(a).ok();
        },
        limit);
}

std::vector<core::UnitId> units_that_can_still_shoot(const std::vector<core::GameAction>& legal) {
    std::vector<core::UnitId> out;
    for (const core::GameAction& a : legal) {
        const auto* s = std::get_if<core::action::Shoot>(&a);
        if (s == nullptr || std::find(out.begin(), out.end(), s->unit) != out.end()) continue;
        out.push_back(s->unit);
    }
    return out;
}

}  // namespace wh40k::client
