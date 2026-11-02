#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/attacks.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::kIntercessorBase;
using wh40k::test::load_v1_scenario;
using wh40k::test::load_v1_scenario_closer;

namespace {

constexpr std::size_t kBoltRifle = 0;
constexpr std::size_t kFocused = 0;
constexpr std::size_t kSaturation = 1;
constexpr std::size_t kFleshborer = 0;

// Records every model choice it is asked for and picks options[pick] (clamped).
struct RecordingChooser {
    std::vector<std::vector<ModelId>> calls;
    std::size_t pick = 0;
    ModelChooser fn() {
        return [this](const Unit&, std::span<const ModelId> options) {
            calls.emplace_back(options.begin(), options.end());
            return options[std::min(pick, options.size() - 1)];
        };
    }
};

ModelChooser never_choose_model() {
    return [](const Unit&, std::span<const ModelId> options) {
        ADD_FAILURE() << "model choice requested with " << options.size() << " options";
        return options.front();
    };
}

AllocationOrderChooser never_choose_order() {
    return [](const Unit&, std::span<const AllocationGroup>, std::span<const std::vector<std::size_t>> legal) {
        ADD_FAILURE() << "allocation order requested with " << legal.size() << " legal orders";
        return legal.front();
    };
}

Model test_model(std::uint32_t id, int w, std::optional<int> sv, std::optional<int> inv = std::nullopt) {
    Model m;
    m.id = ModelId{id};
    m.type_name = "Test";
    m.characteristics.toughness = 4;
    m.characteristics.wounds = w;
    m.characteristics.save = sv;
    m.characteristics.invuln_save = inv;
    m.characteristics.leadership = 7;
    m.base = kIntercessorBase;
    m.wounds_remaining = w;
    return m;
}

Unit test_unit(std::vector<Model> models, std::vector<std::string> keywords = {}) {
    Unit u;
    u.id = UnitId{50};
    u.side = Side::B;
    u.name = "Test Unit";
    u.keywords = std::move(keywords);
    u.starting_strength = static_cast<int>(models.size());
    u.models = std::move(models);
    return u;
}

AttackProfile test_profile(std::optional<int> skill, int s, int ap, int d) {
    AttackProfile p;
    p.skill = skill;
    p.strength = s;
    p.armour_penetration = ap;
    p.damage = DiceExpr::fixed(d);
    return p;
}

std::vector<HitOutcome> outcomes(const HitRolls& r) {
    std::vector<HitOutcome> out;
    for (const auto& x : r.rolls) out.push_back(x.outcome);
    return out;
}

std::vector<WoundOutcome> outcomes(const WoundRolls& r) {
    std::vector<WoundOutcome> out;
    for (const auto& x : r.rolls) out.push_back(x.outcome);
    return out;
}

}  // namespace

// ==== M4 acceptance ====

// 5 Intercessors fire Bolt Rifles (Saturation) at 10 Termagants within 12": A2 + Rapid Fire 1 = 3
// dice each (24.30), 15 in total. BS3+; S5 vs T3 wounds on 3+; Sv5+ with AP-1 saves only on a 6.
TEST(M4_Acceptance, SaturationVolleyAtTermagantsMatchesHandCalculation) {
    GameState gs = load_v1_scenario_closer();
    std::vector<WeaponSelection> sel;
    for (const Model& m : gs.units[0].models) {
        sel.push_back(select_weapon_target(gs, m.id, kBoltRifle, kSaturation, gs.units[1].id));
    }
    const auto batches = gather_attack_batches(gs, sel, AttackContext{AttackType::Shooting, true});
    ASSERT_EQ(batches.size(), 1u);
    ASSERT_EQ(batches[0].attack_dice, 15);

    ScriptedDice dice{
        // 15 hit rolls: 1 2 fail; 3 4 5 hit; 6 crit; 3 3 hit; 2 1 fail; 6 crit; 5 4 hit; 2 fail; 3 hit
        1, 2, 3, 4, 5, 6, 3, 3, 2, 1, 6, 5, 4, 2, 3,
        // 10 wound rolls (3+): 2 F, 3 W, 1 F, 6 CW, 4 W, 5 W, 2 F, 3 W, 3 W, 1 F
        2, 3, 1, 6, 4, 5, 2, 3, 3, 1,
        // 6 save rolls: only the 6s save (6 - 1 = 5 >= 5+)
        6, 5, 1, 4, 6, 2};
    RecordingChooser chooser;
    Unit& gants = gs.units[1];
    const BatchResult r = resolve_attack_batch(dice, gants, batches[0], never_choose_order(), chooser.fn());

    EXPECT_EQ(r.hits.hits(), 10);
    EXPECT_EQ(r.hits.critical_hits(), 2);
    EXPECT_EQ(r.wounds.required, 3);
    EXPECT_EQ(r.wounds.wounds(), 6);
    EXPECT_EQ(r.wounds.critical_wounds(), 1);
    EXPECT_EQ(r.save_rolls, (std::vector<int>{6, 5, 1, 4, 6, 2}));
    ASSERT_EQ(r.damage.steps.size(), 6u);
    // 05.04: lowest result first.
    const std::vector<int> expected_order{1, 2, 4, 5, 6, 6};
    for (std::size_t i = 0; i < 6; ++i) {
        ASSERT_TRUE(r.damage.steps[i].check.has_value());
        EXPECT_EQ(r.damage.steps[i].check->unmodified, expected_order[i]);
        EXPECT_EQ(r.damage.steps[i].check->outcome,
                  i < 4 ? SaveOutcome::InflictsDamage : SaveOutcome::AttackFails);
    }
    EXPECT_EQ(r.damage.models_destroyed, 4);
    EXPECT_EQ(gants.models.size(), 6u);
    EXPECT_EQ(chooser.calls.size(), 4u);  // no wounded Termagants, so a free choice each time
    EXPECT_EQ(dice.remaining(), 0u);
}

// Termagant Fleshborers (S5) vs Intercessors (T5) wound on 4+. The second failed save must go on the
// Intercessor wounded by the first (05.04 Select Model).
TEST(M4_Acceptance, FleshborersWoundOn4PlusAndDamageGoesOnTheWoundedModelFirst) {
    GameState gs = load_v1_scenario();
    std::vector<WeaponSelection> sel;
    for (std::size_t i = 0; i < 3; ++i) {
        sel.push_back(select_weapon_target(gs, gs.units[1].models[i].id, kFleshborer, 0, gs.units[0].id));
    }
    const auto batches = gather_attack_batches(gs, sel, AttackContext{AttackType::Shooting, false});
    ASSERT_EQ(batches.size(), 1u);
    ASSERT_EQ(batches[0].attack_dice, 3);

    Unit& marines = gs.units[0];
    const ModelId victim = marines.models[2].id;
    ScriptedDice dice{4, 5, 6,   // hits (BS4+)
                      4, 5, 6,   // wounds (4+)
                      6, 2, 1};  // saves (Sv3+, AP0): 1 and 2 fail, 6 saves
    RecordingChooser chooser;
    chooser.pick = 2;
    const BatchResult r = resolve_attack_batch(dice, marines, batches[0], never_choose_order(), chooser.fn());

    EXPECT_EQ(r.wounds.required, 4);
    EXPECT_EQ(r.wounds.wounds(), 3);
    ASSERT_EQ(r.allocation_order.size(), 1u);  // Sergeant and Intercessors share W, Sv, InSv
    ASSERT_EQ(r.damage.steps.size(), 3u);
    ASSERT_TRUE(r.damage.steps[0].damage.has_value());
    EXPECT_EQ(r.damage.steps[0].damage->model, victim);
    EXPECT_FALSE(r.damage.steps[0].damage->destroyed);
    ASSERT_TRUE(r.damage.steps[1].damage.has_value());
    EXPECT_EQ(r.damage.steps[1].damage->model, victim);  // forced: the only wounded model
    EXPECT_TRUE(r.damage.steps[1].damage->destroyed);
    EXPECT_FALSE(r.damage.steps[2].damage.has_value());
    ASSERT_EQ(chooser.calls.size(), 1u);  // the second allocation had one legal option
    EXPECT_EQ(chooser.calls[0].size(), 5u);
    ASSERT_EQ(marines.models.size(), 4u);
    for (const Model& m : marines.models) {
        EXPECT_EQ(m.wounds_remaining, 2);
    }
}

// 05.04: excess damage on a destroyed model is lost, not carried over to another model.
TEST(M4_Acceptance, ExcessDamageOnDestroyedTermagantIsLost) {
    GameState gs = load_v1_scenario_closer();
    const WeaponSelection s =
        select_weapon_target(gs, gs.units[0].models[2].id, kBoltRifle, kFocused, gs.units[1].id);
    const auto batches = gather_attack_batches(gs, std::vector{s}, AttackContext{AttackType::Shooting, false});
    ASSERT_EQ(batches[0].attack_dice, 2);  // A1 + Rapid Fire 1
    ScriptedDice dice{3, 1,  // one hit
                      2,     // S6 vs T3 wounds on 2+
                      4};    // 4 - 1 = 3 < 5+: damage
    Unit& gants = gs.units[1];
    RecordingChooser chooser;
    const BatchResult r = resolve_attack_batch(dice, gants, batches[0], never_choose_order(), chooser.fn());
    ASSERT_TRUE(r.damage.steps[0].damage.has_value());
    EXPECT_EQ(r.damage.steps[0].damage->damage, 2);
    EXPECT_EQ(r.damage.steps[0].damage->wounds_lost, 1);
    EXPECT_TRUE(r.damage.steps[0].damage->destroyed);
    EXPECT_EQ(gants.models.size(), 9u);
    for (const Model& m : gants.models) {
        EXPECT_EQ(m.wounds_remaining, 1);
    }
}

TEST(M4_Acceptance, ExcessDamageOnWoundedIntercessorDoesNotSpill) {
    GameState gs = load_v1_scenario();
    Unit& marines = gs.units[0];
    marines.models[1].wounds_remaining = 1;
    const ModelId wounded = marines.models[1].id;
    const WeaponProfile& focused = marines.models[0].weapons[kBoltRifle].profiles[kFocused];
    const AttackBatch batch{marines.id, make_attack_profile(focused, AttackContext{}), {}, 2};
    ScriptedDice dice{3, 3,  // two hits (BS3+)
                      3, 3,  // S6 vs T5 wounds on 3+
                      1, 6};  // 1 inflicts damage; 6 - 1 = 5 saves
    const BatchResult r = resolve_attack_batch(dice, marines, batch, never_choose_order(), never_choose_model());
    ASSERT_TRUE(r.damage.steps[0].damage.has_value());
    EXPECT_EQ(r.damage.steps[0].damage->model, wounded);
    EXPECT_EQ(r.damage.steps[0].damage->wounds_lost, 1);
    EXPECT_TRUE(r.damage.steps[0].damage->destroyed);
    ASSERT_EQ(marines.models.size(), 4u);
    for (const Model& m : marines.models) {
        EXPECT_EQ(m.wounds_remaining, 2);
    }
}

// ==== 05.01 Hit Rolls ====

TEST(R05_01_HitRolls, UnmodifiedOneFailsUnmodifiedSixIsCritical) {
    ScriptedDice dice{1, 2, 3, 6};
    const HitRolls r = make_hit_rolls(dice, test_profile(3, 4, 0, 1), 4);
    EXPECT_EQ(outcomes(r), (std::vector<HitOutcome>{HitOutcome::Fail, HitOutcome::Fail, HitOutcome::Hit,
                                                    HitOutcome::CriticalHit}));
    EXPECT_EQ(r.hits(), 2);
    EXPECT_EQ(r.critical_hits(), 1);
}

TEST(R24_16_Heavy, AddsOneToTheHitRollWhenItApplies) {
    const GameState gs = load_v1_scenario();
    const WeaponProfile& focused = gs.units[0].models[0].weapons[kBoltRifle].profiles[kFocused];
    ScriptedDice with{2};
    const HitRolls h = make_hit_rolls(with, make_attack_profile(focused, AttackContext{AttackType::Shooting, true}), 1);
    EXPECT_EQ(h.rolls[0].modified, 3);
    EXPECT_EQ(h.rolls[0].outcome, HitOutcome::Hit);
    ScriptedDice without{2};
    const HitRolls n = make_hit_rolls(without, make_attack_profile(focused, AttackContext{AttackType::Shooting, false}), 1);
    EXPECT_EQ(n.rolls[0].outcome, HitOutcome::Fail);
}

TEST(R24_37_Torrent, AutomaticallyHitsWithoutRolling) {
    AttackProfile p = test_profile(std::nullopt, 2, 0, 1);
    p.torrent = true;
    ScriptedDice dice(std::vector<int>{});
    const HitRolls r = make_hit_rolls(dice, p, 3);
    EXPECT_EQ(r.hits(), 3);
    EXPECT_EQ(r.critical_hits(), 0);
    EXPECT_EQ(outcomes(r), std::vector<HitOutcome>(3, HitOutcome::AutoHit));
    EXPECT_EQ(dice.consumed(), 0u);
}

TEST(R24_38_TwinLinked, IsRefusedUntilReRollsAreImplemented) {
    AttackProfile p = test_profile(4, 3, 0, 1);
    p.attack_time_abilities = {ability::TwinLinked{}};
    ScriptedDice dice{4};
    EXPECT_THROW((void)make_hit_rolls(dice, p, 1), std::logic_error);
}

// ==== D-011 Modifying Dice Rolls ====

TEST(D011_ModifyingDiceRolls, HitModifierOfPlusTwoIsCappedAtPlusOne) {
    AttackProfile p = test_profile(4, 4, 0, 1);
    p.hit_modifier = 2;
    ScriptedDice dice{2, 3};
    const HitRolls r = make_hit_rolls(dice, p, 2);
    EXPECT_EQ(r.rolls[0].modified, 3);  // 2 + 1, not 2 + 2
    EXPECT_EQ(outcomes(r), (std::vector<HitOutcome>{HitOutcome::Fail, HitOutcome::Hit}));
}

TEST(D011_ModifyingDiceRolls, HitModifierOfMinusThreeIsCappedAtMinusOne) {
    AttackProfile p = test_profile(3, 4, 0, 1);
    p.hit_modifier = -3;
    ScriptedDice dice{4, 3, 6};
    const HitRolls r = make_hit_rolls(dice, p, 3);
    EXPECT_EQ(outcomes(r), (std::vector<HitOutcome>{HitOutcome::Hit, HitOutcome::Fail, HitOutcome::CriticalHit}));
    EXPECT_EQ(r.rolls[2].modified, 5);  // still critical: critical hits need an unmodified 6
}

TEST(D011_ModifyingDiceRolls, ModifiedSixIsAHitButNotCritical) {
    AttackProfile p = test_profile(6, 4, 0, 1);
    p.hit_modifier = 1;
    ScriptedDice dice{5, 6};
    const HitRolls r = make_hit_rolls(dice, p, 2);
    EXPECT_EQ(r.rolls[0].modified, 6);
    EXPECT_EQ(r.rolls[0].outcome, HitOutcome::Hit);
    EXPECT_EQ(r.rolls[1].modified, 7);  // no maximum
    EXPECT_EQ(r.rolls[1].outcome, HitOutcome::CriticalHit);
}

TEST(D011_ModifyingDiceRolls, UnmodifiedOneFailsWhateverTheModifier) {
    AttackProfile p = test_profile(2, 4, 0, 1);
    p.hit_modifier = 1;
    ScriptedDice dice{1};
    const HitRolls r = make_hit_rolls(dice, p, 1);
    EXPECT_EQ(r.rolls[0].modified, 2);
    EXPECT_EQ(r.rolls[0].outcome, HitOutcome::Fail);
}

TEST(D011_ModifyingDiceRolls, WoundModifiersAreCappedToo) {
    AttackProfile plus = test_profile(3, 4, 0, 1);
    plus.wound_modifier = 3;
    ScriptedDice d1{2, 3};
    EXPECT_EQ(outcomes(make_wound_rolls(d1, plus, 2, 4)),
              (std::vector<WoundOutcome>{WoundOutcome::Fail, WoundOutcome::Wound}));
    AttackProfile minus = test_profile(3, 4, 0, 1);
    minus.wound_modifier = -2;
    ScriptedDice d2{5, 4, 6};
    EXPECT_EQ(outcomes(make_wound_rolls(d2, minus, 3, 4)),
              (std::vector<WoundOutcome>{WoundOutcome::Wound, WoundOutcome::Fail, WoundOutcome::CriticalWound}));
}

TEST(D011_ModifyingDiceRolls, SaveModifiersAreUncappedAndFlooredAtOne) {
    const AllocationGroup g{{ModelId{1}}, false, 1, 3, std::nullopt};
    const SaveCheck c = check_save_roll(3, g, -3);
    EXPECT_EQ(c.modified, 1);  // 3 - 3 = 0, counted as 1
    EXPECT_EQ(c.outcome, SaveOutcome::InflictsDamage);
    const AllocationGroup g2{{ModelId{1}}, false, 1, 2, std::nullopt};
    EXPECT_EQ(check_save_roll(6, g2, -3).modified, 3);  // AP-3 applies in full: not capped at -1
    EXPECT_EQ(check_save_roll(6, g2, -3).outcome, SaveOutcome::AttackFails);
    EXPECT_EQ(check_save_roll(4, g2, -3).outcome, SaveOutcome::InflictsDamage);
}

// ==== 05.02 Wound Rolls ====

TEST(R05_02_WoundRolls, StrengthVersusToughnessTable) {
    EXPECT_EQ(required_wound_roll(10, 5), 2);  // twice
    EXPECT_EQ(required_wound_roll(11, 5), 2);  // more than twice
    EXPECT_EQ(required_wound_roll(6, 3), 2);
    EXPECT_EQ(required_wound_roll(5, 3), 3);  // greater (not twice)
    EXPECT_EQ(required_wound_roll(6, 5), 3);
    EXPECT_EQ(required_wound_roll(5, 5), 4);  // equal
    EXPECT_EQ(required_wound_roll(4, 5), 5);  // less
    EXPECT_EQ(required_wound_roll(3, 5), 5);
    EXPECT_EQ(required_wound_roll(2, 4), 6);  // half
    EXPECT_EQ(required_wound_roll(2, 5), 6);  // less than half
    EXPECT_EQ(required_wound_roll(3, 6), 6);
}

TEST(R05_02_WoundRolls, MixedToughnessIsOutOfScope) {
    Unit u = test_unit({test_model(1, 1, 5), test_model(2, 1, 5)});
    u.models[1].characteristics.toughness = 5;
    EXPECT_THROW((void)target_toughness(u), std::logic_error);
    EXPECT_THROW((void)target_toughness(test_unit({})), std::logic_error);
}

// ==== 05.03 Save Rolls: groups and allocation order ====

TEST(R05_03_CreateGroups, GroupsBySameWSvAndInSv) {
    const Unit u = test_unit({test_model(1, 2, 3), test_model(2, 2, 3), test_model(3, 3, 3),
                              test_model(4, 2, 3, 4), test_model(5, 2, 3)});
    const auto groups = create_allocation_groups(u);
    ASSERT_EQ(groups.size(), 3u);
    EXPECT_EQ(groups[0].models, (std::vector<ModelId>{ModelId{1}, ModelId{2}, ModelId{5}}));
    EXPECT_EQ(groups[1].models, (std::vector<ModelId>{ModelId{3}}));
    EXPECT_EQ(groups[2].models, (std::vector<ModelId>{ModelId{4}}));
    EXPECT_EQ(groups[2].invuln_save, 4);
}

TEST(R05_03_CreateGroups, OneGroupPerCharacterModel) {
    const Unit u = test_unit({test_model(1, 4, 3), test_model(2, 4, 3)}, {"Infantry", "Character"});
    const auto groups = create_allocation_groups(u);
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_TRUE(groups[0].character);
    EXPECT_TRUE(groups[1].character);
}

TEST(R05_03_CreateGroups, V1UnitsFormOneGroupEach) {
    const GameState gs = load_v1_scenario();
    EXPECT_EQ(create_allocation_groups(gs.units[0]).size(), 1u);
    EXPECT_EQ(create_allocation_groups(gs.units[1]).size(), 1u);
}

TEST(R05_03_AllocationOrder, WoundedNonCharacterGroupMustBeFirst) {
    Unit u = test_unit({test_model(1, 2, 3), test_model(2, 1, 5)});
    u.models[0].wounds_remaining = 1;
    const auto groups = create_allocation_groups(u);
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_FALSE(check_allocation_order(u, groups, std::vector<std::size_t>{0, 1}).has_value());
    const auto bad = check_allocation_order(u, groups, std::vector<std::size_t>{1, 0});
    ASSERT_TRUE(bad.has_value());
    EXPECT_NE(bad->find("05.03"), std::string::npos);
    EXPECT_EQ(legal_allocation_orders(u, groups), (std::vector<std::vector<std::size_t>>{{0, 1}}));
}

TEST(R05_03_AllocationOrder, CharacterGroupsLastAndWoundedCharactersFirst) {
    // Hand-made groups: V1 units can't mix CHARACTER and other models (keywords are per unit).
    Unit u = test_unit({test_model(1, 2, 3), test_model(2, 2, 3), test_model(3, 4, 2), test_model(4, 4, 2)});
    u.models[3].wounds_remaining = 3;  // model 4: a wounded character
    const std::vector<AllocationGroup> groups{
        {{ModelId{1}, ModelId{2}}, false, 2, 3, std::nullopt},
        {{ModelId{3}}, true, 4, 2, std::nullopt},
        {{ModelId{4}}, true, 4, 2, std::nullopt},
    };
    using O = std::vector<std::size_t>;
    EXPECT_FALSE(check_allocation_order(u, groups, O{0, 2, 1}).has_value());
    EXPECT_TRUE(check_allocation_order(u, groups, O{1, 0, 2}).has_value());  // character before non-character
    EXPECT_TRUE(check_allocation_order(u, groups, O{0, 1, 2}).has_value());  // unwounded character first
    EXPECT_TRUE(check_allocation_order(u, groups, O{0, 0, 1}).has_value());  // not a permutation
    EXPECT_TRUE(check_allocation_order(u, groups, O{0, 2}).has_value());     // missing a group
    EXPECT_EQ(legal_allocation_orders(u, groups), (std::vector<O>{{0, 2, 1}}));
}

// ==== 05.04 Inflict Damage ====

TEST(R05_04_CheckSaveRoll, RowsAppliedInOrder) {
    const AllocationGroup inv{{ModelId{1}}, false, 2, 3, 4};
    EXPECT_EQ(check_save_roll(1, AllocationGroup{{ModelId{1}}, false, 2, 2, 2}, 0).outcome,
              SaveOutcome::InflictsDamage);  // unmodified 1 always inflicts damage
    const SaveCheck invuln = check_save_roll(4, inv, -3);
    EXPECT_EQ(invuln.outcome, SaveOutcome::AttackFails);
    EXPECT_TRUE(invuln.invulnerable_save);  // InSv ignores AP
    EXPECT_EQ(check_save_roll(3, inv, -3).outcome, SaveOutcome::InflictsDamage);
    EXPECT_EQ(check_save_roll(4, AllocationGroup{{ModelId{1}}, false, 2, 3, std::nullopt}, -1).outcome,
              SaveOutcome::AttackFails);  // 4 - 1 = 3 >= 3+
    EXPECT_EQ(check_save_roll(3, AllocationGroup{{ModelId{1}}, false, 2, 3, std::nullopt}, -1).outcome,
              SaveOutcome::InflictsDamage);
    EXPECT_EQ(check_save_roll(6, AllocationGroup{{ModelId{1}}, false, 2, std::nullopt, std::nullopt}, 0).outcome,
              SaveOutcome::InflictsDamage);  // no Sv, no InSv
}

TEST(R05_04_CurrentAllocationGroup, NextGroupBecomesCurrentWhenAllModelsDestroyed) {
    // Group 0: model 1 (Sv6+); group 1: models 2, 3 (Sv4+). Saves 3, 3, 4 against AP0.
    const auto run = [](std::vector<std::size_t> order) {
        Unit u = test_unit({test_model(1, 1, 6), test_model(2, 1, 4), test_model(3, 1, 4)});
        const auto groups = create_allocation_groups(u);
        const auto ordered = order_groups(groups, order);
        RecordingChooser chooser;
        const InflictDamageResult r =
            inflict_damage(u, ordered, std::vector<int>{4, 3, 3}, test_profile(3, 4, 0, 1), chooser.fn());
        return std::make_pair(r, u);
    };
    // Sv6+ group first: 3 kills model 1; 3 then fails against Sv4+ (kills one); 4 is saved by Sv4+.
    const auto [first, u1] = run({0, 1});
    EXPECT_EQ(first.models_destroyed, 2);
    EXPECT_EQ(first.steps[0].group, std::size_t{0});
    EXPECT_EQ(first.steps[1].group, std::size_t{1});
    EXPECT_EQ(first.steps[2].check->outcome, SaveOutcome::AttackFails);
    EXPECT_EQ(u1.models.size(), 1u);
    // Sv4+ group first: both 3s kill models 2 and 3, then the 4 fails against Sv6+.
    const auto [second, u2] = run({1, 0});
    EXPECT_EQ(second.models_destroyed, 3);
    EXPECT_TRUE(u2.models.empty());
}

TEST(R05_04_InflictDamage, ExcessAttacksAreLostWhenTheUnitIsDestroyed) {
    Unit u = test_unit({test_model(1, 1, std::nullopt), test_model(2, 1, std::nullopt)});
    const auto groups = create_allocation_groups(u);
    RecordingChooser chooser;
    const InflictDamageResult r =
        inflict_damage(u, groups, std::vector<int>{2, 3, 4, 5}, test_profile(3, 4, 0, 1), chooser.fn());
    EXPECT_EQ(r.models_destroyed, 2);
    EXPECT_EQ(r.attacks_lost, 2);
    EXPECT_FALSE(r.steps[2].group.has_value());
    EXPECT_TRUE(u.models.empty());
}

TEST(R05_04_InflictDamage, AllocationOrderChoiceIsAskedWhenThereIsMoreThanOneLegalOrder) {
    Unit u = test_unit({test_model(1, 1, 6), test_model(2, 1, 4), test_model(3, 1, 4)});
    const AttackBatch batch{u.id, test_profile(2, 8, 0, 1), {}, 1};
    ScriptedDice dice{2, 2, 3};  // hit, wound (S8 vs T4: 2+), save 3
    std::size_t legal_seen = 0;
    const AllocationOrderChooser choose_order =
        [&](const Unit&, std::span<const AllocationGroup>, std::span<const std::vector<std::size_t>> legal) {
            legal_seen = legal.size();
            return std::vector<std::size_t>{1, 0};
        };
    RecordingChooser chooser;
    const BatchResult r = resolve_attack_batch(dice, u, batch, choose_order, chooser.fn());
    EXPECT_EQ(legal_seen, 2u);
    ASSERT_EQ(r.allocation_order.size(), 2u);
    EXPECT_EQ(r.allocation_order[0].models, (std::vector<ModelId>{ModelId{2}, ModelId{3}}));
    EXPECT_EQ(r.damage.models_destroyed, 1);
    EXPECT_NE(std::find_if(u.models.begin(), u.models.end(), [](const Model& m) { return m.id == ModelId{1}; }),
              u.models.end());  // the Sv6+ model was not first, so it survives
}

TEST(R05_04_InflictDamage, IllegalChoicesAreProgrammerErrors) {
    Unit u = test_unit({test_model(1, 1, std::nullopt), test_model(2, 1, std::nullopt)});
    const auto groups = create_allocation_groups(u);
    const ModelChooser bad = [](const Unit&, std::span<const ModelId>) { return ModelId{99}; };
    EXPECT_THROW((void)inflict_damage(u, groups, std::vector<int>{2}, test_profile(3, 4, 0, 1), bad),
                 std::logic_error);
}

TEST(D002_RandomCharacteristics, RandomDamageIsNotRolled) {
    Unit u = test_unit({test_model(1, 1, std::nullopt)});
    AttackProfile p = test_profile(3, 4, 0, 1);
    p.damage = DiceExpr{1, 3, 0};
    const auto groups = create_allocation_groups(u);
    RecordingChooser chooser;
    EXPECT_THROW((void)inflict_damage(u, groups, std::vector<int>{2}, p, chooser.fn()), std::logic_error);
}

TEST(R05_AttackSequence, DestroyedTargetIsAPreconditionViolation) {
    Unit u = test_unit({});
    const AttackBatch batch{u.id, test_profile(3, 4, 0, 1), {}, 1};
    ScriptedDice dice{6};
    EXPECT_THROW((void)resolve_attack_batch(dice, u, batch, never_choose_order(), never_choose_model()),
                 std::logic_error);
}

TEST(R05_AttackSequence, NoWoundsMeansNoSaveRollsOrChoices) {
    GameState gs = load_v1_scenario();
    Unit& gants = gs.units[1];
    const AttackBatch batch{gants.id, test_profile(3, 4, 0, 1), {}, 2};
    ScriptedDice dice{1, 2};  // both miss
    const BatchResult r = resolve_attack_batch(dice, gants, batch, never_choose_order(), never_choose_model());
    EXPECT_EQ(r.hits.hits(), 0);
    EXPECT_TRUE(r.wounds.rolls.empty());
    EXPECT_TRUE(r.save_rolls.empty());
    EXPECT_EQ(gants.models.size(), 10u);
    EXPECT_EQ(dice.remaining(), 0u);
}
