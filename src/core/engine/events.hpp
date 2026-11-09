#pragma once

// GameEvent: what happened, for the UI's animation and the dice/combat log. Each event type has a
// rule number (event_rule). Events are returned by apply_action/start_game; they are not part of
// GameState (D-014).

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/geometry/primitives.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/attack_types.hpp"
#include "core/rules/fight_types.hpp"
#include "core/rules/leadership.hpp"
#include "core/rules/mortal_wounds.hpp"
#include "core/rules/move_types.hpp"
#include "core/rules/shooting_types.hpp"
#include "core/state/flow.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

namespace event {
struct BattleRoundStarted {  // 07.01
    int round = 0;
    friend bool operator==(const BattleRoundStarted&, const BattleRoundStarted&) = default;
};
struct BattleRoundEnded {  // 07.03
    int round = 0;
    friend bool operator==(const BattleRoundEnded&, const BattleRoundEnded&) = default;
};
struct TurnStarted {  // 07.02
    int round = 0;
    Side side = Side::A;
    friend bool operator==(const TurnStarted&, const TurnStarted&) = default;
};
struct TurnEnded {  // 07.02
    int round = 0;
    Side side = Side::A;
    friend bool operator==(const TurnEnded&, const TurnEnded&) = default;
};
struct PhaseStarted {  // 08-12
    Phase phase = Phase::None;
    Side side = Side::A;  // whose turn
    friend bool operator==(const PhaseStarted&, const PhaseStarted&) = default;
};
struct PhaseEnded {
    Phase phase = Phase::None;
    Side side = Side::A;
    friend bool operator==(const PhaseEnded&, const PhaseEnded&) = default;
};
struct UnitRemainedStationary {  // 09.04
    UnitId unit;
    friend bool operator==(const UnitRemainedStationary&, const UnitRemainedStationary&) = default;
};
struct MoveStarted {  // 09.02: the unit is selected to make this move
    UnitId unit;
    MoveType move_type = MoveType::Normal;
    std::optional<FallBackMode> mode;
    friend bool operator==(const MoveStarted&, const MoveStarted&) = default;
};
struct AdvanceRolled {  // 09.06 Before Moving (D-005: reused if the unit advances again this phase)
    UnitId unit;
    int roll = 0;
    bool reused = false;
    friend bool operator==(const AdvanceRolled&, const AdvanceRolled&) = default;
};
struct HazardRollsMade {  // 09.07 Desperate Escape, 06.03
    UnitId unit;
    HazardRolls rolls;
    friend bool operator==(const HazardRollsMade&, const HazardRollsMade&) = default;
};
struct MortalWoundsInflicted {  // 06.02
    UnitId unit;
    MortalWoundsResult result;
    friend bool operator==(const MortalWoundsInflicted&, const MortalWoundsInflicted&) = default;
};
struct ModelMoveProposed {  // 03.01: a model's path (start position first)
    UnitId unit;
    ModelId model;
    std::vector<Position> path;
    float distance_mm = 0.0f;
    // D-074 (M65): as ModelPath::facings: empty when the path turns the model nowhere, otherwise one per path point
    // (the start's first). Omitted from JSON when empty.
    std::vector<float> facings{};
    friend bool operator==(const ModelMoveProposed&, const ModelMoveProposed&) = default;
};
struct UnitMoveCommitted {  // 03.01 Ending a Move: the move ended
    UnitId unit;
    MoveType move_type = MoveType::Normal;
    friend bool operator==(const UnitMoveCommitted&, const UnitMoveCommitted&) = default;
};
struct UnitMoveReverted {  // 03.01: models returned to their start positions; not selected to make that move
    UnitId unit;
    std::vector<std::string> reasons;  // rule-cited; empty when the player reset the move
    friend bool operator==(const UnitMoveReverted&, const UnitMoveReverted&) = default;
};
struct BattleShockRolled {  // 01.07 (e.g. after a Desperate Escape, 09.07)
    UnitId unit;
    BattleShockRoll roll;
    friend bool operator==(const BattleShockRolled&, const BattleShockRolled&) = default;
};
struct ShootingStarted {  // 10.02: the unit is selected to shoot, with its weapons and targets (04.01, 04.02)
    UnitId unit;
    ShootingType type = ShootingType::Normal;
    std::vector<WeaponSelection> selections;
    friend bool operator==(const ShootingStarted&, const ShootingStarted&) = default;
};
struct ChargeDeclared {  // 11.02 Declare Charge
    UnitId unit;
    friend bool operator==(const ChargeDeclared&, const ChargeDeclared&) = default;
};
struct ChargeRolled {  // 11.02 Make Charge Roll; the enemy units that can be charge targets (11.04)
    UnitId unit;
    TwoD6 roll;
    std::vector<UnitId> eligible_targets;
    friend bool operator==(const ChargeRolled&, const ChargeRolled&) = default;
};
struct ChargeTargetsSelected {  // 11.04 Before Moving
    UnitId unit;
    std::vector<UnitId> targets;
    friend bool operator==(const ChargeTargetsSelected&, const ChargeTargetsSelected&) = default;
};
struct ChargeResolved {  // 11.02 Attempt Charge: the charge is resolved
    UnitId unit;
    bool charge_move_made = false;  // true: the unit made its charge move (Fights First, 11.04)
    std::string reason;             // rule-cited when no charge move was made; empty otherwise
    friend bool operator==(const ChargeResolved&, const ChargeResolved&) = default;
};
struct PileInStarted {  // 12.03 Before Moving: the pile-in targets
    UnitId unit;
    std::vector<UnitId> targets;
    friend bool operator==(const PileInStarted&, const PileInStarted&) = default;
};
struct UnitSelectedToFight {  // 12.04: the unit is selected to fight, with its fight type
    UnitId unit;
    FightType type = FightType::Normal;
    bool forced = false;  // 12.08 Engaging Consolidation
    friend bool operator==(const UnitSelectedToFight&, const UnitSelectedToFight&) = default;
};
struct FightPassed {  // Appendix: Eligible to Fight, but Unable to Fight
    Side side = Side::A;
    friend bool operator==(const FightPassed&, const FightPassed&) = default;
};
struct MeleeAttacksDeclared {  // 04.01 / 04.02 While Fighting: weapons and targets (empty: no attacks)
    UnitId unit;
    std::vector<WeaponSelection> selections;
    friend bool operator==(const MeleeAttacksDeclared&, const MeleeAttacksDeclared&) = default;
};
struct FightStepEnded {  // 12.04 / Appendix: why the Fight step ended
    std::string reason;
    friend bool operator==(const FightStepEnded&, const FightStepEnded&) = default;
};
struct ConsolidationStarted {  // 12.08 Before Moving: the mode and the selected enemy units
    UnitId unit;
    ConsolidationMode mode = ConsolidationMode::Ongoing;
    std::vector<UnitId> targets;
    friend bool operator==(const ConsolidationStarted&, const ConsolidationStarted&) = default;
};
struct UnitsForcedToFight {  // 12.08 Engaging Consolidation After Moving
    std::vector<UnitId> units;
    friend bool operator==(const UnitsForcedToFight&, const UnitsForcedToFight&) = default;
};
struct CommandPointGained {  // 08.02 Gain Core CP
    Side side = Side::A;
    int total = 0;  // the side's CP after gaining
    friend bool operator==(const CommandPointGained&, const CommandPointGained&) = default;
};
struct BattleShockEnded {  // 08.03: the unit was battle-shocked and passed its battle-shock roll
    UnitId unit;
    friend bool operator==(const BattleShockEnded&, const BattleShockEnded&) = default;
};
struct ModelRemovedForCoherency {  // 03.03 Regaining Coherency: destroyed, with no on-death rules
    UnitId unit;
    ModelId model;
    friend bool operator==(const ModelRemovedForCoherency&, const ModelRemovedForCoherency&) = default;
};
struct AttackBatchesGathered {  // 04.03: the attacking unit's attacks, grouped (D-006)
    UnitId attacker;
    std::vector<AttackBatch> batches;
    friend bool operator==(const AttackBatchesGathered&, const AttackBatchesGathered&) = default;
};
struct AttackBatchSelected {  // 04.03 Gather Attack Dice
    UnitId attacker;
    std::size_t batch = 0;
    UnitId target;
    int attack_dice = 0;
    friend bool operator==(const AttackBatchSelected&, const AttackBatchSelected&) = default;
};
struct HitRollsMade {  // 05.01
    std::size_t batch = 0;
    HitRolls rolls;
    friend bool operator==(const HitRollsMade&, const HitRollsMade&) = default;
};
struct WoundRollsMade {  // 05.02
    std::size_t batch = 0;
    WoundRolls rolls;
    friend bool operator==(const WoundRollsMade&, const WoundRollsMade&) = default;
};
struct AllocationOrderDeclared {  // 05.03: groups in allocation order
    UnitId target;
    std::vector<AllocationGroup> groups;
    friend bool operator==(const AllocationOrderDeclared&, const AllocationOrderDeclared&) = default;
};
struct SaveRollsMade {  // 05.03 Make Save Rolls (unmodified, roll order)
    UnitId target;
    std::vector<int> rolls;
    friend bool operator==(const SaveRollsMade&, const SaveRollsMade&) = default;
};
struct SavesResolved {  // 05.04: one step per save roll, lowest first; excess attacks lost
    UnitId target;
    InflictDamageResult result;
    friend bool operator==(const SavesResolved&, const SavesResolved&) = default;
};
struct ModelDestroyed {  // Rules Appendix: Destroyed
    UnitId unit;
    ModelId model;
    friend bool operator==(const ModelDestroyed&, const ModelDestroyed&) = default;
};
struct UnitDestroyed {  // Rules Appendix: Destroyed
    UnitId unit;
    friend bool operator==(const UnitDestroyed&, const UnitDestroyed&) = default;
};
struct AttacksLost {  // D-012: the target is no longer eligible; the batch's attacks are lost
    UnitId target;
    std::size_t batch = 0;
    int attack_dice = 0;
    std::vector<RandomAttackDice> random_attacks{};  // D-069: its random A, lost unrolled (JSON omitted when empty)
    friend bool operator==(const AttacksLost&, const AttacksLost&) = default;
};
struct GameOver {  // D-007
    std::optional<Side> winner;  // nullopt: a draw
    std::string reason;
    // D-076 (M67): both sides' points destroyed, when the scenario decides the game on them (JSON omitted otherwise).
    std::optional<PointsTally> points{};
    friend bool operator==(const GameOver&, const GameOver&) = default;
};
struct WoundRollsRerolled {  // 24.38 [TWIN-LINKED], D-011 (D-072): the attacker's re-rolls of a batch's wound rolls
    std::size_t batch = 0;
    std::vector<WoundReroll> rerolls;  // each re-rolled die (index, before, after), in index order; empty: kept
    WoundRolls rolls;                  // every wound roll of the batch after the re-rolls
    friend bool operator==(const WoundRollsRerolled&, const WoundRollsRerolled&) = default;
};
struct AttackDiceRolled {  // 04.03 Gather Attack Dice, D-069: a batch's random A, one per model and weapon
    std::size_t batch = 0;
    std::vector<AttackDiceRoll> rolls;  // in selection order; each roll's bonus dice added after it
    int attack_dice = 0;                // the batch's attack dice once gathered (fixed ones included)
    friend bool operator==(const AttackDiceRolled&, const AttackDiceRolled&) = default;
};
struct DeploymentStarted {  // D-081 (M74): the armies are set up by hand, the sides alternating
    Side first = Side::A;   // the side that sets up first (the one taking the second turn, if it has units)
    friend bool operator==(const DeploymentStarted&, const DeploymentStarted&) = default;
};
struct UnitSetUp {  // 03.02 (D-081): the unit is set up on the battlefield
    UnitId unit;
    Side side = Side::A;
    friend bool operator==(const UnitSetUp&, const UnitSetUp&) = default;
};
struct UnitNotSetUp {  // D-081: no legal spot was left; the unit counts as destroyed (UnitDestroyed follows)
    UnitId unit;
    Side side = Side::A;
    friend bool operator==(const UnitNotSetUp&, const UnitNotSetUp&) = default;
};
}  // namespace event

// New alternatives are appended at the end, so every existing index stays the same.
using GameEvent =
    std::variant<event::BattleRoundStarted, event::BattleRoundEnded, event::TurnStarted, event::TurnEnded,
                 event::PhaseStarted, event::PhaseEnded, event::UnitRemainedStationary, event::MoveStarted,
                 event::AdvanceRolled, event::HazardRollsMade, event::MortalWoundsInflicted,
                 event::ModelMoveProposed, event::UnitMoveCommitted, event::UnitMoveReverted,
                 event::BattleShockRolled, event::ShootingStarted, event::ChargeDeclared, event::ChargeRolled,
                 event::ChargeTargetsSelected, event::ChargeResolved, event::PileInStarted,
                 event::UnitSelectedToFight, event::FightPassed, event::MeleeAttacksDeclared, event::FightStepEnded,
                 event::ConsolidationStarted, event::UnitsForcedToFight, event::CommandPointGained,
                 event::BattleShockEnded, event::ModelRemovedForCoherency, event::AttackBatchesGathered, event::AttackBatchSelected, event::HitRollsMade,
                 event::WoundRollsMade, event::AllocationOrderDeclared, event::SaveRollsMade,
                 event::SavesResolved, event::ModelDestroyed, event::UnitDestroyed, event::AttacksLost,
                 event::GameOver, event::WoundRollsRerolled, event::AttackDiceRolled>;

[[nodiscard]] std::string_view event_name(const GameEvent& e);  // also the JSON "kind"
[[nodiscard]] std::string_view event_rule(const GameEvent& e);  // e.g. "05.01"

}  // namespace wh40k::core
