#pragma once

// 04 Making Attacks: selecting weapons and targets (inputs), identical attacks (D-006) and
// gathering attack dice (04.03). The dice are rolled by attack_sequence.hpp (05).
// The data types (AttackContext, WeaponSelection, AttackProfile, AttackBatch) are in attack_types.hpp.

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/model/weapon.hpp"
#include "core/rules/attack_types.hpp"
#include "core/state/game_state.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

// Builds a WeaponSelection, recording the Select Targets facts from `state`. Does not check target
// eligibility (visible, in range, unengaged, 04.02): that belongs to the phases (M8/M10).
// Throws std::logic_error for unknown models/units or out-of-range indices.
[[nodiscard]] WeaponSelection select_weapon_target(const GameState& state, ModelId attacker,
                                                   std::size_t weapon_index, std::size_t profile_index,
                                                   UnitId target,
                                                   std::optional<int> declared_attacks = std::nullopt);

// The profile a selection uses. Throws std::logic_error if the attacker or indices are invalid.
[[nodiscard]] const WeaponProfile& selected_profile(const GameState& state, const WeaponSelection& sel);

[[nodiscard]] AttackProfile make_attack_profile(const WeaponProfile& weapon, const AttackContext& context);

// D-026 / D-071: V1 rejects weapons with an ability it does not implement yet ([TWIN-LINKED] 24.38,
// [DEVASTATING WOUNDS] 24.10, and every ability it doesn't recognise, ability::Unknown, e.g. [LETHAL HITS] or
// [ANTI-X Y+]), when they are selected, as D-002 does for random characteristics. Rule-cited reason, or
// nullopt if the profile can be used.
[[nodiscard]] std::optional<std::string> unimplemented_ability_problem(const WeaponProfile& weapon);

// 24.36 [SUSTAINED HITS X]: the X of an attack profile, 0 without the ability. D-071: with two instances
// (24.02 Duplicated Abilities, the controlling player selects one) the highest is used.
[[nodiscard]] int sustained_hits(const AttackProfile& profile);

// 24.11: is `weapon` an [EXTRA ATTACKS] weapon (one of its melee profiles has the ability; D-071)?
[[nodiscard]] bool is_extra_attacks_weapon(const Weapon& weapon);

// 24.15 [HAZARDOUS]: the number of [HAZARDOUS] weapons selected in the Select Weapons step: distinct
// (model, weapon) pairs whose selected profile has the ability, so a melee weapon splitting its attacks
// between targets counts once (D-071). Throws std::logic_error for an unknown model or index.
[[nodiscard]] int hazardous_weapons_selected(const GameState& state, std::span<const WeaponSelection> selections);

// 04.03 Gather Attack Dice for one selection: A (D-002: must be fixed), plus [RAPID FIRE X] if the
// target was within half range at selection (24.30), plus [BLAST X] per five target models at
// selection (24.05). A melee selection that split its attacks gathers exactly its declared attacks.
// Throws std::logic_error on a weapon type that doesn't match the context or an invalid split.
[[nodiscard]] int gather_attack_dice(const WeaponProfile& weapon, const WeaponSelection& selection,
                                     const AttackContext& context);

// 04.03 steps 1-2 for a whole attacking unit: groups the selections into batches of identical attacks
// per target. Batches are in first-appearance order (by target, then profile). The active player
// chooses which batch to resolve next (04.03 Select Enemy Unit / Gather Attack Dice; M5 engine).
[[nodiscard]] std::vector<AttackBatch> gather_attack_batches(const GameState& state,
                                                             std::span<const WeaponSelection> selections,
                                                             const AttackContext& context);

// D-037: what each of a profile's abilities does to one selection, for clients to explain (read-only).
struct AbilityEffect {
    WeaponAbility ability;
    // It changes these attacks: [RAPID FIRE] / [BLAST] add dice, [HEAVY] adds 1 to hit, [TORRENT] hits
    // automatically, [SUSTAINED HITS] adds hits on critical hits, [HAZARDOUS] makes a hazard roll after the
    // unit's attacks and [EXTRA ATTACKS] is fought with in addition to another melee weapon (D-071).
    // [ASSAULT] and [CLOSE-QUARTERS] act when the shooting type is chosen (10.05, 10.06), and unimplemented
    // abilities never act, so they are never active here.
    bool active = false;
    int extra_attack_dice = 0;  // 24.30 [RAPID FIRE X] within half range; 24.05 [BLAST X] per five models
    int hit_modifier = 0;       // 24.16 [HEAVY]: +1 when its conditions apply
    friend bool operator==(const AbilityEffect&, const AbilityEffect&) = default;
};

// One entry per ability on `weapon`, in profile order. The extra dice and hit modifier are those
// gather_attack_dice and make_attack_profile give for the same inputs.
[[nodiscard]] std::vector<AbilityEffect> ability_effects(const WeaponProfile& weapon, const WeaponSelection& selection,
                                                         const AttackContext& context);

// The context a gathered batch was made with: [HEAVY] applied iff the batch's hit modifier is more than
// the selection's own (make_attack_profile adds the +1 to the profile, gather_attack_batches the rest).
[[nodiscard]] AttackContext batch_context(const AttackBatch& batch, const WeaponSelection& selection,
                                          AttackType type);

}  // namespace wh40k::core
