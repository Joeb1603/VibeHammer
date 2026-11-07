#include "fallback.hpp"

#include <variant>

#include "casualty_order.hpp"
#include "casualty_plan.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "melee_plan.hpp"

namespace wh40k::ai {

namespace {

template <typename A>
std::optional<core::GameAction> first_of(const std::vector<core::GameAction>& legal) {
    for (const core::GameAction& a : legal) {
        if (std::holds_alternative<A>(a) && !core::is_template(a)) return a;
    }
    return std::nullopt;
}

std::optional<core::GameAction> remain_stationary(const std::vector<core::GameAction>& legal) {
    for (const core::GameAction& a : legal) {
        const auto* m = std::get_if<core::action::StartMove>(&a);
        if (m != nullptr && m->move_type == core::MoveType::RemainStationary) return a;
    }
    return std::nullopt;
}

// The kind's preferred always-legal answer.
std::optional<core::GameAction> preferred(const core::GameState& state, const std::vector<core::GameAction>& legal) {
    using namespace core;
    const DecisionKind& kind = state.pending->kind;
    if (std::holds_alternative<decision::MoveUnits>(kind)) return remain_stationary(legal);
    if (std::holds_alternative<decision::MoveModels>(kind)) {
        if (move_end_problems(state).empty()) return action::CommitUnitMove{};
        return action::ResetUnitMove{};
    }
    if (std::holds_alternative<decision::Shoot>(kind)) return action::EndShootStep{};
    if (std::holds_alternative<decision::Charge>(kind)) return action::EndChargeStep{};
    if (const auto* d = std::get_if<decision::SelectChargeTargets>(&kind)) return action::SkipChargeMove{d->unit};
    if (std::holds_alternative<decision::PileIn>(kind)) return action::EndPileIns{};
    if (const auto* d = std::get_if<decision::OverrunPileIn>(&kind)) return action::SkipPileIn{d->unit};
    if (std::holds_alternative<decision::SelectUnitToFight>(kind)) {
        if (auto pass = first_of<action::PassFight>(legal)) return pass;
        return first_of<action::SelectToFight>(legal);
    }
    if (std::holds_alternative<decision::ForcedFight>(kind)) return first_of<action::SelectToFight>(legal);
    if (const auto* d = std::get_if<decision::DeclareMeleeAttacks>(&kind)) {
        return client::MeleePlan(state, d->unit).expand();
    }
    if (std::holds_alternative<decision::Consolidate>(kind)) return action::EndConsolidations{};
    if (const auto* d = std::get_if<decision::RestoreCoherency>(&kind)) {
        // D-058 (M47): the default order's first model, else the first offered.
        if (const std::optional<ModelId> m = client::default_coherency_removal(state, d->unit)) {
            return action::RemoveModelForCoherency{d->unit, *m};
        }
        return first_of<action::RemoveModelForCoherency>(legal);
    }
    if (std::holds_alternative<decision::SelectAttackBatch>(kind)) return first_of<action::SelectAttackBatch>(legal);
    if (std::holds_alternative<decision::DeclareAllocationOrder>(kind)) {
        return first_of<action::DeclareAllocationOrder>(legal);
    }
    if (const auto* d = std::get_if<decision::OrderCasualties>(&kind)) {
        return client::CasualtyPlan(state, d->target).action();
    }
    return std::nullopt;
}

// Accepted by the engine, and (for a commit) not going to send the models back (D-054: no silent reverts).
std::optional<std::string> problem(const core::GameState& state, core::Side side, const core::GameAction& a) {
    if (core::is_template(a)) return std::string("D-054: ") + std::string(core::action_name(a)) + " is a template";
    const core::ValidationResult v = core::validate_action(state, side, a);
    if (!v.ok()) return v.reasons.front();
    if (std::holds_alternative<core::action::CommitUnitMove>(a)) {
        const std::vector<std::string> p = core::move_end_problems(state);
        if (!p.empty()) return p.front();
    }
    return std::nullopt;
}

}  // namespace

FallbackResult fallback_action(const core::GameState& state, core::Side side) {
    FallbackResult out;
    if (!state.pending || state.pending->side != side) {
        out.problems.push_back("D-054: the bot has no decision to answer");
        return out;
    }
    const std::vector<core::GameAction> legal = core::legal_actions(state, side);
    std::vector<core::GameAction> candidates;
    if (auto p = preferred(state, legal)) candidates.push_back(*p);
    for (const core::GameAction& a : legal) {
        if (!core::is_template(a)) candidates.push_back(a);
    }
    for (const core::GameAction& a : candidates) {
        if (auto why = problem(state, side, a)) {
            out.problems.push_back(std::string(core::action_name(a)) + ": " + *why);
            continue;
        }
        out.action = a;
        out.reason = "Fallback: " + std::string(core::action_name(a));
        out.problems.clear();
        return out;
    }
    if (candidates.empty()) out.problems.push_back("D-054: no legal action to fall back on");
    return out;
}

bool LoopGuard::visit(std::uint64_t state_hash) {
    if (seen_.size() > (1u << 16)) seen_.clear();  // bounded; a game has far fewer decisions
    return ++seen_[state_hash] >= kLoopLimit;
}

}  // namespace wh40k::ai
