// M62 (D-069, replaces D-002): Random Characteristics. A random A is rolled for each model and weapon when its batch is
// gathered for resolution (04.03), with [BLAST] / [RAPID FIRE] dice added after; a random D is rolled for each attack
// that inflicts damage, after its model is selected (05.04). The casualty check and preview take the D's maximum and
// roll nothing (amends D-051); the defender still orders the casualties once per batch (amends D-013).

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "core/data/datasheet.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/casualties.hpp"
#include "core/engine/engine.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/random_characteristics.hpp"
#include "core/rules/shooting.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::after_successful_charge;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::is_rule_cited;
using wh40k::test::kIntercessorBase;
using wh40k::test::model_ids;
using wh40k::test::repo_root;
using wh40k::test::select;
using wh40k::test::shoot_step_state;
using wh40k::test::starts_with;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;
constexpr std::size_t kBoltRifle = 0;
constexpr std::size_t kFocused = 0;
constexpr std::size_t kSaturation = 1;  // A2, [ASSAULT], [RAPID FIRE 1]: within half range in the Shoot step
constexpr std::size_t kFleshborer = 0;
constexpr std::size_t kKnives = 2;
const AttackContext kShooting{AttackType::Shooting, false};

const DiceExpr kD3{1, 3, 0};
const DiceExpr kD6{1, 6, 0};
const DiceExpr kD6Plus1{1, 6, 1};
const DiceExpr kD6Plus3{1, 6, 3};

// Side A's Shoot step with every Bolt Rifle on Focused Fire a stand-in random-A [BLAST] weapon (no Rapid Fire, so the
// only bonus is Blast's: +2 against the ten Termagants).
GameState blast_rifles(const DiceExpr& attacks) {
    GameState gs = shoot_step_state();
    for (Model& m : gs.units[kMarines].models) {
        WeaponProfile& p = m.weapons[kBoltRifle].profiles[kFocused];
        p.attacks = attacks;
        p.abilities = {ability::Blast{}};
    }
    return gs;
}

std::vector<int> ones(std::size_t n) { return std::vector<int>(n, 1); }

std::vector<int> join(std::vector<int> a, const std::vector<int>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

template <typename E>
std::size_t index_of(const std::vector<GameEvent>& events) {
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (std::holds_alternative<E>(events[i])) return i;
    }
    return events.size();
}

// The Havocs vs Hive Tyrant fixture (D-070) at Side A's Shoot step, the Havocs having remained stationary.
GameState havocs_shoot_step() {
    GameState gs = load_scenario(repo_root() / wh40k::test::kHavocsVsTyrantFixture, repo_root());
    start_game(gs);
    apply_action(gs, Side::A, action::StartMove{gs.units[0].id, MoveType::RemainStationary, std::nullopt});
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
    return gs;
}

// Side B's Shoot step in V1 with the Termagants' fleshborers a stand-in D3 damage, ten fleshborers fired at the
// Intercessors (5 models, W2), scripted so exactly three saves fail: the casualty decision is pending.
// Fleshborer: BS4+, S5 against T5 (4+ to wound), AP0 against Sv3+.
GameState three_failed_saves_d3() {
    GameState gs = shoot_step_state(Side::B);
    for (Model& m : gs.units[kGants].models) m.weapons[kFleshborer].profiles[0].damage = kD3;
    std::vector<WeaponSelection> sels;
    for (std::size_t i = 0; i < gs.units[kGants].models.size(); ++i) {
        sels.push_back(select(gs, kGants, i, kFleshborer, 0, gs.units[kMarines].id));
    }
    // 10 hit rolls (three hit), 3 wound rolls (all wound), 3 save rolls (all fail): nothing more is rolled before the
    // casualty decision (the check takes the D's maximum, D-069).
    ScriptedDice dice(join(join({6, 6, 6, 1, 1, 1, 1, 1, 1, 1}, {6, 6, 6}), {1, 1, 1}));
    begin_attacks(gs, dice, gs.units[kGants].id, kShooting, sels);
    EXPECT_EQ(dice.remaining(), 0u);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{gs.units[kMarines].id}}));
    return gs;
}

Unit test_unit(int models, int wounds, std::optional<int> save) {
    Unit u;
    u.id = UnitId{50};
    u.side = Side::B;
    u.name = "Test Unit";
    for (int i = 0; i < models; ++i) {
        Model m;
        m.id = ModelId{static_cast<std::uint32_t>(i + 1)};
        m.type_name = "Test";
        m.characteristics.toughness = 4;
        m.characteristics.wounds = wounds;
        m.characteristics.save = save;
        m.base = kIntercessorBase;
        m.wounds_remaining = wounds;
        u.models.push_back(m);
    }
    u.starting_strength = models;
    return u;
}

AttackProfile random_d_profile(const DiceExpr& d) {
    AttackProfile p;
    p.skill = 3;
    p.strength = 4;
    p.damage = d;
    return p;
}

DamageRoller roller(DiceSource& dice) {
    return [&dice](const DiceExpr& d) { return roll_characteristic(dice, d); };
}

ModelChooser pick_index(std::size_t i) {
    return [i](const Unit&, std::span<const ModelId> options) { return options[std::min(i, options.size() - 1)]; };
}

}  // namespace

// ==== 01.05: rolling a random characteristic ====

TEST(D069_RandomCharacteristics, RollsFollowTheDiceNotation) {
    ScriptedDice dice{5, 4, 6, 6};
    EXPECT_EQ(roll_characteristic(dice, kD3), (CharacteristicRoll{{5}, 3}));  // 01.05: a D6 halved, rounding up
    EXPECT_EQ(roll_characteristic(dice, DiceExpr{2, 6, 3}), (CharacteristicRoll{{4, 6}, 13}));
    EXPECT_EQ(roll_characteristic(dice, kD6Plus1), (CharacteristicRoll{{6}, 7}));
    EXPECT_EQ(dice.remaining(), 0u);
    ScriptedDice none{};
    EXPECT_EQ(roll_characteristic(none, DiceExpr::fixed(3)), (CharacteristicRoll{{}, 3}));  // a fixed value rolls nothing
    EXPECT_EQ(damage_stand_in(kD6Plus1), (CharacteristicRoll{{}, 7}));
    EXPECT_EQ(damage_stand_in(kD3), (CharacteristicRoll{{}, 3}));
    EXPECT_EQ(damage_stand_in(DiceExpr{2, 6, 0}), (CharacteristicRoll{{}, 12}));
    EXPECT_EQ(kD6Plus3.min_value(), 4);
    EXPECT_EQ(kD6Plus3.max_value(), 9);
    EXPECT_THROW((void)kD6.fixed_value(), std::logic_error);  // still a programmer error
}

// ==== 04.03 Gather Attack Dice: a random A ====

TEST(R04_03_GatherAttackDice, ARandomAIsRolledForEachModelAndWeaponWithBlastAddedAfter) {
    GameState gs = blast_rifles(kD6Plus3);
    const UnitId gants = gs.units[kGants].id;
    ASSERT_EQ(gs.units[kGants].models.size(), 10u);  // 24.05: +2 dice per selection
    const std::vector<WeaponSelection> sels{select(gs, kMarines, 0, kBoltRifle, kFocused, gants),
                                            select(gs, kMarines, 1, kBoltRifle, kFocused, gants)};
    // Two A rolls (4 and 1), then 7 + 2 and 4 + 2 hit rolls, all missing.
    ScriptedDice dice(join({4, 1}, ones(15)));
    const std::vector<GameEvent> events = begin_attacks(gs, dice, gs.units[kMarines].id, kShooting, sels);
    EXPECT_EQ(dice.remaining(), 0u);

    const auto gathered = events_of<event::AttackBatchesGathered>(events);
    ASSERT_EQ(gathered.size(), 1u);
    ASSERT_EQ(gathered[0].batches.size(), 1u);  // identical attacks (D-006): one batch, A is not compared
    const AttackBatch& unrolled = gathered[0].batches[0];
    EXPECT_EQ(unrolled.attack_dice, 0);  // nothing known before the rolls
    ASSERT_EQ(unrolled.random_attacks.size(), 2u);
    EXPECT_EQ(unrolled.random_attacks[0], (RandomAttackDice{0, gs.units[kMarines].models[0].id, kD6Plus3, 2}));
    EXPECT_EQ(unrolled.random_attacks[1], (RandomAttackDice{1, gs.units[kMarines].models[1].id, kD6Plus3, 2}));

    const auto rolled = events_of<event::AttackDiceRolled>(events);
    ASSERT_EQ(rolled.size(), 1u);
    ASSERT_EQ(rolled[0].rolls.size(), 2u);  // one per model and weapon, in selection order
    EXPECT_EQ(rolled[0].rolls[0].roll, (CharacteristicRoll{{4}, 7}));
    EXPECT_EQ(rolled[0].rolls[0].bonus, 2);
    EXPECT_EQ(rolled[0].rolls[0].total(), 9);
    EXPECT_EQ(rolled[0].rolls[1].roll, (CharacteristicRoll{{1}, 4}));
    EXPECT_EQ(rolled[0].rolls[1].total(), 6);
    EXPECT_EQ(rolled[0].attack_dice, 15);
    EXPECT_EQ(events_of<event::AttackBatchSelected>(events).at(0).attack_dice, 15);
    EXPECT_EQ(events_of<event::HitRollsMade>(events).at(0).rolls.rolls.size(), 15u);
    // 04.03: rolled once the batch is gathered for resolution, before its hit rolls.
    EXPECT_LT(index_of<event::AttackBatchesGathered>(events), index_of<event::AttackDiceRolled>(events));
    EXPECT_LT(index_of<event::AttackDiceRolled>(events), index_of<event::AttackBatchSelected>(events));
    EXPECT_LT(index_of<event::AttackBatchSelected>(events), index_of<event::HitRollsMade>(events));
}

TEST(R04_03_GatherAttackDice, RapidFireDiceAreAddedAfterTheRoll) {
    GameState gs = shoot_step_state();
    gs.units[kMarines].models[0].weapons[kBoltRifle].profiles[kSaturation].attacks = kD3;  // stand-in
    const WeaponSelection sel = select(gs, kMarines, 0, kBoltRifle, kSaturation, gs.units[kGants].id);
    ASSERT_TRUE(sel.target_within_half_range);
    ScriptedDice dice(join({5}, ones(4)));  // D3: a 5 is 3, +1 [RAPID FIRE 1] = 4 hit rolls
    const std::vector<GameEvent> events = begin_attacks(gs, dice, gs.units[kMarines].id, kShooting, {sel});
    EXPECT_EQ(dice.remaining(), 0u);
    const auto rolled = events_of<event::AttackDiceRolled>(events);
    ASSERT_EQ(rolled.size(), 1u);
    EXPECT_EQ(rolled[0].rolls.at(0).roll, (CharacteristicRoll{{5}, 3}));
    EXPECT_EQ(rolled[0].rolls.at(0).bonus, 1);
    EXPECT_EQ(rolled[0].attack_dice, 4);
}

TEST(R04_03_GatherAttackDice, AFixedAndARandomAOfIdenticalAttacksShareABatch) {
    GameState gs = blast_rifles(kD6);
    gs.units[kMarines].models[1].weapons[kBoltRifle].profiles[kFocused].attacks = DiceExpr::fixed(1);
    const UnitId gants = gs.units[kGants].id;
    const std::vector<WeaponSelection> sels{select(gs, kMarines, 0, kBoltRifle, kFocused, gants),
                                            select(gs, kMarines, 1, kBoltRifle, kFocused, gants)};
    // The fixed one's 1 + 2 [BLAST] dice are known; the random one's D6 (a 3) + 2 are rolled: 8 hit rolls.
    ScriptedDice dice(join({3}, ones(8)));
    const std::vector<GameEvent> events = begin_attacks(gs, dice, gs.units[kMarines].id, kShooting, sels);
    EXPECT_EQ(dice.remaining(), 0u);
    const AttackBatch unrolled = events_of<event::AttackBatchesGathered>(events).at(0).batches.at(0);
    EXPECT_EQ(unrolled.attack_dice, 3);
    EXPECT_EQ(unrolled.random_attacks.size(), 1u);
    EXPECT_EQ(events_of<event::AttackDiceRolled>(events).at(0).attack_dice, 8);
    EXPECT_EQ(events_of<event::HitRollsMade>(events).at(0).rolls.rolls.size(), 8u);
}

TEST(R04_03_GatherAttackDice, TorrentWithARandomAHitsWithTheRolledNumberOfAttacks) {
    GameState gs = shoot_step_state();
    WeaponProfile& p = gs.units[kMarines].models[0].weapons[kBoltRifle].profiles[kFocused];
    p.attacks = kD6;  // a flamer-like stand-in: A D6, [TORRENT]
    p.skill.reset();
    p.abilities = {ability::Torrent{}};
    ScriptedDice dice{3, 1, 1, 1};  // A 3: three automatic hits, then three wound rolls
    const std::vector<GameEvent> events = begin_attacks(gs, dice, gs.units[kMarines].id, kShooting,
                                                        {select(gs, kMarines, 0, kBoltRifle, kFocused, gs.units[kGants].id)});
    EXPECT_EQ(dice.remaining(), 0u);
    const HitRolls hits = events_of<event::HitRollsMade>(events).at(0).rolls;
    ASSERT_EQ(hits.rolls.size(), 3u);
    for (const HitRoll& h : hits.rolls) EXPECT_EQ(h.outcome, HitOutcome::AutoHit);
}

TEST(R04_03_GatherAttackDice, ABatchLostUnderD012IsNeverRolled) {
    // Two batches at a Termagant unit of one model: the fixed Focused Fire destroys it, so the random-A Saturation
    // batch is lost (D-012) without its A being rolled. A second Termagant unit keeps Side B on the board (D-007).
    GameState gs = shoot_step_state();
    (void)wh40k::test::add_unit_copy(gs, kGants);
    gs.units[kGants].models.resize(1);
    gs.units[kMarines].models[1].weapons[kBoltRifle].profiles[kSaturation].attacks = kD6Plus3;
    const UnitId gants = gs.units[kGants].id;
    const std::vector<WeaponSelection> sels{select(gs, kMarines, 0, kBoltRifle, kFocused, gants),
                                            select(gs, kMarines, 1, kBoltRifle, kSaturation, gants)};
    ScriptedDice none{};
    begin_attacks(gs, none, gs.units[kMarines].id, kShooting, sels);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::SelectAttackBatch{gs.units[kMarines].id}}));
    ASSERT_EQ(gs.flow.attack->batches.size(), 2u);
    ASSERT_EQ(gs.flow.attack->batches[1].random_attacks.size(), 1u);
    // Focused Fire: A1 + 1 [RAPID FIRE] = 2 hits (6, 6), 2 wounds (6, 6), 2 saves failed (1, 1).
    ScriptedDice kill{6, 6, 6, 6, 1, 1};
    apply_action(gs, Side::A, action::SelectAttackBatch{0}, kill);
    EXPECT_EQ(kill.remaining(), 0u);
    ASSERT_TRUE(std::holds_alternative<decision::OrderCasualties>(gs.pending->kind));
    ScriptedDice nothing{};  // any roll for the lost batch would exhaust it
    const std::vector<GameEvent> events =
        apply_action(gs, Side::B, action::OrderCasualties{gants, model_ids(gs.units[kGants])}, nothing);
    const auto lost = events_of<event::AttacksLost>(events);
    ASSERT_EQ(lost.size(), 1u);
    EXPECT_EQ(lost[0].batch, 1u);
    EXPECT_EQ(lost[0].attack_dice, 0);
    ASSERT_EQ(lost[0].random_attacks.size(), 1u);  // lost unrolled
    EXPECT_EQ(lost[0].random_attacks[0].attacks, kD6Plus3);
    EXPECT_EQ(lost[0].random_attacks[0].bonus, 1);  // [RAPID FIRE 1] within half range
    EXPECT_TRUE(events_of<event::AttackDiceRolled>(events).empty());
}

TEST(R04_03_GatherAttackDice, AFixedAIsNeverRolled) {
    // D-069: an old weapon consumes exactly the dice it did before: its hit, wound and save rolls.
    GameState gs = shoot_step_state();
    ScriptedDice dice{6, 6, 6, 1, 1, 1};  // A2 + 1 [RAPID FIRE]: three hit rolls, then three failed wound rolls
    const std::vector<GameEvent> events = begin_attacks(gs, dice, gs.units[kMarines].id, kShooting,
                                                        {select(gs, kMarines, 0, kBoltRifle, kSaturation, gs.units[kGants].id)});
    EXPECT_EQ(dice.remaining(), 0u);
    EXPECT_TRUE(events_of<event::AttackDiceRolled>(events).empty());
    EXPECT_TRUE(events_of<event::AttackBatchesGathered>(events).at(0).batches.at(0).random_attacks.empty());
    EXPECT_EQ(events_of<event::AttackBatchSelected>(events).at(0).attack_dice, 3);
}

// ==== 04.02: a random A on a melee weapon stays refused (D-069) ====

TEST(D069_RandomCharacteristics, AMeleeWeaponWithARandomAIsRefused) {
    GameState gs = after_successful_charge();
    for (Model& m : gs.units[kMarines].models) m.weapons[kKnives].profiles[0].attacks = kD3;  // stand-in
    const WeaponProfile& knives = gs.units[kMarines].models[0].weapons[kKnives].profiles[0];
    const auto problem = unimplemented_ability_problem(knives);
    ASSERT_TRUE(problem.has_value());
    EXPECT_TRUE(starts_with(*problem, "D-069: 'Knives and Fists' has a random A (D3)")) << *problem;
    EXPECT_TRUE(is_rule_cited(*problem));
    const Unit& marines = gs.units[kMarines];
    EXPECT_TRUE(melee_options(gs, marines).empty());       // never offered
    EXPECT_FALSE(can_make_melee_attacks(gs, marines));     // their only melee weapon: unable to fight (D-026)
    const MeleeDeclaration d{marines.models[0].id, kKnives, 0, {MeleeTarget{gs.units[kGants].id, 2}}};
    const auto problems = melee_problems(gs, marines, std::vector<MeleeDeclaration>{d});
    ASSERT_FALSE(problems.empty());
    EXPECT_TRUE(starts_with(problems.front(), "D-069: ")) << problems.front();
    // The attack sequence's safety net.
    WeaponSelection sel;
    sel.target = gs.units[kGants].id;
    EXPECT_THROW((void)selection_dice(knives, sel, AttackContext{AttackType::Fighting, false}), std::logic_error);
    // A random D on a melee weapon is rolled like any other (no melee weapon in the data has one either).
    WeaponProfile random_d = gs.units[kMarines].models[0].weapons[kKnives].profiles[0];
    random_d.attacks = DiceExpr::fixed(3);
    random_d.damage = kD3;
    EXPECT_FALSE(unimplemented_ability_problem(random_d).has_value());
}

// ==== 05.04 Inflict Damage: a random D ====

TEST(R05_04_InflictDamage, ARandomDIsRolledAfterItsModelIsSelected) {
    Unit u = test_unit(2, 3, 3);
    std::vector<std::string> order;
    const ModelChooser choose = [&order](const Unit&, std::span<const ModelId> options) {
        order.push_back("select");
        return options[1];
    };
    ScriptedDice dice{4};
    const DamageRoller roll = [&](const DiceExpr& d) {
        order.push_back("roll");
        return roll_characteristic(dice, d);
    };
    const InflictDamageResult r = inflict_damage(u, create_allocation_groups(u), std::vector<int>{1},
                                                 random_d_profile(kD6Plus1), choose, false, roll);
    EXPECT_EQ(order, (std::vector<std::string>{"select", "roll"}));
    ASSERT_EQ(r.steps.size(), 1u);
    EXPECT_EQ(r.steps[0].damage_roll, (CharacteristicRoll{{4}, 5}));
    EXPECT_EQ(r.steps[0].damage, (DamageResult{ModelId{2}, 5, 3, true}));  // 3 wounds lost, 2 excess lost
}

TEST(R05_04_InflictDamage, ExcessRandomDamageIsLostAndTheNextAttackRollsItsOwn) {
    Unit u = test_unit(2, 3, 3);
    ScriptedDice dice{6, 1};  // D6+1: 7, then 2
    const InflictDamageResult r = inflict_damage(u, create_allocation_groups(u), std::vector<int>{1, 1},
                                                 random_d_profile(kD6Plus1), pick_index(0), false, roller(dice));
    EXPECT_EQ(dice.remaining(), 0u);
    ASSERT_EQ(r.steps.size(), 2u);
    EXPECT_EQ(r.steps[0].damage, (DamageResult{ModelId{1}, 7, 3, true}));  // 05.04: the excess is lost
    EXPECT_EQ(r.steps[1].damage, (DamageResult{ModelId{2}, 2, 2, false}));
    EXPECT_EQ(u.models.size(), 1u);
    EXPECT_EQ(u.models[0].wounds_remaining, 1);
}

TEST(R05_04_InflictDamage, AWoundedModelTakesTheNextRandomDamage) {
    Unit u = test_unit(3, 10, 3);
    ScriptedDice dice{1, 1, 3};  // D6+1: 2, 2, 4
    int calls = 0;
    const ModelChooser choose = [&calls](const Unit&, std::span<const ModelId> options) {
        ++calls;
        return options.back();
    };
    const InflictDamageResult r = inflict_damage(u, create_allocation_groups(u), std::vector<int>{1, 1, 1},
                                                 random_d_profile(kD6Plus1), choose, false, roller(dice));
    EXPECT_EQ(calls, 1);  // the first pick; then the wounded model is the only choice (05.04 Select Model)
    for (const SaveStep& s : r.steps) EXPECT_EQ(s.damage->model, ModelId{3});
    EXPECT_EQ(find_model(u, ModelId{3})->wounds_remaining, 2);
}

TEST(R05_04_InflictDamage, SavedAttacksAndAttacksLostRollNoDamage) {
    {
        Unit u = test_unit(2, 3, 3);
        ScriptedDice dice{2};  // only the failed save (the 1) rolls; the 6 is saved
        const InflictDamageResult r = inflict_damage(u, create_allocation_groups(u), std::vector<int>{6, 1},
                                                     random_d_profile(kD6Plus1), pick_index(0), false, roller(dice));
        EXPECT_EQ(dice.remaining(), 0u);
        EXPECT_TRUE(r.steps[0].damage_roll.has_value());
        EXPECT_FALSE(r.steps[1].damage_roll.has_value());
    }
    {
        Unit u = test_unit(1, 1, 3);
        ScriptedDice dice{3};  // the unit is destroyed by the first attack: the rest are lost without a roll
        const InflictDamageResult r = inflict_damage(u, create_allocation_groups(u), std::vector<int>{1, 1, 6},
                                                     random_d_profile(kD6Plus1), pick_index(0), false, roller(dice));
        EXPECT_EQ(dice.remaining(), 0u);
        EXPECT_EQ(r.attacks_lost, 2);
        EXPECT_FALSE(r.steps[1].damage_roll.has_value());
        EXPECT_FALSE(r.steps[2].damage_roll.has_value());
    }
}

TEST(R05_04_InflictDamage, OneCasualtyOrderPerBatchAndEachRollLandsOnTheModelItPicks) {
    // D-013 (amended by D-069): one OrderCasualties for the batch; each D3 is rolled as its step is resolved.
    GameState gs = three_failed_saves_d3();
    const Unit& marines = gs.units[kMarines];
    const std::vector<ModelId> ids = model_ids(marines);
    const std::vector<ModelId> priority{ids[3], ids[1], ids[2], ids[4], ids[0]};
    ScriptedDice damage{1, 1, 6};  // D3: 1, 1, 3
    const std::vector<GameEvent> events =
        apply_action(gs, Side::A, action::OrderCasualties{marines.id, priority}, damage);
    EXPECT_EQ(damage.remaining(), 0u);
    const auto resolved = events_of<event::SavesResolved>(events);
    ASSERT_EQ(resolved.size(), 1u);
    const auto& steps = resolved[0].result.steps;
    ASSERT_EQ(steps.size(), 3u);
    EXPECT_EQ(steps[0].damage, (DamageResult{ids[3], 1, 1, false}));
    EXPECT_EQ(steps[1].damage, (DamageResult{ids[3], 1, 1, true}));  // wounded first (05.04)
    EXPECT_EQ(steps[2].damage, (DamageResult{ids[1], 3, 2, true}));  // the next in the order; 1 excess lost
    EXPECT_EQ(steps[2].damage_roll, (CharacteristicRoll{{6}, 3}));
    // No second casualty decision for the batch.
    EXPECT_FALSE(gs.pending && std::holds_alternative<decision::OrderCasualties>(gs.pending->kind));
}

// ==== D-051 (amended): the casualty check and preview take the maximum and roll nothing ====

TEST(D069_RandomCharacteristics, ThePreviewTakesTheMaximumAndMarksTheStepsAfterTheFirstRollProvisional) {
    const GameState gs = three_failed_saves_d3();
    const std::vector<ModelId> ids = model_ids(gs.units[kMarines]);
    const auto start = preview_casualties(gs, {});
    ASSERT_TRUE(start);
    EXPECT_EQ(start->random_damage, kD3);
    EXPECT_EQ(start->options.size(), 5u);
    EXPECT_EQ(start->next_damage, 3);   // up to 3
    EXPECT_EQ(start->damage_left, 9);   // three attacks, up to 3 each
    EXPECT_FALSE(start->provisional_from.has_value());
    EXPECT_FALSE(start->next_provisional());  // the first pick takes the first roll, whatever it is

    const std::vector<ModelId> one{ids[3]};
    const auto after_one = preview_casualties(gs, one);
    ASSERT_TRUE(after_one);
    ASSERT_EQ(after_one->results.size(), 1u);
    EXPECT_EQ(after_one->results[0], (DamageResult{ids[3], 3, 2, true}));  // at the maximum
    EXPECT_EQ(after_one->provisional_from, 1u);
    EXPECT_TRUE(after_one->next_provisional());  // with a 1 or 2, model 3 survives and takes the next attack
    EXPECT_EQ(after_one->damage_left, 6);

    const std::vector<ModelId> three{ids[3], ids[1], ids[2]};
    const auto done = preview_casualties(gs, three);
    ASSERT_TRUE(done);
    EXPECT_TRUE(done->done());
    EXPECT_EQ(done->results.size(), 3u);
    EXPECT_EQ(done->damage_left, 0);
}

TEST(D069_RandomCharacteristics, PreviewsAndDecisionChecksNeverMoveTheDice) {
    // three_failed_saves_d3 already scripts exactly the hit, wound and save dice: the casualty-decision check made no
    // roll. Here, with the game's own dice, nothing read-only changes the state (its dice position included).
    GameState gs = three_failed_saves_d3();
    gs.dice = DiceState{62, 17};
    const GameState before = gs;
    const std::vector<ModelId> ids = model_ids(gs.units[kMarines]);
    for (std::size_t k = 0; k <= ids.size(); ++k) {
        (void)preview_casualties(gs, std::span<const ModelId>(ids.data(), k));
    }
    (void)legal_actions(gs, Side::A);
    (void)validate_action(gs, Side::A, action::OrderCasualties{gs.units[kMarines].id, ids});
    (void)describe(gs, *gs.pending);
    (void)decision_rule(gs, *gs.pending);
    EXPECT_EQ(gs, before);
    EXPECT_EQ(gs.dice, (DiceState{62, 17}));
}

TEST(D069_RandomCharacteristics, TheModelsTheRollsDamageAreAlwaysAmongThePicks) {
    // D-051 still holds with the maximum: whatever the rolls, the damaged models are a prefix of the picks, in order.
    const GameState gs = three_failed_saves_d3();
    const std::vector<ModelId> ids = model_ids(gs.units[kMarines]);
    std::vector<ModelId> picks;
    for (;;) {
        const auto pv = preview_casualties(gs, picks);
        ASSERT_TRUE(pv);
        if (pv->done()) break;
        picks.push_back(pv->options.back());
    }
    std::vector<ModelId> priority = picks;
    for (const ModelId id : ids) {
        if (std::find(priority.begin(), priority.end(), id) == priority.end()) priority.push_back(id);
    }
    for (int a = 1; a <= 6; ++a) {
        for (int b = 1; b <= 6; ++b) {
            for (int c = 1; c <= 6; ++c) {
                GameState copy = gs;
                ScriptedDice dice{a, b, c, 1, 1, 1};  // spare dice are never used by fewer damaging steps
                const auto events =
                    apply_action(copy, Side::A, action::OrderCasualties{gs.units[kMarines].id, priority}, dice);
                std::vector<ModelId> hit;
                const auto resolved = events_of<event::SavesResolved>(events);
                for (const SaveStep& s : resolved.at(0).result.steps) {
                    if (s.damage && (hit.empty() || hit.back() != s.damage->model)) hit.push_back(s.damage->model);
                }
                ASSERT_LE(hit.size(), picks.size());
                for (std::size_t i = 0; i < hit.size(); ++i) EXPECT_EQ(hit[i], picks[i]) << a << b << c;
            }
        }
    }
}

// ==== resolve_attack_batch, the engine's real weapons ====

TEST(D069_RandomCharacteristics, ResolveAttackBatchRollsLikeTheEngine) {
    // The Havocs' lascannons (D6+1) at the Hive Tyrant, and the Tyrant's heavy venom cannon (D3, [BLAST]) at the
    // Havocs: the same seed gives the engine's dice through resolve_attack_batch (the bots' sampler), with the first
    // legal choices.
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        GameState gs = havocs_shoot_step();
        gs.dice = DiceState{seed, 0};
        const UnitId tyrant = gs.units[1].id;
        action::Shoot shoot{gs.units[0].id, ShootingType::Normal, {}};
        for (const Model& m : gs.units[0].models) {
            if (m.weapons[0].name == "Havoc lascannon") shoot.shots.push_back(ShotDeclaration{m.id, 0, 0, tyrant});
        }
        const DiceState start = gs.dice;
        const Unit target_before = gs.units[1];
        std::vector<GameEvent> events = apply_action(gs, Side::A, shoot);
        while (gs.flow.attack) {
            GameAction a = legal_actions(gs, decider(gs)).front();
            if (auto* oc = std::get_if<action::OrderCasualties>(&a)) oc->priority = model_ids(*gs.find_unit(oc->target));
            const auto more = apply_action(gs, decider(gs), a);
            events.insert(events.end(), more.begin(), more.end());
        }
        const AttackBatch batch = events_of<event::AttackBatchesGathered>(events).at(0).batches.at(0);
        DiceState mine = start;
        SeededDice dice(mine);
        Unit target = target_before;
        const BatchResult r = resolve_attack_batch(
            dice, target, batch,
            [](const Unit&, std::span<const AllocationGroup>, std::span<const std::vector<std::size_t>> legal) {
                return legal.front();
            },
            [](const Unit&, std::span<const ModelId> options) { return options.front(); });
        EXPECT_EQ(r.hits, events_of<event::HitRollsMade>(events).at(0).rolls) << seed;
        EXPECT_EQ(r.wounds, events_of<event::WoundRollsMade>(events).at(0).rolls) << seed;
        const auto resolved = events_of<event::SavesResolved>(events);
        if (r.wounds.wounds() > 0) {
            ASSERT_EQ(resolved.size(), 1u);
            EXPECT_EQ(r.damage, resolved[0].result) << seed;
        }
        EXPECT_EQ(target.models, gs.units[1].models) << seed;
        EXPECT_EQ(mine, gs.dice) << seed;
    }
    // Random A: the heavy venom cannon at the five Havocs (+1 [BLAST]).
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        GameState gs = load_scenario(repo_root() / wh40k::test::kHavocsVsTyrantFixture, repo_root());
        gs.units[1].models[0].weapons[0] =
            *load_datasheet(repo_root() / "data/units/hive_tyrant.json").find_weapon("Heavy venom cannon");
        gs.first_turn = Side::B;
        start_game(gs);
        apply_action(gs, Side::B, action::StartMove{gs.units[1].id, MoveType::RemainStationary, std::nullopt});
        ASSERT_EQ(*gs.pending, (PendingDecision{Side::B, decision::Shoot{}}));
        gs.dice = DiceState{seed, 0};
        const DiceState start = gs.dice;
        const Unit target_before = gs.units[0];
        std::vector<GameEvent> events = apply_action(
            gs, Side::B,
            action::Shoot{gs.units[1].id, ShootingType::Normal, {ShotDeclaration{gs.units[1].models[0].id, 0, 0, gs.units[0].id}}});
        while (gs.flow.attack) {
            GameAction a = legal_actions(gs, decider(gs)).front();
            if (auto* oc = std::get_if<action::OrderCasualties>(&a)) oc->priority = model_ids(*gs.find_unit(oc->target));
            const auto more = apply_action(gs, decider(gs), a);
            events.insert(events.end(), more.begin(), more.end());
        }
        const AttackBatch batch = events_of<event::AttackBatchesGathered>(events).at(0).batches.at(0);
        ASSERT_EQ(batch.random_attacks.size(), 1u);
        EXPECT_EQ(batch.random_attacks[0].bonus, 1);  // 24.05: five models
        DiceState mine = start;
        SeededDice dice(mine);
        Unit target = target_before;
        const BatchResult r = resolve_attack_batch(
            dice, target, batch,
            [](const Unit&, std::span<const AllocationGroup>, std::span<const std::vector<std::size_t>> legal) {
                return legal.front();
            },
            [](const Unit&, std::span<const ModelId> options) { return options.front(); });
        const auto rolled = events_of<event::AttackDiceRolled>(events);
        ASSERT_EQ(rolled.size(), 1u);
        EXPECT_EQ(r.attack_rolls, rolled[0].rolls) << seed;
        EXPECT_EQ(r.attack_dice, rolled[0].attack_dice) << seed;
        EXPECT_EQ(r.hits, events_of<event::HitRollsMade>(events).at(0).rolls) << seed;
        EXPECT_EQ(target.models, gs.units[0].models) << seed;
        EXPECT_EQ(mine, gs.dice) << seed;
    }
}

// ==== what D-069 makes usable ====

TEST(D069_RandomCharacteristics, TheRosterWeaponsWithARandomAOrDCanBeSelected) {
    const auto profile = [](const char* unit, const char* weapon, std::size_t p = 0) {
        return load_datasheet(repo_root() / (std::string("data/units/") + unit + ".json")).find_weapon(weapon)->profiles.at(p);
    };
    for (const WeaponProfile& p :
         {profile("havocs", "Havoc lascannon"), profile("screamer_killer", "Bio-plasmic scream"),
          profile("hive_tyrant", "Heavy venom cannon"), profile("hive_tyrant", "Stranglethorn cannon"),
          profile("havocs", "Havoc missile launcher", 0), profile("havocs", "Havoc missile launcher", 1),
          profile("legionaries", "Lascannon"), profile("legionaries", "Missile launcher", 0),
          profile("legionaries", "Missile launcher", 1), profile("termagants", "Shardlauncher"),
          profile("tyrannofex", "Rupture cannon"), profile("tyrannofex", "Acid spray")}) {
        EXPECT_TRUE(!p.attacks.is_fixed() || !p.damage.is_fixed()) << p.name;
        EXPECT_FALSE(unimplemented_ability_problem(p).has_value()) << p.name;
    }
    // Still refused for another ability.
    EXPECT_TRUE(starts_with(*unimplemented_ability_problem(profile("havocs", "Meltagun")), "D-071: "));
    EXPECT_TRUE(starts_with(*unimplemented_ability_problem(profile("havocs", "Flamer")), "D-071: "));
    EXPECT_TRUE(starts_with(*unimplemented_ability_problem(profile("chaos_terminator_squad", "Heavy flamer")), "D-071: "));
    EXPECT_TRUE(starts_with(*unimplemented_ability_problem(profile("termagants", "Strangleweb")), "24.10: "));

    // On the roster fixture the Havocs are offered their lascannons in their first Shooting phase.
    GameState gs = wh40k::test::load_rosters_fixture();
    start_game(gs);
    for (std::uint32_t id = 1; id <= 5; ++id) {
        apply_action(gs, Side::A, action::StartMove{UnitId{id}, MoveType::RemainStationary, std::nullopt});
    }
    const Unit& havocs = *gs.find_unit(UnitId{5});
    bool lascannon = false;
    for (const ShotDeclaration& d : shot_options(gs, havocs, ShootingType::Normal)) {
        lascannon = lascannon || find_model(havocs, d.model)->weapons.at(d.weapon_index).name == "Havoc lascannon";
    }
    EXPECT_TRUE(lascannon);
}

TEST(D069_RandomCharacteristics, NoWeaponEquippedInTheOldScenariosHasARandomAOrD) {
    // So V1, both bot scenarios and Strike play exactly as before (every replay and arena hash).
    for (const char* file : {"v1_skirmish.json", "bot_termagants_first.json", "bot_20_termagants.json", "strike_44x60.json"}) {
        const GameState gs = load_scenario(repo_root() / "data/scenarios" / file, repo_root());
        for (const Unit& u : gs.units) {
            for (const Model& m : u.models) {
                for (const Weapon& w : m.weapons) {
                    for (const WeaponProfile& p : w.profiles) {
                        EXPECT_TRUE(p.attacks.is_fixed() && p.damage.is_fixed()) << file << ": " << p.name;
                    }
                }
            }
        }
    }
}

// ==== JSON (new fields omitted when default) ====

TEST(D069_Json, TheNewFieldsRoundTripAndAreOmittedWhenDefault) {
    AttackBatch batch;
    batch.target = UnitId{2};
    batch.profile.skill = 3;
    batch.profile.damage = kD6Plus1;
    batch.selections = {0, 1};
    batch.attack_dice = 3;
    {
        const Json j = batch;
        EXPECT_FALSE(j.contains("random_attacks"));
        EXPECT_EQ(j.get<AttackBatch>(), batch);
    }
    batch.random_attacks = {RandomAttackDice{1, ModelId{7}, kD6Plus3, 2}};
    {
        const Json j = batch;
        EXPECT_EQ(j.at("random_attacks").at(0), Json::parse(R"({"selection": 1, "model": 7, "A": "D6+3", "bonus": 2})"));
        EXPECT_EQ(j.get<AttackBatch>(), batch);
    }

    SaveStep step;
    step.group = 0;
    step.damage = DamageResult{ModelId{3}, 5, 3, true};
    {
        const Json j = step;
        EXPECT_FALSE(j.contains("damage_roll"));
        EXPECT_EQ(j.get<SaveStep>(), step);
    }
    step.damage_roll = CharacteristicRoll{{4}, 5};
    {
        const Json j = step;
        EXPECT_EQ(j.at("damage_roll"), Json::parse(R"({"dice": [4], "value": 5})"));
        EXPECT_EQ(j.get<SaveStep>(), step);
    }

    const auto round_trip = [](const GameEvent& e) {
        Json j;
        event::to_json(j, e);
        GameEvent back;
        event::from_json(Json::parse(j.dump()), back);
        return std::make_pair(j, back);
    };
    const event::AttacksLost plain{UnitId{2}, 1, 4, {}};
    EXPECT_FALSE(round_trip(plain).first.contains("random_attacks"));
    EXPECT_EQ(round_trip(plain).second, GameEvent{plain});
    const event::AttacksLost unrolled{UnitId{2}, 1, 0, {RandomAttackDice{0, ModelId{9}, kD3, 1}}};
    EXPECT_TRUE(round_trip(unrolled).first.contains("random_attacks"));
    EXPECT_EQ(round_trip(unrolled).second, GameEvent{unrolled});
    const event::AttackDiceRolled rolled{0, {AttackDiceRoll{0, ModelId{9}, kD3, CharacteristicRoll{{5}, 3}, 1}}, 4};
    const auto [j, back] = round_trip(rolled);
    EXPECT_EQ(j.at("kind"), "AttackDiceRolled");
    EXPECT_EQ(j.at("rule"), "04.03");
    EXPECT_EQ(j.at("rolls").at(0),
              Json::parse(R"({"selection": 0, "model": 9, "A": "D3", "dice": [5], "value": 3, "bonus": 1})"));
    EXPECT_EQ(back, GameEvent{rolled});
}

TEST(D069_Json, AStateWaitingWithAnUnrolledRandomARoundTrips) {
    // A save between the start of the attacks and the A roll: the SelectAttackBatch decision with the random batch
    // not rolled yet.
    GameState gs = shoot_step_state();
    gs.units[kMarines].models[1].weapons[kBoltRifle].profiles[kSaturation].attacks = kD6Plus3;
    const UnitId gants = gs.units[kGants].id;
    ScriptedDice none{};
    begin_attacks(gs, none, gs.units[kMarines].id, kShooting,
                  {select(gs, kMarines, 0, kBoltRifle, kFocused, gants), select(gs, kMarines, 1, kBoltRifle, kSaturation, gants)});
    ASSERT_TRUE(std::holds_alternative<decision::SelectAttackBatch>(gs.pending->kind));
    const Json j = gs;
    EXPECT_NE(j.dump().find("\"random_attacks\""), std::string::npos);
    EXPECT_EQ(Json::parse(j.dump()).get<GameState>(), gs);
}
