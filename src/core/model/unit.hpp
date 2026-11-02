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
    friend bool operator==(const UnitTurnFlags&, const UnitTurnFlags&) = default;
};

// Per-phase selection flags. Reset when the phase starts.
struct UnitPhaseFlags {
    bool selected_to_move = false;  // 09.02
    // D-005: dice rolled for a move stand for the rest of the phase, even if the move is reverted.
    std::optional<int> advance_roll;           // 09.06
    bool desperate_escape_hazards_made = false;  // 09.07
    bool selected_to_shoot = false;  // 10.02; reset when the Shooting phase starts
    friend bool operator==(const UnitPhaseFlags&, const UnitPhaseFlags&) = default;
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
    bool battle_shocked = false;  // 01.07
    UnitTurnFlags turn_flags;
    UnitPhaseFlags phase_flags;

    friend bool operator==(const Unit&, const Unit&) = default;
};

}  // namespace wh40k::core
