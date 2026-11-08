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
    // 24.36 [SUSTAINED HITS X]: the additional hits scored by critical hits (X each). They are hits, not
    // critical hits (D-071), and have no roll of their own.
    int sustained_hits = 0;
    [[nodiscard]] int hits() const;  // every hit scored: rolls that hit (critical and automatic included) plus sustained
    [[nodiscard]] int rolled_hits() const;  // the rolls (or automatic hits) that hit, without the sustained hits
    [[nodiscard]] int critical_hits() const;
    friend bool operator==(const HitRolls&, const HitRolls&) = default;
};

// One hit roll per attack dice. Unmodified 1 fails, unmodified 6 is a critical hit, otherwise the
// modified result (D-011: net modifier capped to +-1, floor 1) must be >= BS/WS. Each critical hit of a
// [SUSTAINED HITS X] profile scores X additional hits (24.36); [TORRENT]'s automatic hits are not critical.
[[nodiscard]] HitRolls make_hit_rolls(DiceSource& dice, const AttackProfile& profile, int attack_dice);

// ---- 05.02 Wound Rolls ----

// 05.02 table: the result required to wound for this Strength against this Toughness.
[[nodiscard]] int required_wound_roll(int strength, int toughness);

// 05.02 "target's Toughness". V1 units have one T; a unit with mixed T is out of scope
// (std::logic_error). Also throws for a unit with no models.
[[nodiscard]] int target_toughness(const Unit& target);

// WoundOutcome, WoundRoll and WoundRolls are in attack_types.hpp (M61: the attack flow stores a batch's wound
// rolls while its attacker decides which to re-roll, 24.38).

// One wound roll per hit (HitRolls::hits(), sustained hits included). Unmodified 1 fails, unmodified 6 is a critical wound, otherwise the
// modified result (D-011) must be >= required_wound_roll(S, T).
[[nodiscard]] WoundRolls make_wound_rolls(DiceSource& dice, const AttackProfile& profile, int hits,
                                          int toughness);

// 05.02 for one die: the first row of the table that applies to `unmodified`, with the (already capped, D-011)
// wound modifier, against the required result. make_wound_rolls and reroll_wound_rolls both use it.
[[nodiscard]] WoundRoll wound_roll_result(int unmodified, int modifier, int required);

// ---- 24.38 [TWIN-LINKED]: re-rolling wound rolls (D-011, D-072) ----

// Does the profile have [TWIN-LINKED] (24.38)?
[[nodiscard]] bool twin_linked(const AttackProfile& profile);

// The indices of the wound rolls that failed (05.02: an unmodified 1, or a modified result below the required roll),
// in ascending order.
[[nodiscard]] std::vector<std::size_t> failed_wound_rolls(const WoundRolls& rolls);

// One re-rolled die: its index into WoundRolls::rolls and its roll before and after.
struct WoundReroll {
    std::size_t index = 0;
    WoundRoll before;
    WoundRoll after;
    friend bool operator==(const WoundReroll&, const WoundReroll&) = default;
};

// D-011 Re-rolls: each listed wound roll is rolled again (one D6 each, in ascending index order whatever the order
// of `indices`), then the profile's capped wound modifier is applied; the new roll replaces the old one in `rolls`.
// Preconditions (std::logic_error otherwise): the indices are in range and distinct.
std::vector<WoundReroll> reroll_wound_rolls(DiceSource& dice, const AttackProfile& profile, WoundRolls& rolls,
                                            std::span<const std::size_t> indices);

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
// 24.38 (D-072): the attacker's choice of a [TWIN-LINKED] batch's wound rolls to re-roll (indices into
// rolls.rolls), asked only when at least one of them failed, as the engine asks.
using WoundRerollChooser = std::function<std::vector<std::size_t>(const WoundRolls& rolls)>;
// D-069: rolls a random D (the profile's) for one attack that inflicts damage, after its model is selected (05.04).
// Never called for a fixed D. The engine rolls from its DiceSource; the casualty check and preview use
// damage_stand_in (random_characteristics.hpp), which rolls nothing.
using DamageRoller = std::function<CharacteristicRoll(const DiceExpr& damage)>;

struct SaveStep {
    std::optional<std::size_t> group;  // index into the ordered groups; nullopt: attack lost
    std::optional<SaveCheck> check;
    std::optional<DamageResult> damage;
    // D-069: the random D rolled for this attack once its model was selected (DamageResult::damage is its value).
    // nullopt for a fixed D; omitted from JSON then.
    std::optional<CharacteristicRoll> damage_roll{};
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
// (D-051's preview, which follows each step). D-069: a random D is rolled by `roll_damage` for each attack that
// inflicts damage, after its model is selected; a random D without a roller is a programmer error (std::logic_error).
[[nodiscard]] InflictDamageResult inflict_damage(Unit& target, std::span<const AllocationGroup> ordered_groups,
                                                 std::span<const int> save_rolls,
                                                 const AttackProfile& profile, const ModelChooser& choose_model,
                                                 bool ask_when_forced = false, const DamageRoller& roll_damage = {});

// ---- One batch, 05.01 to 05.04 ----

struct BatchResult {
    HitRolls hits;
    WoundRolls wounds;                   // after any re-rolls (24.38)
    std::vector<WoundReroll> rerolls;    // 24.38: the re-rolled wound rolls, if any
    std::vector<AllocationGroup> allocation_order;  // empty if nothing wounded
    std::vector<int> save_rolls;                    // in roll order
    InflictDamageResult damage;
    std::vector<AttackDiceRoll> attack_rolls{};  // D-069 (04.03): the batch's random A, rolled first
    int attack_dice = 0;                          // the dice resolved (the batch's, plus its random A)
    friend bool operator==(const BatchResult&, const BatchResult&) = default;
};

// Resolves the attack sequence for one batch of attack dice against `target`, in the engine's dice order: the batch's
// random A (D-069, 04.03), hit rolls, wound rolls, the attacker's re-rolls of a [TWIN-LINKED] batch (24.38;
// `choose_rerolls` is called only when a wound roll failed, and is required then), save rolls, then a random D for
// each attack that inflicts damage (D-069, 05.04).
// Precondition: the target still has models ("Target No Longer Eligible or Viable" is an app-only rule
// not yet supplied; std::logic_error otherwise).
[[nodiscard]] BatchResult resolve_attack_batch(DiceSource& dice, Unit& target, const AttackBatch& batch,
                                               const AllocationOrderChooser& choose_order,
                                               const ModelChooser& choose_model,
                                               const WoundRerollChooser& choose_rerolls = {});

}  // namespace wh40k::core
