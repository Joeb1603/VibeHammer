#pragma once

// The casualty editor's model (D-013): a priority over every model of the target unit, most-preferred
// casualty first. Starts in unit order; the player reorders it.

#include <cstddef>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

class CasualtyPlan {
public:
    CasualtyPlan(const core::GameState& state, core::UnitId target);

    [[nodiscard]] core::UnitId target() const noexcept { return target_; }
    [[nodiscard]] const std::vector<core::ModelId>& priority() const noexcept { return priority_; }
    void move_up(std::size_t index);    // swap with the one before; no-op at the top
    void move_down(std::size_t index);  // swap with the one after; no-op at the bottom
    [[nodiscard]] core::action::OrderCasualties action() const { return {target_, priority_}; }

private:
    core::UnitId target_;
    std::vector<core::ModelId> priority_;
};

}  // namespace wh40k::client
