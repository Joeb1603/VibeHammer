#include "core/engine/actions.hpp"

#include <type_traits>

namespace wh40k::core {

std::string_view action_name(const GameAction& a) {
    return std::visit(
        [](const auto& x) -> std::string_view {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, action::StartMove>) return "StartMove";
            else if constexpr (std::is_same_v<T, action::ProposeModelMove>) return "ProposeModelMove";
            else if constexpr (std::is_same_v<T, action::CommitUnitMove>) return "CommitUnitMove";
            else if constexpr (std::is_same_v<T, action::ResetUnitMove>) return "ResetUnitMove";
            else if constexpr (std::is_same_v<T, action::Shoot>) return "Shoot";
            else if constexpr (std::is_same_v<T, action::EndShootStep>) return "EndShootStep";
            else if constexpr (std::is_same_v<T, action::DeclareCharge>) return "DeclareCharge";
            else if constexpr (std::is_same_v<T, action::StartChargeMove>) return "StartChargeMove";
            else if constexpr (std::is_same_v<T, action::SkipChargeMove>) return "SkipChargeMove";
            else if constexpr (std::is_same_v<T, action::EndChargeStep>) return "EndChargeStep";
            else if constexpr (std::is_same_v<T, action::StartPileIn>) return "StartPileIn";
            else if constexpr (std::is_same_v<T, action::SkipPileIn>) return "SkipPileIn";
            else if constexpr (std::is_same_v<T, action::EndPileIns>) return "EndPileIns";
            else if constexpr (std::is_same_v<T, action::SelectToFight>) return "SelectToFight";
            else if constexpr (std::is_same_v<T, action::PassFight>) return "PassFight";
            else if constexpr (std::is_same_v<T, action::Fight>) return "Fight";
            else if constexpr (std::is_same_v<T, action::StartConsolidation>) return "StartConsolidation";
            else if constexpr (std::is_same_v<T, action::EndConsolidations>) return "EndConsolidations";
            else if constexpr (std::is_same_v<T, action::RemoveModelForCoherency>) return "RemoveModelForCoherency";
            else if constexpr (std::is_same_v<T, action::SelectAttackBatch>) return "SelectAttackBatch";
            else if constexpr (std::is_same_v<T, action::DeclareAllocationOrder>) return "DeclareAllocationOrder";
            else if constexpr (std::is_same_v<T, action::OrderCasualties>) return "OrderCasualties";
            else if constexpr (std::is_same_v<T, action::RerollWoundRolls>) return "RerollWoundRolls";
            else static_assert(sizeof(T) == 0, "unhandled action");
        },
        a);
}

std::string_view action_rule(const GameAction& a) {
    return std::visit(
        [](const auto& x) -> std::string_view {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, action::StartMove>) return "09.02";
            else if constexpr (std::is_same_v<T, action::ProposeModelMove>) return "03.01";
            else if constexpr (std::is_same_v<T, action::CommitUnitMove>) return "03.01";
            else if constexpr (std::is_same_v<T, action::ResetUnitMove>) return "03.01";
            else if constexpr (std::is_same_v<T, action::Shoot>) return "10.02";
            else if constexpr (std::is_same_v<T, action::EndShootStep>) return "10.02";
            else if constexpr (std::is_same_v<T, action::DeclareCharge>) return "11.02";
            else if constexpr (std::is_same_v<T, action::StartChargeMove>) return "11.04";
            else if constexpr (std::is_same_v<T, action::SkipChargeMove>) return "11.02";
            else if constexpr (std::is_same_v<T, action::EndChargeStep>) return "11.02";
            else if constexpr (std::is_same_v<T, action::StartPileIn>) return "12.03";
            else if constexpr (std::is_same_v<T, action::SkipPileIn>) return "12.06";
            else if constexpr (std::is_same_v<T, action::EndPileIns>) return "12.02";
            else if constexpr (std::is_same_v<T, action::SelectToFight>) return "12.04";
            else if constexpr (std::is_same_v<T, action::PassFight>) return "12.04";  // Appendix: Eligible to Fight, but Unable
            else if constexpr (std::is_same_v<T, action::Fight>) return "04.02";
            else if constexpr (std::is_same_v<T, action::StartConsolidation>) return "12.08";
            else if constexpr (std::is_same_v<T, action::EndConsolidations>) return "12.07";
            else if constexpr (std::is_same_v<T, action::RemoveModelForCoherency>) return "03.03";
            else if constexpr (std::is_same_v<T, action::SelectAttackBatch>) return "04.03";
            else if constexpr (std::is_same_v<T, action::DeclareAllocationOrder>) return "05.03";
            else if constexpr (std::is_same_v<T, action::OrderCasualties>) return "05.04";
            else if constexpr (std::is_same_v<T, action::RerollWoundRolls>) return "24.38";
            else static_assert(sizeof(T) == 0, "unhandled action");
        },
        a);
}

bool is_template(const GameAction& a) {
    if (const auto* c = std::get_if<action::OrderCasualties>(&a)) {
        return c->priority.empty();  // D-013: the priority is the open parameter
    }
    if (const auto* f = std::get_if<action::Fight>(&a)) {
        return f->declarations.empty();  // D-021: the declarations are the open parameter
    }
    if (const auto* s = std::get_if<action::Shoot>(&a)) {
        return s->shots.empty();  // D-016: the declarations are the open parameter
    }
    if (const auto* m = std::get_if<action::ProposeModelMove>(&a)) {
        return m->path.empty();  // D-004: the path is the open parameter
    }
    return false;
}

}  // namespace wh40k::core
