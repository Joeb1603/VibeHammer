// M101 (D-103): the client's side of [INDIRECT FIRE] with Indirect Shooting (24.19, 10.07) and [PRECISION] (24.28):
// labels and help, the effect lines, the action and log text, and the Precision decision as buttons in a session.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

#include "action_text.hpp"
#include "core/engine/engine.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/shooting.hpp"
#include "datasheet_view.hpp"
#include "decision_view.hpp"
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

// Side B's Termagants have shot the CHARACTER Intercessors with one [PRECISION] Fleshborer that hit and wounded: the
// attacking unit's player selects a CHARACTER's group (24.28).
GameState precision_pending() {
    GameState gs = wh40k::test::shoot_step_state(Side::B);
    gs.units[kMarines].keywords.push_back("Character");
    for (Model& m : gs.units[kGants].models) m.weapons[0].profiles[0].abilities.push_back(ability::Precision{});
    ScriptedDice dice{4, 4};
    apply_action(gs, Side::B,
                 action::Shoot{gs.units[kGants].id, ShootingType::Normal,
                               {ShotDeclaration{gs.units[kGants].models[0].id, 0, 0, gs.units[kMarines].id}}},
                 dice);
    EXPECT_TRUE(gs.pending && std::holds_alternative<decision::SelectPrecisionGroup>(gs.pending->kind));
    return gs;
}

}  // namespace

TEST(M101_Labels, TheAbilitiesAndTheShootingTypeHaveTheirTextAndHelp) {
    EXPECT_EQ(weapon_ability_label(ability::IndirectFire{}), "[INDIRECT FIRE]");
    EXPECT_EQ(weapon_ability_label(ability::Precision{}), "[PRECISION]");
    const HelpEntry indirect = weapon_ability_help(ability::IndirectFire{});
    EXPECT_TRUE(indirect.in_this_game);
    EXPECT_EQ(indirect.rule, "24.19");
    const HelpEntry precision = weapon_ability_help(ability::Precision{});
    EXPECT_TRUE(precision.in_this_game);
    EXPECT_EQ(precision.rule, "24.28");
    const HelpEntry type = shooting_type_help(ShootingType::Indirect);
    EXPECT_EQ(type.title, "Indirect shooting");
    EXPECT_EQ(type.rule, "10.07");
    EXPECT_TRUE(type.in_this_game);
    EXPECT_NE(type.summary.find("1-5"), std::string::npos);
}

TEST(M101_EffectLines, IndirectFireSaysWhichRollsFailAndPrecisionWhatItSelects) {
    WeaponProfile weapon;
    weapon.name = "Test mortar";
    weapon.kind = WeaponKind::Ranged;
    weapon.range_mm = inches_to_mm(48.0f);
    weapon.skill = 4;
    weapon.abilities = {ability::IndirectFire{}, ability::Precision{}};
    WeaponSelection sel;
    sel.hit_fails_to = 5;
    sel.benefit_of_cover = true;
    std::vector<WeaponSelection> sels{sel};
    const AttackContext shooting{AttackType::Shooting, false};
    std::vector<EffectLine> lines = effect_lines(weapon, sels, shooting, ShootingType::Indirect);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0].text, "Indirect Fire: the target needn't be visible; it has cover, and hit rolls of 1-5 fail");
    EXPECT_EQ(lines[0].kind, EffectKind::Active);
    EXPECT_EQ(lines[1].kind, EffectKind::Active);
    EXPECT_NE(lines[1].text.find("Precision"), std::string::npos);
    sels[0].hit_fails_to = 3;
    EXPECT_NE(effect_lines(weapon, sels, shooting, ShootingType::Indirect)[0].text.find("1-3 fail"), std::string::npos);
    // In normal shooting the ability only says what it could do.
    sels[0] = WeaponSelection{};
    lines = effect_lines(weapon, sels, shooting, ShootingType::Normal);
    EXPECT_EQ(lines[0].kind, EffectKind::Info);
}

TEST(M101_Precision, TheDecisionIsButtonsWithEachCharacterAndNone) {
    GameSession session(precision_pending());
    const GameState& gs = session.state();
    const std::vector<GameAction> legal = session.legal();
    const DecisionView view = make_decision_view(legal);
    EXPECT_TRUE(view.templates.empty());
    ASSERT_EQ(view.buttons.size(), 6u);  // five CHARACTER models, then none
    const ModelId third = gs.units[kMarines].models[2].id;
    EXPECT_EQ(action_label(gs, GameAction{action::SelectPrecisionGroup{third}}),
              "Precision: model " + std::to_string(third.v) + " (2 wounds left)");
    EXPECT_EQ(action_label(gs, view.buttons.back()), "No Precision target");
    EXPECT_NE(describe(gs, *gs.pending).find("[PRECISION]"), std::string::npos);
    // Submitting one goes on to the defender's allocation order.
    ASSERT_TRUE(session.submit(GameAction{action::SelectPrecisionGroup{third}}).ok());
    EXPECT_TRUE(session.state().pending &&
                std::holds_alternative<decision::DeclareAllocationOrder>(session.state().pending->kind));
    const std::string line = format_event(GameEvent{event::PrecisionGroupSelected{UnitId{2}, UnitId{1}, third}});
    EXPECT_NE(line.find("24.28"), std::string::npos) << line;
    EXPECT_NE(line.find("selects"), std::string::npos) << line;
    EXPECT_NE(format_event(GameEvent{event::PrecisionGroupSelected{UnitId{2}, UnitId{1}, std::nullopt}})
                  .find("no CHARACTER"),
              std::string::npos);
}

TEST(M101_IndirectShooting, TheSessionOffersAndAcceptsTheIndirectType) {
    // The V1 scenario with [INDIRECT FIRE] Bolt Rifles from the start: at Side A's Shoot step both shooting types are
    // offered as templates, and an Indirect Shoot is accepted.
    GameState gs = wh40k::test::load_v1_scenario_closer();
    for (Model& m : gs.units[kMarines].models) m.weapons[0].profiles[0].abilities.push_back(ability::IndirectFire{});
    start_game(gs);
    for (int guard = 0; guard < 50 && !(gs.flow.stage == Stage::Shoot && turn_side(gs) == Side::A); ++guard) {
        apply_action(gs, gs.pending->side, legal_actions(gs, gs.pending->side).front());
    }
    GameSession session(gs);
    std::vector<ShootingType> types;
    for (const GameAction& a : make_decision_view(session.legal()).templates) {
        if (const auto* s = std::get_if<action::Shoot>(&a)) types.push_back(s->type);
    }
    EXPECT_EQ(types, (std::vector<ShootingType>{ShootingType::Normal, ShootingType::Indirect}));
    const Unit& marines = session.state().units[kMarines];
    action::Shoot shoot{marines.id, ShootingType::Indirect, {}};
    for (const ShotDeclaration& o : shot_options(session.state(), marines, ShootingType::Indirect)) {
        const bool have = std::any_of(shoot.shots.begin(), shoot.shots.end(), [&o](const ShotDeclaration& d) {
            return d.model == o.model;
        });
        if (o.weapon_index == 0 && !have) shoot.shots.push_back(o);
    }
    ASSERT_EQ(shoot.shots.size(), marines.models.size());
    EXPECT_TRUE(session.submit(GameAction{shoot}).ok());
    EXPECT_TRUE(session.state().units[kMarines].phase_flags.selected_to_shoot);
}
