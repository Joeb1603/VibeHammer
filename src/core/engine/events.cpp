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
    else if constexpr (std::is_same_v<T, event::MoveStarted>) return {"MoveStarted", "09.02"};
    else if constexpr (std::is_same_v<T, event::AdvanceRolled>) return {"AdvanceRolled", "09.06"};
    else if constexpr (std::is_same_v<T, event::HazardRollsMade>) return {"HazardRollsMade", "06.03"};
    else if constexpr (std::is_same_v<T, event::MortalWoundsInflicted>) return {"MortalWoundsInflicted", "06.02"};
    else if constexpr (std::is_same_v<T, event::ModelMoveProposed>) return {"ModelMoveProposed", "03.01"};
    else if constexpr (std::is_same_v<T, event::UnitMoveCommitted>) return {"UnitMoveCommitted", "03.01"};
    else if constexpr (std::is_same_v<T, event::UnitMoveReverted>) return {"UnitMoveReverted", "03.01"};
    else if constexpr (std::is_same_v<T, event::BattleShockRolled>) return {"BattleShockRolled", "01.07"};
    else if constexpr (std::is_same_v<T, event::ShootingStarted>) return {"ShootingStarted", "10.02"};
    else if constexpr (std::is_same_v<T, event::ChargeDeclared>) return {"ChargeDeclared", "11.02"};
    else if constexpr (std::is_same_v<T, event::ChargeRolled>) return {"ChargeRolled", "11.02"};
    else if constexpr (std::is_same_v<T, event::ChargeTargetsSelected>) return {"ChargeTargetsSelected", "11.04"};
    else if constexpr (std::is_same_v<T, event::ChargeResolved>) return {"ChargeResolved", "11.02"};
    else if constexpr (std::is_same_v<T, event::PileInStarted>) return {"PileInStarted", "12.03"};
    else if constexpr (std::is_same_v<T, event::UnitSelectedToFight>) return {"UnitSelectedToFight", "12.04"};
    else if constexpr (std::is_same_v<T, event::FightPassed>) return {"FightPassed", "Appendix: Eligible to Fight, but Unable to Fight"};
    else if constexpr (std::is_same_v<T, event::MeleeAttacksDeclared>) return {"MeleeAttacksDeclared", "04.02"};
    else if constexpr (std::is_same_v<T, event::FightStepEnded>) return {"FightStepEnded", "12.04"};
    else if constexpr (std::is_same_v<T, event::ConsolidationStarted>) return {"ConsolidationStarted", "12.08"};
    else if constexpr (std::is_same_v<T, event::UnitsForcedToFight>) return {"UnitsForcedToFight", "12.08"};
    else if constexpr (std::is_same_v<T, event::CommandPointGained>) return {"CommandPointGained", "08.02"};
    else if constexpr (std::is_same_v<T, event::BattleShockEnded>) return {"BattleShockEnded", "08.03"};
    else if constexpr (std::is_same_v<T, event::ModelRemovedForCoherency>) return {"ModelRemovedForCoherency", "03.03"};
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
    else if constexpr (std::is_same_v<T, event::WoundRollsRerolled>) return {"WoundRollsRerolled", "24.38"};
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
