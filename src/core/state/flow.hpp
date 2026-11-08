#pragma once

// Where the game is in the battle round -> turn -> phase -> step sequence (07-12), and the
// sub-sequences in progress (a unit selected to move/shoot/fight, an attack being resolved).

#include <cstddef>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/geometry/primitives.hpp"
#include "core/rules/attack_types.hpp"
#include "core/rules/fight_types.hpp"
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
    std::optional<std::vector<std::size_t>> order;  // 05.03 Allocation Order (indices into groups), once declared
    std::optional<std::vector<int>> save_rolls;     // 05.03 Make Save Rolls (unmodified), once made
    friend bool operator==(const BatchInProgress&, const BatchInProgress&) = default;
};

// 24.38 [TWIN-LINKED]: a batch's wound rolls, made, while the attacking unit's player chooses which to re-roll
// (D-072). Only set when at least one of them failed; the batch goes on (05.03) once the re-rolls are made.
struct PendingWoundRerolls {
    std::size_t batch = 0;  // index into AttackFlow::batches
    WoundRolls rolls;       // 05.02, as rolled
    friend bool operator==(const PendingWoundRerolls&, const PendingWoundRerolls&) = default;
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
    // 24.15 [HAZARDOUS]: hazard rolls (06.03) to make for the attacking unit once all of its attacks are
    // resolved, one per [HAZARDOUS] weapon selected (D-071); 0 once they are made. Omitted from JSON when 0.
    int hazard_rolls = 0;
    // 06.02: mortal wounds from failed hazard rolls not yet inflicted on the attacking unit; its player
    // answers OrderCasualties for it (D-013, D-051). Omitted from JSON when 0.
    int pending_mortal_wounds = 0;
    // 24.38 (D-072): the wound rolls waiting for the attacker's RerollWoundRolls answer. Omitted from JSON when absent.
    std::optional<PendingWoundRerolls> wound_rerolls;
    friend bool operator==(const AttackFlow&, const AttackFlow&) = default;
};

// A model's position at the start of the move (03.01: models return here if the move fails).
struct ModelStart {
    ModelId model;
    Position position;
    float facing_deg = 0.0f;  // D-074: its facing then (returned to with the position); omitted from JSON when 0
    friend bool operator==(const ModelStart&, const ModelStart&) = default;
};

// A model's proposed path for the move in progress: start position first, then each straight line.
struct ModelPath {
    ModelId model;
    std::vector<Position> points;
    // D-074 (M65): empty when the path turns the model nowhere (it keeps its start facing); otherwise one facing per
    // point, the start's first, each normalised modulo 180 (03.01: it turns at points[i] to facings[i + 1], then moves
    // to points[i + 1]). Omitted from JSON when empty.
    std::vector<float> facings{};
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
    // 11.04 charge targets, 12.03 pile-in targets or 12.08 selected enemy units (Before Moving).
    std::vector<UnitId> targets;
    std::optional<ConsolidationMode> consolidation_mode;  // 12.08 consolidation moves only
    friend bool operator==(const MoveInProgress&, const MoveInProgress&) = default;
};

// 11.02: a unit that has declared a charge and made its charge roll, until the charge is resolved.
// While no charge move is in progress, the player selects charge targets (or makes no charge move).
struct ChargeInProgress {
    UnitId unit;
    friend bool operator==(const ChargeInProgress&, const ChargeInProgress&) = default;
};

// 12.04 Fight step: which sub-step the sequence is in and who selects next (D-020).
enum class FightSubStep : std::uint8_t {
    FightsFirst,  // Resolve Fights First Combats
    Remaining,    // Resolve Remaining Combats
};

struct FightSequence {
    FightSubStep sub_step = FightSubStep::FightsFirst;
    Side selecting = Side::A;      // the player who selects the next unit to fight
    std::optional<Side> last_pass;  // Appendix: the player who just passed, if the last action was a pass
    friend bool operator==(const FightSequence&, const FightSequence&) = default;
};

// 12.04: a unit selected to fight, until its attacks are resolved.
struct FightInProgress {
    UnitId unit;
    FightType type = FightType::Normal;
    bool overrun_pile_in_pending = false;  // 12.06: the additional pile-in move not yet made or skipped
    bool attacks_started = false;          // 04.03: its attacks are being (or have been) resolved
    bool forced = false;                   // 12.08 Engaging Consolidation: selected by the opponent's move
    friend bool operator==(const FightInProgress&, const FightInProgress&) = default;
};

struct FlowState {
    Stage stage = Stage::NotStarted;
    int battle_round = 0;   // 1-based once started
    int turn_in_round = 0;  // 0: the first turn of the battle round, 1: the second
    std::optional<Activation> activation;
    std::optional<AttackFlow> attack;
    std::optional<MoveInProgress> move;
    std::optional<ChargeInProgress> charge;
    // 12.02 / 12.07: the player whose turn it is resolves all of their moves first, then their
    // opponent. Indexed by side_index; reset when the step starts.
    std::array<bool, 2> step_done{};
    std::optional<FightSequence> fight_sequence;  // 12.04, while the Fight step runs
    std::optional<FightInProgress> fight;
    std::vector<UnitId> forced_fights;  // 12.08: units the opponent must still select to fight
    friend bool operator==(const FlowState&, const FlowState&) = default;
};

// D-007: the end of the game.
struct GameResult {
    std::optional<Side> winner;  // nullopt: a draw
    std::string reason;          // rule-cited
    friend bool operator==(const GameResult&, const GameResult&) = default;
};

}  // namespace wh40k::core
