#include <gtest/gtest.h>

#include "core/model/weapon.hpp"

using namespace wh40k::core;

TEST(R24_Abilities, ParsesSimpleKeywords) {
    EXPECT_EQ(parse_weapon_ability("Assault"), WeaponAbility{ability::Assault{}});
    EXPECT_EQ(parse_weapon_ability("Heavy"), WeaponAbility{ability::Heavy{}});
    EXPECT_EQ(parse_weapon_ability("Torrent"), WeaponAbility{ability::Torrent{}});
    EXPECT_EQ(parse_weapon_ability("Hazardous"), WeaponAbility{ability::Hazardous{}});
    EXPECT_EQ(parse_weapon_ability("Devastating Wounds"), WeaponAbility{ability::DevastatingWounds{}});
    EXPECT_EQ(parse_weapon_ability("Twin-linked"), WeaponAbility{ability::TwinLinked{}});
}

TEST(R24_Abilities, IgnoresCaseBracketsAndUnicodeHyphens) {
    EXPECT_EQ(parse_weapon_ability("[ASSAULT]"), WeaponAbility{ability::Assault{}});
    // U+2011 non-breaking hyphen, as in the rulebook text.
    EXPECT_EQ(parse_weapon_ability("Twin\xE2\x80\x91linked"), WeaponAbility{ability::TwinLinked{}});
    EXPECT_EQ(parse_weapon_ability("CLOSE\xE2\x80\x91QUARTERS"), WeaponAbility{ability::CloseQuarters{}});
}

TEST(R24_07_CloseQuarters, ParsesCloseQuarters) {
    EXPECT_EQ(parse_weapon_ability("Close-quarters"), WeaponAbility{ability::CloseQuarters{}});
}

TEST(R24_27_Pistol, PistolLoadsAsCloseQuarters) {
    EXPECT_EQ(parse_weapon_ability("Pistol"), WeaponAbility{ability::CloseQuarters{}});
}

TEST(R24_30_RapidFire, ParsesRapidFireX) {
    EXPECT_EQ(parse_weapon_ability("Rapid Fire 1"), WeaponAbility{ability::RapidFire{1}});
    EXPECT_EQ(parse_weapon_ability("Rapid Fire 2"), WeaponAbility{ability::RapidFire{2}});
}

TEST(R24_30_RapidFire, RapidFireWithoutXIsUnknown) {
    // 24.30: "always takes the form [RAPID FIRE X]".
    EXPECT_EQ(parse_weapon_ability("Rapid Fire"), WeaponAbility{ability::Unknown{"Rapid Fire"}});
}

TEST(R24_05_Blast, BareBlastIsBlast1) {
    EXPECT_EQ(parse_weapon_ability("Blast"), WeaponAbility{ability::Blast{1}});
    EXPECT_EQ(parse_weapon_ability("Blast 1"), WeaponAbility{ability::Blast{1}});
    EXPECT_EQ(parse_weapon_ability("Blast 2"), WeaponAbility{ability::Blast{2}});
}

TEST(R24_Abilities, UnknownAbilityKeepsOriginalTextAndDoesNotThrow) {
    for (const char* s : {"Lethal Hits", "Anti-Infantry 4+", "Sustained Hits 1", "", "Blast X",
                          "Rapid Fire 0"}) {
        WeaponAbility a;
        EXPECT_NO_THROW(a = parse_weapon_ability(s)) << s;
        EXPECT_EQ(a, WeaponAbility{ability::Unknown{s}}) << s;
    }
}
