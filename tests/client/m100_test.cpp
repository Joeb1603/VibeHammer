// M100 (D-102): the client's side of [LETHAL HITS] (24.23), [PSYCHIC] (24.29), [CLEAVE X] (24.06), [ONE SHOT] (24.26)
// and the keyword-conditioned abilities (24.01): their labels and help, and what they do to a selection (the menus' and
// dice cards' effect lines).

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/model/unit.hpp"
#include "core/rules/attacks.hpp"
#include "datasheet_view.hpp"
#include "event_text.hpp"
#include "rules_help.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;

TEST(M100_Labels, TheAbilitiesReadAsOnTheDatasheet) {
    EXPECT_EQ(weapon_ability_label(ability::LethalHits{}), "[LETHAL HITS]");
    EXPECT_EQ(weapon_ability_label(ability::Psychic{}), "[PSYCHIC]");
    EXPECT_EQ(weapon_ability_label(ability::Cleave{2}), "[CLEAVE 2]");
    EXPECT_EQ(weapon_ability_label(ability::OneShot{}), "[ONE SHOT]");
    // 24.01: the keywords an ability is followed by.
    EXPECT_EQ(weapon_ability_label(parse_weapon_ability("Lethal Hits: Vehicle")), "[LETHAL HITS: VEHICLE]");
    EXPECT_EQ(weapon_ability_label(parse_weapon_ability("Sustained Hits 1: Infantry/Beasts")),
              "[SUSTAINED HITS 1: INFANTRY/BEASTS]");
    EXPECT_EQ(weapon_ability_label(parse_weapon_ability("Devastating Wounds: non-Monster/Vehicle")),
              "[DEVASTATING WOUNDS: NON-MONSTER/VEHICLE]");
    EXPECT_EQ(weapon_ability_label(ability::SustainedHits{1}), "[SUSTAINED HITS 1]");
    EXPECT_EQ(weapon_ability_label(ability::DevastatingWounds{}), "[DEVASTATING WOUNDS]");
}

TEST(M100_Help, TheAbilitiesAreInTheGameWithTheirRules) {
    const std::vector<std::pair<WeaponAbility, std::string>> all{{ability::LethalHits{}, "24.23"},
                                                                 {ability::Psychic{}, "24.29"},
                                                                 {ability::Cleave{1}, "24.06"},
                                                                 {ability::OneShot{}, "24.26"}};
    for (const auto& [a, rule] : all) {
        const HelpEntry h = weapon_ability_help(a);
        EXPECT_TRUE(h.in_this_game) << h.title;
        EXPECT_EQ(h.rule, rule);
        EXPECT_FALSE(h.summary.empty());
        EXPECT_EQ(h.summary.find("Not in this game"), std::string::npos) << h.summary;
    }
}

TEST(M100_EffectLines, SayWhatEachAbilityDoesHere) {
    WeaponProfile weapon;
    weapon.name = "Test blade";
    weapon.kind = WeaponKind::Melee;
    weapon.attacks = DiceExpr::fixed(3);
    weapon.skill = 3;
    weapon.abilities = {ability::LethalHits{ability::TargetCondition{{"vehicle"}, false}}, ability::Psychic{},
                        ability::Cleave{1}, ability::OneShot{},
                        ability::SustainedHits{1, ability::TargetCondition{{"infantry"}, false}}};
    Unit infantry;
    infantry.keywords = {"Infantry"};
    Unit vehicle;
    vehicle.keywords = {"Vehicle"};
    WeaponSelection sel;
    sel.target_models_at_selection = 10;
    const AttackContext fighting{AttackType::Fighting, false};

    std::vector<WeaponSelection> sels{sel};
    std::vector<EffectLine> lines = effect_lines(weapon, sels, fighting, std::nullopt, &infantry);
    ASSERT_EQ(lines.size(), 5u);
    EXPECT_EQ(lines[0].text, "Lethal Hits: no effect (the target doesn't have the keyword)");
    EXPECT_EQ(lines[0].kind, EffectKind::Inactive);
    EXPECT_EQ(lines[1].kind, EffectKind::Active);
    EXPECT_NE(lines[1].text.find("Psychic"), std::string::npos);
    EXPECT_EQ(lines[2].text, "Cleave 1: +2 attacks (10 models in the target)");
    EXPECT_EQ(lines[2].kind, EffectKind::Active);
    EXPECT_EQ(lines[3].kind, EffectKind::Active);
    EXPECT_NE(lines[3].text.find("One Shot"), std::string::npos);
    EXPECT_EQ(lines[4].kind, EffectKind::Active);  // [SUSTAINED HITS 1: INFANTRY] at INFANTRY
    for (const EffectLine& l : lines) EXPECT_NE(l.kind, EffectKind::NotInGame) << l.text;

    lines = effect_lines(weapon, sels, fighting, std::nullopt, &vehicle);
    EXPECT_EQ(lines[0].text, "Lethal Hits: each critical hit wounds automatically");
    EXPECT_EQ(lines[0].kind, EffectKind::Active);
    EXPECT_EQ(lines[4].text, "Sustained Hits 1: no effect (the target doesn't have the keyword)");
    EXPECT_EQ(lines[4].kind, EffectKind::Inactive);

    // A weapon splitting its attacks gets no [CLEAVE] dice (24.06).
    sels[0].declared_attacks = 2;
    lines = effect_lines(weapon, sels, fighting, std::nullopt, &infantry);
    EXPECT_EQ(lines[2].kind, EffectKind::Inactive);
}

TEST(M100_EventText, AutomaticWoundsAreNamedInTheWoundRolls) {
    WoundRolls rolls;
    rolls.required = 4;
    rolls.rolls = {WoundRoll{5, 5, WoundOutcome::Wound}, WoundRoll{2, 2, WoundOutcome::Fail}};
    rolls.automatic = 2;
    const std::string text = format_event(GameEvent{event::WoundRollsMade{0, rolls}});
    EXPECT_NE(text.find("2 automatic"), std::string::npos) << text;
    rolls.automatic = 0;
    EXPECT_EQ(format_event(GameEvent{event::WoundRollsMade{0, rolls}}).find("automatic"), std::string::npos);
}
