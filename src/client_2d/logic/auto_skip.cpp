#include "auto_skip.hpp"

#include <variant>

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

std::vector<core::GameAction> run_auto_skips(GameSession& session,
                                             const std::function<bool(const core::GameAction&)>& submit,
                                             std::size_t limit) {
    std::vector<core::GameAction> done;
    while (done.size() < limit && session.pending()) {
        const std::optional<core::GameAction> skip = lone_skip(session.legal());
        if (!skip) break;
        const bool ok = submit ? submit(*skip) : session.submit(*skip).ok();
        if (!ok) break;
        done.push_back(*skip);
    }
    return done;
}

bool undo_with_auto_skips(GameSession& session, std::vector<std::size_t>& auto_steps) {
    bool any = false;
    while (!auto_steps.empty() && auto_steps.back() + 1 == session.history().size()) {
        any = session.undo() || any;
        auto_steps.pop_back();
    }
    return session.undo() || any;
}

}  // namespace wh40k::client
