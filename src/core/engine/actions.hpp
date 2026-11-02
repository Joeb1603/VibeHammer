#pragma once

// GameAction: every input to the engine answers the current PendingDecision.

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

#include "core/types/ids.hpp"

namespace wh40k::core {

// 09.02 Select Move Type. M5 implements Remain Stationary (09.04); M7 adds Normal, Advance and
// Fall-back moves.
enum class MoveType : std::uint8_t { RemainStationary };

namespace action {
// 09.02: select a friendly unit that has not been selected to move this phase, and its move type.
struct StartMove {
    UnitId unit;
    MoveType move_type = MoveType::RemainStationary;
    friend bool operator==(const StartMove&, const StartMove&) = default;
};
// 10.02: no more units will be selected to shoot this phase.
struct EndShootStep {
    friend bool operator==(const EndShootStep&, const EndShootStep&) = default;
};
// 11.02: no more units will declare a charge this phase.
struct EndChargeStep {
    friend bool operator==(const EndChargeStep&, const EndChargeStep&) = default;
};
// 04.03: resolve this batch of attacks next (index into the gathered batches).
struct SelectAttackBatch {
    std::size_t batch = 0;
    friend bool operator==(const SelectAttackBatch&, const SelectAttackBatch&) = default;
};
// 05.03 Allocation Order: indices into the target's allocation groups.
struct DeclareAllocationOrder {
    UnitId target;
    std::vector<std::size_t> order;
    friend bool operator==(const DeclareAllocationOrder&, const DeclareAllocationOrder&) = default;
};
// 05.04 Select Model, answered once for the whole batch (D-013): every model in the target unit,
// most-preferred casualty first. An empty priority is the template legal_actions returns.
struct OrderCasualties {
    UnitId target;
    std::vector<ModelId> priority;
    friend bool operator==(const OrderCasualties&, const OrderCasualties&) = default;
};
}  // namespace action

using GameAction = std::variant<action::StartMove, action::EndShootStep, action::EndChargeStep,
                                action::SelectAttackBatch, action::DeclareAllocationOrder,
                                action::OrderCasualties>;

[[nodiscard]] std::string_view action_name(const GameAction& a);  // also the JSON "kind"
[[nodiscard]] std::string_view action_rule(const GameAction& a);  // e.g. "09.02"

// D-004 / D-013: an action template returned by legal_actions, with its open parameter still empty.
// Templates must be filled in before they can be valid.
[[nodiscard]] bool is_template(const GameAction& a);

}  // namespace wh40k::core
