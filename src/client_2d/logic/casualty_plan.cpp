#include "casualty_plan.hpp"

#include <utility>

#include "casualty_order.hpp"

namespace wh40k::client {

CasualtyPlan::CasualtyPlan(const core::GameState& state, core::UnitId target)
    : target_(target), priority_(default_casualty_order(state, target)) {}

void CasualtyPlan::move_up(std::size_t index) {
    if (index > 0 && index < priority_.size()) std::swap(priority_[index - 1], priority_[index]);
}

void CasualtyPlan::move_down(std::size_t index) {
    if (index + 1 < priority_.size()) std::swap(priority_[index], priority_[index + 1]);
}

}  // namespace wh40k::client
