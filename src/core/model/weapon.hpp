#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/model/dice_expr.hpp"

namespace wh40k::core {

// Weapon abilities (24.xx), parsed here. The rules apply them in rules/ and engine/; D-026 / D-071 refuse a
// profile with one the engine does not implement (unimplemented_ability_problem, rules/attacks.hpp).
namespace ability {
// 24.01 (M100, D-102): "If a weapon ability is followed by one or more keywords ... that ability only applies if the
// target unit has one or more of those keywords" ("[LETHAL HITS: VEHICLE]"). `keywords` empty: the ability always
// applies. `non`: the "non-X" form (a target unit with none of them), as [ANTI] reads it (D-101).
struct TargetCondition {
    std::vector<std::string> keywords;
    bool non = false;
    [[nodiscard]] bool always() const noexcept { return keywords.empty(); }
    friend bool operator==(const TargetCondition&, const TargetCondition&) = default;
};
struct Assault {  // 24.04
    friend bool operator==(const Assault&, const Assault&) = default;
};
struct Heavy {  // 24.16
    friend bool operator==(const Heavy&, const Heavy&) = default;
};
struct CloseQuarters {  // 24.07; [PISTOL] is identical (24.27)
    friend bool operator==(const CloseQuarters&, const CloseQuarters&) = default;
};
struct RapidFire {  // 24.30 [RAPID FIRE X]
    int x = 1;
    friend bool operator==(const RapidFire&, const RapidFire&) = default;
};
struct Blast {  // 24.05; [BLAST] == [BLAST 1]
    int x = 1;
    friend bool operator==(const Blast&, const Blast&) = default;
};
struct TwinLinked {  // 24.38
    friend bool operator==(const TwinLinked&, const TwinLinked&) = default;
};
struct Torrent {  // 24.37
    friend bool operator==(const Torrent&, const Torrent&) = default;
};
struct Hazardous {  // 24.15
    friend bool operator==(const Hazardous&, const Hazardous&) = default;
};
struct DevastatingWounds {  // 24.10
    TargetCondition only_against{};  // 24.01 (M100): omitted from JSON when it always applies
    friend bool operator==(const DevastatingWounds&, const DevastatingWounds&) = default;
};
struct SustainedHits {  // 24.36 [SUSTAINED HITS X]
    int x = 1;
    TargetCondition only_against{};  // 24.01 (M100)
    friend bool operator==(const SustainedHits&, const SustainedHits&) = default;
};
struct ExtraAttacks {  // 24.11
    friend bool operator==(const ExtraAttacks&, const ExtraAttacks&) = default;
};
struct IgnoresCover {  // 24.18 (M90, D-091): the target can't have the benefit of cover (13.08) against its attacks
    friend bool operator==(const IgnoresCover&, const IgnoresCover&) = default;
};
// 24.03 [ANTI-X Y+] (M99, D-101): against a target unit with the keyword X, an unmodified wound roll of Y+ is a critical
// wound. `keywords` is X split at '/' (24.01: one or more of them); `non` is the FAQ's "ANTI-non-X" form (a target unit
// with none of them).
struct Anti {
    std::vector<std::string> keywords;
    int threshold = 6;
    bool non = false;
    friend bool operator==(const Anti&, const Anti&) = default;
};
struct Melta {  // 24.25 [MELTA X] (M99, D-101): +X to D when the target was within half range at Select Targets
    int x = 1;
    friend bool operator==(const Melta&, const Melta&) = default;
};
struct Lance {  // 24.21 (M99, D-101): +1 to the wound roll if the attacking model's unit made a charge move this turn
    friend bool operator==(const Lance&, const Lance&) = default;
};
// 24.23 [LETHAL HITS] (M100, D-102): a critical hit automatically wounds the target (no wound roll is made for it).
struct LethalHits {
    TargetCondition only_against{};  // 24.01
    friend bool operator==(const LethalHits&, const LethalHits&) = default;
};
struct Psychic {  // 24.29 (M100, D-102): the modifiers that worsen its BS/WS or its hit roll are ignored; a psychic attack
    friend bool operator==(const Psychic&, const Psychic&) = default;
};
struct Cleave {  // 24.06 [CLEAVE X] (M100, D-102): +X attack dice per five models in the weapon's one target
    int x = 1;
    friend bool operator==(const Cleave&, const Cleave&) = default;
};
struct OneShot {  // 24.26 (M100, D-102): the weapon can be selected to make attacks with once per battle
    friend bool operator==(const OneShot&, const OneShot&) = default;
};
// Anything not recognised. Loaded and displayed; a profile with one can't be selected (D-071).
struct Unknown {
    std::string text;
    friend bool operator==(const Unknown&, const Unknown&) = default;
};
}  // namespace ability

using WeaponAbility =
    std::variant<ability::Assault, ability::Heavy, ability::CloseQuarters, ability::RapidFire,
                 ability::Blast, ability::TwinLinked, ability::Torrent, ability::Hazardous,
                 ability::DevastatingWounds, ability::SustainedHits, ability::ExtraAttacks, ability::IgnoresCover,
                 ability::Unknown, ability::Anti, ability::Melta, ability::Lance, ability::LethalHits,
                 ability::Psychic, ability::Cleave, ability::OneShot>;
// New alternatives are appended after Unknown: the bots' state_hash hashes the variant's index, so the older ones keep
// theirs.

// Never throws: unrecognised text becomes ability::Unknown{text}.
[[nodiscard]] WeaponAbility parse_weapon_ability(std::string_view text);

enum class WeaponKind : std::uint8_t { Ranged, Melee };

// 02.04 Weapons: one profile.
struct WeaponProfile {
    std::string name;
    WeaponKind kind = WeaponKind::Ranged;
    std::optional<float> range_mm;  // nullopt for melee
    DiceExpr attacks;               // A
    std::optional<int> skill;       // BS (ranged) or WS (melee); nullopt e.g. for [TORRENT]
    int strength = 0;               // S
    int armour_penetration = 0;     // AP (0 or negative)
    DiceExpr damage;                // D
    std::vector<WeaponAbility> abilities;

    template <typename T>
    [[nodiscard]] bool has() const {
        for (const auto& a : abilities) {
            if (std::holds_alternative<T>(a)) {
                return true;
            }
        }
        return false;
    }

    template <typename T>
    [[nodiscard]] const T* get() const {
        for (const auto& a : abilities) {
            if (const T* p = std::get_if<T>(&a)) {
                return p;
            }
        }
        return nullptr;
    }

    friend bool operator==(const WeaponProfile&, const WeaponProfile&) = default;
};

// A weapon with one or more profiles. D-001: each model picks its own profile.
struct Weapon {
    std::string name;
    WeaponKind kind = WeaponKind::Ranged;
    std::vector<WeaponProfile> profiles;
    // 24.26 [ONE SHOT] (M100, D-102): this weapon (a model's own copy) has been selected to make attacks with, so its
    // [ONE SHOT] profiles can't be selected again this battle. JSON "spent", omitted when false.
    bool one_shot_spent = false;

    friend bool operator==(const Weapon&, const Weapon&) = default;
};

}  // namespace wh40k::core
