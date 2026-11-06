#include "bot.hpp"

#include <utility>
#include <variant>

#include "baseline_bot.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "state_hash.hpp"

namespace wh40k::ai {

namespace {

// Accepted by the engine and, for a commit, not going to be reverted (D-054). The first problem, if any.
std::optional<std::string> problem(const core::GameState& state, core::Side side, const core::GameAction& a) {
    if (core::is_template(a)) return "D-054: " + std::string(core::action_name(a)) + " is an unfilled template";
    const core::ValidationResult v = core::validate_action(state, side, a);
    if (!v.ok()) return v.reasons.front();
    if (std::holds_alternative<core::action::CommitUnitMove>(a)) {
        const std::vector<std::string> p = core::move_end_problems(state);
        if (!p.empty()) return p.front();
    }
    return std::nullopt;
}

}  // namespace

BotLevel effective_level(BotLevel requested) {
    switch (requested) {
        case BotLevel::Easy: return BotLevel::Easy;
        case BotLevel::Normal:  // M41-M43
        case BotLevel::Hard:    // M44
            return BotLevel::Easy;
    }
    return BotLevel::Easy;
}

std::uint64_t decision_seed(const core::GameState& state) {
    Fnv1a h;
    h.u64(state_hash(state));
    h.u64(0x626f74ull);  // "bot": a seed apart from the hash itself
    return h.value();
}

void Bot::forget() noexcept {
    memo_.clear();
    guard_.clear();
}

BotChoice Bot::decide(const core::GameState& state, core::Side side) {
    BotChoice out;
    if (!state.pending || state.pending->side != side) {
        out.stopped = true;
        out.problems.push_back("D-054: the bot has no decision to answer");
        out.reason = "Nothing to do";
        return out;
    }
    const std::uint64_t hash = state_hash(state);
    const bool looping = guard_.visit(hash);
    const std::vector<core::GameAction> legal = core::legal_actions(state, side);

    std::optional<PolicyResult> chosen;
    if (!looping && !legal.empty()) {
        if (!memo_.empty() && memo_.front().expect_hash == hash) {
            chosen = PolicyResult{memo_.front().action, memo_.front().reason, {}};
            memo_.erase(memo_.begin());
            ++memo_hits_;
        } else {
            memo_.clear();
            switch (effective_level(config_.level)) {
                case BotLevel::Easy:
                case BotLevel::Normal:
                case BotLevel::Hard: chosen = easy_policy(state, side, legal, decision_seed(state)); break;
            }
            memo_ = std::move(chosen->rest);
            chosen->rest.clear();
        }
    }
    if (chosen && !problem(state, side, chosen->action)) {
        out.action = chosen->action;
        out.reason = chosen->reason;
        return out;
    }
    // The policy's answer isn't legal (or the bot is going round in circles): the fallback.
    memo_.clear();
    ++fallbacks_;
    const FallbackResult fb = fallback_action(state, side);
    if (!fb.action) {
        out.stopped = true;
        out.problems = fb.problems;
        if (chosen) {
            if (auto why = problem(state, side, chosen->action)) out.problems.insert(out.problems.begin(), *why);
        }
        out.reason = "The bot found no legal action and stopped";
        out.action = legal.empty() ? core::GameAction{core::action::EndShootStep{}} : legal.front();
        return out;
    }
    out.action = *fb.action;
    out.reason = (looping ? "Loop guard (D-054): " : "") + fb.reason;
    return out;
}

}  // namespace wh40k::ai
