#pragma once

// Plain data types of the Shooting phase (10), kept free of GameState so actions and events can
// include them. The rules that use them are in shooting.hpp.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "core/types/ids.hpp"

namespace wh40k::core {

// 10.02 Select Shooting Type.
enum class ShootingType : std::uint8_t {
    Normal,         // 10.04
    Assault,        // 10.05
    CloseQuarters,  // 10.06
    Snap,           // 15.09 Snap Shooting (D-097, M96): only as a rule allows it (15.08 Fire Overwatch), never in 10.02
    Indirect,       // 10.07 Indirect Shooting (M101, D-103): a unit with an [INDIRECT FIRE] weapon (24.19)
};

[[nodiscard]] std::string_view shooting_type_name(ShootingType t);  // also the JSON name
[[nodiscard]] std::string_view shooting_type_rule(ShootingType t);  // "10.04"
[[nodiscard]] std::optional<ShootingType> shooting_type_from_name(std::string_view name);

// 04.01 + 04.02 while shooting: one ranged weapon selected by one model, the profile it fires (D-001:
// chosen per model, one profile per weapon, D-016) and its target.
struct ShotDeclaration {
    ModelId model;
    std::size_t weapon_index = 0;   // into Model::weapons
    std::size_t profile_index = 0;  // into Weapon::profiles
    UnitId target;
    friend bool operator==(const ShotDeclaration&, const ShotDeclaration&) = default;
};

}  // namespace wh40k::core
