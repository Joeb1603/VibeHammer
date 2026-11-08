// M24: rules help for weapon abilities and keywords, and what abilities do to a volley (D-037).

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

#include "board_interaction.hpp"
#include "casualty_plan.hpp"
#include "datasheet_view.hpp"
#include "decision_view.hpp"
#include "dice_cards.hpp"
#include "rules_help.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;

namespace {

bool plain_ascii(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 128; });
}

void expect_readable(const HelpEntry& h) {
    EXPECT_FALSE(h.title.empty());
    EXPECT_FALSE(h.summary.empty()) << h.title;
    EXPECT_FALSE(h.rule.empty()) << h.title;
    EXPECT_TRUE(plain_ascii(h.title + h.summary + h.rule)) << h.title;
}

const EffectLine* line_starting(const std::vector<EffectLine>& lines, const std::string& prefix) {
    for (const EffectLine& l : lines) {
        if (l.text.rfind(prefix, 0) == 0) return &l;
    }
    return nullptr;
}

// The weapon menu Side A's Intercessors get when they click the Termagants in `gs`.
std::vector<MenuEntry> weapon_menu(const GameState& gs) {
    const auto legal = legal_actions(gs, Side::A);
    BoardInteraction board(gs, legal);
    (void)board.click(gs, gs.units[0].id);
    return board.click(gs, gs.units[1].id).menu;
}

}  // namespace

TEST(M24_RulesHelp, EveryWeaponAbilityHasPlainHelp) {
    const std::vector<WeaponAbility> all{ability::Assault{},    ability::Heavy{},     ability::CloseQuarters{},
                                         ability::RapidFire{2}, ability::Blast{1},    ability::TwinLinked{},
                                         ability::Torrent{},    ability::Hazardous{}, ability::DevastatingWounds{},
                                         ability::SustainedHits{1}, ability::ExtraAttacks{},
                                         ability::Unknown{"Lethal Hits"}};
    ASSERT_EQ(all.size(), std::variant_size_v<WeaponAbility>);
    for (const WeaponAbility& a : all) expect_readable(weapon_ability_help(a));
    EXPECT_TRUE(weapon_ability_help(ability::Heavy{}).in_this_game);
    EXPECT_EQ(weapon_ability_help(ability::RapidFire{2}).rule, "24.30");
    EXPECT_FALSE(weapon_ability_help(ability::TwinLinked{}).in_this_game);
    EXPECT_FALSE(weapon_ability_help(ability::Unknown{"Lethal Hits"}).in_this_game);
    // M60 (D-071): 24.15, 24.36 and 24.11 are in the game now.
    EXPECT_TRUE(weapon_ability_help(ability::Hazardous{}).in_this_game);
    EXPECT_EQ(weapon_ability_help(ability::SustainedHits{1}).rule, "24.36");
    EXPECT_TRUE(weapon_ability_help(ability::SustainedHits{1}).in_this_game);
    EXPECT_EQ(weapon_ability_help(ability::ExtraAttacks{}).rule, "24.11");
    EXPECT_TRUE(weapon_ability_help(ability::ExtraAttacks{}).in_this_game);
}

TEST(M24_RulesHelp, EveryKeywordOnTheDatasheetsHasPlainHelp) {
    const GameState gs = wh40k::test::load_v1_scenario();
    for (const Unit& u : gs.units) {
        const DatasheetView v = datasheet_view(u);
        ASSERT_EQ(v.keyword_help.size(), u.keywords.size());
        ASSERT_EQ(v.faction_keyword_help.size(), u.faction_keywords.size());
        for (const HelpEntry& h : v.keyword_help) expect_readable(h);
        for (const HelpEntry& h : v.faction_keyword_help) expect_readable(h);
        for (const auto* rows : {&v.ranged, &v.melee}) {
            for (const DatasheetWeaponRow& r : *rows) {
                for (const HelpEntry& h : r.ability_help) expect_readable(h);
            }
        }
    }
    EXPECT_EQ(keyword_help("Infantry").title, "INFANTRY");
    EXPECT_EQ(keyword_help("Explosives").rule, "15.05");
    EXPECT_FALSE(keyword_help("Infantry").in_this_game);  // no terrain yet
}

TEST(M24_RulesHelp, DatasheetRowsCarryTheirAbilitiesHelp) {
    const GameState gs = wh40k::test::load_v1_scenario();
    const DatasheetView v = datasheet_view(gs.units[0]);
    const auto focused = std::find_if(v.ranged.begin(), v.ranged.end(),
                                      [](const DatasheetWeaponRow& r) { return r.name == "Bolt Rifle - Focused Fire"; });
    ASSERT_NE(focused, v.ranged.end());
    ASSERT_EQ(focused->ability_help.size(), 2u);
    EXPECT_EQ(focused->ability_help[0].title, "[HEAVY]");
    EXPECT_EQ(focused->ability_help[1].title, "[RAPID FIRE 1]");
}

TEST(M24_WeaponMenu, FocusedFireShowsHeavyAndRapidFireApplyingWhenStationaryAndClose) {
    const GameState gs = wh40k::test::shoot_step_state();  // the Intercessors remained stationary
    const auto menu = weapon_menu(gs);
    ASSERT_GE(menu.size(), 3u);
    ASSERT_EQ(menu[0].label, "Bolt Rifle - Focused Fire: 5 models");
    EXPECT_EQ(menu[0].detail.rfind("24\"  A", 0), 0u) << menu[0].detail;
    ASSERT_EQ(menu[0].help.size(), 2u);
    const EffectLine* heavy = line_starting(menu[0].effects, "Heavy");
    ASSERT_NE(heavy, nullptr);
    EXPECT_EQ(heavy->kind, EffectKind::Active) << heavy->text;
    const EffectLine* rapid = line_starting(menu[0].effects, "Rapid Fire 1");
    ASSERT_NE(rapid, nullptr);
    EXPECT_NE(rapid->text.find("half range, 12\""), std::string::npos) << rapid->text;
    // Saturation: [ASSAULT] is explained, but doesn't change these attacks.
    const EffectLine* assault = line_starting(menu[1].effects, "Assault");
    ASSERT_NE(assault, nullptr);
    EXPECT_EQ(assault->kind, EffectKind::Info);
    for (const MenuEntry& e : menu) {
        for (const EffectLine& l : e.effects) EXPECT_TRUE(plain_ascii(l.text)) << l.text;
    }
}

TEST(M24_WeaponMenu, HeavyShowsAsNotApplyingAfterAMoveOfMoreThanThreeInches) {
    GameState gs = wh40k::test::shoot_step_state();
    gs.units[0].turn_flags.moved_over_3in = true;
    const auto menu = weapon_menu(gs);
    ASSERT_FALSE(menu.empty());
    const EffectLine* heavy = line_starting(menu[0].effects, "Heavy");
    ASSERT_NE(heavy, nullptr);
    EXPECT_EQ(heavy->kind, EffectKind::Inactive) << heavy->text;
}

TEST(M24_DiceCards, TheHitCardSaysHowManyDiceAndWhichAbilitiesApplied) {
    GameSession session(wh40k::test::shoot_step_state());
    const std::size_t from = session.log().size();
    const auto menu = weapon_menu(session.state());
    ASSERT_FALSE(menu.empty());
    ASSERT_TRUE(session.submit(menu.front().action).ok());  // Focused Fire, every model
    for (int i = 0; i < 50 && session.pending() && !std::holds_alternative<decision::Shoot>(session.pending()->kind);
         ++i) {
        const DecisionView view = make_decision_view(session.legal());
        GameAction a = view.buttons.empty() ? GameAction{} : view.buttons.front();
        for (const auto& t : view.templates) {
            if (const auto* c = std::get_if<action::OrderCasualties>(&t)) a = CasualtyPlan(session.state(), c->target).action();
        }
        ASSERT_TRUE(session.submit(a).ok());
    }
    std::vector<GameEvent> events;
    for (std::size_t i = from; i < session.log().size(); ++i) {
        events.insert(events.end(), session.log()[i].events.begin(), session.log()[i].events.end());
    }
    const auto selected = wh40k::test::events_of<event::AttackBatchSelected>(events);
    ASSERT_FALSE(selected.empty());
    const std::vector<DiceCard> cards = dice_cards(session.state(), session.log(), from);
    ASSERT_FALSE(cards.empty());
    ASSERT_EQ(cards[0].title, "Hit rolls");
    ASSERT_FALSE(cards[0].notes.empty());
    EXPECT_EQ(cards[0].notes[0].text, std::to_string(selected[0].attack_dice) + " attack dice from 5 models");
    const EffectLine* heavy = line_starting(cards[0].notes, "Heavy");
    ASSERT_NE(heavy, nullptr);
    EXPECT_EQ(heavy->kind, EffectKind::Active);
    EXPECT_NE(line_starting(cards[0].notes, "Rapid Fire 1"), nullptr);
}

TEST(M24_RulesHelp, ProfileStatsReadLikeTheDatasheet) {
    WeaponProfile p;
    p.kind = WeaponKind::Ranged;
    p.range_mm = 24 * MM_PER_INCH;
    p.attacks = DiceExpr::fixed(2);
    p.skill = 3;
    p.strength = 4;
    p.armour_penetration = -1;
    p.damage = DiceExpr::fixed(1);
    EXPECT_EQ(profile_stats(p), "24\"  A2  BS3+  S4  AP-1  D1");
    p.kind = WeaponKind::Melee;
    p.range_mm.reset();
    EXPECT_EQ(profile_stats(p), "Melee  A2  WS3+  S4  AP-1  D1");
}

TEST(M24_WeaponMenu, AssaultAndCloseQuartersApplyWhenTheyAreWhatLetsTheUnitShoot) {
    WeaponProfile p;
    p.kind = WeaponKind::Ranged;
    p.range_mm = 24 * MM_PER_INCH;
    p.attacks = DiceExpr::fixed(2);
    p.skill = 3;
    p.abilities = {ability::Assault{}, ability::CloseQuarters{}};
    const std::vector<WeaponSelection> sels{WeaponSelection{}};
    const AttackContext ctx{AttackType::Shooting, false};
    const auto normal = effect_lines(p, sels, ctx, ShootingType::Normal);
    ASSERT_EQ(normal.size(), 2u);
    EXPECT_EQ(normal[0].kind, EffectKind::Info);
    EXPECT_EQ(normal[1].kind, EffectKind::Info);
    const auto assault = effect_lines(p, sels, ctx, ShootingType::Assault);
    EXPECT_EQ(assault[0].kind, EffectKind::Active) << assault[0].text;
    EXPECT_EQ(assault[1].kind, EffectKind::Info);
    const auto cq = effect_lines(p, sels, ctx, ShootingType::CloseQuarters);
    EXPECT_EQ(cq[0].kind, EffectKind::Info);
    EXPECT_EQ(cq[1].kind, EffectKind::Active) << cq[1].text;
}
