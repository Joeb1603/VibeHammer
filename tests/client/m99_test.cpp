// M99 (D-101): the client's side of [ANTI-X Y+] (24.03), [DEVASTATING WOUNDS] (24.10), [MELTA X] (24.25) and [LANCE]
// (24.21): their labels and help, what they do to a selection (the menus' and dice cards' effect lines), the log line of
// 24.10's mortal wounds, and picking their casualties on the board (06.02, D-051).

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "board_interaction.hpp"
#include "casualty_picks.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/rules/attacks.hpp"
#include "datasheet_view.hpp"
#include "event_text.hpp"
#include "rules_help.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;
constexpr std::size_t kFleshborer = 0;

const EffectLine& line_of(const std::vector<EffectLine>& lines, std::size_t i) { return lines.at(i); }

// Side B's Termagants shoot the Intercessors with `n` Fleshborers that have [DEVASTATING WOUNDS] and D 3: every wound
// roll is a critical wound, so Side A (the Intercessors' player) picks the models for the mortal wounds (06.02).
GameState devastating_casualties_pending(int n) {
    GameState gs = wh40k::test::shoot_step_state(Side::B);
    for (Model& m : gs.units[kGants].models) {
        m.weapons[kFleshborer].profiles[0].abilities.push_back(ability::DevastatingWounds{});
        m.weapons[kFleshborer].profiles[0].damage = DiceExpr::fixed(3);
    }
    std::vector<WeaponSelection> sel;
    std::vector<int> rolls;
    for (int i = 0; i < n; ++i) {
        sel.push_back(wh40k::test::select(gs, kGants, static_cast<std::size_t>(i), kFleshborer, 0, gs.units[kMarines].id));
    }
    for (int i = 0; i < 2 * n; ++i) rolls.push_back(i < n ? 4 : 6);  // hits, then critical wounds
    ScriptedDice dice(rolls);
    begin_attacks(gs, dice, gs.units[kGants].id, AttackContext{}, sel);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{gs.units[kMarines].id}}));
    return gs;
}

}  // namespace

TEST(M99_Labels, TheAbilitiesReadAsOnTheDatasheet) {
    EXPECT_EQ(weapon_ability_label(ability::Anti{{"infantry"}, 4, false}), "[ANTI-INFANTRY 4+]");
    EXPECT_EQ(weapon_ability_label(ability::Anti{{"monster", "vehicle"}, 2, true}), "[ANTI-NON-MONSTER/VEHICLE 2+]");
    EXPECT_EQ(weapon_ability_label(ability::Melta{2}), "[MELTA 2]");
    EXPECT_EQ(weapon_ability_label(ability::Lance{}), "[LANCE]");
    EXPECT_EQ(weapon_ability_label(parse_weapon_ability("Anti-VEHICLE 3+")), "[ANTI-VEHICLE 3+]");
}

TEST(M99_Help, TheAbilitiesAreInTheGameWithTheirRules) {
    const std::vector<std::pair<WeaponAbility, std::string>> all{{ability::Anti{{"infantry"}, 4, false}, "24.03"},
                                                                 {ability::DevastatingWounds{}, "24.10"},
                                                                 {ability::Melta{2}, "24.25"},
                                                                 {ability::Lance{}, "24.21"}};
    for (const auto& [a, rule] : all) {
        const HelpEntry h = weapon_ability_help(a);
        EXPECT_TRUE(h.in_this_game) << h.title;
        EXPECT_EQ(h.rule, rule);
        EXPECT_EQ(h.summary.find("Not in this game"), std::string::npos) << h.summary;
    }
    EXPECT_NE(weapon_ability_help(ability::Anti{{"vehicle"}, 3, false}).summary.find("3+"), std::string::npos);
    EXPECT_NE(weapon_ability_help(ability::Anti{{"vehicle"}, 3, true}).summary.find("without"), std::string::npos);
    EXPECT_NE(weapon_ability_help(ability::Melta{2}).summary.find("+2 Damage"), std::string::npos);
}

TEST(M99_EffectLines, MeltaAntiLanceAndDevastatingWoundsSayWhatTheyDoHere) {
    WeaponProfile weapon;
    weapon.name = "Test gun";
    weapon.kind = WeaponKind::Ranged;
    weapon.range_mm = inches_to_mm(12.0f);
    weapon.skill = 3;
    weapon.abilities = {ability::Melta{2}, ability::Anti{{"infantry"}, 4, false}, ability::DevastatingWounds{},
                        ability::Lance{}};
    Unit infantry;
    infantry.keywords = {"Infantry"};
    Unit monster;
    monster.keywords = {"Monster"};
    WeaponSelection near;
    near.target_within_half_range = true;
    WeaponSelection far;
    const AttackContext shooting{AttackType::Shooting, false};

    std::vector<WeaponSelection> sels{near};
    std::vector<EffectLine> lines = effect_lines(weapon, sels, shooting, ShootingType::Normal, &infantry);
    ASSERT_EQ(lines.size(), 4u);
    EXPECT_EQ(line_of(lines, 0).text, "Melta 2: +2 Damage (target within half range, 6\")");
    EXPECT_EQ(line_of(lines, 0).kind, EffectKind::Active);
    EXPECT_EQ(line_of(lines, 1).text, "Anti-INFANTRY 4+: a wound roll of 4+ is a critical wound");
    EXPECT_EQ(line_of(lines, 1).kind, EffectKind::Active);
    EXPECT_EQ(line_of(lines, 2).kind, EffectKind::Active);
    EXPECT_NE(line_of(lines, 2).text.find("Devastating Wounds"), std::string::npos);
    EXPECT_EQ(line_of(lines, 3).text, "Lance: no bonus (the unit made no charge move this turn)");
    EXPECT_EQ(line_of(lines, 3).kind, EffectKind::Inactive);

    sels = {far};
    AttackContext charged = shooting;
    charged.charged = true;
    lines = effect_lines(weapon, sels, charged, ShootingType::Normal, &monster);
    EXPECT_EQ(line_of(lines, 0).text, "Melta 2: no extra Damage (target beyond half range, 6\")");
    EXPECT_EQ(line_of(lines, 0).kind, EffectKind::Inactive);
    EXPECT_EQ(line_of(lines, 1).text, "Anti-INFANTRY 4+: no effect (the target doesn't have the keyword)");
    EXPECT_EQ(line_of(lines, 1).kind, EffectKind::Inactive);
    EXPECT_EQ(line_of(lines, 3).text, "Lance: +1 to wound (the unit made a charge move this turn)");
    EXPECT_EQ(line_of(lines, 3).kind, EffectKind::Active);

    // Some models within half range.
    sels = {near, far, far};
    lines = effect_lines(weapon, sels, shooting, ShootingType::Normal, &infantry);
    EXPECT_EQ(line_of(lines, 0).text, "Melta 2: +2 Damage (1 of 3 models within half range, 6\")");
    // None of the four is "not in this game".
    for (const EffectLine& l : lines) EXPECT_NE(l.kind, EffectKind::NotInGame) << l.text;
}

TEST(M99_EventText, DevastatingWoundsReadAsOneLine) {
    const event::DevastatingWoundsInflicted e{
        UnitId{2}, UnitId{1},
        DevastatingWoundsResult{{DevastatingStep{DamageResult{ModelId{4}, 3, 2, true}, std::nullopt},
                                 DevastatingStep{DamageResult{ModelId{5}, 1, 1, false}, CharacteristicRoll{{2}, 1}}},
                                1, 0}};
    const std::string text = format_event(GameEvent{e});
    EXPECT_NE(text.find("Devastating Wounds on"), std::string::npos) << text;
    EXPECT_NE(text.find("3 mortal wounds on"), std::string::npos) << text;
    EXPECT_NE(text.find("(destroyed)"), std::string::npos) << text;
    EXPECT_NE(text.find("1 mortal wound (rolled 2) on"), std::string::npos) << text;
    EXPECT_NE(text.find("24.10"), std::string::npos) << text;
    // The normal damage had destroyed the unit: nothing inflicted.
    const event::DevastatingWoundsInflicted lost{UnitId{2}, UnitId{1}, DevastatingWoundsResult{{}, 0, 2}};
    const std::string none = format_event(GameEvent{lost});
    EXPECT_NE(none.find("none inflicted; 2 critical wounds lost"), std::string::npos) << none;
}

TEST(M99_CasualtyPicks, TheMortalWoundsArePickedOnTheBoardOneCriticalWoundAtATime) {
    // Two critical wounds, D 3 each, into fresh W 2 Intercessors: each click takes one critical wound's mortal wounds
    // (2 of its 3; the third is lost, 24.10), and a destroyed model can't be clicked again.
    GameSession session(devastating_casualties_pending(2));
    const GameState& gs = session.state();
    const UnitId marines = gs.units[kMarines].id;
    const ModelId first = gs.units[kMarines].models[1].id;
    const ModelId second = gs.units[kMarines].models[3].id;
    BoardInteraction board(gs, session.legal());
    ASSERT_TRUE(board.active());
    EXPECT_TRUE(board.picking_model());
    ASSERT_NE(board.casualty_picks(), nullptr);
    EXPECT_EQ(board.casualty_picks()->preview()->next_damage, 3);
    EXPECT_EQ(board.casualty_picks()->preview()->damage_left, 6);
    EXPECT_EQ(board.casualty_picks()->options().size(), 5u);  // 06.02: any non-CHARACTER model, none is wounded

    board.click_model(gs, marines, first);
    EXPECT_EQ(board.casualty_picks()->marks(), (std::vector<CasualtyMark>{{first, 2, true}}));
    EXPECT_EQ(board.casualty_picks()->preview()->damage_left, 3);
    EXPECT_TRUE(board.model_dimmed(marines, first));
    EXPECT_FALSE(board.confirm().has_value());
    board.click_model(gs, marines, second);
    EXPECT_EQ(board.casualty_picks()->marks(), (std::vector<CasualtyMark>{{first, 2, true}, {second, 2, true}}));
    const auto confirm = board.confirm();
    ASSERT_TRUE(confirm.has_value());
    ASSERT_TRUE(session.submit(*confirm).ok());
    const Unit& after = *session.state().find_unit(marines);
    EXPECT_EQ(after.models.size(), 3u);
    EXPECT_EQ(find_model(after, first), nullptr);
    EXPECT_EQ(find_model(after, second), nullptr);
    for (const Model& m : after.models) EXPECT_EQ(m.wounds_remaining, 2);

    // "Use default order" answers it too (D-058), with a legal order.
    GameSession other(devastating_casualties_pending(2));
    const BoardInteraction default_board(other.state(), other.legal());
    ASSERT_EQ(default_board.step_buttons().size(), 1u);
    EXPECT_TRUE(other.submit(default_board.step_buttons().front()).ok());
    EXPECT_EQ(other.state().find_unit(marines)->models.size(), 3u);
}
