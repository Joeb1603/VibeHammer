// M86 (D-084; 14.01-14.03): objective areas, their control at the end of each phase and turn, secured objectives
// (state only), and the objective mission: VP for the areas a player controls in their own Command phase from battle
// round 2, kill VP for the points destroyed, then the result on VP, points destroyed and a draw.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/data/army_list.hpp"
#include "core/data/datasheet.hpp"
#include "core/data/load_error.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/objectives.hpp"
#include "core/state/points_destroyed.hpp"
#include "core/state/queries.hpp"
#include "support/army_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::at;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::in;
using wh40k::test::model_ids;
using wh40k::test::neutral_action;
using wh40k::test::repo_root;

namespace {

constexpr const char* kObjectiveScenario = "data/scenarios/incursion_objectives_44x60.json";
constexpr std::size_t kCentre = 0;
constexpr std::size_t kHomeA = 1;
constexpr std::size_t kHomeB = 2;

GameState load_objectives() { return load_scenario(repo_root() / kObjectiveScenario, repo_root()); }

nlohmann::json objectives_json() { return read_json_file(repo_root() / kObjectiveScenario); }

GameState build(const nlohmann::json& scenario) {
    return build_game_state(scenario, [](const std::string& unit_file) { return load_datasheet(repo_root() / unit_file); });
}

std::string refusal(const nlohmann::json& scenario) {
    try {
        (void)build(scenario);
    } catch (const DataError& e) {
        return e.what();
    }
    return "";
}

std::string state_refusal(const nlohmann::json& state) {
    try {
        (void)state.get<GameState>();
    } catch (const DataError& e) {
        return e.what();
    }
    return "";
}

Unit& unit(GameState& gs, std::uint32_t id) { return *gs.find_unit(UnitId{id}); }
ObjectiveArea& area(GameState& gs, std::size_t i) { return gs.mission->objectives.at(i); }

// Keeps only the units with these ids (the others leave the battle: they are not destroyed, so they count no points).
void keep_units(GameState& gs, std::vector<std::uint32_t> ids) {
    std::erase_if(gs.units, [&ids](const Unit& u) { return std::find(ids.begin(), ids.end(), u.id.v) == ids.end(); });
}

// Sets the unit up in rows of `cols` 1.3" apart (bases up to 32 mm stay in coherency), the first row centred on
// (x, y) and the next ones north of it.
void place(Unit& u, float x_in, float y_in, int cols = 5) {
    for (std::size_t k = 0; k < u.models.size(); ++k) {
        const float col = static_cast<float>(static_cast<int>(k) % cols) - static_cast<float>(cols - 1) * 0.5f;
        const float row = static_cast<float>(static_cast<int>(k) / cols);
        u.models[k].position = at(x_in + col * 1.3f, y_in + row * 1.3f);
    }
}

// Neutral answers (remain stationary, end every step) until `done`; every event on the way.
template <typename Done>
std::vector<GameEvent> play_until(GameState& gs, Done done, int guard = 5000) {
    std::vector<GameEvent> all;
    for (int i = 0; i < guard && gs.pending && !done(gs); ++i) {
        const std::vector<GameEvent> ev = apply_action(gs, decider(gs), neutral_action(gs));
        all.insert(all.end(), ev.begin(), ev.end());
    }
    return all;
}

std::vector<GameEvent> start_and_play_to_end(GameState& gs) {
    std::vector<GameEvent> all = start_game(gs);
    const std::vector<GameEvent> rest = play_until(gs, [](const GameState&) { return false; });
    all.insert(all.end(), rest.begin(), rest.end());
    return all;
}

template <typename E>
std::ptrdiff_t first_index(const std::vector<GameEvent>& events) {
    const auto it = std::find_if(events.begin(), events.end(), [](const GameEvent& e) { return std::holds_alternative<E>(e); });
    return it == events.end() ? -1 : it - events.begin();
}

}  // namespace

// ==== 14.01 / 14.02: within range of an objective (D-009 "within") ====

TEST(R14_02_LevelOfControl, AModelIsWithinRangeWhileAnyPartOfItsBaseIsWithinTheArea) {
    const ObjectiveArea centre{"Centre", at(22.0f, 30.0f), in(12.0f), {5, 5}, std::nullopt, std::nullopt};
    Model m;
    m.base = Base{BaseShape::Round, 32.0f, 0.0f};
    // The base's edge just touches the area's edge: 6" + 16 mm from the centre.
    m.position = Position{in(22.0f) + in(6.0f) + 16.0f, in(30.0f), 0.0f};
    EXPECT_TRUE(model_within_objective(m, centre));
    m.position.x += 2.0f * EPS_MM;  // just outside
    EXPECT_FALSE(model_within_objective(m, centre));
    m.position = at(22.0f, 30.0f);  // wholly within
    EXPECT_TRUE(model_within_objective(m, centre));
}

TEST(R14_02_LevelOfControl, AnOvalBaseIsWithinRangeByItsOwnShape) {
    // The Tyrannofex's 120 x 92 mm oval: at facing 0 its length runs south to north (D-074).
    const ObjectiveArea centre{"Centre", at(22.0f, 30.0f), in(12.0f), {5, 5}, std::nullopt, std::nullopt};
    Model m;
    m.base = Base{BaseShape::Oval, 92.0f, 120.0f};
    const float r = in(6.0f);
    m.position = Position{in(22.0f) + r + 46.0f, in(30.0f), 0.0f};  // its side touches the area
    EXPECT_TRUE(model_within_objective(m, centre));
    m.position.x += 1.0f;
    EXPECT_FALSE(model_within_objective(m, centre));
    m.position = Position{in(22.0f), in(30.0f) + r + 60.0f, 0.0f};  // its end touches the area
    EXPECT_TRUE(model_within_objective(m, centre));
    m.facing_deg = 90.0f;  // turned, its side faces the area: 14 mm short
    EXPECT_FALSE(model_within_objective(m, centre));
}

TEST(R14_02_LevelOfControl, ALevelIsTheSumOfTheOcOfThePlayersModelsWithinRange) {
    GameState gs = load_objectives();
    const ObjectiveArea centre = area(gs, kCentre);
    EXPECT_EQ(level_of_control(gs, centre), (std::array<int, 2>{0, 0}));
    place(unit(gs, 1), 19.0f, 29.0f);  // 10 Legionaries, OC 2 each
    place(unit(gs, 10), 25.0f, 31.0f);  // the Hive Tyrant, OC 3
    EXPECT_EQ(level_of_control(gs, centre), (std::array<int, 2>{20, 3}));
    unit(gs, 1).models.resize(3);  // three left
    EXPECT_EQ(level_of_control(gs, centre), (std::array<int, 2>{6, 3}));
    // Only the models within range count: one Legionary off the area.
    unit(gs, 1).models[2].position = at(22.0f, 20.0f);
    EXPECT_EQ(level_of_control(gs, centre), (std::array<int, 2>{4, 3}));
}

TEST(R14_02_LevelOfControl, BattleShockedModelsAndModelsWithOcDashAddNothing) {
    GameState gs = load_objectives();
    const ObjectiveArea centre = area(gs, kCentre);
    place(unit(gs, 1), 19.0f, 29.0f);
    place(unit(gs, 10), 25.0f, 31.0f);
    unit(gs, 1).battle_shocked = true;  // 01.07: OC '-'
    EXPECT_EQ(level_of_control(gs, centre), (std::array<int, 2>{0, 3}));
    unit(gs, 1).battle_shocked = false;
    for (Model& m : unit(gs, 1).models) m.characteristics.objective_control = std::nullopt;  // 02.02: OC '-'
    EXPECT_EQ(level_of_control(gs, centre), (std::array<int, 2>{0, 3}));
}

TEST(R14_02_LevelOfControl, TheHigherLevelControlsAndEqualLevelsControlNeither) {
    const ObjectiveArea a{"Centre", at(22.0f, 30.0f), in(12.0f), {5, 5}, std::nullopt, std::nullopt};
    EXPECT_EQ(determine_control(a, {6, 3}, true).controller, Side::A);
    EXPECT_EQ(determine_control(a, {1, 4}, true).controller, Side::B);
    EXPECT_EQ(determine_control(a, {3, 3}, true).controller, std::nullopt);
    EXPECT_EQ(determine_control(a, {0, 0}, true).controller, std::nullopt);  // no OC 1+ model: nobody
    ObjectiveArea held = a;
    held.controller = Side::A;  // controlled before: a tie now loses it (not secured)
    EXPECT_EQ(determine_control(held, {2, 2}, true).controller, std::nullopt);
    EXPECT_EQ(determine_control(held, {0, 0}, false).controller, std::nullopt);
}

TEST(R14_02_LevelOfControl, AtTheStartOfTheBattleNoObjectiveIsControlled) {
    const GameState gs = load_objectives();
    ASSERT_TRUE(gs.mission.has_value());
    for (const ObjectiveArea& a : gs.mission->objectives) {
        EXPECT_FALSE(a.controller.has_value()) << a.name;
        EXPECT_FALSE(a.secured_by.has_value()) << a.name;
    }
    EXPECT_EQ(gs.mission->objective_vp, (std::array<int, 2>{0, 0}));
}

TEST(R14_02_LevelOfControl, ControlIsDeterminedAtTheEndOfEachPhaseAndHoldsUntilTheNextCheck) {
    GameState gs = load_objectives();
    keep_units(gs, {1, 6});
    place(unit(gs, 1), 19.0f, 29.0f);
    const std::vector<GameEvent> start = start_game(gs);
    ASSERT_TRUE(gs.pending.has_value());
    ASSERT_EQ(gs.flow.stage, Stage::MoveUnits);
    // Determined at the end of Side A's Command phase.
    EXPECT_EQ(area(gs, kCentre).controller, Side::A);
    const auto changed = events_of<event::ObjectiveControlChanged>(start);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], (event::ObjectiveControlChanged{kCentre, "Centre", Side::A, {20, 0}}));
    EXPECT_EQ(event_rule(GameEvent{changed[0]}), "14.02");
    EXPECT_LT(first_index<event::ObjectiveControlChanged>(start), first_index<event::PhaseEnded>(start));
    // The Legionaries leave in the Movement phase (moved by hand here): control holds until the phase ends.
    place(unit(gs, 1), 22.0f, 20.0f);
    EXPECT_EQ(area(gs, kCentre).controller, Side::A);
    const std::vector<GameEvent> rest = play_until(gs, [](const GameState& s) { return s.flow.stage == Stage::Shoot; });
    EXPECT_EQ(area(gs, kCentre).controller, std::nullopt);
    const auto lost = events_of<event::ObjectiveControlChanged>(rest);
    ASSERT_EQ(lost.size(), 1u);  // only on a change: none for the areas nobody holds
    EXPECT_EQ(lost[0], (event::ObjectiveControlChanged{kCentre, "Centre", std::nullopt, {0, 0}}));
}

TEST(R14_02_LevelOfControl, ControlIsDeterminedAtTheEndOfTheTurnToo) {
    // One Legionary in the Centre, out of coherency with its unit: at the end of Side A's turn its player removes models
    // to regain coherency (03.03). Before they do, a Termagant steps into the area (moved by hand): the check at the end
    // of the turn (after the Fight phase's) sees it, so control changes before TurnEnded.
    GameState gs = load_objectives();
    keep_units(gs, {1, 6});
    unit(gs, 1).models[0].position = at(19.0f, 29.0f);
    start_game(gs);
    ASSERT_EQ(area(gs, kCentre).controller, Side::A);
    play_until(gs, [](const GameState& s) { return s.flow.stage == Stage::EndOfTurn; });
    ASSERT_EQ(gs.flow.stage, Stage::EndOfTurn);
    ASSERT_TRUE(gs.pending.has_value());
    EXPECT_EQ(area(gs, kCentre).controller, Side::A);  // the Fight phase's check: still Side A's
    unit(gs, 6).models[0].position = at(25.0f, 31.0f);
    unit(gs, 6).models[1].position = at(25.0f, 32.3f);
    // Its player removes models one at a time (03.03) until the unit is coherent; then the turn ends.
    const std::vector<GameEvent> ev = play_until(gs, [](const GameState& s) { return s.flow.stage != Stage::EndOfTurn; });
    const std::ptrdiff_t change = first_index<event::ObjectiveControlChanged>(ev);
    ASSERT_GE(change, 0);
    EXPECT_LT(change, first_index<event::TurnEnded>(ev));
    EXPECT_NE(std::get<event::ObjectiveControlChanged>(ev[static_cast<std::size_t>(change)]).controller, Side::A);
}

TEST(R14_02_LevelOfControl, TheUnitsControllingAnObjectiveAreTheControllersUnitsInRangeWithOcOneOrMore) {
    GameState gs = load_objectives();
    keep_units(gs, {1, 2, 6});
    place(unit(gs, 1), 19.0f, 29.0f);
    place(unit(gs, 2), 22.0f, 33.0f);
    place(unit(gs, 6), 25.0f, 31.0f);
    EXPECT_TRUE(controlling_units(gs, kCentre).empty());  // nobody controls it yet
    area(gs, kCentre).controller = Side::A;
    EXPECT_EQ(controlling_units(gs, kCentre), (std::vector<UnitId>{UnitId{1}, UnitId{2}}));
    unit(gs, 2).battle_shocked = true;  // no model with OC 1+
    EXPECT_EQ(controlling_units(gs, kCentre), (std::vector<UnitId>{UnitId{1}}));
    EXPECT_TRUE(controlling_units(gs, kHomeA).empty());
}

// ==== 14.03 Secured objectives (state only: nothing secures one yet) ====

TEST(R14_03_SecuredObjectives, ASecuredObjectiveStaysUnderControlWithNoUnitsInRangeAndOnEqualLevels) {
    ObjectiveArea a{"Centre", at(22.0f, 30.0f), in(12.0f), {5, 5}, Side::A, Side::A};
    EXPECT_EQ(determine_control(a, {0, 0}, true), (ControlOutcome{Side::A, Side::A}));
    EXPECT_EQ(determine_control(a, {2, 2}, true), (ControlOutcome{Side::A, Side::A}));
    EXPECT_EQ(determine_control(a, {3, 1}, true), (ControlOutcome{Side::A, Side::A}));
}

TEST(R14_03_SecuredObjectives, ItIsLostWhenTheOpponentsLevelIsGreaterAtTheEndOfAPhase) {
    ObjectiveArea a{"Centre", at(22.0f, 30.0f), in(12.0f), {5, 5}, Side::A, Side::A};
    EXPECT_EQ(determine_control(a, {1, 3}, true), (ControlOutcome{Side::B, std::nullopt}));
    a.secured_by = Side::B;
    a.controller = Side::B;
    EXPECT_EQ(determine_control(a, {2, 0}, true), (ControlOutcome{Side::A, std::nullopt}));
}

TEST(R14_03_SecuredObjectives, TheCheckAtTheEndOfATurnAloneDoesNotEndIt) {
    // 14.03 says "at the end of a phase": the end-of-turn check keeps it secured.
    const ObjectiveArea a{"Centre", at(22.0f, 30.0f), in(12.0f), {5, 5}, Side::A, Side::A};
    EXPECT_EQ(determine_control(a, {0, 5}, false), (ControlOutcome{Side::A, Side::A}));
}

TEST(R14_03_SecuredObjectives, InAGameASecuredObjectiveHoldsWithNobodyThere) {
    GameState gs = load_objectives();
    keep_units(gs, {1, 6});
    area(gs, kCentre).controller = Side::B;
    area(gs, kCentre).secured_by = Side::B;
    const std::vector<GameEvent> ev = start_game(gs);
    EXPECT_EQ(area(gs, kCentre).controller, Side::B);
    EXPECT_EQ(area(gs, kCentre).secured_by, Side::B);
    EXPECT_TRUE(events_of<event::ObjectiveControlChanged>(ev).empty());
}

// ==== D-084: scoring ====

TEST(D084_Scoring, EachPlayerScoresTheObjectivesTheyControlInTheirOwnCommandPhaseFromBattleRoundTwo) {
    // Side A: the Chaos Lord on its own home (2 VP) and 10 Legionaries on the Centre (5 VP); Side B: 10 Termagants on
    // its own home (2 VP). Nobody moves, shoots or charges.
    GameState gs = load_objectives();
    keep_units(gs, {1, 2, 6});
    place(unit(gs, 1), 20.0f, 29.0f);
    place(unit(gs, 2), 22.0f, 6.0f);
    place(unit(gs, 6), 22.0f, 54.0f);
    const std::vector<GameEvent> all = start_and_play_to_end(gs);
    ASSERT_TRUE(gs.result.has_value());
    const auto scored = events_of<event::VictoryPointsScored>(all);
    ASSERT_EQ(scored.size(), 12u);  // 4 scorings each: A 2 objectives, B 1
    EXPECT_EQ(scored[0], (event::VictoryPointsScored{Side::A, kCentre, "Centre", 5, 5}));
    EXPECT_EQ(scored[1], (event::VictoryPointsScored{Side::A, kHomeA, "A home", 2, 7}));
    EXPECT_EQ(scored[2], (event::VictoryPointsScored{Side::B, kHomeB, "B home", 2, 2}));
    EXPECT_EQ(event_rule(GameEvent{scored[0]}), "D-084");
    EXPECT_EQ(gs.mission->objective_vp, (std::array<int, 2>{28, 8}));
    // Each scoring is in the scorer's own Command phase, battle rounds 2-5: the round's first events score A, then B.
    int round = 0;
    std::optional<Side> turn;
    std::vector<std::pair<int, Side>> when;
    for (const GameEvent& e : all) {
        if (const auto* t = std::get_if<event::TurnStarted>(&e)) {
            round = t->round;
            turn = t->side;
        }
        if (const auto* s = std::get_if<event::VictoryPointsScored>(&e)) {
            EXPECT_EQ(s->side, turn);
            when.emplace_back(round, s->side);
        }
    }
    for (const auto& [r, s] : when) EXPECT_GE(r, 2);
    EXPECT_EQ(when.front().first, 2);
    EXPECT_EQ(when.back(), (std::pair<int, Side>{5, Side::B}));
}

TEST(D084_Scoring, ARaidOnTheEnemyHomeScoresItsVpForTheRaider) {
    // The Chaos Lord on Side B's home (4 VP for A) and 5 Genestealers on Side A's home (4 VP for B).
    GameState gs = load_objectives();
    keep_units(gs, {2, 7});
    place(unit(gs, 2), 22.0f, 54.0f);
    place(unit(gs, 7), 22.0f, 6.0f);
    start_and_play_to_end(gs);
    EXPECT_EQ(gs.mission->objective_vp, (std::array<int, 2>{16, 16}));
}

TEST(D084_Scoring, AnObjectiveScoresOnlyIfItIsStillControlledInTheScorersCommandPhase) {
    // The Legionaries hold the Centre in round 1, and are moved off it (by hand) during Side B's round-1 turn: control
    // is lost at the end of that phase, so Side A scores nothing in round 2.
    GameState gs = load_objectives();
    keep_units(gs, {1, 6});
    place(unit(gs, 1), 20.0f, 29.0f);
    start_game(gs);
    play_until(gs, [](const GameState& s) { return s.flow.battle_round == 1 && s.flow.turn_in_round == 1 && s.pending &&
                                                   s.flow.stage == Stage::MoveUnits; });
    ASSERT_EQ(area(gs, kCentre).controller, Side::A);
    place(unit(gs, 1), 22.0f, 20.0f);
    const std::vector<GameEvent> ev = play_until(gs, [](const GameState& s) { return s.flow.battle_round == 3; });
    EXPECT_TRUE(events_of<event::VictoryPointsScored>(ev).empty());
    EXPECT_EQ(gs.mission->objective_vp, (std::array<int, 2>{0, 0}));
}

TEST(D084_Scoring, TheFirstScoringRoundAndTheVpComeFromTheScenario) {
    nlohmann::json j = objectives_json();
    j["mission"]["first_scoring_round"] = 4;
    j["mission"]["objectives"][0]["vp"] = {{"A", 7}, {"B", 1}};
    GameState gs = build(j);
    keep_units(gs, {1, 6});
    place(unit(gs, 1), 20.0f, 29.0f);
    start_and_play_to_end(gs);
    EXPECT_EQ(gs.mission->objective_vp, (std::array<int, 2>{14, 0}));  // rounds 4 and 5
}

// ==== D-084: kill VP ====

TEST(D084_KillVp, OneVpPerFullFiftyPointsDestroyed) {
    GameState gs = load_objectives();
    EXPECT_EQ(kill_vp(gs), (std::array<int, 2>{0, 0}));
    unit(gs, 6).points = 49;
    unit(gs, 6).models.clear();  // destroyed: 49 points
    EXPECT_EQ(kill_vp(gs), (std::array<int, 2>{0, 0}));
    unit(gs, 6).points = 50;
    EXPECT_EQ(kill_vp(gs), (std::array<int, 2>{1, 0}));
    unit(gs, 6).points = 100;
    EXPECT_EQ(kill_vp(gs), (std::array<int, 2>{2, 0}));
    unit(gs, 6).points = 55;
    // Partial units count by wounds lost (D-076): the Hive Tyrant 3 of 10 wounds lost, 195 x 3 / 10 = 58.5;
    // 55 + 58.5 = 113.5 -> 2 VP.
    unit(gs, 10).models[0].wounds_remaining -= 3;
    EXPECT_EQ(kill_vp(gs), (std::array<int, 2>{2, 0}));
    // The Chaos Lord (95) destroyed for Side B: 1 VP.
    unit(gs, 2).models.clear();
    EXPECT_EQ(kill_vp(gs), (std::array<int, 2>{2, 1}));
    // The step and the VP per step come from the scenario.
    gs.mission->kill_points = 25;
    gs.mission->kill_vp = 3;
    EXPECT_EQ(kill_vp(gs), (std::array<int, 2>{12, 9}));  // 113.5 / 25 = 4 steps; 95 / 25 = 3
    const VpTally t = vp_tally(gs);
    EXPECT_EQ(t.kills, (std::array<int, 2>{12, 9}));
    EXPECT_EQ(t.total, (std::array<int, 2>{12, 9}));
}

// ==== D-084: the result ====

TEST(D084_Result, AfterTheLastBattleRoundTheMostVictoryPointsWin) {
    GameState gs = load_objectives();
    keep_units(gs, {1, 2, 6});
    place(unit(gs, 1), 20.0f, 29.0f);
    place(unit(gs, 2), 22.0f, 6.0f);
    place(unit(gs, 6), 22.0f, 54.0f);
    unit(gs, 6).models.resize(9);  // a Termagant lost before the battle: 5.5 points, no kill VP
    const std::vector<GameEvent> all = start_and_play_to_end(gs);
    ASSERT_TRUE(gs.result.has_value());
    EXPECT_EQ(gs.flow.battle_round, 5);
    EXPECT_EQ(gs.result->winner, Side::A);
    EXPECT_EQ(gs.result->reason,
              "D-084: battle round 5 has ended with both sides on the battlefield; victory points: Side A 28 (28 "
              "objectives + 0 kills), Side B 8 (8 objectives + 0 kills); Side A wins on victory points");
    ASSERT_TRUE(gs.result->vp.has_value());
    EXPECT_EQ(gs.result->vp->objectives, (std::array<int, 2>{28, 8}));
    EXPECT_EQ(gs.result->vp->kills, (std::array<int, 2>{0, 0}));
    EXPECT_EQ(gs.result->vp->total, (std::array<int, 2>{28, 8}));
    ASSERT_TRUE(gs.result->points.has_value());
    EXPECT_EQ(gs.result->points->display, (std::array<std::string, 2>{"5.5", "0"}));
    const auto over = events_of<event::GameOver>(all);
    ASSERT_EQ(over.size(), 1u);
    EXPECT_EQ(over[0].vp, gs.result->vp);
    EXPECT_EQ(over[0].points, gs.result->points);
    EXPECT_EQ(event_rule(GameEvent{over[0]}), "D-084");
}

TEST(D084_Result, KillVpCountTowardsTheResult) {
    // Nobody holds an objective; Side A destroyed 113.5 points (2 kill VP), Side B 95 (1 kill VP).
    GameState gs = load_objectives();
    keep_units(gs, {1, 2, 6, 10});
    unit(gs, 6).models.clear();
    unit(gs, 10).models[0].wounds_remaining -= 3;
    unit(gs, 2).models.clear();
    start_and_play_to_end(gs);
    ASSERT_TRUE(gs.result.has_value());
    EXPECT_EQ(gs.result->winner, Side::A);
    EXPECT_EQ(gs.result->reason,
              "D-084: battle round 5 has ended with both sides on the battlefield; victory points: Side A 2 (0 "
              "objectives + 2 kills), Side B 1 (0 objectives + 1 kills); Side A wins on victory points");
}

TEST(D084_Result, EqualVictoryPointsAreDecidedOnPointsDestroyed) {
    GameState gs = load_objectives();
    keep_units(gs, {1, 6});
    unit(gs, 6).models.resize(3);   // 7 Termagants lost: 55 x 7 / 10 = 38.5 points, 0 kill VP
    unit(gs, 1).models.resize(9);   // a Legionary lost: 180 x 2 / 20 = 18 points, 0 kill VP
    start_and_play_to_end(gs);
    ASSERT_TRUE(gs.result.has_value());
    EXPECT_EQ(gs.result->winner, Side::A);
    EXPECT_EQ(gs.result->reason,
              "D-084: battle round 5 has ended with both sides on the battlefield; victory points: Side A 0 (0 "
              "objectives + 0 kills), Side B 0 (0 objectives + 0 kills); equal victory points (0 each); points "
              "destroyed: Side A 38.5, Side B 18; Side A wins on points destroyed");
    EXPECT_EQ(gs.result->vp->total, (std::array<int, 2>{0, 0}));
}

TEST(D084_Result, EqualVictoryPointsAndEqualPointsDestroyedAreADraw) {
    GameState gs = load_objectives();
    keep_units(gs, {1, 6});
    start_and_play_to_end(gs);
    ASSERT_TRUE(gs.result.has_value());
    EXPECT_FALSE(gs.result->winner.has_value());
    EXPECT_EQ(gs.result->reason,
              "D-084: battle round 5 has ended with both sides on the battlefield; victory points: Side A 0 (0 "
              "objectives + 0 kills), Side B 0 (0 objectives + 0 kills); equal victory points (0 each); points "
              "destroyed: Side A 0, Side B 0; equal points destroyed: the game is a draw");
}

TEST(D007_GameEnd, AWipeOutStillWinsAtOnceUnderTheMission) {
    // The V1 shooting set-up with the mission on: Side B holds the only objective, but Side A shoots the last Termagant
    // and wins at once (D-007), in battle round 1.
    GameState gs = wh40k::test::shoot_step_state();
    gs.result_rule = ResultRule::Mission;
    Mission m;
    m.objectives.push_back(ObjectiveArea{"Centre", at(12.0f, 12.0f), in(6.0f), {5, 5}, Side::B, std::nullopt});
    m.objective_vp = {0, 10};
    gs.mission = m;
    gs.units[0].points = 100;
    gs.units[0].starting_wounds = 10;
    gs.units[1].points = 60;
    gs.units[1].starting_wounds = 10;
    gs.units[1].models.resize(1);
    EXPECT_NO_THROW(check_mission_data(gs, "test"));
    ScriptedDice dice{3, 1, 1, 3, 1};  // one hit, one wound, failed save
    const UnitId gants = gs.units[1].id;
    begin_attacks(gs, dice, gs.units[0].id, AttackContext{AttackType::Shooting, true},
                  {wh40k::test::select(gs, 0, 0, 0, 1, gants)});
    const auto events = apply_action(gs, Side::B, action::OrderCasualties{gants, model_ids(gs.units[1])}, dice);
    ASSERT_TRUE(gs.result.has_value());
    EXPECT_EQ(gs.flow.battle_round, 1);
    EXPECT_EQ(gs.result->winner, Side::A);
    EXPECT_EQ(gs.result->reason, "D-007: Side B has no models on the battlefield; Side A wins");
    EXPECT_FALSE(gs.result->vp.has_value());
    const auto over = events_of<event::GameOver>(events);
    ASSERT_EQ(over.size(), 1u);
    EXPECT_EQ(event_rule(GameEvent{over[0]}), "D-007");
}

// ==== The scenario and its loader ====

TEST(D084_Scenario, TheObjectiveScenarioIsTheRostersScenarioWithTheMission) {
    const GameState gs = load_objectives();
    const GameState rosters = wh40k::test::load_rosters_scenario();
    EXPECT_EQ(gs.scenario_name, "Incursion objectives - Chaos Space Marines vs Tyranids (44x60 portrait)");
    EXPECT_EQ(gs.units, rosters.units);
    EXPECT_EQ(gs.sides, rosters.sides);
    EXPECT_EQ(gs.battlefield, rosters.battlefield);
    EXPECT_EQ(gs.battle_rounds, 5);
    EXPECT_EQ(gs.first_turn, Side::A);
    EXPECT_EQ(gs.result_rule, ResultRule::Mission);
    ASSERT_TRUE(gs.mission.has_value());
    const Mission& m = *gs.mission;
    EXPECT_EQ(m.first_scoring_round, 2);
    EXPECT_EQ(m.kill_points, 50);
    EXPECT_EQ(m.kill_vp, 1);
    // D-084: a 12" area at the centre; a 6" home per side on the centre line, 6" from its own edge (south is y = 0).
    ASSERT_EQ(m.objectives.size(), 3u);
    EXPECT_EQ(m.objectives[kCentre], (ObjectiveArea{"Centre", at(22.0f, 30.0f), in(12.0f), {5, 5}, std::nullopt, std::nullopt}));
    EXPECT_EQ(m.objectives[kHomeA], (ObjectiveArea{"A home", at(22.0f, 6.0f), in(6.0f), {2, 4}, std::nullopt, std::nullopt}));
    EXPECT_EQ(m.objectives[kHomeB], (ObjectiveArea{"B home", at(22.0f, 54.0f), in(6.0f), {4, 2}, std::nullopt, std::nullopt}));
    // Each home is wholly within its side's deployment zone.
    for (const Side s : {Side::A, Side::B}) {
        const ObjectiveArea& home = m.objectives[s == Side::A ? kHomeA : kHomeB];
        const Rect zone = zone_rect(gs.battlefield, gs.side(s).deployment_zone);
        EXPECT_GE(home.centre.y - home.diameter_mm / 2, zone.min_y);
        EXPECT_LE(home.centre.y + home.diameter_mm / 2, zone.max_y);
    }
}

TEST(D084_Scenario, OlderScenariosHaveNoMission) {
    for (const char* file : {"data/scenarios/v1_skirmish.json", "data/scenarios/rosters_44x60.json",
                             "data/scenarios/strike_44x60.json"}) {
        const GameState gs = load_scenario(repo_root() / file, repo_root());
        EXPECT_FALSE(gs.mission.has_value()) << file;
        EXPECT_NE(gs.result_rule, ResultRule::Mission) << file;
    }
}

TEST(D084_Scenario, AMalformedMissionIsRefused) {
    const auto with = [](auto change) {
        nlohmann::json j = objectives_json();
        change(j);
        return refusal(j);
    };
    EXPECT_EQ(with([](nlohmann::json&) {}), "");
    EXPECT_EQ(with([](nlohmann::json& j) { j.erase("mission"); }),
              "scenario: D-084: result \"mission\" needs a \"mission\" block");
    EXPECT_EQ(with([](nlohmann::json& j) { j["result"] = "points_destroyed"; }),
              "scenario: D-084: a \"mission\" block needs result \"mission\"");
    EXPECT_EQ(with([](nlohmann::json& j) { j["mission"]["first_scoring_round"] = 0; }),
              "scenario: D-084: first_scoring_round 0 is not a battle round (1..5)");
    EXPECT_EQ(with([](nlohmann::json& j) { j["mission"]["first_scoring_round"] = 6; }),
              "scenario: D-084: first_scoring_round 6 is not a battle round (1..5)");
    EXPECT_EQ(with([](nlohmann::json& j) { j["mission"]["kill_vp"]["points"] = 0; }),
              "scenario: D-084: kill_vp points 0 must be 1..100000");
    EXPECT_EQ(with([](nlohmann::json& j) { j["mission"]["kill_vp"]["vp"] = -1; }),
              "scenario: D-084: kill_vp vp -1 must be 0..1000");
    EXPECT_EQ(with([](nlohmann::json& j) { j["mission"]["objectives"] = nlohmann::json::array(); }),
              "scenario: D-084: a mission has 1..32 objectives, not 0");
    EXPECT_EQ(with([](nlohmann::json& j) { j["mission"]["objectives"][0]["diameter_in"] = 0; }),
              "scenario: D-084: objective 'Centre' needs a diameter greater than 0");
    EXPECT_EQ(with([](nlohmann::json& j) { j["mission"]["objectives"][1]["y_in"] = 2; }),
              "scenario: D-084: objective 'A home' is not wholly within the battlefield");
    EXPECT_EQ(with([](nlohmann::json& j) { j["mission"]["objectives"][2]["vp"]["A"] = -4; }),
              "scenario: D-084: objective 'B home' has VP -4; VP must be 0..1000");
    EXPECT_EQ(with([](nlohmann::json& j) { j["mission"]["objectives"][0]["name"] = ""; }),
              "scenario: D-084: an objective needs a name");
    EXPECT_EQ(with([](nlohmann::json& j) { j["mission"]["objectives"][0].erase("vp"); }),
              "scenario: mission.objectives[0]: missing field 'vp'");
    EXPECT_EQ(with([](nlohmann::json& j) { j["mission"]["objectives"][0]["x_in"] = "middle"; }),
              "scenario: mission.objectives[0]: field 'x_in' must be a number of inches");
    EXPECT_EQ(with([](nlohmann::json& j) { j["mission"]["first_scoring_round"] = 2.5; }),
              "scenario: mission: field 'first_scoring_round' must be an integer, not 2.5");
    EXPECT_EQ(with([](nlohmann::json& j) { j["result"] = "objectives"; }),
              "scenario: root: D-076: unknown result \"objectives\" (the values are \"points_destroyed\" and "
              "\"mission\")");
    EXPECT_EQ(with([](nlohmann::json& j) { j["sides"]["A"]["units"][1].erase("points"); }),
              "scenario: side A unit 'Chaos Lord': D-084: missing field 'points' (result \"mission\" needs every "
              "unit's points)");
}

TEST(D084_Scenario, TheMissionBlockCanBeReadOnItsOwn) {
    EXPECT_EQ(load_mission_block(repo_root() / kObjectiveScenario), objectives_json().at("mission"));
    EXPECT_THROW((void)load_mission_block(repo_root() / "data/scenarios/rosters_44x60.json"), DataError);
}

TEST(D084_CustomBattle, ACustomBattleGivenTheMissionPlaysIt) {
    namespace t = wh40k::test;
    const nlohmann::json block = load_mission_block(repo_root() / kObjectiveScenario);
    const auto lists = [] { return std::pair{t::stock_army(t::csm()), t::stock_army(t::tyranids())}; };
    const auto [a, b] = lists();
    const nlohmann::json plain = custom_battle_scenario(a, b, Side::A, t::factions(), t::army_resolver());
    const nlohmann::json with = custom_battle_scenario(a, b, Side::A, t::factions(), t::army_resolver(),
                                                       DeploymentMode::Auto, &block);
    EXPECT_EQ(plain.at("result"), "points_destroyed");
    EXPECT_FALSE(plain.contains("mission"));
    EXPECT_EQ(with.at("result"), "mission");
    EXPECT_EQ(with.at("mission"), block);
    nlohmann::json same = with;  // everything else is the same battle
    same["result"] = "points_destroyed";
    same.erase("mission");
    same["notes"] = plain.at("notes");
    EXPECT_EQ(same, plain);
    const GameState gs = build_game_state(with, t::army_resolver());
    EXPECT_EQ(gs.result_rule, ResultRule::Mission);
    ASSERT_TRUE(gs.mission.has_value());
    EXPECT_EQ(gs.mission->objectives.size(), 3u);
    // By hand too (D-081).
    const nlohmann::json hand = custom_battle_scenario(a, b, Side::B, t::factions(), t::army_resolver(),
                                                       DeploymentMode::Hand, &block);
    EXPECT_EQ(build_game_state(hand, t::army_resolver()).result_rule, ResultRule::Mission);
}

// ==== State JSON ====

TEST(D084_Json, TheStateRoundTripsWithTheMissionItsControlAndTheResult) {
    GameState gs = load_objectives();
    keep_units(gs, {1, 2, 6});
    place(unit(gs, 1), 20.0f, 29.0f);
    place(unit(gs, 2), 22.0f, 6.0f);
    place(unit(gs, 6), 22.0f, 54.0f);
    area(gs, kHomeB).secured_by = Side::B;
    start_and_play_to_end(gs);
    ASSERT_TRUE(gs.result.has_value());
    const nlohmann::json j = gs;
    EXPECT_EQ(j.at("result_rule"), "mission");
    ASSERT_TRUE(j.contains("mission"));
    EXPECT_EQ(j.at("mission").at("objective_vp"), nlohmann::json::array({28, 8}));
    EXPECT_EQ(j.at("mission").at("objectives").at(kHomeB).at("secured_by"), "B");
    EXPECT_FALSE(j.at("mission").at("objectives").at(kCentre).contains("secured_by"));
    EXPECT_TRUE(j.at("result").contains("vp"));
    EXPECT_EQ(j.get<GameState>(), gs);
    EXPECT_EQ(nlohmann::json(j.get<GameState>()).dump(), j.dump());
    // Mid-game too, with an area nobody controls (no "controller" key).
    GameState mid = load_objectives();
    start_game(mid);
    const nlohmann::json m = mid;
    EXPECT_FALSE(m.at("mission").at("objectives").at(kCentre).contains("controller"));
    EXPECT_EQ(m.get<GameState>(), mid);
}

TEST(D084_Json, AStateWithoutAMissionHasNoMissionKey) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    const nlohmann::json j = gs;
    EXPECT_FALSE(j.contains("mission"));
    EXPECT_FALSE(j.contains("result_rule"));
}

TEST(D084_Json, AMalformedMissionInAStateIsRefused) {
    GameState gs = load_objectives();
    start_game(gs);
    const auto with = [&gs](auto change) {
        nlohmann::json j = gs;
        change(j);
        return state_refusal(j);
    };
    EXPECT_EQ(with([](nlohmann::json&) {}), "");
    EXPECT_EQ(with([](nlohmann::json& j) { j.erase("mission"); }),
              "state JSON: D-084: result \"mission\" needs a \"mission\" block");
    EXPECT_EQ(with([](nlohmann::json& j) { j.erase("result_rule"); }),
              "state JSON: D-084: a \"mission\" block needs result \"mission\"");
    EXPECT_EQ(with([](nlohmann::json& j) { j["mission"]["objective_vp"] = {-1, 0}; }),
              "state JSON: D-084: objective VP scored can't be negative (-1)");
    EXPECT_EQ(with([](nlohmann::json& j) { j["mission"]["objectives"][0]["diameter_mm"] = -1.0; }),
              "state JSON: D-084: objective 'Centre' needs a diameter greater than 0");
}

TEST(D084_Json, TheNewEventsAndAMissionResultRoundTrip) {
    const auto round_trip = [](const GameEvent& e) {
        nlohmann::json j;
        event::to_json(j, e);
        GameEvent back;
        event::from_json(j, back);
        return back;
    };
    PointsTally p;
    p.destroyed = {480, 240};
    p.denominator = 10;
    p.display = {"48", "24"};
    p.battle_round = 5;
    const event::GameOver over{Side::A, "D-084: ...", p, VpTally{{20, 9}, {0, 0}, {20, 9}}};
    EXPECT_EQ(round_trip(over), GameEvent{over});
    EXPECT_EQ(event_rule(GameEvent{over}), "D-084");
    const event::ObjectiveControlChanged none{0, "Centre", std::nullopt, {2, 2}};
    EXPECT_EQ(round_trip(none), GameEvent{none});
    nlohmann::json bad = GameResult{Side::A, "x", p, VpTally{{1, 0}, {0, 0}, {2, 0}}};
    EXPECT_THROW((void)bad.get<GameResult>(), DataError);  // the total isn't the sum
}
