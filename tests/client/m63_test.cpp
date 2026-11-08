// M63 (D-073): the client and Monsters and Vehicles. The shaded area of a MONSTER's Normal move passes through enemy
// infantry (17.01, from core's move_reach); the target menu offers an engaged enemy MONSTER to an unengaged unit, with
// the 17.03 line and help; the hit card shows the -1 citing 17.03, and the D-011 cap when 10.06 and 17.03 make -2.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

#include "board_interaction.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "dice_cards.hpp"
#include "launch_options.hpp"
#include "drop_fix.hpp"
#include "overlay_plan.hpp"
#include "reach_area.hpp"
#include "rules_help.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::at;
namespace mf = wh40k::test::monsters;

namespace {

bool has_line(const std::vector<EffectLine>& lines, const std::string& text) {
    return std::any_of(lines.begin(), lines.end(), [&](const EffectLine& l) { return l.text == text; });
}

const DiceCard* find_card(const std::vector<DiceCard>& cards, const std::string& title) {
    for (const DiceCard& c : cards) {
        if (c.title == title) return &c;
    }
    return nullptr;
}

const MenuEntry* entry_named(const std::vector<MenuEntry>& menu, const std::string& prefix) {
    for (const MenuEntry& e : menu) {
        if (e.label.rfind(prefix, 0) == 0) return &e;
    }
    return nullptr;
}

std::vector<ShotDeclaration> lascannons_at(const GameState& gs, std::size_t target) {
    std::vector<ShotDeclaration> out;
    for (std::size_t i = 1; i < gs.units[mf::kHavocs].models.size(); ++i) {
        out.push_back(ShotDeclaration{gs.units[mf::kHavocs].models[i].id, 0, 0, gs.units[target].id});
    }
    return out;
}

const std::string k17_03 = "Engaged MONSTER/VEHICLE target: -1 to hit (17.03)";
const std::string k10_06 = "Close-quarters shooting by a MONSTER/VEHICLE: -1 to hit (10.06)";

}  // namespace

TEST(M63_ReachArea, AMonstersNormalMoveIsShadedThroughEnemyInfantry) {
    GameState gs = mf::at_movement();
    apply_action(gs, Side::B, action::StartMove{gs.units[mf::kTyrant].id, MoveType::Normal, std::nullopt});
    const auto reach = move_reach(gs, gs.units[mf::kTyrant].models[0].id);
    ASSERT_TRUE(reach);
    const ReachRegion region = model_region(*reach);
    const Position beyond = at(22.0f, mf::kThroughY);
    EXPECT_TRUE(in_region(region, beyond));
    EXPECT_FALSE(drop_problem(fix_region(region), beyond).has_value());
    EXPECT_TRUE(region.outside_shadows.empty());
    // Not on a Legionary, nor within 2" of one (09.05 After Moving).
    const Model& legionary = gs.units[mf::kLegionaries].models[5];
    EXPECT_FALSE(in_region(region, legionary.position));
    EXPECT_FALSE(in_region(region, Position{legionary.position.x, legionary.position.y - wh40k::test::in(2.5f), 0.0f}));

    // The Tyrant Guard (INFANTRY) in the Tyrant's place: the far side is in the rank's shadow (D-044).
    GameState guard = mf::fixture();
    mf::place_rank(guard.units[mf::kGuard], 22.0f, mf::kTyrantY);
    mf::place_rank(guard.units[mf::kTyrant], 6.0f, 40.0f);
    start_game(guard);
    mf::play_until(guard, [](const GameState& g) { return g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind); });
    apply_action(guard, Side::B, action::StartMove{guard.units[mf::kGuard].id, MoveType::Normal, std::nullopt});
    const auto guard_reach = move_reach(guard, guard.units[mf::kGuard].models[1].id);
    ASSERT_TRUE(guard_reach);
    EXPECT_FALSE(in_region(model_region(*guard_reach), at(22.0f, mf::kRankY - 3.0f)));
}

TEST(M63_ReachArea, ThePathThroughIsLegalInTheSession) {
    GameSession session(mf::at_movement());
    ASSERT_TRUE(session.submit(action::StartMove{session.state().units[mf::kTyrant].id, MoveType::Normal, std::nullopt}).ok());
    const action::ProposeModelMove through{session.state().units[mf::kTyrant].models[0].id, {at(22.0f, mf::kThroughY)}};
    EXPECT_TRUE(session.check(through).ok());
    ASSERT_TRUE(session.submit(through).ok());
    EXPECT_TRUE(move_end_problems(session.state()).empty());
}

TEST(M63_TargetMenu, TheEngagedTyrantIsOfferedToAnUnengagedUnit) {
    const GameState gs = mf::at_side_a_shooting();
    const UnitId havocs = gs.units[mf::kHavocs].id;
    const UnitId tyrant = gs.units[mf::kTyrant].id;
    BoardInteraction board(gs, legal_actions(gs, Side::A));
    (void)board.click(gs, havocs);
    ASSERT_EQ(board.actor(), havocs);
    EXPECT_TRUE(board.clickable(tyrant));
    EXPECT_FALSE(board.clickable(gs.units[mf::kGuard].id));  // engaged INFANTRY: 04.02
    const ClickResult r = board.click(gs, tyrant);
    const MenuEntry* lascannon = entry_named(r.menu, "Havoc lascannon");
    ASSERT_NE(lascannon, nullptr);
    EXPECT_TRUE(has_line(lascannon->effects, k17_03));
    EXPECT_TRUE(std::any_of(lascannon->help.begin(), lascannon->help.end(),
                            [](const HelpEntry& h) { return h.rule == "17.03" && h.in_this_game; }));
    // The plasma gun at the unengaged Screamer-killer has no such line.
    BoardInteraction other(gs, legal_actions(gs, Side::A));
    (void)other.click(gs, havocs);
    ASSERT_TRUE(other.clickable(gs.units[mf::kScreamer].id));
    for (const MenuEntry& e : other.click(gs, gs.units[mf::kScreamer].id).menu) {
        EXPECT_FALSE(has_line(e.effects, k17_03)) << e.label;
    }
}

TEST(M63_DiceCards, TheHitCardShowsTheMinusOneCiting17_03) {
    GameState gs = mf::at_side_a_shooting();
    ScriptedDice dice{4, 4, 4, 4, 1, 1, 1, 1};
    const action::Shoot shoot{gs.units[mf::kHavocs].id, ShootingType::Normal, lascannons_at(gs, mf::kTyrant)};
    const std::vector<LogEntry> log{LogEntry{"Shoot", apply_action(gs, Side::A, shoot, dice)}};
    const std::vector<DiceCard> cards = dice_cards(gs, log, 0);
    const DiceCard* card = find_card(cards, "Hit rolls");
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->need, "Need 3+ to hit (modifier -1); an unmodified 6 is a critical hit");
    EXPECT_TRUE(has_line(card->notes, k17_03));
    EXPECT_FALSE(has_line(card->notes, k10_06));
    EXPECT_TRUE(std::none_of(card->notes.begin(), card->notes.end(),
                             [](const EffectLine& l) { return l.text.find("D-011") != std::string::npos; }));
}

TEST(M63_DiceCards, TheCapIsNotedWhenItBites) {
    GameState gs = mf::fixture(true, Side::A);
    gs.units[mf::kHavocs].keywords.push_back("Vehicle");  // stand-in: no CSM unit is a VEHICLE
    const float tyrant_y = mf::kRankY + mf::tyrant_centre_gap(1.0f);
    mf::place_rank(gs.units[mf::kHavocs], 22.0f + mm_to_inches(30.0f) + 1.0f + 5.0f * mm_to_inches(20.0f), tyrant_y);
    start_game(gs);
    mf::play_until(gs, [](const GameState& g) { return g.pending && std::holds_alternative<decision::Shoot>(g.pending->kind); });
    ScriptedDice dice{4, 4, 4, 4, 1, 1, 1, 1};
    const action::Shoot shoot{gs.units[mf::kHavocs].id, ShootingType::CloseQuarters, lascannons_at(gs, mf::kTyrant)};
    const std::vector<LogEntry> log{LogEntry{"Shoot", apply_action(gs, Side::A, shoot, dice)}};
    const std::vector<DiceCard> cards = dice_cards(gs, log, 0);
    const DiceCard* card = find_card(cards, "Hit rolls");
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->need, "Need 3+ to hit (modifier -1, capped from -2, D-011); an unmodified 6 is a critical hit");
    EXPECT_TRUE(has_line(card->notes, k10_06));
    EXPECT_TRUE(has_line(card->notes, k17_03));
    EXPECT_TRUE(has_line(card->notes, "Net -2 to hit, capped at -1 (D-011)"));
}

TEST(M63_Help, TheMoveMenuOfAMonsterHasThe17_01Help) {
    const GameState gs = mf::at_movement();
    BoardInteraction board(gs, legal_actions(gs, Side::B));
    const ClickResult r = board.click(gs, gs.units[mf::kTyrant].id);
    ASSERT_GE(r.menu.size(), 3u);  // Remain Stationary? Normal, Advance, ..., Cancel
    for (const MenuEntry& e : r.menu) {
        const auto* m = std::get_if<action::StartMove>(&e.action);
        const bool expected = m != nullptr && !e.cancel && (m->move_type == MoveType::Normal || m->move_type == MoveType::Advance);
        const bool has = std::any_of(e.help.begin(), e.help.end(), [](const HelpEntry& h) { return h.rule == "17.01"; });
        EXPECT_EQ(has, expected) << e.label;
    }
    const ClickResult guard = BoardInteraction(gs, legal_actions(gs, Side::B)).click(gs, gs.units[mf::kGuard].id);
    for (const MenuEntry& e : guard.menu) EXPECT_TRUE(e.help.empty()) << e.label;
}

TEST(M63_Help, MonsterAndVehicleKeywordsAnd17_01) {
    for (const char* k : {"Monster", "Vehicle"}) {
        const HelpEntry h = keyword_help(k);
        EXPECT_TRUE(h.in_this_game) << k;
        EXPECT_EQ(h.rule, "17.01");
        EXPECT_NE(h.summary.find("17.03"), std::string::npos);
    }
    EXPECT_EQ(monsters_moving_help().rule, "17.01");
    EXPECT_EQ(engaged_monster_target_help().rule, "17.03");
    EXPECT_NE(engaged_monster_target_help().summary.find("[BLAST]"), std::string::npos);
    EXPECT_NE(ring_meaning(RingKind::MoveRange).find("17.01"), std::string_view::npos);
    EXPECT_NE(shooting_type_help(ShootingType::CloseQuarters).summary.find("MONSTER"), std::string::npos);
    // hit_modifier_lines: nothing for plain selections; the cap only when it bites.
    EXPECT_TRUE(hit_modifier_lines(std::vector<WeaponSelection>(2), 0).empty());
    EXPECT_TRUE(hit_modifier_lines(std::vector<WeaponSelection>(2), 1).empty());
    WeaponSelection one;
    one.hit_modifier = -1;
    one.hit_modifier_rules = {HitModifierRule::EngagedMonsterTarget};
    const auto lines = hit_modifier_lines(std::vector<WeaponSelection>{one, WeaponSelection{}}, -1);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].text, k17_03 + " (1 of 2 models)");
}

TEST(M63_LaunchOptions, ClickUnits) {
    // --click-units (for the screenshot of a target menu, D-073): the units clicked on the board, as clicks would.
    EXPECT_EQ(parse_launch_args({"--click-units", "5,6"}).click_units, (std::vector<unsigned>{5, 6}));
    EXPECT_TRUE(parse_launch_args({"--steps", "1"}).click_units.empty());
    for (const char* bad : {"", "5,", "a", "5;6", "-1"}) {
        EXPECT_THROW((void)parse_launch_args({"--click-units", bad}), std::invalid_argument) << bad;
    }
}
