#pragma once

// PendingDecision: the single source of "who must act now, and how". Decisions are small; the
// options are computed by legal_actions (engine/engine.hpp).

#include <string>
#include <string_view>
#include <variant>

#include "core/types/ids.hpp"

namespace wh40k::core {

namespace decision {
// 09.02 Move Units: select a friendly unit that has not been selected to move, and its move type.
struct MoveUnits {
    friend bool operator==(const MoveUnits&, const MoveUnits&) = default;
};
// 10.02 Shoot: select a unit to shoot, or end the Shoot step.
struct Shoot {
    friend bool operator==(const Shoot&, const Shoot&) = default;
};
// 11.02 Charge: declare a charge, or end the Charge step.
struct Charge {
    friend bool operator==(const Charge&, const Charge&) = default;
};
// 04.03 Select Enemy Unit / Gather Attack Dice: the attacker picks the next batch to resolve.
struct SelectAttackBatch {
    UnitId attacker;
    friend bool operator==(const SelectAttackBatch&, const SelectAttackBatch&) = default;
};
// 05.03 Allocation Order: the opposing player orders the target's allocation groups.
struct DeclareAllocationOrder {
    UnitId target;
    friend bool operator==(const DeclareAllocationOrder&, const DeclareAllocationOrder&) = default;
};
// 05.04 Select Model, answered once per batch with a casualty priority (D-013).
struct OrderCasualties {
    UnitId target;
    friend bool operator==(const OrderCasualties&, const OrderCasualties&) = default;
};
}  // namespace decision

using DecisionKind = std::variant<decision::MoveUnits, decision::Shoot, decision::Charge,
                                  decision::SelectAttackBatch, decision::DeclareAllocationOrder,
                                  decision::OrderCasualties>;

struct PendingDecision {
    Side side = Side::A;  // the player who must act (not necessarily the active player, 01.03)
    DecisionKind kind;
    friend bool operator==(const PendingDecision&, const PendingDecision&) = default;
};

// The rule the decision comes from, e.g. "09.02".
[[nodiscard]] std::string_view decision_rule(const PendingDecision& d);
// The decision kind's name, e.g. "MoveUnits"; also its JSON name.
[[nodiscard]] std::string_view decision_name(const DecisionKind& kind);
// Human-readable, e.g. "Side A: select a unit to move (09.02)".
[[nodiscard]] std::string describe(const PendingDecision& d);

}  // namespace wh40k::core
