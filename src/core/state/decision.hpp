#pragma once

// PendingDecision: the single source of "who must act now, and how". Decisions are small; the
// options are computed by legal_actions (engine/engine.hpp).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "core/types/ids.hpp"

namespace wh40k::core {

// D-095 (M94): the dice roll a RollDice decision waits for, one per roll point (05: "if there is more than one dice to
// roll, make all of those rolls simultaneously"). Appended at the end only (JSON names, the bots' state_hash).
enum class RollKind : std::uint8_t {
    Advance,        // 09.06 Advance roll (D6)
    Charge,         // 11.02 Make Charge Roll (2D6)
    BattleShock,    // 01.07 battle-shock roll (2D6): the 08.03 step, or after a Desperate Escape (09.07)
    Hazard,         // 06.03 hazard rolls: Desperate Escape (09.07) or [HAZARDOUS] (24.15)
    RandomAttacks,  // 04.03 Gather Attack Dice: an attack batch's random A (D-069)
    Hit,            // 05.01 Hit Rolls
    Wound,          // 05.02 Wound Rolls
    Save,           // 05.03 Make Save Rolls (the opposing player)
    Damage,         // 05.04 Resolve Damage: an attack batch's random D, one per attack that inflicts damage (D-069)
};

[[nodiscard]] std::string_view roll_kind_name(RollKind roll);  // "Hit"; also the JSON name
[[nodiscard]] std::optional<RollKind> roll_kind_from_name(std::string_view name);
[[nodiscard]] std::string_view roll_rule(RollKind roll);  // the roll's own rule, e.g. "05.01"

// D-096 (M95): when a reaction window comes (01.03.02, core/state/stratagems.hpp). Appended at the end only.
enum class WindowTiming : std::uint8_t {
    AfterRoll,     // just after a player makes a roll (15.02 Command Re-roll)
    BeforeRoll,    // just before a player makes a roll (15.04 Insane Bravery)
    EndOfPhase,    // D-097 (M96): the end of the opponent's Movement (15.08) or Charge (15.11) phase
    AfterAttacks,  // D-097: just after an enemy unit has resolved its attacks in the Fight step (15.12)
};

[[nodiscard]] std::string_view window_timing_name(WindowTiming t);  // "AfterRoll"; also the JSON name
[[nodiscard]] std::optional<WindowTiming> window_timing_from_name(std::string_view name);

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
// 24.38 [TWIN-LINKED]: the attacking unit's player chooses which of a batch's wound rolls to re-roll, asked only when
// at least one of them failed (D-072).
struct RerollWoundRolls {
    UnitId attacker;
    friend bool operator==(const RerollWoundRolls&, const RerollWoundRolls&) = default;
};
// 03.02 / D-081 (M74): set up one of your units (a deployment by hand, the sides alternating). The deciding side is
// the one that sets up next (side_to_deploy, core/state/deployment.hpp); it may be the opposing player (01.03).
struct DeployUnits {
    friend bool operator==(const DeployUnits&, const DeployUnits&) = default;
};
// 04.03.03 Target No Longer Eligible or Viable (app-only, D-093, M92; replaces D-012's "lost"): one or more batches'
// target is no longer an eligible target and at least one of their weapons has another eligible target, so the
// attacking unit's player can select new targets for them. Weapons not given one are lost.
struct RetargetAttacks {
    UnitId attacker;
    friend bool operator==(const RetargetAttacks&, const RetargetAttacks&) = default;
};
// D-095 (M94): make a dice roll (RollKind). `unit` is the unit the roll is for, and its controlling player rolls: the
// moving, charging, tested or attacking unit, but for a Save the target unit (05.03: the opposing player makes the save
// rolls). A Damage roll is the attacking unit's (D-069). Its one answer is action::RollDice with the same roll and unit.
struct RollDice {
    RollKind roll = RollKind::Hit;
    UnitId unit;
    friend bool operator==(const RollDice&, const RollDice&) = default;
};
// D-096 (M95): a reaction window (01.03.02): the deciding side can use one of its stratagems that this timing allows
// (15.01), or decline. `unit` is the unit the window's roll is for; the roll itself and its dice are in
// FlowState::window. Its answers are action::DeclineStratagems (listed first) and action::UseStratagem. D-097 (M96): an
// EndOfPhase window has no unit; an AfterAttacks window's unit is the enemy unit that has just fought.
struct UseStratagems {
    WindowTiming timing = WindowTiming::AfterRoll;
    std::optional<UnitId> unit;
    friend bool operator==(const UseStratagems&, const UseStratagems&) = default;
};
// D-097 (M96): 15.08 Fire Overwatch's unit shoots using snap shooting (15.09). Its one answer is action::Shoot of type
// Snap for that unit (a template, D-016).
struct SnapShoot {
    UnitId unit;
    friend bool operator==(const SnapShoot&, const SnapShoot&) = default;
};
}  // namespace decision

// Appended at the end only: the bots' state_hash hashes the index (D-054).
using DecisionKind = std::variant<decision::MoveUnits, decision::MoveModels, decision::Shoot, decision::Charge,
                                  decision::SelectChargeTargets, decision::PileIn, decision::OverrunPileIn,
                                  decision::SelectUnitToFight, decision::DeclareMeleeAttacks,
                                  decision::Consolidate, decision::ForcedFight, decision::RestoreCoherency,
                                  decision::SelectAttackBatch, decision::DeclareAllocationOrder,
                                  decision::OrderCasualties, decision::RerollWoundRolls, decision::DeployUnits,
                                  decision::RetargetAttacks, decision::RollDice, decision::UseStratagems,
                                  decision::SnapShoot>;

struct PendingDecision {
    Side side = Side::A;  // the player who must act (not necessarily the active player, 01.03)
    DecisionKind kind;
    friend bool operator==(const PendingDecision&, const PendingDecision&) = default;
};

// The rule the decision comes from, e.g. "09.02". OrderCasualties gives 05.04 here; the state-aware form below
// tells 06.02's mortal wounds apart.
[[nodiscard]] std::string_view decision_rule(const PendingDecision& d);
// The decision kind's name, e.g. "MoveUnits"; also its JSON name.
[[nodiscard]] std::string_view decision_name(const DecisionKind& kind);
// Human-readable, e.g. "Side A: select a unit to move (09.02)".
[[nodiscard]] std::string describe(const PendingDecision& d);

struct GameState;
// M61: the rule as it applies in `state`. OrderCasualties is 06.02 when the attack in progress has mortal wounds to
// inflict ([HAZARDOUS], 24.15) or no attack is in progress (Desperate Escape, 09.07), else 05.04 (D-071, D-072).
// D-095 (M94): a Hazard roll is 24.15 during an attack and 09.07 otherwise; a BattleShock roll is 08.03 in the
// Battle-shock step and 09.07 otherwise; every other roll its roll_rule.
[[nodiscard]] std::string_view decision_rule(const GameState& state, const PendingDecision& d);
// describe() with that rule.
[[nodiscard]] std::string describe(const GameState& state, const PendingDecision& d);

}  // namespace wh40k::core
