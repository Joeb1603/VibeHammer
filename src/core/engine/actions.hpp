#pragma once

// GameAction: every input to the engine answers the current PendingDecision.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "core/geometry/primitives.hpp"
#include "core/rules/move_types.hpp"
#include "core/rules/shooting_types.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

namespace action {
// 09.02: select a friendly unit that has not been selected to move this phase, and its move type
// (with the fall-back mode for a Fall-back move, 09.07).
struct StartMove {
    UnitId unit;
    MoveType move_type = MoveType::RemainStationary;
    std::optional<FallBackMode> fall_back_mode;
    friend bool operator==(const StartMove&, const StartMove&) = default;
};
// 03.01: move one model of the moving unit. `path` is the end of each straight line, starting from the
// model's position at the start of the move; proposing again replaces the model's earlier path.
// An empty path is the template legal_actions returns (D-004).
struct ProposeModelMove {
    ModelId model;
    std::vector<Position> path;
    friend bool operator==(const ProposeModelMove&, const ProposeModelMove&) = default;
};
// 03.01 Ending a Move: finish the move. If an end condition fails, every model returns to its start
// position and the unit has not been selected to make that move (03 sidebar; D-005 for dice).
struct CommitUnitMove {
    friend bool operator==(const CommitUnitMove&, const CommitUnitMove&) = default;
};
// Abandon the move: models return to their start positions; the unit can be selected again (D-005).
struct ResetUnitMove {
    friend bool operator==(const ResetUnitMove&, const ResetUnitMove&) = default;
};
// 10.02 Select Unit + Select Shooting Type, then 04.01/04.02 for every model: the weapons each model
// fires, with their profiles (D-001) and targets, and the attacks are resolved (04.03). An empty
// `shots` list is the template legal_actions returns (D-004, D-016).
struct Shoot {
    UnitId unit;
    ShootingType type = ShootingType::Normal;
    std::vector<ShotDeclaration> shots;
    friend bool operator==(const Shoot&, const Shoot&) = default;
};
// 10.02: no more units will be selected to shoot this phase.
struct EndShootStep {
    friend bool operator==(const EndShootStep&, const EndShootStep&) = default;
};
// 11.02 Declare Charge: the unit declares a charge; the engine makes its charge roll (2D6).
struct DeclareCharge {
    UnitId unit;
    friend bool operator==(const DeclareCharge&, const DeclareCharge&) = default;
};
// 11.04 Before Moving: select the charge targets (one or more enemy units within 12" and within the
// charge roll), then move the models (03.01: ProposeModelMove, CommitUnitMove / ResetUnitMove).
struct StartChargeMove {
    UnitId unit;
    std::vector<UnitId> targets;
    friend bool operator==(const StartChargeMove&, const StartChargeMove&) = default;
};
// 11.02 Attempt Charge: the unit does not make a charge move; the charge is resolved (it fails).
struct SkipChargeMove {
    UnitId unit;
    friend bool operator==(const SkipChargeMove&, const SkipChargeMove&) = default;
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

using GameAction = std::variant<action::StartMove, action::ProposeModelMove, action::CommitUnitMove,
                                action::ResetUnitMove, action::Shoot, action::EndShootStep, action::DeclareCharge,
                                action::StartChargeMove, action::SkipChargeMove, action::EndChargeStep,
                                action::SelectAttackBatch, action::DeclareAllocationOrder,
                                action::OrderCasualties>;

[[nodiscard]] std::string_view action_name(const GameAction& a);  // also the JSON "kind"
[[nodiscard]] std::string_view action_rule(const GameAction& a);  // e.g. "09.02"

// D-004 / D-013: an action template returned by legal_actions, with its open parameter still empty.
// Templates must be filled in before they can be valid.
[[nodiscard]] bool is_template(const GameAction& a);

}  // namespace wh40k::core
