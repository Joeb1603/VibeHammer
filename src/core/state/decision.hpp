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
// 03.01 / 09.05-09.07: move the selected unit's models one at a time, then commit or reset the move.
struct MoveModels {
    UnitId unit;
    friend bool operator==(const MoveModels&, const MoveModels&) = default;
};
// 10.02 Shoot: select a unit to shoot, or end the Shoot step.
struct Shoot {
    friend bool operator==(const Shoot&, const Shoot&) = default;
};
// 11.02 Charge: declare a charge, or end the Charge step.
struct Charge {
    friend bool operator==(const Charge&, const Charge&) = default;
};
// 11.04 Before Moving / 11.02 Attempt Charge: after the charge roll, select the charge targets and
// start the charge move, or make no charge move.
struct SelectChargeTargets {
    UnitId unit;
    friend bool operator==(const SelectChargeTargets&, const SelectChargeTargets&) = default;
};
// 12.02 Pile In: make a pile-in move with an eligible unit, or end this player's pile-in moves.
struct PileIn {
    friend bool operator==(const PileIn&, const PileIn&) = default;
};
// 12.06 Overrun Fight: make the additional pile-in move, or skip it.
struct OverrunPileIn {
    UnitId unit;
    friend bool operator==(const OverrunPileIn&, const OverrunPileIn&) = default;
};
// 12.04 Fight: select a friendly unit to fight and its fight type, or pass (Appendix).
struct SelectUnitToFight {
    friend bool operator==(const SelectUnitToFight&, const SelectUnitToFight&) = default;
};
// 04.01 / 04.02 While Fighting: each model's melee weapon and targets (D-021).
struct DeclareMeleeAttacks {
    UnitId unit;
    friend bool operator==(const DeclareMeleeAttacks&, const DeclareMeleeAttacks&) = default;
};
// 12.07 Consolidate: make a consolidation move with an eligible unit, or end this player's moves.
struct Consolidate {
    friend bool operator==(const Consolidate&, const Consolidate&) = default;
};
// 12.08 Engaging Consolidation After Moving: select each enemy-engaged unit that hasn't fought.
struct ForcedFight {
    friend bool operator==(const ForcedFight&, const ForcedFight&) = default;
};
// 03.03 Regaining Coherency (End of Turn step): remove one model from an incoherent unit.
struct RestoreCoherency {
    UnitId unit;
    friend bool operator==(const RestoreCoherency&, const RestoreCoherency&) = default;
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
// 05.04 Select Model (attacks) or 06.02 Select Model (mortal wounds), answered once with a casualty
// priority (D-013). Owned by the controlling player of the unit losing models.
struct OrderCasualties {
    UnitId target;
    friend bool operator==(const OrderCasualties&, const OrderCasualties&) = default;
};
}  // namespace decision

using DecisionKind = std::variant<decision::MoveUnits, decision::MoveModels, decision::Shoot, decision::Charge,
                                  decision::SelectChargeTargets, decision::PileIn, decision::OverrunPileIn,
                                  decision::SelectUnitToFight, decision::DeclareMeleeAttacks,
                                  decision::Consolidate, decision::ForcedFight, decision::RestoreCoherency,
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
