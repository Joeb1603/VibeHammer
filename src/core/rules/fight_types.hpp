#pragma once

// Plain data types of the Fight phase (12), kept free of GameState so flow state, actions and events
// can include them. The rules that use them are in fight.hpp.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "core/types/ids.hpp"

namespace wh40k::core {

// 12.04 When a Unit is Selected to Fight: the fight types.
enum class FightType : std::uint8_t {
    Normal,   // 12.05
    Overrun,  // 12.06
};

[[nodiscard]] std::string_view fight_type_name(FightType t);  // also the JSON name
[[nodiscard]] std::string_view fight_type_rule(FightType t);  // "12.05"
[[nodiscard]] std::optional<FightType> fight_type_from_name(std::string_view name);

// 04.02 While Fighting: one target of a model's melee weapon and the attacks declared against it
// (04.02 Splitting Melee Attacks). With a single target, `attacks` is the weapon's A.
struct MeleeTarget {
    UnitId unit;
    int attacks = 0;
    friend bool operator==(const MeleeTarget&, const MeleeTarget&) = default;
};

// 04.01 While Fighting: the one melee weapon (and profile, D-001) a model fights with, and its targets
// (D-021).
struct MeleeDeclaration {
    ModelId model;
    std::size_t weapon_index = 0;
    std::size_t profile_index = 0;
    std::vector<MeleeTarget> targets;
    friend bool operator==(const MeleeDeclaration&, const MeleeDeclaration&) = default;
};

}  // namespace wh40k::core
