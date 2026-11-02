#include "core/engine/events.hpp"

#include <type_traits>

namespace wh40k::core {

namespace {

struct NameRule {
    std::string_view name;
    std::string_view rule;
};

template <typename T>
constexpr NameRule name_rule() {
    if constexpr (std::is_same_v<T, event::BattleRoundStarted>) return {"BattleRoundStarted", "07.01"};
    else if constexpr (std::is_same_v<T, event::BattleRoundEnded>) return {"BattleRoundEnded", "07.03"};
    else if constexpr (std::is_same_v<T, event::TurnStarted>) return {"TurnStarted", "07.02"};
    else if constexpr (std::is_same_v<T, event::TurnEnded>) return {"TurnEnded", "07.02"};
    else if constexpr (std::is_same_v<T, event::PhaseStarted>) return {"PhaseStarted", "07.02"};
    else if constexpr (std::is_same_v<T, event::PhaseEnded>) return {"PhaseEnded", "07.02"};
    else if constexpr (std::is_same_v<T, event::UnitRemainedStationary>) return {"UnitRemainedStationary", "09.04"};
    else if constexpr (std::is_same_v<T, event::AttackBatchesGathered>) return {"AttackBatchesGathered", "04.03"};
    else if constexpr (std::is_same_v<T, event::AttackBatchSelected>) return {"AttackBatchSelected", "04.03"};
    else if constexpr (std::is_same_v<T, event::HitRollsMade>) return {"HitRollsMade", "05.01"};
    else if constexpr (std::is_same_v<T, event::WoundRollsMade>) return {"WoundRollsMade", "05.02"};
    else if constexpr (std::is_same_v<T, event::AllocationOrderDeclared>) return {"AllocationOrderDeclared", "05.03"};
    else if constexpr (std::is_same_v<T, event::SaveRollsMade>) return {"SaveRollsMade", "05.03"};
    else if constexpr (std::is_same_v<T, event::SavesResolved>) return {"SavesResolved", "05.04"};
    else if constexpr (std::is_same_v<T, event::ModelDestroyed>) return {"ModelDestroyed", "Appendix: Destroyed"};
    else if constexpr (std::is_same_v<T, event::UnitDestroyed>) return {"UnitDestroyed", "Appendix: Destroyed"};
    else if constexpr (std::is_same_v<T, event::AttacksLost>) return {"AttacksLost", "D-012"};
    else if constexpr (std::is_same_v<T, event::GameOver>) return {"GameOver", "D-007"};
    else static_assert(sizeof(T) == 0, "unhandled event");
}

}  // namespace

std::string_view event_name(const GameEvent& e) {
    return std::visit([](const auto& x) { return name_rule<std::decay_t<decltype(x)>>().name; }, e);
}

std::string_view event_rule(const GameEvent& e) {
    return std::visit([](const auto& x) { return name_rule<std::decay_t<decltype(x)>>().rule; }, e);
}

}  // namespace wh40k::core
