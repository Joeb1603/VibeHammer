#include <gtest/gtest.h>

#include "core/data/datasheet.hpp"
#include "core/data/load_error.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::in;
using wh40k::test::repo_root;

namespace {

// Loaded once and kept alive, so pointers/references into them stay valid within a test.
const Datasheet& intercessors() {
    static const Datasheet ds = load_datasheet(repo_root() / "data/units/intercessor_squad.json");
    return ds;
}
const Datasheet& termagants() {
    static const Datasheet ds = load_datasheet(repo_root() / "data/units/termagants.json");
    return ds;
}

const WeaponProfile& only_profile(const Datasheet& ds, const char* weapon) {
    const Weapon* w = ds.find_weapon(weapon);
    if (w == nullptr || w->profiles.size() != 1) {
        throw std::logic_error(std::string("expected one profile for ") + weapon);
    }
    return w->profiles.front();
}

}  // namespace

TEST(R02_Datasheet, IntercessorSquadHeader) {
    const Datasheet& ds = intercessors();
    EXPECT_EQ(ds.name, "Intercessor Squad");
    EXPECT_EQ(ds.source_commit, "0f9035541630ae6d83869bab34b63d909767d081");
    EXPECT_EQ(ds.faction_keywords, (std::vector<std::string>{"Adeptus Astartes"}));
    EXPECT_NE(std::find(ds.keywords.begin(), ds.keywords.end(), "Infantry"), ds.keywords.end());
    EXPECT_EQ(ds.min_models, 5);
    EXPECT_EQ(ds.max_models, 10);
    EXPECT_EQ(ds.abilities.size(), 4u);
}

TEST(R02_02_Profiles, IntercessorCharacteristics) {
    const ModelTypeSheet* mt = intercessors().find_model_type("Intercessor");
    ASSERT_NE(mt, nullptr);
    const Characteristics& c = mt->characteristics;
    ASSERT_TRUE(c.move_mm.has_value());
    EXPECT_FLOAT_EQ(*c.move_mm, in(6));
    EXPECT_EQ(c.toughness, 5);
    EXPECT_EQ(c.save, 3);
    EXPECT_FALSE(c.invuln_save.has_value());
    EXPECT_EQ(c.wounds, 2);
    EXPECT_EQ(c.leadership, 6);
    EXPECT_EQ(c.objective_control, 2);
    EXPECT_EQ(mt->base, (Base{BaseShape::Round, 32.0f, 0.0f}));
}

TEST(R02_04_Weapons, BoltRifleHasBothProfiles) {
    const Weapon* w = intercessors().find_weapon("Bolt Rifle");
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->kind, WeaponKind::Ranged);
    ASSERT_EQ(w->profiles.size(), 2u);

    const WeaponProfile& focused = w->profiles[0];
    EXPECT_EQ(focused.name, "Bolt Rifle - Focused Fire");
    EXPECT_FLOAT_EQ(*focused.range_mm, in(24));
    EXPECT_EQ(focused.attacks, DiceExpr::fixed(1));
    EXPECT_EQ(focused.skill, 3);
    EXPECT_EQ(focused.strength, 6);
    EXPECT_EQ(focused.armour_penetration, -1);
    EXPECT_EQ(focused.damage, DiceExpr::fixed(2));
    EXPECT_EQ(focused.abilities,
              (std::vector<WeaponAbility>{ability::Heavy{}, ability::RapidFire{1}}));

    const WeaponProfile& saturation = w->profiles[1];
    EXPECT_EQ(saturation.name, "Bolt Rifle - Saturation");
    EXPECT_EQ(saturation.attacks, DiceExpr::fixed(2));
    EXPECT_EQ(saturation.strength, 5);
    EXPECT_EQ(saturation.damage, DiceExpr::fixed(1));
    EXPECT_EQ(saturation.abilities,
              (std::vector<WeaponAbility>{ability::Assault{}, ability::RapidFire{1}}));
}

TEST(R02_04_Weapons, BoltPistolIsCloseQuarters) {
    const WeaponProfile& p = only_profile(intercessors(), "Bolt pistol");
    EXPECT_FLOAT_EQ(*p.range_mm, in(12));
    EXPECT_TRUE(p.has<ability::CloseQuarters>());
}

TEST(R02_04_Weapons, MeleeWeaponHasNoRange) {
    const Weapon* w = intercessors().find_weapon("Knives and Fists");
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->kind, WeaponKind::Melee);
    EXPECT_FALSE(w->profiles[0].range_mm.has_value());
    EXPECT_EQ(w->profiles[0].skill, 3);  // WS 3+
}

TEST(R02_04_Weapons, FragGrenadeBlast1) {
    const Weapon* w = intercessors().find_weapon("Grenade Launcher");
    ASSERT_NE(w, nullptr);
    ASSERT_EQ(w->profiles.size(), 2u);
    const ability::Blast* blast = w->profiles[1].get<ability::Blast>();
    ASSERT_NE(blast, nullptr);
    EXPECT_EQ(blast->x, 1);
}

TEST(R02_04_Weapons, PlasmaPistolHazardous) {
    const Weapon* w = intercessors().find_weapon("Plasma pistol");
    ASSERT_NE(w, nullptr);
    EXPECT_TRUE(w->profiles[0].has<ability::Hazardous>());
    EXPECT_TRUE(w->profiles[0].has<ability::CloseQuarters>());
}

TEST(R02_02_Profiles, TermagantCharacteristics) {
    const ModelTypeSheet* mt = termagants().find_model_type("Termagants");
    ASSERT_NE(mt, nullptr);
    const Characteristics& c = mt->characteristics;
    EXPECT_FLOAT_EQ(*c.move_mm, in(6));
    EXPECT_EQ(c.toughness, 3);
    EXPECT_EQ(c.save, 5);
    EXPECT_FALSE(c.invuln_save.has_value());
    EXPECT_EQ(c.wounds, 1);
    EXPECT_EQ(c.leadership, 8);
    EXPECT_EQ(c.objective_control, 2);
    EXPECT_EQ(mt->base, (Base{BaseShape::Round, 28.5f, 0.0f}));
}

TEST(R24_37_Torrent, StranglewebLoadsWithNoSkillAndD6Attacks) {
    const WeaponProfile& p = only_profile(termagants(), "Strangleweb");
    EXPECT_FALSE(p.skill.has_value());  // "N/A" in data
    EXPECT_EQ(p.attacks, (DiceExpr{1, 6, 0}));
    EXPECT_TRUE(p.has<ability::Torrent>());
    EXPECT_TRUE(p.has<ability::Assault>());
    EXPECT_TRUE(p.has<ability::DevastatingWounds>());
}

TEST(R24_27_Pistol, SpinefistLoadsPistolAsCloseQuarters) {
    const WeaponProfile& p = only_profile(termagants(), "Termagant spinefist");
    EXPECT_TRUE(p.has<ability::CloseQuarters>());
    EXPECT_TRUE(p.has<ability::Assault>());
    EXPECT_TRUE(p.has<ability::TwinLinked>());
    EXPECT_FALSE(p.has<ability::Unknown>());
}

TEST(R24_05_Blast, ShardlauncherD3AttacksBareBlast) {
    const WeaponProfile& p = only_profile(termagants(), "Shardlauncher");
    EXPECT_EQ(p.attacks, (DiceExpr{1, 3, 0}));
    const ability::Blast* blast = p.get<ability::Blast>();
    ASSERT_NE(blast, nullptr);
    EXPECT_EQ(blast->x, 1);
    EXPECT_TRUE(p.has<ability::Heavy>());
}

TEST(R02_04_Weapons, FleshborerProfile) {
    const WeaponProfile& p = only_profile(termagants(), "Fleshborer");
    EXPECT_FLOAT_EQ(*p.range_mm, in(18));
    EXPECT_EQ(p.attacks, DiceExpr::fixed(1));
    EXPECT_EQ(p.skill, 4);
    EXPECT_EQ(p.strength, 5);
    EXPECT_EQ(p.armour_penetration, 0);
    EXPECT_EQ(p.abilities, (std::vector<WeaponAbility>{ability::Assault{}}));
}

TEST(R02_04_Weapons, UnknownAbilityInDataLoadsAsUnknown) {
    nlohmann::json j = nlohmann::json::parse(R"({
        "name": "Test Unit",
        "composition": {"min_models": 1, "max_models": 1},
        "model_types": [],
        "weapons": {
            "Odd gun": {"type": "ranged", "profiles": [
                {"name": "Odd gun", "range_in": 12, "A": 1, "skill": 3, "S": 4, "AP": 0, "D": 1,
                 "abilities": ["Lethal Hits", "Anti-Infantry 4+"]}]}
        }
    })");
    Datasheet ds;
    ASSERT_NO_THROW(ds = parse_datasheet(j));
    const WeaponProfile& p = ds.weapons.at(0).profiles.at(0);
    EXPECT_EQ(p.abilities, (std::vector<WeaponAbility>{ability::Unknown{"Lethal Hits"},
                                                       ability::Unknown{"Anti-Infantry 4+"}}));
}

TEST(R02_04_Weapons, RangedWeaponWithoutRangeIsDataError) {
    nlohmann::json j = nlohmann::json::parse(R"({
        "name": "Test Unit",
        "composition": {"min_models": 1, "max_models": 1},
        "model_types": [],
        "weapons": {"Bad gun": {"type": "ranged", "profiles": [
            {"name": "Bad gun", "range_in": null, "A": 1, "skill": 3, "S": 4, "AP": 0, "D": 1,
             "abilities": []}]}}
    })");
    EXPECT_THROW((void)parse_datasheet(j), DataError);
}

TEST(R02_Datasheet, MissingFileIsDataError) {
    EXPECT_THROW((void)load_datasheet(repo_root() / "data/units/does_not_exist.json"), DataError);
}
