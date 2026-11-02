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
    friend bool operator==(const AttacksLost&, const AttacksLost&) = default;
};
struct GameOver {  // D-007
    std::optional<Side> winner;  // nullopt: a draw
    std::string reason;
    friend bool operator==(const GameOver&, const GameOver&) = default;
};
}  // namespace event

using GameEvent =
    std::variant<event::BattleRoundStarted, event::BattleRoundEnded, event::TurnStarted, event::TurnEnded,
                 event::PhaseStarted, event::PhaseEnded, event::UnitRemainedStationary, event::MoveStarted,
                 event::AdvanceRolled, event::HazardRollsMade, event::MortalWoundsInflicted,
                 event::ModelMoveProposed, event::UnitMoveCommitted, event::UnitMoveReverted,
                 event::BattleShockRolled, event::ShootingStarted, event::AttackBatchesGathered, event::AttackBatchSelected, event::HitRollsMade,
                 event::WoundRollsMade, event::AllocationOrderDeclared, event::SaveRollsMade,
                 event::SavesResolved, event::ModelDestroyed, event::UnitDestroyed, event::AttacksLost,
                 event::GameOver>;

[[nodiscard]] std::string_view event_name(const GameEvent& e);  // also the JSON "kind"
[[nodiscard]] std::string_view event_rule(const GameEvent& e);  // e.g. "05.01"

}  // namespace wh40k::core
