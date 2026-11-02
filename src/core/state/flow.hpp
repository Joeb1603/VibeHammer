#pragma once

// Where the game is in the battle round -> turn -> phase -> step sequence (07-12), and the
// sub-sequences in progress (a unit selected to move/shoot/fight, an attack being resolved).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/geometry/primitives.hpp"
#include "core/rules/attack_types.hpp"
#include "core/rules/move_types.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

enum class Phase : std::uint8_t { None, Command, Movement, Shooting, Charge, Fight };

// One value per position in the sequence, in order of play.
enum class Stage : std::uint8_t {
    NotStarted,
    StartOfBattleRound,    // 07.01
    StartOfTurn,           // 07.02 Start of Turn step
    StartOfCommandPhase,   // 08.01
    GainCoreCP,            // 08.02
    BattleShock,           // 08.03
    CommandAbilities,      // 08.04
    EndOfCommandPhase,     // 08.05
    StartOfMovementPhase,  // 09.01
    MoveUnits,             // 09.02
    EndOfMovementPhase,    // 09.03
    StartOfShootingPhase,  // 10.01
    Shoot,                 // 10.02
    EndOfShootingPhase,    // 10.03
    StartOfChargePhase,    // 11.01
    Charge,                // 11.02
    EndOfChargePhase,      // 11.03
    StartOfFightPhase,     // 12.01
    PileIn,                // 12.02
    Fight,                 // 12.04
    Consolidate,           // 12.07
    EndOfFightPhase,       // 12.09
    EndOfTurn,             // 07.02 End of Turn step
    EndOfBattleRound,      // 07.03
    GameOver,              // D-007
};

inline constexpr std::size_t kStageCount = static_cast<std::size_t>(Stage::GameOver) + 1;

[[nodiscard]] Phase phase_of(Stage stage);
[[nodiscard]] std::string_view stage_name(Stage stage);  // "MoveUnits"; also the JSON name
[[nodiscard]] std::string_view stage_rule(Stage stage);  // "09.02"
[[nodiscard]] std::optional<Stage> stage_from_name(std::string_view name);
[[nodiscard]] std::string_view phase_name(Phase phase);  // "Movement"; also the JSON name
[[nodiscard]] std::string_view phase_rule(Phase phase);  // "09"
[[nodiscard]] std::optional<Phase> phase_from_name(std::string_view name);

// 01.03: a unit selected to move, shoot or fight; its controlling player is the active player until
// that move ends or those attacks are resolved.
enum class ActivationKind : std::uint8_t { Move, Shoot, Fight };

struct Activation {
    UnitId unit;
    ActivationKind kind = ActivationKind::Move;
    friend bool operator==(const Activation&, const Activation&) = default;
};

// The attack batch currently being resolved (05.01-05.04), after its hit and wound rolls.
struct BatchInProgress {
    std::size_t batch = 0;                // index into AttackFlow::batches
    int wounds = 0;                       // 05.02 result (> 0)
    std::vector<AllocationGroup> groups;  // 05.03 Create Groups, in unit order
    std::vector<std::size_t> order;       // 05.03 Allocation Order (indices into groups); empty until declared
    std::vector<int> save_rolls;          // 05.03 Make Save Rolls; empty until made
    friend bool operator==(const BatchInProgress&, const BatchInProgress&) = default;
};

// 04.03 Resolve Attacks in progress for one attacking unit.
struct AttackFlow {
    UnitId attacker;
    AttackContext context;
    std::vector<WeaponSelection> selections;
    std::vector<AttackBatch> batches;
    std::vector<bool> batch_done;          // resolved, or lost (D-012)
    std::optional<UnitId> current_target;  // 04.03 Select Enemy Unit
    std::optional<BatchInProgress> current;
    friend bool operator==(const AttackFlow&, const AttackFlow&) = default;
};

// A model's position at the start of the move (03.01: models return here if the move fails).
struct ModelStart {
    ModelId model;
    Position position;
    friend bool operator==(const ModelStart&, const ModelStart&) = default;
};

// A model's proposed path for the move in progress: start position first, then each straight line.
struct ModelPath {
    ModelId model;
    std::vector<Position> points;
    friend bool operator==(const ModelPath&, const ModelPath&) = default;
};

// 09.02 / 03.01: a unit selected to make a move (other than Remain Stationary), moving model by model.
struct MoveInProgress {
    UnitId unit;
    MoveType type = MoveType::Normal;
    std::optional<FallBackMode> mode;  // 09.07 fall-back moves only
    std::vector<ModelStart> starts;    // every model's position when the move started
    std::vector<ModelPath> paths;      // proposed paths, one per moved model
    int pending_mortal_wounds = 0;     // 09.07 Desperate Escape hazard rolls (06.03) not yet allocated
    std::vector<UnitId> charge_targets;  // 11.04 Before Moving (charge moves only)
    friend bool operator==(const MoveInProgress&, const MoveInProgress&) = default;
};

// 11.02: a unit that has declared a charge and made its charge roll, until the charge is resolved.
// While no charge move is in progress, the player selects charge targets (or makes no charge move).
struct ChargeInProgress {
    UnitId unit;
    friend bool operator==(const ChargeInProgress&, const ChargeInProgress&) = default;
};

struct FlowState {
    Stage stage = Stage::NotStarted;
    int battle_round = 0;   // 1-based once started
    int turn_in_round = 0;  // 0: the first turn of the battle round, 1: the second
    std::optional<Activation> activation;
    std::optional<AttackFlow> attack;
    std::optional<MoveInProgress> move;
    std::optional<ChargeInProgress> charge;
    friend bool operator==(const FlowState&, const FlowState&) = default;
};

// D-007: the end of the game.
struct GameResult {
    std::optional<Side> winner;  // nullopt: a draw
    std::string reason;          // rule-cited
    friend bool operator==(const GameResult&, const GameResult&) = default;
};

}  // namespace wh40k::core
