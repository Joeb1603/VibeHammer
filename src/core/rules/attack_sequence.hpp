#pragma once

// 05 Attack Sequence: hit rolls, wound rolls, save rolls and inflicting damage, as pure step functions
// over the target unit. The opposing player's choices (allocation order, which model to select) are
// inputs here; M5 turns them into PendingDecisions.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/model/unit.hpp"
#include "core/rules/attack_types.hpp"
#include "core/rules/attacks.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

// ---- 05.01 Hit Rolls ----

enum class HitOutcome : std::uint8_t { Fail, Hit, CriticalHit, AutoHit };

struct HitRoll {
    std::optional<int> unmodified;  // nullopt: [TORRENT] automatically hits, no roll is made (24.37)
    std::optional<int> modified;
    HitOutcome outcome = HitOutcome::Fail;
    friend bool operator==(const HitRoll&, const HitRoll&) = default;
};

struct HitRolls {
    std::vector<HitRoll> rolls;
    [[nodiscard]] int hits() const;           // includes critical hits and automatic hits
    [[nodiscard]] int critical_hits() const;
    friend bool operator==(const HitRolls&, const HitRolls&) = default;
};

// One hit roll per attack dice. Unmodified 1 fails, unmodified 6 is a critical hit, otherwise the
// modified result (D-011: net modifier capped to +-1, floor 1) must be >= BS/WS.
[[nodiscard]] HitRolls make_hit_rolls(DiceSource& dice, const AttackProfile& profile, int attack_dice);

// ---- 05.02 Wound Rolls ----

// 05.02 table: the result required to wound for this Strength against this Toughness.
[[nodiscard]] int required_wound_roll(int strength, int toughness);

// 05.02 "target's Toughness". V1 units have one T; a unit with mixed T is out of scope
// (std::logic_error). Also throws for a unit with no models.
[[nodiscard]] int target_toughness(const Unit& target);

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

// One wound roll per hit. Unmodified 1 fails, unmodified 6 is a critical wound, otherwise the
// modified result (D-011) must be >= required_wound_roll(S, T).
[[nodiscard]] WoundRolls make_wound_rolls(DiceSource& dice, const AttackProfile& profile, int hits,
                                          int toughness);

// ---- 05.03 Save Rolls ----

// 05.03 Create Groups (AllocationGroup is in attack_types.hpp). Groups are in order of their first
// model in the unit.
[[nodiscard]] std::vector<AllocationGroup> create_allocation_groups(const Unit& target);

// 05.03 Allocation Order: `order` lists indices into `groups`. Returns nullopt if legal, otherwise a
// rule-cited reason.
[[nodiscard]] std::optional<std::string> check_allocation_order(const Unit& target,
                                                                std::span<const AllocationGroup> groups,
                                                                std::span<const std::size_t> order);

// Every legal allocation order (permutations of group indices), in lexicographic order.
[[nodiscard]] std::vector<std::vector<std::size_t>> legal_allocation_orders(
    const Unit& target, std::span<const AllocationGroup> groups);

// Applies an order: groups[order[0]], groups[order[1]], ...
[[nodiscard]] std::vector<AllocationGroup> order_groups(std::span<const AllocationGroup> groups,
                                                        std::span<const std::size_t> order);

// 05.03 Make Save Rolls: one D6 per attack that wounded, in roll order (unmodified).
[[nodiscard]] std::vector<int> make_save_rolls(DiceSource& dice, int wounds);

// ---- 05.04 Inflict Damage ----

enum class SaveOutcome : std::uint8_t {
    AttackFails,     // saved: the attack fails
    InflictsDamage,  // the attack inflicts damage
};

struct SaveCheck {
    int unmodified = 0;
    int modified = 0;               // after AP (D-011: floor 1, no cap)
    bool invulnerable_save = false;  // the InSv row stopped the attack
    SaveOutcome outcome = SaveOutcome::InflictsDamage;
    friend bool operator==(const SaveCheck&, const SaveCheck&) = default;
};

// 05.04 Check Save Roll against the current allocation group, first matching row: unmodified 1
// inflicts damage; result >= InSv fails; result + AP >= Sv fails; otherwise inflicts damage.
[[nodiscard]] SaveCheck check_save_roll(int unmodified, const AllocationGroup& group, int armour_penetration);

// 05.04 Current Allocation Group: the first group (in allocation order) with a model still in the unit.
// nullopt when every model in every group is destroyed.
[[nodiscard]] std::optional<std::size_t> current_allocation_group(const Unit& target,
                                                                  std::span<const AllocationGroup> ordered);

// 05.04 Select Model: models of the current group still in the unit; only wounded ones if any are.
[[nodiscard]] std::vector<ModelId> selectable_models(const Unit& target, const AllocationGroup& group);

struct DamageResult {
    ModelId model;
    int damage = 0;        // D of the attack (or 1 for a mortal wound)
    int wounds_lost = 0;   // min(damage, wounds the model had); the rest is lost (no spill-over)
    bool destroyed = false;
    friend bool operator==(const DamageResult&, const DamageResult&) = default;
};

// 05.04 Resolve Damage / 06.02 Resolve Damage: the model loses `damage` wounds; at 0 or fewer it is
// destroyed and removed from the unit (Rules Appendix, Destroyed: V1 has no rules triggered on
// destruction, so removal is immediate). Throws std::logic_error if the model is not in the unit.
DamageResult resolve_damage(Unit& target, ModelId model, int damage);

// The opposing player's choices, as inputs (M5 turns them into PendingDecisions). They are only
// called when there is more than one legal option; the returned choice must be one of the options
// (std::logic_error otherwise).
using ModelChooser = std::function<ModelId(const Unit& target, std::span<const ModelId> options)>;
using AllocationOrderChooser = std::function<std::vector<std::size_t>(
    const Unit& target, std::span<const AllocationGroup> groups,
    std::span<const std::vector<std::size_t>> legal_orders)>;

struct SaveStep {
    std::optional<std::size_t> group;  // index into the ordered groups; nullopt: attack lost
    std::optional<SaveCheck> check;
    std::optional<DamageResult> damage;
    friend bool operator==(const SaveStep&, const SaveStep&) = default;
};

struct InflictDamageResult {
    std::vector<SaveStep> steps;  // one per save roll, lowest roll first
    int models_destroyed = 0;
    int attacks_lost = 0;  // save rolls left after every model in the unit was destroyed
    friend bool operator==(const InflictDamageResult&, const InflictDamageResult&) = default;
};

// 05.04 for a set of save rolls, lowest to highest, against groups in allocation order. Each save
// roll is checked against the current group first; a model is selected only when the attack
// inflicts damage. (Selecting first, as written, gives the same result: every model in a group has
// the same Sv and InSv, and a failed attack does nothing to the selected model.) `choose_model` is called
// only when there is more than one legal model, or for every damaging step when `ask_when_forced`
// (D-051's preview, which follows each step).
[[nodiscard]] InflictDamageResult inflict_damage(Unit& target, std::span<const AllocationGroup> ordered_groups,
                                                 std::span<const int> save_rolls,
                                                 const AttackProfile& profile, const ModelChooser& choose_model,
                                                 bool ask_when_forced = false);

// ---- One batch, 05.01 to 05.04 ----

struct BatchResult {
    HitRolls hits;
    WoundRolls wounds;
    std::vector<AllocationGroup> allocation_order;  // empty if nothing wounded
    std::vector<int> save_rolls;                    // in roll order
    InflictDamageResult damage;
    friend bool operator==(const BatchResult&, const BatchResult&) = default;
};

// Resolves the attack sequence for one batch of attack dice against `target`.
// Precondition: the target still has models ("Target No Longer Eligible or Viable" is an app-only rule
// not yet supplied; std::logic_error otherwise).
[[nodiscard]] BatchResult resolve_attack_batch(DiceSource& dice, Unit& target, const AttackBatch& batch,
                                               const AllocationOrderChooser& choose_order,
                                               const ModelChooser& choose_model);

}  // namespace wh40k::core
