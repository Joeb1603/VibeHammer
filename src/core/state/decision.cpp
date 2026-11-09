#include "core/state/decision.hpp"

#include <type_traits>

#include "core/state/game_state.hpp"

namespace wh40k::core {

namespace {

std::string side_label(Side s) { return s == Side::A ? "Side A" : "Side B"; }

}  // namespace

std::string_view decision_rule(const PendingDecision& d) {
    return std::visit(
        [](const auto& k) -> std::string_view {
            using T = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<T, decision::MoveUnits>) return "09.02";
            else if constexpr (std::is_same_v<T, decision::MoveModels>) return "03.01";
            else if constexpr (std::is_same_v<T, decision::Shoot>) return "10.02";
            else if constexpr (std::is_same_v<T, decision::Charge>) return "11.02";
            else if constexpr (std::is_same_v<T, decision::SelectChargeTargets>) return "11.04";
            else if constexpr (std::is_same_v<T, decision::PileIn>) return "12.02";
            else if constexpr (std::is_same_v<T, decision::OverrunPileIn>) return "12.06";
            else if constexpr (std::is_same_v<T, decision::SelectUnitToFight>) return "12.04";
            else if constexpr (std::is_same_v<T, decision::DeclareMeleeAttacks>) return "04.02";
            else if constexpr (std::is_same_v<T, decision::Consolidate>) return "12.07";
            else if constexpr (std::is_same_v<T, decision::ForcedFight>) return "12.08";
            else if constexpr (std::is_same_v<T, decision::RestoreCoherency>) return "03.03";
            else if constexpr (std::is_same_v<T, decision::SelectAttackBatch>) return "04.03";
            else if constexpr (std::is_same_v<T, decision::DeclareAllocationOrder>) return "05.03";
            else if constexpr (std::is_same_v<T, decision::OrderCasualties>) return "05.04";
            else if constexpr (std::is_same_v<T, decision::RerollWoundRolls>) return "24.38";
            else if constexpr (std::is_same_v<T, decision::DeployUnits>) return "03.02";  // D-081
            else static_assert(sizeof(T) == 0, "unhandled decision");
        },
        d.kind);
}

std::string_view decision_name(const DecisionKind& kind) {
    return std::visit(
        [](const auto& k) -> std::string_view {
            using T = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<T, decision::MoveUnits>) return "MoveUnits";
            else if constexpr (std::is_same_v<T, decision::MoveModels>) return "MoveModels";
            else if constexpr (std::is_same_v<T, decision::Shoot>) return "Shoot";
            else if constexpr (std::is_same_v<T, decision::Charge>) return "Charge";
            else if constexpr (std::is_same_v<T, decision::SelectChargeTargets>) return "SelectChargeTargets";
            else if constexpr (std::is_same_v<T, decision::PileIn>) return "PileIn";
            else if constexpr (std::is_same_v<T, decision::OverrunPileIn>) return "OverrunPileIn";
            else if constexpr (std::is_same_v<T, decision::SelectUnitToFight>) return "SelectUnitToFight";
            else if constexpr (std::is_same_v<T, decision::DeclareMeleeAttacks>) return "DeclareMeleeAttacks";
            else if constexpr (std::is_same_v<T, decision::Consolidate>) return "Consolidate";
            else if constexpr (std::is_same_v<T, decision::ForcedFight>) return "ForcedFight";
            else if constexpr (std::is_same_v<T, decision::RestoreCoherency>) return "RestoreCoherency";
            else if constexpr (std::is_same_v<T, decision::SelectAttackBatch>) return "SelectAttackBatch";
            else if constexpr (std::is_same_v<T, decision::DeclareAllocationOrder>) return "DeclareAllocationOrder";
            else if constexpr (std::is_same_v<T, decision::OrderCasualties>) return "OrderCasualties";
            else if constexpr (std::is_same_v<T, decision::RerollWoundRolls>) return "RerollWoundRolls";
            else if constexpr (std::is_same_v<T, decision::DeployUnits>) return "DeployUnits";
            else static_assert(sizeof(T) == 0, "unhandled decision");
        },
        kind);
}

namespace {

std::string what_to_do(const PendingDecision& d) {
    return std::visit(
        [](const auto& k) -> std::string {
            using T = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<T, decision::MoveUnits>) {
                return "select a unit to move";
            } else if constexpr (std::is_same_v<T, decision::MoveModels>) {
                return "move the models of unit " + std::to_string(k.unit.v) + ", then commit or reset the move";
            } else if constexpr (std::is_same_v<T, decision::Shoot>) {
                return "select a unit to shoot, or end the Shoot step";
            } else if constexpr (std::is_same_v<T, decision::Charge>) {
                return "declare a charge, or end the Charge step";
            } else if constexpr (std::is_same_v<T, decision::SelectChargeTargets>) {
                return "select charge targets for unit " + std::to_string(k.unit.v) + ", or make no charge move";
            } else if constexpr (std::is_same_v<T, decision::PileIn>) {
                return "make a pile-in move, or end pile-in moves";
            } else if constexpr (std::is_same_v<T, decision::OverrunPileIn>) {
                return "make unit " + std::to_string(k.unit.v) + "'s overrun pile-in move, or skip it";
            } else if constexpr (std::is_same_v<T, decision::SelectUnitToFight>) {
                return "select a unit to fight";
            } else if constexpr (std::is_same_v<T, decision::DeclareMeleeAttacks>) {
                return "select melee weapons and targets for unit " + std::to_string(k.unit.v);
            } else if constexpr (std::is_same_v<T, decision::Consolidate>) {
                return "make a consolidation move, or end consolidation moves";
            } else if constexpr (std::is_same_v<T, decision::ForcedFight>) {
                return "select a unit engaged by an engaging consolidation to fight";
            } else if constexpr (std::is_same_v<T, decision::RestoreCoherency>) {
                return "remove a model from unit " + std::to_string(k.unit.v) + " to restore coherency";
            } else if constexpr (std::is_same_v<T, decision::SelectAttackBatch>) {
                return "select the next attacks to resolve for unit " + std::to_string(k.attacker.v);
            } else if constexpr (std::is_same_v<T, decision::DeclareAllocationOrder>) {
                return "declare the allocation order for unit " + std::to_string(k.target.v);
            } else if constexpr (std::is_same_v<T, decision::OrderCasualties>) {
                return "order the casualties for unit " + std::to_string(k.target.v);
            } else if constexpr (std::is_same_v<T, decision::RerollWoundRolls>) {
                return "choose the wound rolls to re-roll for unit " + std::to_string(k.attacker.v) +
                       " ([TWIN-LINKED])";
            } else if constexpr (std::is_same_v<T, decision::DeployUnits>) {
                return "set up one of your units";  // D-081: the sides alternate
            } else {
                static_assert(sizeof(T) == 0, "unhandled decision");
            }
        },
        d.kind);
}

}  // namespace

std::string describe(const PendingDecision& d) {
    return side_label(d.side) + ": " + what_to_do(d) + " (" + std::string(decision_rule(d)) + ")";
}

std::string_view decision_rule(const GameState& state, const PendingDecision& d) {
    if (std::holds_alternative<decision::OrderCasualties>(d.kind)) {
        // 06.02 Select Model for mortal wounds: [HAZARDOUS] after a unit's attacks (24.15), or Desperate Escape (09.07)
        // outside an attack; otherwise 05.04 for an attack's save rolls.
        if (!state.flow.attack || state.flow.attack->pending_mortal_wounds > 0) return "06.02";
        return "05.04";
    }
    return decision_rule(d);
}

std::string describe(const GameState& state, const PendingDecision& d) {
    return side_label(d.side) + ": " + what_to_do(d) + " (" + std::string(decision_rule(state, d)) + ")";
}

}  // namespace wh40k::core
