#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/geometry/primitives.hpp"
#include "core/model/characteristics.hpp"
#include "core/model/weapon.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

// 01.02: a model. Its base is part of the model for all rules purposes.
struct Model {
    ModelId id;
    std::string type_name;  // datasheet model type, e.g. "Intercessor Sergeant"
    Characteristics characteristics;
    Base base;
    Position position;
    // D-074 (M65): the base's facing in degrees. An oval's is stored modulo 180, in (-90, 90]
    // (footprint_facing_deg); a round base has no facing, so its facing is always 0.
    float facing_deg = 0.0f;
    int wounds_remaining = 0;  // 02.02: destroyed at 0 or fewer
    std::vector<Weapon> weapons;

    friend bool operator==(const Model&, const Model&) = default;
};

// 02.03: datasheet abilities. Loaded and displayed; unit-specific abilities are out of scope in V1.
struct AbilityText {
    std::string name;
    std::string text;
    friend bool operator==(const AbilityText&, const AbilityText&) = default;
};

// Per-turn movement flags consumed by later phases (M7+). Reset at the start of each turn.
struct UnitTurnFlags {
    bool advanced = false;
    bool fell_back = false;
    bool moved_over_3in = false;  // 24.16 [HEAVY]
    bool made_charge_move = false;  // 11.04 (12.02 / 12.04 refer to it)
    bool fights_first = false;      // 11.04 After Moving: every model has Fights First (24.13) until end of turn
    friend bool operator==(const UnitTurnFlags&, const UnitTurnFlags&) = default;
};

// Per-phase selection flags. Reset when the phase starts.
struct UnitPhaseFlags {
    bool selected_to_move = false;  // 09.02
    // D-005: dice rolled for a move stand for the rest of the phase, even if the move is reverted.
    std::optional<int> advance_roll;           // 09.06
    bool desperate_escape_hazards_made = false;  // 09.07
    bool selected_to_shoot = false;  // 10.02; reset when the Shooting phase starts
    bool declared_charge = false;    // 11.02; reset when the Charge phase starts
    std::optional<int> charge_roll;  // 11.02 Make Charge Roll (2D6 total); stands for the phase (D-018)
    // Fight phase (12); reset when the Fight phase starts.
    bool piled_in = false;                      // 12.02: one pile-in move per unit in the Pile In step
    bool engaged_at_fight_step_start = false;   // 12.04 / 12.06
    bool selected_to_fight = false;             // 12.04
    bool eligible_to_fight_this_phase = false;  // 12.08 Eligible If
    bool selected_for_overrun = false;          // 12.03 Eligible If
    bool forced_to_fight = false;               // 12.08 Engaging Consolidation: became eligible to fight
    bool consolidated = false;                  // 12.07: one consolidation move per unit
    friend bool operator==(const UnitPhaseFlags&, const UnitPhaseFlags&) = default;
};

// D-088 (M88): the record behind 13.09 Hidden: whether the unit made one or more ranged attacks during this turn and
// during the previous turn. Kept only on a battlefield with terrain (the only place 13.09 reads it): a Shoot action
// sets this_turn; leaving the End of Turn step, previous_turn takes this_turn and this_turn clears.
struct RangedAttackRecord {
    bool this_turn = false;
    bool previous_turn = false;
    friend bool operator==(const RangedAttackRecord&, const RangedAttackRecord&) = default;
};

// 01.02: a unit of one or more models.
struct Unit {
    UnitId id;
    Side side = Side::A;
    std::string name;  // 02.01 datasheet name
    std::vector<std::string> faction_keywords;  // 02.05
    std::vector<std::string> keywords;          // 02.05
    std::vector<AbilityText> abilities;
    std::vector<Model> models;
    int starting_strength = 0;  // number of models when added to the army (Rules Appendix)
    // D-076 (M67): the unit's points (scenario "points"; 0: none given) and its starting wounds, the sum of the W
    // characteristic of every model as set up (Starting Strength, Rules Appendix; 0 when it has no points). Both are
    // fixed for the game and omitted from the JSON at 0, so older files are unchanged.
    int points = 0;
    int starting_wounds = 0;
    bool battle_shocked = false;  // 01.07
    UnitTurnFlags turn_flags;
    UnitPhaseFlags phase_flags;
    // D-088 (M88): JSON "ranged_attacks", omitted when both are false (always, without terrain).
    RangedAttackRecord ranged_attacks;

    friend bool operator==(const Unit&, const Unit&) = default;
};

}  // namespace wh40k::core
