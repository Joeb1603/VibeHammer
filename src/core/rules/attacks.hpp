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

// D-026: V1 rejects weapons whose abilities act during the attack sequence but are not implemented yet
// ([TWIN-LINKED] 24.38, [DEVASTATING WOUNDS] 24.10), when they are selected, as D-002 does for random
// characteristics. Rule-cited reason, or nullopt if the profile can be used.
[[nodiscard]] std::optional<std::string> unimplemented_ability_problem(const WeaponProfile& weapon);

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

}  // namespace wh40k::core
