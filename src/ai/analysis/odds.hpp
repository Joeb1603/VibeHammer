#pragma once

// M40 (D-054): attack odds for the bot. Every probability comes from core's own roll functions: the hit and wound
// chances by driving make_hit_rolls / make_wound_rolls with ScriptedDice faces 1-6, the save chances from
// check_save_roll for each face, and the attack dice and hit modifiers from make_attack_profile /
// gather_attack_dice. No threshold ("3+", the S-vs-T table) is typed here.
//
// Three layers, cheapest first:
// - OddsCache: per-die chances for an AttackProfile against a target's Toughness and allocation groups, cached by
//   the fields that change them (so the inner loops of M41-M44 never re-derive them).
// - expected_outcome: the exact distribution of a volley (a list of batches) against a TargetSnapshot (the target's
//   models in the order they take damage), by dynamic programming over (models destroyed, wounds left on the
//   front model). It follows 05.04: wounded models first, excess damage lost when a model is destroyed, attacks
//   left after the unit is destroyed lost.
// - sample_outcome: the same volley resolved by core::resolve_attack_batch on a scratch Unit with the bot's own
//   dice, for validation (M40 acceptance) and for M44's rollouts.
// On top of these, state-based helpers turn ShotDeclarations / MeleeDeclarations into the batches the engine would
// gather, and context-based helpers build a unit's volley for a hypothetical situation (moved or not, within half
// range or not) without a GameState.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

#include "core/rules/attack_types.hpp"
#include "core/rules/fight_types.hpp"
#include "core/rules/shooting_types.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::ai::analysis {

// ---- per-die chances ----

// One attack die of a profile against a target's Toughness (05.01, 05.02). `hit` includes critical and automatic
// hits ([TORRENT], 24.37); `wound` is P(wound | hit) and includes critical wounds. V1 gives criticals no extra
// effect beyond succeeding ([DEVASTATING WOUNDS] and [TWIN-LINKED] are rejected, D-026), so they are reported but
// add nothing.
struct HitWoundOdds {
    double hit = 0.0;
    double critical_hit = 0.0;
    double wound = 0.0;
    double critical_wound = 0.0;
};

// Per-die chances, computed once per key. Lookups are deterministic and the values do not depend on what else is
// in the cache (tested).
class OddsCache {
public:
    [[nodiscard]] const HitWoundOdds& hit_wound(const core::AttackProfile& profile, int toughness);
    // P(a save roll against `group` with this AP inflicts damage), from check_save_roll for faces 1-6 (05.04).
    [[nodiscard]] double unsaved(const core::AllocationGroup& group, int armour_penetration);

    [[nodiscard]] std::size_t entries() const noexcept { return hit_wound_.size() + saves_.size(); }
    [[nodiscard]] std::size_t computed() const noexcept { return computed_; }
    [[nodiscard]] std::size_t lookups() const noexcept { return lookups_; }

private:
    // (skill or -1, torrent, strength, hit modifier, wound modifier, toughness)
    using HitWoundKey = std::tuple<int, bool, int, int, int, int>;
    // (AP, Sv or -1, InSv or -1)
    using SaveKey = std::tuple<int, int, int>;
    std::map<HitWoundKey, HitWoundOdds> hit_wound_;
    std::map<SaveKey, double> saves_;
    std::size_t computed_ = 0;
    std::size_t lookups_ = 0;
};

// ---- the target ----

// The target unit as the attack sequence sees it: its Toughness, its allocation groups in allocation order
// (05.03; the first legal order when the defender has a choice) and its models in the order they take damage
// (05.04 Select Model: within a group, a wounded model first). In V1 every unit is one allocation group.
struct TargetSnapshot {
    struct Slot {
        int wounds_left = 0;     // wounds remaining now
        int wounds_char = 0;     // W characteristic
        std::size_t group = 0;   // index into groups
    };
    int toughness = 0;
    std::vector<core::AllocationGroup> groups;
    std::vector<Slot> slots;

    [[nodiscard]] int models() const noexcept { return static_cast<int>(slots.size()); }
    [[nodiscard]] int wounds_left() const noexcept;
};

// The target as it is now. Precondition: the unit has models (05.02 target_toughness).
[[nodiscard]] TargetSnapshot snapshot_of(const core::Unit& target);
// A hypothetical version with only `models` unwounded models left (for M44's chain over models left). Keeps the
// first `models` models of the unit.
[[nodiscard]] TargetSnapshot snapshot_with_models(const core::Unit& target, int models);

// ---- volleys and outcomes ----

// One batch of identical attacks (04.03, D-006): the profile the engine resolves and its attack dice.
struct VolleyBatch {
    core::AttackProfile profile;
    int attack_dice = 0;
    friend bool operator==(const VolleyBatch&, const VolleyBatch&) = default;
};
using Volley = std::vector<VolleyBatch>;  // resolved in order against one target

struct AttackOutcome {
    double attack_dice = 0.0;  // total attack dice
    double hits = 0.0;         // expected hits (05.01)
    double wounds = 0.0;       // expected wounds (05.02)
    double unsaved = 0.0;      // expected attacks that inflict damage, before overkill
    double damage = 0.0;       // expected wounds lost by the target (05.04: excess damage is lost)
    double models_destroyed = 0.0;
    // Destroyed models plus the change in the front model's lost-wound fraction: additive across volleys, so a
    // W2 model at 1 wound counts half. The unit of "value" for M41-M44.
    double model_equivalents = 0.0;
    std::vector<double> models_destroyed_pmf;  // [k] = P(exactly k models destroyed)
    [[nodiscard]] double p_wipe() const noexcept {
        return models_destroyed_pmf.empty() ? 0.0 : models_destroyed_pmf.back();
    }
};

// The exact outcome of `volley` against `target`. Exact for a target with one allocation group (every V1 unit);
// with several groups each attack's save uses the group the next damage would go to (it ignores 05.04's
// lowest-roll-first ordering across groups).
[[nodiscard]] AttackOutcome expected_outcome(OddsCache& cache, std::span<const VolleyBatch> volley,
                                             const TargetSnapshot& target);

struct SampledOutcome {
    int samples = 0;
    double damage = 0.0;            // mean wounds lost
    double damage_sd = 0.0;         // standard deviation of one sample's wounds lost
    double models_destroyed = 0.0;  // mean
    double models_sd = 0.0;
    std::vector<double> models_destroyed_pmf;
};

// `samples` resolutions of `volley` by core::resolve_attack_batch, each on a fresh copy of `target` (weapons
// stripped, for speed), with the bot's own dice seeded by `seed` (D-054). The defender's choices are the
// engine's first legal ones (the first allocation order, the first selectable model).
[[nodiscard]] SampledOutcome sample_outcome(std::span<const VolleyBatch> volley, const core::Unit& target,
                                            std::uint64_t seed, int samples);

// ---- profiles ----

// D-002 / D-026: can the engine resolve this profile (fixed A and D, no unimplemented ability)?
[[nodiscard]] bool profile_supported(const core::WeaponProfile& profile);

// 24.30 [RAPID FIRE]: is `gap_mm` within half the profile's range? The same test select_weapon_target records
// (consistency-tested against it); false for a melee profile.
[[nodiscard]] bool within_half_range(const core::WeaponProfile& profile, float gap_mm);
[[nodiscard]] bool within_range(const core::WeaponProfile& profile, float gap_mm);

// ---- from a game state ----

// The batches the engine would gather for these declarations (make_weapon_selections / make_melee_selections,
// then gather_attack_batches with shooting_context / the Fighting context). Empty when shot_problems /
// melee_problems isn't empty.
[[nodiscard]] std::vector<core::AttackBatch> shooting_batches(const core::GameState& state, core::UnitId shooter,
                                                              core::ShootingType type,
                                                              std::span<const core::ShotDeclaration> shots);
[[nodiscard]] std::vector<core::AttackBatch> melee_batches(const core::GameState& state, core::UnitId attacker,
                                                           std::span<const core::MeleeDeclaration> declarations);

struct TargetOutcome {
    core::UnitId target;
    AttackOutcome outcome;
};
// The batches grouped by target (first-appearance order), each target's batches in the given order, evaluated
// against the target's snapshot in `state`.
[[nodiscard]] std::vector<TargetOutcome> evaluate_batches(OddsCache& cache, const core::GameState& state,
                                                          std::span<const core::AttackBatch> batches);
// Sum of model_equivalents over the targets (a simple scalar for comparisons).
[[nodiscard]] double total_model_equivalents(std::span<const TargetOutcome> outcomes);

// M41's question: what are these declarations worth?
[[nodiscard]] std::vector<TargetOutcome> evaluate_shots(OddsCache& cache, const core::GameState& state,
                                                        core::UnitId shooter, core::ShootingType type,
                                                        std::span<const core::ShotDeclaration> shots);
[[nodiscard]] std::vector<TargetOutcome> evaluate_melee(OddsCache& cache, const core::GameState& state,
                                                        core::UnitId attacker,
                                                        std::span<const core::MeleeDeclaration> declarations);

struct ShootingPlan {
    core::ShootingType type = core::ShootingType::Normal;
    std::vector<core::ShotDeclaration> shots;
    AttackOutcome outcome;
};
// A good legal shooting declaration of `shooter` at `target` in `state`: for each shooting type the unit is
// eligible for, each model picks the weapon set (its [CLOSE-QUARTERS] weapons or its others, 24.07) and the
// profile per weapon with the best value against the fresh target, from shot_options. The type with the best
// combined value wins. nullopt if the unit can't shoot at `target` (no options). M41 refines this (overkill).
[[nodiscard]] std::optional<ShootingPlan> best_shooting(OddsCache& cache, const core::GameState& state,
                                                        core::UnitId shooter, core::UnitId target);

struct MeleePlanValue {
    std::vector<core::MeleeDeclaration> declarations;
    AttackOutcome outcome;
};
// Each model's best melee option against `target` from melee_options (one weapon per model, D-021). nullopt if
// no model can fight `target` now.
[[nodiscard]] std::optional<MeleePlanValue> best_melee(OddsCache& cache, const core::GameState& state,
                                                       core::UnitId attacker, core::UnitId target);

// M43's seam: one GameState copy whose units can be moved and re-flagged for "what if we stood here after this
// move?", so best_shooting can be asked about hypothetical positions without a state copy per candidate.
class PositionProbe {
public:
    explicit PositionProbe(const core::GameState& state) : original_(state), scratch_(state) {}
    [[nodiscard]] const core::GameState& state() const noexcept { return scratch_; }
    // Puts `unit`'s models at `positions` (in the unit's model order; extra positions are ignored).
    void place(core::UnitId unit, std::span<const core::Position> positions);
    // The movement the unit made this turn (09.05-09.07: advanced, fell back, moved more than 3") and a fresh
    // Shooting phase for it (not yet selected to shoot), so the engine's shooting queries answer for "after this
    // move".
    void set_moved(core::UnitId unit, const core::UnitTurnFlags& flags);
    void reset() { scratch_ = original_; }

private:
    core::GameState original_;
    core::GameState scratch_;
};

// ---- hypothetical (no game state) ----

// The situation a unit shoots in, for threat and matchup estimates (strategic knowledge, D-054; the live state
// is always confirmed through the engine):
// - type Normal: any ranged weapon; Assault (after an Advance): [ASSAULT] weapons only (10.05); CloseQuarters
//   (engaged): [CLOSE-QUARTERS] weapons only, unless MONSTER/VEHICLE (10.06);
// - heavy: 24.16 applies (remained stationary or moved no more than 3", unengaged);
// - every target model counts for [BLAST] (24.05), and `gap_mm` decides range and half range per profile.
struct ShotContext {
    core::ShootingType type = core::ShootingType::Normal;
    bool heavy = false;
    float gap_mm = 0.0f;  // edge-to-edge distance to the target unit
};

// Each model's best usable ranged weapons against `target` in `context` (24.07: its [CLOSE-QUARTERS] weapons or
// its others, one profile per weapon), as batches of identical attacks. Empty if nothing is usable.
[[nodiscard]] Volley unit_ranged_volley(OddsCache& cache, const core::Unit& attacker, const core::Unit& target,
                                        const ShotContext& context);
// Each model's best melee weapon (D-021), with all its attacks, as if every model can fight `target`.
[[nodiscard]] Volley unit_melee_volley(OddsCache& cache, const core::Unit& attacker, const core::Unit& target);

}  // namespace wh40k::ai::analysis
