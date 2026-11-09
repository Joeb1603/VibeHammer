// M67 (D-076, amends D-007; 07.03): the portrait roster scenario and the points tiebreak. A wipe-out still wins at
// once; otherwise, after the End of Battle Round step of the last battle round, the side that destroyed more points
// wins, compared exactly, and equal points is a draw.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "core/data/datasheet.hpp"
#include "core/data/load_error.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/oval.hpp"
#include "core/geometry/pose.hpp"
#include "core/state/points_destroyed.hpp"
#include "core/rules/shooting.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/queries.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::in;
using wh40k::test::load_rosters_scenario;
using wh40k::test::model_ids;
using wh40k::test::neutral_action;
using wh40k::test::repo_root;

namespace {

Unit& unit(GameState& gs, std::uint32_t id) { return *gs.find_unit(UnitId{id}); }

// Plays neutral answers (remain stationary, end every step) from the start to the end of the game; the events of the
// last action.
std::vector<GameEvent> play_to_end(GameState& gs) {
    std::vector<GameEvent> last = start_game(gs);
    for (int guard = 0; guard < 5000 && gs.pending; ++guard) last = apply_action(gs, decider(gs), neutral_action(gs));
    return last;
}

nlohmann::json rosters_json() { return read_json_file(repo_root() / wh40k::test::kRostersScenario); }

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

std::vector<std::string> weapon_names(const Model& m) {
    std::vector<std::string> out;
    for (const Weapon& w : m.weapons) out.push_back(w.name);
    return out;
}

}  // namespace

// ==== The scenario ====

TEST(R02_07_WargearOptions, TheRostersScenarioCarriesEveryRosterUnitWithItsLoadoutAndPoints) {
    // The two New Recruit rosters (docs/datasheets/): every unit, model by model, with the roster's points. A
    // duplicated roster unit is printed once with a count at the points of one unit ("75 pts Genestealers 2").
    const GameState gs = load_rosters_scenario();
    EXPECT_EQ(gs.scenario_name, "Incursion - Chaos Space Marines vs Tyranids (44x60 portrait)");
    EXPECT_EQ(gs.result_rule, ResultRule::PointsDestroyed);
    ASSERT_EQ(gs.units.size(), 14u);
    using V = std::vector<std::string>;
    struct Expected {
        std::uint32_t id;
        Side side;
        const char* name;
        int points;
        int starting_wounds;
        std::vector<std::pair<const char*, V>> models;  // type, weapons, one entry per model
    };
    auto many = [](int n, const char* type, V weapons) {
        return std::vector<std::pair<const char*, V>>(static_cast<std::size_t>(n), {type, std::move(weapons)});
    };
    auto join = [](std::vector<std::pair<const char*, V>> a, const std::vector<std::pair<const char*, V>>& b) {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    };
    const V champion{"Bolt pistol", "Astartes chainsword", "Close combat weapon"};
    const std::vector<Expected> expected{
        {1, Side::A, "Legionaries", 180, 20,
         join(many(1, "Aspiring Champion", champion), many(9, "Legionary w/ chainsword", champion))},
        {2, Side::A, "Chaos Lord", 95, 5, many(1, "Chaos Lord", {"Plasma pistol", "Astartes chainblade"})},
        {3, Side::A, "Chaos Terminator Squad", 370, 30,
         join(many(1, "Terminator Champion", {"Combi-bolter", "Accursed weapon"}),
              many(9, "Accursed weapon and combi-bolter", {"Combi-bolter", "Accursed weapon"}))},
        {4, Side::A, "Havocs", 135, 15,
         join(many(1, "Havoc Champion", {"Plasma gun", "Astartes chainsword"}),
              many(4, "Havoc w/ lascannon", {"Havoc lascannon", "Close combat weapon"}))},
        {5, Side::A, "Legionaries", 180, 20,
         join(many(1, "Aspiring Champion", champion),
              many(9, "Legionary w/ boltgun", {"Boltgun", "Bolt pistol", "Close combat weapon"}))},
        {6, Side::B, "Termagants", 55, 10, many(10, "Termagants", {"Fleshborer", "Chitinous claws and teeth"})},
        {7, Side::B, "Genestealers", 75, 10, many(5, "Genestealer", {"Genestealers claws and talons"})},
        {8, Side::B, "Screamer-killer", 125, 10, many(1, "Screamer-killer", {"Bio-plasmic scream", "Screamer-killer talons"})},
        {9, Side::B, "Tyrant Guard", 80, 12, many(3, "Tyrant Guard", {"Scything talons and rending claws"})},
        {10, Side::B, "Hive Tyrant", 195, 10,
         many(1, "Hive Tyrant", {"Monstrous bonesword and lash whip", "Monstrous scything talons"})},
        {11, Side::B, "Tyrannofex", 170, 16, many(1, "Tyrannofex", {"Powerful limbs", "Stinger salvoes", "Fleshborer hive"})},
        {12, Side::B, "Screamer-killer", 125, 10, many(1, "Screamer-killer", {"Bio-plasmic scream", "Screamer-killer talons"})},
        {13, Side::B, "Genestealers", 75, 10, many(5, "Genestealer", {"Genestealers claws and talons"})},
        {14, Side::B, "Termagants", 55, 10, many(10, "Termagants", {"Fleshborer", "Chitinous claws and teeth"})},
    };
    std::array<int, 2> totals{};
    for (const Expected& e : expected) {
        SCOPED_TRACE("unit " + std::to_string(e.id));
        const Unit* u = gs.find_unit(UnitId{e.id});
        ASSERT_NE(u, nullptr);
        EXPECT_EQ(u->side, e.side);
        EXPECT_EQ(u->name, e.name);
        EXPECT_EQ(u->points, e.points);
        EXPECT_EQ(u->starting_wounds, e.starting_wounds);
        EXPECT_EQ(u->starting_wounds, sum_of_wounds_characteristics(*u));
        ASSERT_EQ(u->models.size(), e.models.size());
        for (std::size_t i = 0; i < e.models.size(); ++i) {
            EXPECT_EQ(u->models[i].type_name, e.models[i].first) << "model " << i;
            EXPECT_EQ(weapon_names(u->models[i]), e.models[i].second) << "model " << i;
        }
        totals[side_index(u->side)] += u->points;
    }
    EXPECT_EQ(totals[0], 960);  // "AppTestCSM [960pts]"
    EXPECT_EQ(totals[1], 955);  // "AppTestTyranids [955pts]"
    EXPECT_EQ(gs.side(Side::A).name, "Chaos Space Marines");
    EXPECT_EQ(gs.side(Side::B).name, "Tyranids");
    EXPECT_EQ(points_denominator(gs), 240);  // lcm(20, 5, 30, 15, 10, 12, 16)
}

TEST(R03_02_SetUp, TheRostersScenarioIsALegalSetUpNumberedWestToEast) {
    // 03.02 / 03.03 / D-009 / D-064, measured with the facings (the Tyrannofex's oval at facing 0).
    const GameState gs = load_rosters_scenario();
    EXPECT_FLOAT_EQ(gs.battlefield.width_mm, in(44));
    EXPECT_FLOAT_EQ(gs.battlefield.depth_mm, in(60));
    EXPECT_EQ(gs.battle_rounds, 5);
    EXPECT_EQ(gs.first_turn, Side::A);
    EXPECT_EQ(gs.side(Side::A).deployment_zone.edge, BattlefieldEdge::South);
    EXPECT_EQ(gs.side(Side::B).deployment_zone.edge, BattlefieldEdge::North);
    EXPECT_FLOAT_EQ(gs.side(Side::A).deployment_zone.depth_mm, in(12));
    EXPECT_FLOAT_EQ(gs.side(Side::B).deployment_zone.depth_mm, in(12));
    float closest_enemy = 1e9f;
    std::array<float, 2> last_x{-1.0f, -1.0f};
    for (const Unit& u : gs.units) {
        EXPECT_TRUE(unit_coherency(u).ok()) << u.id.v;  // 03.03
        const Rect zone = zone_rect(gs.battlefield, gs.side(u.side).deployment_zone);
        float x = 0.0f;
        for (const Model& m : u.models) {
            EXPECT_TRUE(wholly_within(zone, pose_of(m))) << "model " << m.id.v;  // D-009
            x += m.position.x;
        }
        x /= static_cast<float>(u.models.size());
        EXPECT_GT(x, last_x[side_index(u.side)]) << "unit " << u.id.v << " is not east of the one before";
        last_x[side_index(u.side)] = x;
        for (const Unit& v : gs.units) {
            for (const Model& a : u.models) {
                for (const Model& b : v.models) {
                    if (a.id == b.id) continue;
                    EXPECT_FALSE(bases_overlap(pose_of(a), pose_of(b))) << a.id.v << " " << b.id.v;
                    if (u.side != v.side) closest_enemy = std::min(closest_enemy, base_distance(pose_of(a), pose_of(b)));
                }
            }
        }
        EXPECT_TRUE(engaged_enemy_units(gs, u).empty()) << u.id.v;
    }
    EXPECT_GE(closest_enemy, in(36) - EPS_MM);  // 36" between the zones
    const Model& tyrannofex = gs.find_unit(UnitId{11})->models.at(0);
    EXPECT_EQ(tyrannofex.base.shape, BaseShape::Oval);
    EXPECT_EQ(tyrannofex.facing_deg, 0.0f);
    // Held back: the Havocs 3", the Tyrant Guard and the Tyrannofex 4" (their front edges, from the zone fronts).
    auto front_edge = [&](std::uint32_t id) {
        const Unit& u = *gs.find_unit(UnitId{id});
        float edge = u.side == Side::A ? -1e9f : 1e9f;
        for (const Model& m : u.models) {
            const BaseExtents e = base_extents(m.base, m.facing_deg);
            edge = u.side == Side::A ? std::max(edge, m.position.y + e.half_y_mm) : std::min(edge, m.position.y - e.half_y_mm);
        }
        return edge;
    };
    EXPECT_NEAR(front_edge(1), in(12), 1e-3f);
    EXPECT_NEAR(front_edge(4), in(12 - 3), 1e-3f);
    EXPECT_NEAR(front_edge(6), in(48), 1e-3f);
    EXPECT_NEAR(front_edge(9), in(48 + 4), 1e-3f);
    EXPECT_NEAR(front_edge(11), in(48 + 4), 1e-3f);
}

// ==== The loader (D-076) ====

TEST(D076_Loader, PointsAndTheResultOptionAreRead) {
    // Absent "result": D-007 as before. Points without the option are kept (and checked) but decide nothing.
    nlohmann::json j = rosters_json();
    j.erase("result");
    const GameState gs = build(j);
    EXPECT_EQ(gs.result_rule, ResultRule::D007Draw);
    EXPECT_EQ(gs.find_unit(UnitId{3})->points, 370);
    EXPECT_EQ(gs.find_unit(UnitId{3})->starting_wounds, 30);
    // Without any points either, the units hold none.
    for (const Side side : {Side::A, Side::B}) {
        for (nlohmann::json& u : j["sides"][side == Side::A ? "A" : "B"]["units"]) u.erase("points");
    }
    const GameState plain = build(j);
    for (const Unit& u : plain.units) {
        EXPECT_EQ(u.points, 0);
        EXPECT_EQ(u.starting_wounds, 0);
    }
}

TEST(D076_Loader, MalformedPointsOrResultAreRefused) {
    {
        nlohmann::json j = rosters_json();
        j["result"] = "points";
        EXPECT_EQ(refusal(j), "scenario: root: D-076: unknown result \"points\" (the only value is \"points_destroyed\")");
        j["result"] = 7;
        EXPECT_EQ(refusal(j), "scenario: root: D-076: unknown result 7 (the only value is \"points_destroyed\")");
    }
    {
        nlohmann::json j = rosters_json();
        j["sides"]["B"]["units"][2].erase("points");
        EXPECT_EQ(refusal(j), "scenario: side B unit 'Screamer-killer': D-076: missing field 'points' (result "
                              "\"points_destroyed\" needs every unit's points)");
    }
    for (const nlohmann::json& bad : {nlohmann::json(0), nlohmann::json(-5), nlohmann::json(2.5), nlohmann::json("100"),
                                      nlohmann::json(100001), nlohmann::json(nullptr)}) {
        nlohmann::json j = rosters_json();
        j["sides"]["A"]["units"][1]["points"] = bad;
        EXPECT_EQ(refusal(j), "scenario: side A unit 'Chaos Lord': D-076: points must be a positive integer of at most "
                              "100000, not " + bad.dump());
        j.erase("result");  // also without the option
        EXPECT_NE(refusal(j), "") << bad.dump();
    }
    {
        nlohmann::json j = rosters_json();
        j["sides"]["A"]["units"][1]["points"] = 100000;  // the largest value is fine
        EXPECT_EQ(build(j).find_unit(UnitId{2})->points, 100000);
    }
}

// ==== The rule (07.03 End of Battle Round, D-076) ====

TEST(R07_03_PointsTiebreak, AfterTheLastBattleRoundTheSideThatDestroyedMorePointsWins) {
    GameState gs = load_rosters_scenario();
    unit(gs, 6).models.clear();                       // the west Termagants destroyed: 55
    unit(gs, 10).models[0].wounds_remaining -= 3;     // the Hive Tyrant 3 of 10 wounds lost: 195 x 3 / 10 = 58.5
    unit(gs, 2).models[0].wounds_remaining -= 1;      // the Chaos Lord 1 of 5 lost: 95 / 5 = 19
    const std::vector<GameEvent> last = play_to_end(gs);
    ASSERT_TRUE(gs.result.has_value());
    EXPECT_EQ(gs.flow.battle_round, 5);  // not before the last battle round
    EXPECT_EQ(gs.result->winner, Side::A);
    EXPECT_EQ(gs.result->reason,
              "D-076: battle round 5 has ended with both sides on the battlefield; points destroyed: Side A 113.5, "
              "Side B 19; Side A wins on points destroyed");
    ASSERT_TRUE(gs.result->points.has_value());
    const PointsTally& t = *gs.result->points;
    EXPECT_EQ(t.denominator, 240);
    EXPECT_EQ(t.destroyed[0], 27240);  // exact: 113.5 x 240
    EXPECT_EQ(t.destroyed[1], 19 * 240);
    EXPECT_EQ(t.display[0], "113.5");
    EXPECT_EQ(t.display[1], "19");
    EXPECT_EQ(t.battle_round, 5);
    const auto over = events_of<event::GameOver>(last);
    ASSERT_EQ(over.size(), 1u);
    EXPECT_EQ(over[0].winner, Side::A);
    EXPECT_EQ(over[0].reason, gs.result->reason);
    EXPECT_EQ(over[0].points, gs.result->points);
    EXPECT_EQ(event_rule(GameEvent{over[0]}), "D-076");
    // The rounds before the last ended without a result.
    EXPECT_EQ(events_of<event::BattleRoundEnded>(last).size(), 1u);
}

TEST(R07_03_PointsTiebreak, TheSideThatDestroyedFewerPointsLoses) {
    GameState gs = load_rosters_scenario();
    gs.battle_rounds = 1;
    unit(gs, 3).models.resize(8);                  // two Terminators destroyed: 6 of 30 wounds, 370 x 6 / 30 = 74
    unit(gs, 13).models[4].wounds_remaining = 1;   // a Genestealer wounded: 1 of 10, 75 / 10 = 7.5
    play_to_end(gs);
    ASSERT_TRUE(gs.result.has_value());
    EXPECT_EQ(gs.result->winner, Side::B);
    EXPECT_EQ(gs.result->reason,
              "D-076: battle round 1 has ended with both sides on the battlefield; points destroyed: Side A 7.5, Side B "
              "74; Side B wins on points destroyed");
    EXPECT_EQ(gs.result->points->display, (std::array<std::string, 2>{"7.5", "74"}));
}

TEST(D076_PointsTiebreak, AnExactTieIsADrawWhereFloatsWouldPickAWinner) {
    // Side A destroys 1/10 of a 1-point unit and 1/10 of a 2-point unit (0.1 + 0.2); Side B destroys 2/20 of a 3-point
    // unit (0.3). Exactly equal, so a draw; summed as doubles, 0.1 + 0.2 > 0.3 would give Side A the game.
    GameState gs = load_rosters_scenario();
    gs.battle_rounds = 1;
    unit(gs, 6).points = 1;
    unit(gs, 6).models.pop_back();                  // a Termagant: 1 of 10 wounds
    unit(gs, 7).points = 2;
    unit(gs, 7).models[2].wounds_remaining = 1;     // a Genestealer wounded: 1 of 10
    unit(gs, 1).points = 3;
    unit(gs, 1).models.pop_back();                  // a Legionary: 2 of 20
    const double a = 1.0 * 1 / 10 + 2.0 * 1 / 10;
    const double b = 3.0 * 2 / 20;
    EXPECT_GT(a, b);  // the float answer
    play_to_end(gs);
    ASSERT_TRUE(gs.result.has_value());
    EXPECT_FALSE(gs.result->winner.has_value());
    EXPECT_EQ(gs.result->points->destroyed[0], gs.result->points->destroyed[1]);
    EXPECT_EQ(gs.result->points->display, (std::array<std::string, 2>{"0.3", "0.3"}));
    EXPECT_EQ(gs.result->reason,
              "D-076: battle round 1 has ended with both sides on the battlefield; points destroyed: Side A 0.3, Side B "
              "0.3; equal points destroyed: the game is a draw");
}

TEST(D076_PointsTiebreak, ANearTieThatDoublesCannotTellApartIsDecidedExactly) {
    // Two units with very large starting wounds (a legal input: lcm 154781107 x 137511488 = 2.1e16, times the 12 points
    // in play, is under 2^59). Side A destroyed 6 x 103952281 / 154781107 of the Hive Tyrant; Side B 6 x 92353861 /
    // 137511488 of the Chaos Lord. Side A's total is larger by 6 / (154781107 x 137511488), less than half a double's
    // step at that size, so both come out as the same double.
    GameState gs = load_rosters_scenario();
    gs.battle_rounds = 1;
    std::erase_if(gs.units, [](const Unit& u) { return u.id != UnitId{2} && u.id != UnitId{10}; });
    auto set = [&](std::uint32_t id, int points, int w, int lost) {
        Unit& u = unit(gs, id);
        u.points = points;
        u.starting_wounds = w;
        u.models[0].characteristics.wounds = w;
        u.models[0].wounds_remaining = w - lost;
    };
    set(10, 6, 154781107, 103952281);
    set(2, 6, 137511488, 92353861);
    EXPECT_NO_THROW(check_points_data(gs, "test"));
    const double a = 6.0 * 103952281 / 154781107;
    const double b = 6.0 * 92353861 / 137511488;
    EXPECT_EQ(a, b);  // the double answer: a draw
    play_to_end(gs);
    ASSERT_TRUE(gs.result.has_value());
    EXPECT_EQ(gs.result->winner, Side::A);
    const PointsTally& t = *gs.result->points;
    EXPECT_EQ(t.destroyed[0] - t.destroyed[1], 6);  // over 154781107 x 137511488
    EXPECT_EQ(t.denominator, std::int64_t{154781107} * 137511488);
    EXPECT_NE(t.display[0], t.display[1]);  // shown to enough places to read apart
    EXPECT_GT(t.display[0], t.display[1]);
}

TEST(D007_GameEnd, AWipeOutStillWinsAtOnceUnderThePointsTiebreak) {
    // The V1 shooting set-up with the tiebreak on: Side B is far ahead on points (8 of the 10 Intercessor wounds, worth
    // 800 of 1000), but Side A shoots the last Termagant: the game ends at once (D-007), in battle round 1.
    GameState gs = wh40k::test::shoot_step_state();
    gs.result_rule = ResultRule::PointsDestroyed;
    gs.units[0].points = 1000;
    gs.units[0].starting_wounds = 10;
    gs.units[1].points = 1;
    gs.units[1].starting_wounds = 10;
    gs.units[0].models.resize(1);
    gs.units[1].models.resize(1);
    const PointsTally before = points_destroyed(gs);
    EXPECT_GT(before.destroyed[1], before.destroyed[0]);
    ScriptedDice dice{3, 1, 1, 3, 1};  // one hit, one wound, failed save
    const UnitId gants = gs.units[1].id;
    begin_attacks(gs, dice, gs.units[0].id, AttackContext{AttackType::Shooting, true},
                  {wh40k::test::select(gs, 0, 0, 0, 1, gants)});
    const auto events = apply_action(gs, Side::B, action::OrderCasualties{gants, model_ids(gs.units[1])}, dice);
    ASSERT_TRUE(gs.result.has_value());
    EXPECT_EQ(gs.flow.battle_round, 1);
    EXPECT_EQ(gs.result->winner, Side::A);
    EXPECT_EQ(gs.result->reason, "D-007: Side B has no models on the battlefield; Side A wins");
    EXPECT_FALSE(gs.result->points.has_value());  // not decided on points
    const auto over = events_of<event::GameOver>(events);
    ASSERT_EQ(over.size(), 1u);
    EXPECT_FALSE(over[0].points.has_value());
    EXPECT_EQ(event_rule(GameEvent{over[0]}), "D-007");
}

TEST(D007_GameEnd, AScenarioWithoutTheOptionStillDrawsWhateverWasDestroyed) {
    for (const bool with_points : {false, true}) {
        GameState gs = wh40k::test::load_v1_scenario();
        gs.units[1].models.resize(3);  // seven Termagants destroyed
        if (with_points) {  // points alone decide nothing: the option is what counts
            gs.units[0].points = 90;
            gs.units[0].starting_wounds = 10;
            gs.units[1].points = 60;
            gs.units[1].starting_wounds = 10;
        }
        const std::vector<GameEvent> last = play_to_end(gs);
        ASSERT_TRUE(gs.result.has_value());
        EXPECT_FALSE(gs.result->winner.has_value());
        EXPECT_EQ(gs.result->reason,
                  "D-007: battle round 5 has ended with both sides on the battlefield; the game is a draw");
        EXPECT_FALSE(gs.result->points.has_value());
        EXPECT_EQ(event_rule(GameEvent{events_of<event::GameOver>(last).at(0)}), "D-007");
    }
}

// ==== Counting (D-076) ====

TEST(D076_PointsDestroyed, DestroyedUnitsCountTheirFullPointsAndDamagedOnesTheirFraction) {
    GameState gs = load_rosters_scenario();
    const PointsTally none = points_destroyed(gs);
    EXPECT_EQ(none.destroyed, (std::array<std::int64_t, 2>{0, 0}));
    EXPECT_EQ(none.display, (std::array<std::string, 2>{"0", "0"}));
    unit(gs, 4).models.clear();                    // Havocs destroyed: 135
    unit(gs, 3).models.pop_back();                 // a Terminator destroyed: 3 wounds
    unit(gs, 3).models[0].wounds_remaining = 1;    // and one wounded by 2: 5 of 30, 370 x 5 / 30 = 61.67
    unit(gs, 11).models[0].wounds_remaining = 11;  // the Tyrannofex 5 of 16: 170 x 5 / 16 = 53.125
    EXPECT_EQ(wounds_lost(unit(gs, 3)), 5);
    EXPECT_EQ(wounds_lost(unit(gs, 11)), 5);
    EXPECT_EQ(wounds_lost(unit(gs, 4)), 15);
    const PointsTally t = points_destroyed(gs);
    EXPECT_EQ(t.denominator, 240);
    EXPECT_EQ(t.destroyed[side_index(Side::B)], 135 * 240 + 370 * 8 * 5);  // 47200 / 240 = 196.666...
    EXPECT_EQ(t.destroyed[side_index(Side::A)], 170 * 15 * 5);             // 12750 / 240 = 53.125
    EXPECT_EQ(t.display[side_index(Side::B)], "196.67");
    EXPECT_EQ(t.display[side_index(Side::A)], "53.13");  // rounded half up
    // A destroyed unit counts its full points even when its starting wounds don't match its models any more.
    unit(gs, 4).starting_wounds = 15;
    EXPECT_EQ(points_destroyed(gs).destroyed[side_index(Side::B)], t.destroyed[side_index(Side::B)]);
}

TEST(D076_PointsDestroyed, TheDisplayNeverShowsUnequalTotalsAlike) {
    EXPECT_EQ(points_decimal(27240, 240, 2), "113.5");
    EXPECT_EQ(points_decimal(0, 240, 2), "0");
    EXPECT_EQ(points_decimal(2, 3, 2), "0.67");
    EXPECT_EQ(points_decimal(1, 3, 2), "0.33");
    EXPECT_EQ(points_decimal(1999, 2000, 2), "1");  // 0.9995 rounds up into the whole part
    EXPECT_EQ(points_decimal(1, 2, 0), "1");
    // 100/3 and 100.001/3: 33.33 at 2 places both, so more places.
    EXPECT_EQ(points_display({100000, 100001}, 3000), (std::array<std::string, 2>{"33.333", "33.334"}));
    EXPECT_EQ(points_display({100000, 100000}, 3000), (std::array<std::string, 2>{"33.33", "33.33"}));  // equal
    // The smallest step there can be, 1 / 2^59, still reads apart.
    const std::array<std::string, 2> tiny = points_display({1, 2}, kPointsLimit);
    EXPECT_NE(tiny[0], tiny[1]);
}

// ==== JSON (D-076) ====

TEST(D076_Json, TheStateRoundTripsWithThePointsTheRuleAndTheResult) {
    GameState gs = load_rosters_scenario();
    const nlohmann::json j = gs;
    EXPECT_EQ(j.at("result_rule"), "points_destroyed");
    EXPECT_EQ(j.at("units").at(2).at("points"), 370);
    EXPECT_EQ(j.at("units").at(2).at("starting_wounds"), 30);
    EXPECT_EQ(j.get<GameState>(), gs);
    // A finished game: the result's tally too, and its GameOver event.
    gs.battle_rounds = 1;
    unit(gs, 9).models.pop_back();
    const std::vector<GameEvent> last = play_to_end(gs);
    ASSERT_TRUE(gs.result && gs.result->points);
    EXPECT_EQ(nlohmann::json(gs).get<GameState>(), gs);
    EXPECT_TRUE(nlohmann::json(gs).at("result").contains("points"));
    const GameEvent over{events_of<event::GameOver>(last).at(0)};
    const nlohmann::json ej = over;
    EXPECT_EQ(ej.at("rule"), "D-076");
    EXPECT_EQ(ej.get<GameEvent>(), over);
}

TEST(D076_Json, AnOldFileLoadsAndSavesTheSame) {
    // No new key in a state without points: V1 as loaded and every saved replay's embedded state come back byte for byte.
    const GameState v1 = wh40k::test::load_v1_scenario();
    const nlohmann::json j = v1;
    EXPECT_FALSE(j.contains("result_rule"));
    for (const nlohmann::json& u : j.at("units")) {
        EXPECT_FALSE(u.contains("points"));
        EXPECT_FALSE(u.contains("starting_wounds"));
    }
    EXPECT_EQ(j.get<GameState>().result_rule, ResultRule::D007Draw);
    int embedded = 0;
    for (const auto& entry : std::filesystem::directory_iterator(repo_root() / "tests/replays")) {
        if (entry.path().extension() != ".json" || entry.path().filename().string().rfind("m67_", 0) == 0) continue;
        std::ifstream in(entry.path(), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const Replay replay = load_replay(entry.path());
        EXPECT_EQ(nlohmann::json(replay).dump(2) + "\n", text) << entry.path().filename();
        // M72: m72_custom_battle_round2.json is a new file: its embedded custom battle is decided on points (D-076).
        if (replay.initial.state && entry.path().filename().string().rfind("m72_", 0) != 0) {
            ++embedded;
            EXPECT_EQ(replay.initial.state->result_rule, ResultRule::D007Draw);
        }
    }
    EXPECT_GT(embedded, 0);
}

TEST(D076_Json, MalformedPointsInAStateAreRefused) {
    const nlohmann::json good = load_rosters_scenario();
    auto with = [&](auto change) {
        nlohmann::json j = good;
        change(j);
        return state_refusal(j);
    };
    EXPECT_EQ(with([](nlohmann::json& j) { j["result_rule"] = "points"; }),
              "state JSON: D-076: unknown result_rule \"points\" (the only value is \"points_destroyed\")");
    EXPECT_EQ(with([](nlohmann::json& j) { j["units"][0]["points"] = -5; }),
              "state JSON: D-076: unit 1 'Legionaries' has points -5; points must be a positive integer of at most 100000");
    EXPECT_EQ(with([](nlohmann::json& j) { j["units"][0]["points"] = 2.5; }),
              "state JSON: D-076: unit 1's points must be a whole number: 2.5");
    EXPECT_EQ(with([](nlohmann::json& j) { j["units"][0].erase("starting_wounds"); }),
              "state JSON: D-076: unit 1 'Legionaries' has points 180 and starting wounds 0; a unit has both or neither");
    EXPECT_EQ(with([](nlohmann::json& j) {
                  j["units"][0].erase("points");
                  j["units"][0].erase("starting_wounds");
              }),
              "state JSON: D-076: unit 1 'Legionaries' has no points; result \"points_destroyed\" needs the points of "
              "every unit");
    // Starting wounds whose lcm passes 2^59, and whose lcm times the points in play does.
    EXPECT_EQ(with([](nlohmann::json& j) {
                  j["units"][0]["starting_wounds"] = 1000000007;
                  j["units"][1]["starting_wounds"] = 1000000009;
              }),
              "state JSON: D-076: the units' starting wounds have a least common multiple over 2^59, too large to count "
              "points exactly");
    EXPECT_EQ(with([](nlohmann::json& j) {
                  j["units"][0]["starting_wounds"] = 3000017;
                  j["units"][1]["starting_wounds"] = 3000029;
              }),
              "state JSON: D-076: the least common multiple of the units' starting wounds (2160033120118320) times "
              "their total points (1915) is over 2^59, too large to count points exactly");
    // Without the option, the points still have to be well formed; dropping them all gives the plain state.
    EXPECT_EQ(with([](nlohmann::json& j) {
                  j.erase("result_rule");
                  j["units"][0]["points"] = 0;
              }),
              "state JSON: D-076: unit 1 'Legionaries' has points 0 and starting wounds 20; a unit has both or neither");
    nlohmann::json bad_tally = good;
    bad_tally["result"] = {{"reason", "x"}, {"winner", nullptr},
                           {"points", {{"destroyed", {1, 2}}, {"denominator", 0}, {"display", {"a", "b"}}, {"battle_round", 5}}}};
    EXPECT_EQ(state_refusal(bad_tally).rfind("state JSON: D-076: a points tally needs a positive denominator", 0), 0u);
}
