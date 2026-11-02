#include "core/state/decision.hpp"

#include <type_traits>

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
            else if constexpr (std::is_same_v<T, decision::SelectAttackBatch>) return "04.03";
            else if constexpr (std::is_same_v<T, decision::DeclareAllocationOrder>) return "05.03";
            else if constexpr (std::is_same_v<T, decision::OrderCasualties>) return "05.04";
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
            else if constexpr (std::is_same_v<T, decision::SelectAttackBatch>) return "SelectAttackBatch";
            else if constexpr (std::is_same_v<T, decision::DeclareAllocationOrder>) return "DeclareAllocationOrder";
            else if constexpr (std::is_same_v<T, decision::OrderCasualties>) return "OrderCasualties";
            else static_assert(sizeof(T) == 0, "unhandled decision");
        },
        kind);
}

std::string describe(const PendingDecision& d) {
    const std::string what = std::visit(
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
            } else if constexpr (std::is_same_v<T, decision::SelectAttackBatch>) {
                return "select the next attacks to resolve for unit " + std::to_string(k.attacker.v);
            } else if constexpr (std::is_same_v<T, decision::DeclareAllocationOrder>) {
                return "declare the allocation order for unit " + std::to_string(k.target.v);
            } else if constexpr (std::is_same_v<T, decision::OrderCasualties>) {
                return "order the casualties for unit " + std::to_string(k.target.v);
            } else {
                static_assert(sizeof(T) == 0, "unhandled decision");
            }
        },
        d.kind);
    return side_label(d.side) + ": " + what + " (" + std::string(decision_rule(d)) + ")";
}

}  // namespace wh40k::core
