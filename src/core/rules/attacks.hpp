#pragma once

// 04 Making Attacks: selecting weapons and targets (inputs), identical attacks (D-006) and
// gathering attack dice (04.03). The dice are rolled by attack_sequence.hpp (05).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/model/dice_expr.hpp"
#include "core/model/weapon.hpp"
#include "core/state/game_state.hpp"
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
    friend bool operator==(const WeaponSelection&, const WeaponSelection&) = default;
};

// Builds a WeaponSelection, recording the Select Targets facts from `state`. Does not check target
// eligibility (visible, in range, unengaged, 04.02): that belongs to the phases (M8/M10).
// Throws std::logic_error for unknown models/units or out-of-range indices.
[[nodiscard]] WeaponSelection select_weapon_target(const GameState& state, ModelId attacker,
                                                   std::size_t weapon_index, std::size_t profile_index,
                                                   UnitId target,
                                                   std::optional<int> declared_attacks = std::nullopt);

// The profile a selection uses. Throws std::logic_error if the attacker or indices are invalid.
[[nodiscard]] const WeaponProfile& selected_profile(const GameState& state, const WeaponSelection& sel);

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

[[nodiscard]] AttackProfile make_attack_profile(const WeaponProfile& weapon, const AttackContext& context);

// 04.03 Gather Attack Dice for one selection: A (D-002: must be fixed), plus [RAPID FIRE X] if the
// target was within half range at selection (24.30), plus [BLAST X] per five target models at
// selection (24.05). A melee selection that split its attacks gathers exactly its declared attacks.
// Throws std::logic_error on a weapon type that doesn't match the context or an invalid split.
[[nodiscard]] int gather_attack_dice(const WeaponProfile& weapon, const WeaponSelection& selection,
                                     const AttackContext& context);

// 04.03: the attack dice gathered together because they come from identical attacks (D-006) against
// the same target.
struct AttackBatch {
    UnitId target;
    AttackProfile profile;
    std::vector<std::size_t> selections;  // indices into the WeaponSelection list
    int attack_dice = 0;
    friend bool operator==(const AttackBatch&, const AttackBatch&) = default;
};

// 04.03 steps 1-2 for a whole attacking unit: groups the selections into batches of identical attacks
// per target. Batches are in first-appearance order (by target, then profile). The active player
// chooses which batch to resolve next (04.03 Select Enemy Unit / Gather Attack Dice); M5 makes that a
// PendingDecision.
[[nodiscard]] std::vector<AttackBatch> gather_attack_batches(const GameState& state,
                                                             std::span<const WeaponSelection> selections,
                                                             const AttackContext& context);

}  // namespace wh40k::core
