// M86 (D-084): the client's view of objectives and the mission: the board's objective marks, the Game panel's VP line,
// the victory pop-up's VP line, the log lines, the default scenario, and custom battles that play the mission.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "army_builder.hpp"
#include "core/data/load_error.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/events.hpp"
#include "core/state/points_destroyed.hpp"
#include "event_text.hpp"
#include "phase_track.hpp"
#include "settings.hpp"
#include "test_helpers.hpp"

using namespace wh40k::client;
using wh40k::core::GameEvent;
using wh40k::core::GameState;
using wh40k::core::Side;
using wh40k::test::in;
using wh40k::test::repo_root;
namespace event = wh40k::core::event;

namespace {

// The objective scenario as M86 shipped it (M87 tuned the shipped file's numbers, D-086): the labels and VP below are
// M86's.
constexpr const char* kM86Objectives = "tests/data/scenarios/m86_objectives_44x60.json";

GameState objectives() { return wh40k::core::load_scenario(repo_root() / kM86Objectives, repo_root()); }

}  // namespace

TEST(M86_Board, EachObjectiveIsMarkedWithItsControllerAndVp) {
    EXPECT_TRUE(objective_marks(wh40k::test::load_rosters_scenario()).empty());
    GameState gs = objectives();
    std::vector<ObjectiveMark> marks = objective_marks(gs);
    ASSERT_EQ(marks.size(), 3u);
    EXPECT_EQ(marks[0].label, "Centre 5/5");
    EXPECT_EQ(marks[1].label, "A home 2/4");
    EXPECT_EQ(marks[2].label, "B home 4/2");
    EXPECT_FLOAT_EQ(marks[0].centre.x, in(22.0f));
    EXPECT_FLOAT_EQ(marks[0].centre.y, in(30.0f));
    EXPECT_FLOAT_EQ(marks[0].radius_mm, in(6.0f));
    EXPECT_FLOAT_EQ(marks[1].radius_mm, in(3.0f));
    for (const ObjectiveMark& m : marks) {
        EXPECT_FALSE(m.controller.has_value());
        EXPECT_FALSE(m.secured);
    }
    gs.mission->objectives[2].controller = Side::B;
    gs.mission->objectives[2].secured_by = Side::B;
    marks = objective_marks(gs);
    EXPECT_EQ(marks[2].controller, Side::B);
    EXPECT_TRUE(marks[2].secured);
}

TEST(M86_Panel, TheVpLineShowsObjectiveVpAndTheKillVpTheyWouldGiveNow) {
    EXPECT_FALSE(victory_points_text(wh40k::test::load_rosters_scenario()).has_value());
    GameState gs = objectives();
    EXPECT_EQ(victory_points_text(gs), "Victory points: Side A 0 (0 + 0 kills), Side B 0 (0 + 0 kills)");
    gs.mission->objective_vp = {7, 2};
    gs.find_unit(wh40k::core::UnitId{6})->models.clear();  // 55 points of Termagants destroyed: 1 kill VP
    EXPECT_EQ(victory_points_text(gs), "Victory points: Side A 8 (7 + 1 kills), Side B 2 (2 + 0 kills)");
    // The mission keeps the points line too (they settle equal VP).
    EXPECT_EQ(points_destroyed_text(gs), "Points destroyed: Side A 55, Side B 0");
}

TEST(M86_PopUp, TheVictoryPopUpNamesBothSidesVp) {
    GameState gs = objectives();
    wh40k::core::PointsTally p = wh40k::core::points_destroyed(gs);
    gs.result = wh40k::core::GameResult{Side::A, "D-084: ...", p,
                                        wh40k::core::VpTally{{18, 15}, {5, 4}, {23, 19}}};
    const std::optional<GameOverText> t = game_over_text(gs);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->title, "Side A (Chaos Space Marines) wins!");
    EXPECT_EQ(t->vp, "Victory points: Side A (Chaos Space Marines) 23 (18 objectives + 5 kills), Side B (Tyranids) 19 "
                     "(15 objectives + 4 kills)");
    EXPECT_EQ(t->points, "Points destroyed: Side A (Chaos Space Marines) 0, Side B (Tyranids) 0");
    // After a wipe-out (no VP in the result) the VP as the game ended.
    gs.mission->objective_vp = {10, 0};
    gs.result = wh40k::core::GameResult{Side::A, "D-007: ...", std::nullopt, std::nullopt};
    EXPECT_EQ(game_over_text(gs)->vp, "Victory points: Side A (Chaos Space Marines) 10 (10 objectives + 0 kills), "
                                      "Side B (Tyranids) 0 (0 objectives + 0 kills)");
    EXPECT_FALSE(game_over_text(wh40k::test::load_v1_scenario()).has_value());
}

TEST(M86_Log, ControlChangesScoringAndTheMissionResultHaveLogLines) {
    EXPECT_EQ(format_event(GameEvent{event::ObjectiveControlChanged{0, "Centre", Side::A, {4, 2}}}),
              "[14.02] Side A takes control of Centre (OC 4 to 2)");
    EXPECT_EQ(format_event(GameEvent{event::ObjectiveControlChanged{0, "Centre", std::nullopt, {2, 2}}}),
              "[14.02] Centre is no longer controlled (OC 2 each)");
    EXPECT_EQ(format_event(GameEvent{event::ObjectiveControlChanged{1, "A home", std::nullopt, {0, 0}}}),
              "[14.02] A home is no longer controlled (OC 0 each)");
    EXPECT_EQ(format_event(GameEvent{event::VictoryPointsScored{Side::B, 2, "B home", 2, 6}}),
              "[D-084] Side B scores 2 VP for holding B home (6 objective VP)");
    wh40k::core::PointsTally p;
    p.destroyed = {385, 180};
    p.denominator = 10;
    p.display = {"38.5", "18"};
    p.battle_round = 5;
    EXPECT_EQ(format_event(GameEvent{event::GameOver{Side::B, "r", p, wh40k::core::VpTally{{18, 20}, {0, 0}, {18, 20}}}}),
              "[D-084] Battle round 5 ends: Side B wins on victory points (20 to 18)");
    EXPECT_EQ(format_event(GameEvent{event::GameOver{Side::A, "r", p, wh40k::core::VpTally{{9, 9}, {0, 0}, {9, 9}}}}),
              "[D-084] Battle round 5 ends: Side A wins on points destroyed (38.5 to 18) after equal victory points "
              "(9 each)");
    p.destroyed = {180, 180};
    p.display = {"18", "18"};
    EXPECT_EQ(format_event(GameEvent{event::GameOver{std::nullopt, "r", p, wh40k::core::VpTally{{9, 9}, {0, 0}, {9, 9}}}}),
              "[D-084] Battle round 5 ends: a draw: equal victory points (9 each) and equal points destroyed (18 each)");
}

TEST(M86_Settings, TheObjectiveScenarioIsTheDefaultAndInTheList) {
    EXPECT_EQ(AppSettings{}.scenario, kObjectiveScenario);
    EXPECT_EQ(settings_from_json(nlohmann::json{{"scenario", ""}}).scenario, kObjectiveScenario);
    // A saved file keeps the scenario it names.
    EXPECT_EQ(settings_from_json(nlohmann::json{{"scenario", "data/scenarios/v1_skirmish.json"}}).scenario,
              "data/scenarios/v1_skirmish.json");
    const std::vector<std::string> scenarios = list_scenarios(repo_root());
    EXPECT_NE(std::find(scenarios.begin(), scenarios.end(), kObjectiveScenario), scenarios.end());
    const ScenarioInfo info = scenario_info(repo_root() / kObjectiveScenario);
    EXPECT_EQ(info.name, "Incursion objectives - Chaos Space Marines vs Tyranids (44x60 portrait)");
    EXPECT_EQ(info.sides[0], "Chaos Space Marines");
    EXPECT_EQ(info.sides[1], "Tyranids");
}

TEST(M86_Builder, ACustomBattleWithTheObjectiveScenariosMissionPlaysIt) {
    ArmyBuilder b(repo_root());
    EXPECT_EQ(b.mission(), nullptr);
    EXPECT_EQ(b.battle_state().result_rule, wh40k::core::ResultRule::PointsDestroyed);  // without one, as before
    b.load_mission(repo_root() / kObjectiveScenario);
    ASSERT_NE(b.mission(), nullptr);
    const GameState gs = b.battle_state();
    EXPECT_EQ(gs.result_rule, wh40k::core::ResultRule::Mission);
    ASSERT_TRUE(gs.mission.has_value());
    EXPECT_EQ(gs.mission->objectives.size(), 3u);
    EXPECT_EQ(b.battle_scenario().at("mission"), wh40k::core::load_mission_block(repo_root() / kObjectiveScenario));
    // "New game" rebuilds the same battle from the setup and the mission.
    EXPECT_EQ(custom_battle_state(b.setup(), b.factions(), b.resolver(), b.mission()), gs);
    b.set_mission(std::nullopt);
    EXPECT_EQ(b.battle_state().result_rule, wh40k::core::ResultRule::PointsDestroyed);
    EXPECT_THROW(b.load_mission(repo_root() / "data/scenarios/rosters_44x60.json"), wh40k::core::DataError);
}
