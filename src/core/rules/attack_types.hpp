#pragma once

// Plain data types of the attack rules (04, 05) that GameState stores while an attack is in
// progress. Kept free of GameState so state/flow.hpp can include them; the functions that use them
// live in attacks.hpp and attack_sequence.hpp.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/model/dice_expr.hpp"
#include "core/model/weapon.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

enum class AttackType : std::uint8_t {
    Shooting,  // ranged attacks with ranged weapons (04.01 While Shooting)
    Fighting,  // melee attacks with melee weapons (04.01 While Fighting)
};

// Facts about the attacking unit that the caller (Shooting/Fight phase, M8/M10) determines.
struct AttackContext {
    AttackType type = AttackType::Shooting;
    // 24.16 [HEAVY]: in your Shooting phase, the attacking unit is unengaged, was not set up on the
    // battlefield this turn, and no model in it has moved more than 3" this turn.
    bool heavy_conditions_met = false;
    friend bool operator==(const AttackContext&, const AttackContext&) = default;
};

// 04.01 + 04.02: one weapon selected by one model, the profile it uses (D-001: chosen per model),
// and its target. A melee weapon splitting its attacks (04.02) appears once per target.
struct WeaponSelection {
    ModelId attacker;
    std::size_t weapon_index = 0;   // into Model::weapons
    std::size_t profile_index = 0;  // into Weapon::profiles
    UnitId target;
    // 04.02 Splitting Melee Attacks: attacks declared against this target. nullopt = all of them.
    std::optional<int> declared_attacks;
    // Recorded in the Select Targets step, because Gather Attack Dice refers back to it:
    bool target_within_half_range = false;  // 24.30 [RAPID FIRE]
    int target_models_at_selection = 0;     // 24.05 [BLAST]
    // Hit roll modifier from the shooting type, before the D-011 cap (10.06: -1 for a MONSTER/VEHICLE
    // model unless a [CLOSE-QUARTERS] weapon targets a unit its unit is engaged with).
    int hit_modifier = 0;
    friend bool operator==(const WeaponSelection&, const WeaponSelection&) = default;
};

// D-006 / 04.03 Identical Attacks: everything that still affects an attack from the hit roll onward.
// Two attacks against the same target are identical iff their AttackProfiles are equal.
struct AttackProfile {
    std::optional<int> skill;  // BS/WS; nullopt for [TORRENT] (auto-hit, 24.37)
    bool torrent = false;
    int strength = 0;
    int armour_penetration = 0;
    DiceExpr damage;
    // Sum of applicable hit / wound roll modifiers, before the D-011 cap. Includes [HEAVY] +1 only when
    // it applies (24.16).
    int hit_modifier = 0;
    int wound_modifier = 0;
    // Remaining abilities that act at hit roll or later (e.g. [TWIN-LINKED], [HAZARDOUS],
    // [DEVASTATING WOUNDS], unrecognised text), in a canonical order. Abilities used up when weapons,
    // targets or dice are selected/gathered ([ASSAULT], [CLOSE-QUARTERS], [RAPID FIRE], [BLAST]) are
    // excluded (D-006).
    std::vector<WeaponAbility> attack_time_abilities;
    friend bool operator==(const AttackProfile&, const AttackProfile&) = default;
};

// ---- D-069 Random Characteristics (app-only rule; the functions are in random_characteristics.hpp) ----

// One roll of a random characteristic (01.05): the D6 rolled, in order (a D3 is one D6 halved, rounding up), and the
// result (the dice, halved for a D3, plus the expression's modifier).
struct CharacteristicRoll {
    std::vector<int> dice;
    int value = 0;
    friend bool operator==(const CharacteristicRoll&, const CharacteristicRoll&) = default;
};

// D-069 (04.03 Gather Attack Dice): a selection whose weapon has a random A, not rolled yet. It is rolled for that
// model and weapon when its batch is gathered for resolution; the [BLAST] / [RAPID FIRE] dice are added after.
struct RandomAttackDice {
    std::size_t selection = 0;  // index into the WeaponSelection list
    ModelId model;              // the selection's attacker
    DiceExpr attacks;           // the weapon's A, e.g. D6+3
    int bonus = 0;              // 24.05 [BLAST] and 24.30 [RAPID FIRE] dice, added after the roll
    friend bool operator==(const RandomAttackDice&, const RandomAttackDice&) = default;
};

// D-069: one selection's random A, rolled.
struct AttackDiceRoll {
    std::size_t selection = 0;
    ModelId model;
    DiceExpr attacks;
    CharacteristicRoll roll;  // the A rolled
    int bonus = 0;            // added after the roll
    [[nodiscard]] int total() const noexcept { return roll.value + bonus; }
    friend bool operator==(const AttackDiceRoll&, const AttackDiceRoll&) = default;
};

// 04.03: the attack dice gathered together because they come from identical attacks (D-006) against
// the same target.
struct AttackBatch {
    UnitId target;
    AttackProfile profile;
    std::vector<std::size_t> selections;  // indices into the WeaponSelection list
    // The dice gathered so far: the fixed-A selections' (with their bonus dice). D-069: a random A is added when it
    // is rolled, as the batch is gathered for resolution.
    int attack_dice = 0;
    // D-069: the selections whose random A is still to be rolled (empty for fixed A, and once rolled). Omitted from
    // JSON when empty.
    std::vector<RandomAttackDice> random_attacks{};
    friend bool operator==(const AttackBatch&, const AttackBatch&) = default;
};

// ---- 05.02 Wound Rolls (the functions are in attack_sequence.hpp) ----

enum class WoundOutcome : std::uint8_t { Fail, Wound, CriticalWound };

struct WoundRoll {
    int unmodified = 0;
    int modified = 0;
    WoundOutcome outcome = WoundOutcome::Fail;
    friend bool operator==(const WoundRoll&, const WoundRoll&) = default;
};

struct WoundRolls {
    int required = 0;
    std::vector<WoundRoll> rolls;
    [[nodiscard]] int wounds() const;  // includes critical wounds
    [[nodiscard]] int critical_wounds() const;
    friend bool operator==(const WoundRolls&, const WoundRolls&) = default;
};

// 05.03 Create Groups: one group per CHARACTER model; one group for all other models with the same
// W, Sv and InSv characteristics.
struct AllocationGroup {
    std::vector<ModelId> models;
    bool character = false;
    int wounds = 0;  // W characteristic shared by the group
    std::optional<int> save;
    std::optional<int> invuln_save;
    friend bool operator==(const AllocationGroup&, const AllocationGroup&) = default;
};

}  // namespace wh40k::core
