#include "casualty_plan.hpp"

#include <utility>

namespace wh40k::client {

CasualtyPlan::CasualtyPlan(const core::GameState& state, core::UnitId target) : target_(target) {
    if (const core::Unit* u = state.find_unit(target)) {
        for (const core::Model& m : u->models) priority_.push_back(m.id);
    }
}

void CasualtyPlan::move_up(std::size_t index) {
    if (index > 0 && index < priority_.size()) std::swap(priority_[index - 1], priority_[index]);
}

void CasualtyPlan::move_down(std::size_t index) {
    if (index + 1 < priority_.size()) std::swap(priority_[index], priority_[index + 1]);
}

}  // namespace wh40k::client
