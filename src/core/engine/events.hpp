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

#include "core/rules/attack_sequence.hpp"
#include "core/rules/attack_types.hpp"
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
                 event::PhaseStarted, event::PhaseEnded, event::UnitRemainedStationary,
                 event::AttackBatchesGathered, event::AttackBatchSelected, event::HitRollsMade,
                 event::WoundRollsMade, event::AllocationOrderDeclared, event::SaveRollsMade,
                 event::SavesResolved, event::ModelDestroyed, event::UnitDestroyed, event::AttacksLost,
                 event::GameOver>;

[[nodiscard]] std::string_view event_name(const GameEvent& e);  // also the JSON "kind"
[[nodiscard]] std::string_view event_rule(const GameEvent& e);  // e.g. "05.01"

}  // namespace wh40k::core
