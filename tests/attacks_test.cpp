#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "core/rules/attacks.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::load_v1_scenario;
using wh40k::test::load_v1_scenario_closer;

namespace {

// Scenario weapon order: 0 Bolt Rifle (profiles 0 Focused Fire, 1 Saturation), 1 Bolt pistol,
// 2 Knives and Fists. Termagants: 0 Fleshborer, 1 Chitinous claws and teeth.
constexpr std::size_t kBoltRifle = 0;
constexpr std::size_t kBoltPistol = 1;
constexpr std::size_t kKnives = 2;
constexpr std::size_t kFocused = 0;
constexpr std::size_t kSaturation = 1;

const AttackContext kShootingHeavy{AttackType::Shooting, true};
const AttackContext kShootingNoHeavy{AttackType::Shooting, false};
const AttackContext kFighting{AttackType::Fighting, false};

WeaponSelection sel(const GameState& gs, std::size_t model, std::size_t weapon, std::size_t profile,
                    UnitId target) {
    return select_weapon_target(gs, gs.units[0].models[model].id, weapon, profile, target);
}

const WeaponProfile& marine_profile(const GameState& gs, std::size_t weapon, std::size_t profile) {
    return gs.units[0].models[0].weapons[weapon].profiles[profile];
}

}  // namespace

// ---- 04.02 Select Targets: facts recorded for Gather Attack Dice ----

TEST(R04_02_SelectTargets, RecordsTargetModelCountAndHalfRange) {
    const GameState gs = load_v1_scenario_closer();
    const WeaponSelection s = sel(gs, 0, kBoltRifle, kSaturation, gs.units[1].id);
    EXPECT_EQ(s.target_models_at_selection, 10);
    EXPECT_TRUE(s.target_within_half_range);
}

TEST(R24_30_RapidFire, HalfRangeIsMeasuredToTheClosestTargetModel) {
    // D-008 start positions: the centre Intercessor is exactly 12" (half of 24") from the nearest
    // Termagant, the end Intercessor is 0.07 mm further.
    const GameState gs = load_v1_scenario();
    EXPECT_TRUE(sel(gs, 2, kBoltRifle, kSaturation, gs.units[1].id).target_within_half_range);
    EXPECT_FALSE(sel(gs, 0, kBoltRifle, kSaturation, gs.units[1].id).target_within_half_range);
}

// ---- 04.03 Gather Attack Dice ----

TEST(R24_30_RapidFire, AddsXDiceWithinHalfRange) {
    const GameState gs = load_v1_scenario();
    const WeaponProfile& saturation = marine_profile(gs, kBoltRifle, kSaturation);
    const WeaponSelection in_half = sel(gs, 2, kBoltRifle, kSaturation, gs.units[1].id);
    const WeaponSelection outside = sel(gs, 0, kBoltRifle, kSaturation, gs.units[1].id);
    EXPECT_EQ(gather_attack_dice(saturation, in_half, kShootingHeavy), 3);  // A2 + Rapid Fire 1
    EXPECT_EQ(gather_attack_dice(saturation, outside, kShootingHeavy), 2);
}

TEST(R24_05_Blast, AddsXDicePerFiveTargetModelsRoundingDown) {
    WeaponProfile frag;
    frag.name = "Frag";
    frag.kind = WeaponKind::Ranged;
    frag.range_mm = 24 * MM_PER_INCH;
    frag.attacks = DiceExpr::fixed(4);
    frag.skill = 3;
    frag.abilities = {ability::Blast{1}};
    WeaponSelection s;
    s.target_models_at_selection = 10;
    EXPECT_EQ(gather_attack_dice(frag, s, kShootingNoHeavy), 6);
    s.target_models_at_selection = 9;
    EXPECT_EQ(gather_attack_dice(frag, s, kShootingNoHeavy), 5);
    s.target_models_at_selection = 4;
    EXPECT_EQ(gather_attack_dice(frag, s, kShootingNoHeavy), 4);
    frag.abilities = {ability::Blast{2}};
    s.target_models_at_selection = 12;  // 24.05 example: BLAST 2, A3, 12 models -> 7
    frag.attacks = DiceExpr::fixed(3);
    EXPECT_EQ(gather_attack_dice(frag, s, kShootingNoHeavy), 7);
}

TEST(R04_02_SplittingMeleeAttacks, GathersOnlyTheDeclaredAttacks) {
    const GameState gs = load_v1_scenario();
    const WeaponProfile& knives = marine_profile(gs, kKnives, 0);  // A3
    WeaponSelection s;
    s.declared_attacks = 2;
    EXPECT_EQ(gather_attack_dice(knives, s, kFighting), 2);
    s.declared_attacks = 4;
    EXPECT_THROW((void)gather_attack_dice(knives, s, kFighting), std::logic_error);
    s.declared_attacks = 0;
    EXPECT_THROW((void)gather_attack_dice(knives, s, kFighting), std::logic_error);
    s.declared_attacks.reset();
    EXPECT_EQ(gather_attack_dice(knives, s, kFighting), 3);
}

TEST(R04_01_SelectWeapons, WeaponTypeMustMatchShootingOrFighting) {
    const GameState gs = load_v1_scenario();
    const WeaponSelection s;
    EXPECT_THROW((void)gather_attack_dice(marine_profile(gs, kKnives, 0), s, kShootingNoHeavy),
                 std::logic_error);
    EXPECT_THROW((void)gather_attack_dice(marine_profile(gs, kBoltPistol, 0), s, kFighting),
                 std::logic_error);
}

TEST(D002_RandomCharacteristics, RandomAttacksAreNotRolled) {
    WeaponProfile web;
    web.kind = WeaponKind::Ranged;
    web.attacks = DiceExpr{1, 6, 0};
    web.abilities = {ability::Torrent{}};
    EXPECT_THROW((void)gather_attack_dice(web, WeaponSelection{}, kShootingNoHeavy), std::logic_error);
}

// ---- D-006 Identical attacks ----

TEST(D006_IdenticalAttacks, BoltPistolAndSaturationFromDifferentModelsAreOneBatch) {
    const GameState gs = load_v1_scenario_closer();
    const UnitId gants = gs.units[1].id;
    const std::vector<WeaponSelection> s{
        sel(gs, 0, kBoltRifle, kSaturation, gants),
        sel(gs, 1, kBoltRifle, kSaturation, gants),
        sel(gs, 2, kBoltPistol, 0, gants),
        sel(gs, 3, kBoltRifle, kFocused, gants),
        sel(gs, 4, kBoltRifle, kFocused, gants),
    };
    const auto batches = gather_attack_batches(gs, s, kShootingHeavy);
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[0].selections, (std::vector<std::size_t>{0, 1, 2}));
    EXPECT_EQ(batches[0].attack_dice, 3 + 3 + 1);  // 2x (A2 + RF1), 1x A1
    EXPECT_EQ(batches[0].profile.strength, 5);
    EXPECT_EQ(batches[0].profile.hit_modifier, 0);
    // Focused Fire: S6 D2, a separate batch.
    EXPECT_EQ(batches[1].selections, (std::vector<std::size_t>{3, 4}));
    EXPECT_EQ(batches[1].attack_dice, 2 + 2);  // 2x (A1 + RF1)
    EXPECT_EQ(batches[1].profile.strength, 6);
    EXPECT_EQ(batches[1].profile.hit_modifier, 1);  // Heavy applies
}

TEST(D006_IdenticalAttacks, FocusedFireWithAndWithoutHeavyBonusDiffer) {
    const GameState gs = load_v1_scenario();
    const WeaponProfile& focused = marine_profile(gs, kBoltRifle, kFocused);
    const AttackProfile heavy = make_attack_profile(focused, kShootingHeavy);
    const AttackProfile no_heavy = make_attack_profile(focused, kShootingNoHeavy);
    EXPECT_EQ(heavy.hit_modifier, 1);
    EXPECT_EQ(no_heavy.hit_modifier, 0);
    EXPECT_NE(heavy, no_heavy);
}

TEST(D006_IdenticalAttacks, HeavyThatDoesNotApplyIsIgnored) {
    const GameState gs = load_v1_scenario();
    const WeaponProfile& focused = marine_profile(gs, kBoltRifle, kFocused);
    WeaponProfile not_heavy = focused;
    not_heavy.abilities = {ability::RapidFire{1}};
    EXPECT_EQ(make_attack_profile(focused, kShootingNoHeavy), make_attack_profile(not_heavy, kShootingNoHeavy));
    EXPECT_NE(make_attack_profile(focused, kShootingHeavy), make_attack_profile(not_heavy, kShootingHeavy));
}

TEST(D006_IdenticalAttacks, HeavyNeverAppliesWhileFighting) {
    const GameState gs = load_v1_scenario();
    WeaponProfile heavy_melee = marine_profile(gs, kKnives, 0);
    heavy_melee.abilities = {ability::Heavy{}};
    const AttackContext fight_flag_set{AttackType::Fighting, true};
    EXPECT_EQ(make_attack_profile(heavy_melee, fight_flag_set).hit_modifier, 0);
}

TEST(D006_IdenticalAttacks, AttackTimeAbilitiesSplitBatchesInAnyListedOrder) {
    const GameState gs = load_v1_scenario();
    WeaponProfile a = marine_profile(gs, kBoltPistol, 0);
    WeaponProfile b = a;
    a.abilities = {ability::Hazardous{}, ability::Unknown{"Lethal Hits"}};
    b.abilities = {ability::Unknown{"Lethal Hits"}, ability::CloseQuarters{}, ability::Hazardous{}};
    EXPECT_EQ(make_attack_profile(a, kShootingNoHeavy), make_attack_profile(b, kShootingNoHeavy));
    WeaponProfile c = a;
    c.abilities = {ability::Hazardous{}};
    EXPECT_NE(make_attack_profile(a, kShootingNoHeavy), make_attack_profile(c, kShootingNoHeavy));
}

TEST(D006_IdenticalAttacks, TorrentHasNoSkill) {
    const GameState gs = load_v1_scenario();
    WeaponProfile flamer = marine_profile(gs, kBoltPistol, 0);
    flamer.skill.reset();
    flamer.abilities = {ability::Torrent{}};
    const AttackProfile p = make_attack_profile(flamer, kShootingNoHeavy);
    EXPECT_TRUE(p.torrent);
    EXPECT_FALSE(p.skill.has_value());
    EXPECT_NE(p, make_attack_profile(marine_profile(gs, kBoltPistol, 0), kShootingNoHeavy));
}

TEST(R04_03_ResolveAttacks, BatchesAreGroupedByTargetInFirstAppearanceOrder) {
    GameState gs = load_v1_scenario_closer();
    Unit second = gs.units[1];
    second.id = UnitId{3};
    for (auto& m : second.models) {
        m.id.v += 100;
    }
    gs.units.push_back(second);
    const std::vector<WeaponSelection> s{
        sel(gs, 0, kBoltRifle, kSaturation, gs.units[1].id),
        sel(gs, 1, kBoltRifle, kSaturation, UnitId{3}),
        sel(gs, 2, kBoltRifle, kFocused, gs.units[1].id),
        sel(gs, 3, kBoltRifle, kSaturation, gs.units[1].id),
    };
    const auto batches = gather_attack_batches(gs, s, kShootingNoHeavy);
    ASSERT_EQ(batches.size(), 3u);
    EXPECT_EQ(batches[0].target, gs.units[1].id);
    EXPECT_EQ(batches[0].selections, (std::vector<std::size_t>{0, 3}));
    EXPECT_EQ(batches[1].target, gs.units[1].id);
    EXPECT_EQ(batches[1].selections, (std::vector<std::size_t>{2}));
    EXPECT_EQ(batches[2].target, UnitId{3});
    EXPECT_EQ(batches[2].selections, (std::vector<std::size_t>{1}));
}

TEST(R04_03_ResolveAttacks, InvalidSelectionIsProgrammerError) {
    const GameState gs = load_v1_scenario();
    EXPECT_THROW((void)select_weapon_target(gs, ModelId{999}, 0, 0, gs.units[1].id), std::logic_error);
    EXPECT_THROW((void)select_weapon_target(gs, gs.units[0].models[0].id, 7, 0, gs.units[1].id),
                 std::logic_error);
    EXPECT_THROW((void)select_weapon_target(gs, gs.units[0].models[0].id, 0, 5, gs.units[1].id),
                 std::logic_error);
    EXPECT_THROW((void)select_weapon_target(gs, gs.units[0].models[0].id, 0, 0, UnitId{42}),
                 std::logic_error);
}

// ---- D-037: ability_effects / batch_context (read-only explanations for clients) ----

TEST(R24_30_RapidFire, AbilityEffectsGiveTheExtraDiceOnlyWithinHalfRange) {
    const GameState gs = load_v1_scenario();
    const WeaponProfile& focused = marine_profile(gs, kBoltRifle, kFocused);  // [HEAVY] [RAPID FIRE 1]
    const WeaponSelection in_half = sel(gs, 2, kBoltRifle, kFocused, gs.units[1].id);
    const WeaponSelection outside = sel(gs, 0, kBoltRifle, kFocused, gs.units[1].id);
    for (const WeaponSelection& s : {in_half, outside}) {
        int extra = 0;
        for (const AbilityEffect& e : ability_effects(focused, s, kShootingNoHeavy)) extra += e.extra_attack_dice;
        EXPECT_EQ(focused.attacks.fixed_value() + extra, gather_attack_dice(focused, s, kShootingNoHeavy));
    }
    const auto effects = ability_effects(focused, in_half, kShootingNoHeavy);
    ASSERT_EQ(effects.size(), focused.abilities.size());
    bool found = false;
    for (const AbilityEffect& e : effects) {
        if (!std::holds_alternative<ability::RapidFire>(e.ability)) continue;
        found = true;
        EXPECT_TRUE(e.active);
        EXPECT_EQ(e.extra_attack_dice, 1);
    }
    EXPECT_TRUE(found);
    for (const AbilityEffect& e : ability_effects(focused, outside, kShootingNoHeavy)) {
        if (std::holds_alternative<ability::RapidFire>(e.ability)) EXPECT_FALSE(e.active);
    }
}

TEST(R24_16_Heavy, AbilityEffectsGivePlusOneToHitOnlyWhenTheConditionsApply) {
    const GameState gs = load_v1_scenario();
    const WeaponProfile& focused = marine_profile(gs, kBoltRifle, kFocused);
    const WeaponSelection s = sel(gs, 2, kBoltRifle, kFocused, gs.units[1].id);
    auto heavy = [&](const AttackContext& c) {
        for (const AbilityEffect& e : ability_effects(focused, s, c)) {
            if (std::holds_alternative<ability::Heavy>(e.ability)) return e;
        }
        return AbilityEffect{};
    };
    EXPECT_TRUE(heavy(kShootingHeavy).active);
    EXPECT_EQ(heavy(kShootingHeavy).hit_modifier, make_attack_profile(focused, kShootingHeavy).hit_modifier);
    EXPECT_FALSE(heavy(kShootingNoHeavy).active);
    EXPECT_EQ(heavy(kShootingNoHeavy).hit_modifier, 0);
}

TEST(R24_16_Heavy, BatchContextRecoversWhetherTheBonusApplied) {
    const GameState gs = load_v1_scenario();
    const std::vector<WeaponSelection> sels{sel(gs, 2, kBoltRifle, kFocused, gs.units[1].id)};
    for (const AttackContext& c : {kShootingHeavy, kShootingNoHeavy}) {
        const auto batches = gather_attack_batches(gs, sels, c);
        ASSERT_EQ(batches.size(), 1u);
        EXPECT_EQ(batch_context(batches[0], sels[0], AttackType::Shooting), c);
    }
}

TEST(R24_05_Blast, AbilityEffectsGiveXDicePerFiveModels) {
    WeaponProfile frag;
    frag.kind = WeaponKind::Ranged;
    frag.attacks = DiceExpr::fixed(3);
    frag.skill = 3;
    frag.abilities = {ability::Blast{2}};
    WeaponSelection s;
    s.target_models_at_selection = 12;
    const auto effects = ability_effects(frag, s, kShootingNoHeavy);
    ASSERT_EQ(effects.size(), 1u);
    EXPECT_TRUE(effects[0].active);
    EXPECT_EQ(effects[0].extra_attack_dice, 4);
    s.target_models_at_selection = 4;
    EXPECT_FALSE(ability_effects(frag, s, kShootingNoHeavy)[0].active);
}

TEST(R24_37_Torrent, AbilityEffectsMarkTorrentActiveAndAssaultNot) {
    WeaponProfile flamer;
    flamer.kind = WeaponKind::Ranged;
    flamer.attacks = DiceExpr::fixed(1);
    flamer.abilities = {ability::Torrent{}, ability::Assault{}, ability::TwinLinked{}};
    const auto effects = ability_effects(flamer, WeaponSelection{}, kShootingHeavy);
    ASSERT_EQ(effects.size(), 3u);
    EXPECT_TRUE(effects[0].active);
    EXPECT_FALSE(effects[1].active);  // acts when the shooting type is chosen (10.05)
    EXPECT_TRUE(effects[2].active);   // 24.38: the wound rolls can be re-rolled (D-072, M61; was refused, D-026)
}
