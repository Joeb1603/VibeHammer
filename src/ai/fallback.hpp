#pragma once

// D-054: the bot's safety net. An always-legal answer for every one of the 15 decision kinds (a lone skip or
// End step, Remain Stationary, SkipChargeMove, PassFight, the MeleePlan default, the CasualtyPlan default (the
// D-058 order), the default order's coherency model (D-058, M47), the first allocation order / batch, ...), and
// the loop guard that switches to it.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai {

struct FallbackResult {
    std::optional<core::GameAction> action;  // validated by the engine; nullopt: nothing was accepted
    std::string reason;
    std::vector<std::string> problems;  // why each candidate was rejected (when `action` is nullopt)
};

// The fallback for `side`'s pending decision. Templates are always filled; the result is validated with
// validate_action (and a CommitUnitMove with move_end_problems), trying the kind's preferred answer first and
// then every other non-template legal action in order.
[[nodiscard]] FallbackResult fallback_action(const core::GameState& state, core::Side side);

// D-054 loop guard: the same state hash (which includes the pending decision) seen `kLoopLimit` times means
// the bot is going round in circles, so it uses the fallback instead.
class LoopGuard {
public:
    static constexpr int kLoopLimit = 3;
    // Records one more visit; true when this visit reaches the limit.
    bool visit(std::uint64_t state_hash);
    void clear() noexcept { seen_.clear(); }

private:
    std::map<std::uint64_t, int> seen_;
};

}  // namespace wh40k::ai
