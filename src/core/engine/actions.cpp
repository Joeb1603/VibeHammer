#include "core/engine/actions.hpp"

#include <type_traits>

namespace wh40k::core {

std::string_view action_name(const GameAction& a) {
    return std::visit(
        [](const auto& x) -> std::string_view {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, action::StartMove>) return "StartMove";
            else if constexpr (std::is_same_v<T, action::EndShootStep>) return "EndShootStep";
            else if constexpr (std::is_same_v<T, action::EndChargeStep>) return "EndChargeStep";
            else if constexpr (std::is_same_v<T, action::SelectAttackBatch>) return "SelectAttackBatch";
            else if constexpr (std::is_same_v<T, action::DeclareAllocationOrder>) return "DeclareAllocationOrder";
            else if constexpr (std::is_same_v<T, action::OrderCasualties>) return "OrderCasualties";
            else static_assert(sizeof(T) == 0, "unhandled action");
        },
        a);
}

std::string_view action_rule(const GameAction& a) {
    return std::visit(
        [](const auto& x) -> std::string_view {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, action::StartMove>) return "09.02";
            else if constexpr (std::is_same_v<T, action::EndShootStep>) return "10.02";
            else if constexpr (std::is_same_v<T, action::EndChargeStep>) return "11.02";
            else if constexpr (std::is_same_v<T, action::SelectAttackBatch>) return "04.03";
            else if constexpr (std::is_same_v<T, action::DeclareAllocationOrder>) return "05.03";
            else if constexpr (std::is_same_v<T, action::OrderCasualties>) return "05.04";
            else static_assert(sizeof(T) == 0, "unhandled action");
        },
        a);
}

bool is_template(const GameAction& a) {
    if (const auto* c = std::get_if<action::OrderCasualties>(&a)) {
        return c->priority.empty();  // D-013: the priority is the open parameter
    }
    return false;
}

}  // namespace wh40k::core
