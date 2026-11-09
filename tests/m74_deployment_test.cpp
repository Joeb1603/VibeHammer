// M74 (D-081, amends D-003, D-007, D-065; 03.02, 03.03, 03.04, 01.03): the deployment step. Units waiting to be set up
// are in GameState::undeployed (not on the battlefield); the sides alternate setting one up (DeployUnits), the side
// taking the second turn first, then the rest for the side not yet done; a unit with no legal spot left counts as
// destroyed (CannotSetUp) and the opponent scores its points.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "core/data/army_list.hpp"
#include "core/data/datasheet.hpp"
#include "core/data/load_error.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/oval.hpp"
#include "core/geometry/pose.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/deployment.hpp"
#include "core/state/points_destroyed.hpp"
#include "core/state/queries.hpp"
#include "support/army_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::in;
using wh40k::test::rejection;
using wh40k::test::repo_root;
using wh40k::test::TestRng;

namespace {

// ---- fixtures ----

ArmyList stock(const Faction& f) { return wh40k::test::stock_army(f); }

// The stock armies' custom battle (D-079), set up by hand (D-081) unless `mode` says otherwise.
GameState stock_battle(Side first = Side::A, DeploymentMode mode = DeploymentMode::Hand) {
    return wh40k::test::custom_battle(stock(wh40k::test::csm()), stock(wh40k::test::tyranids()), first, mode);
}

GameState build(const nlohmann::json& scenario) {
    return build_game_state(scenario, [](const std::string& unit_file) { return load_datasheet(repo_root() / unit_file); });
}

std::string load_refusal(const nlohmann::json& scenario) {
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

nlohmann::json intercessors(int points = 0) {
    nlohmann::json u{{"unit_file", "data/units/intercessor_squad.json"},
                     {"models",
                      {{{"type", "Intercessor Sergeant"}, {"count", 1}, {"weapons", {"Bolt Rifle", "Bolt pistol", "Knives and Fists"}}},
                       {{"type", "Intercessor"}, {"count", 4}, {"weapons", {"Bolt Rifle", "Bolt pistol", "Knives and Fists"}}}}}};
    if (points > 0) u["points"] = points;
    return u;
}

nlohmann::json termagants(int points = 0) {
    nlohmann::json u{{"unit_file", "data/units/termagants.json"},
                     {"models", {{{"type", "Termagants"}, {"count", 10}, {"weapons", {"Fleshborer", "Chitinous claws and teeth"}}}}}};
    if (points > 0) u["points"] = points;
    return u;
}

// A scenario set up by hand: a `width_in` x `depth_in` battlefield, side A's zone along the south edge and side B's
// along the north.
nlohmann::json hand_scenario(float width_in, float depth_in, float zone_a_in, float zone_b_in, nlohmann::json units_a,
                             nlohmann::json units_b, const char* first = "A") {
    return nlohmann::json{
        {"name", "M74 fixture"},
        {"battlefield", {{"width_in", width_in}, {"depth_in", depth_in}}},
        {"battle_rounds", 5},
        {"first_turn", first},
        {"deployment", "alternating"},
        {"sides",
         {{"A", {{"name", "South"}, {"deployment_zone", {{"edge", "south"}, {"depth_in", zone_a_in}}}, {"units", units_a}}},
          {"B", {{"name", "North"}, {"deployment_zone", {{"edge", "north"}, {"depth_in", zone_b_in}}}, {"units", units_b}}}}}};
}

// D-081 / 03.04: zones 1" apart (24" x 25", 12" zones): Intercessors (A, unit 1) and Termagants (B, unit 2).
GameState zones_one_inch_apart() {
    return build(hand_scenario(24.0f, 25.0f, 12.0f, 12.0f, nlohmann::json::array({intercessors()}),
                               nlohmann::json::array({termagants()})));
}

// D-081: a 12" x 24" battlefield whose south zone (2.5" deep) holds two lines of 10 Termagants but not a third: side A
// has three Termagant units (1-3, 60 points each), side B one Intercessor Squad (4, 90 points) in a 3" north zone.
GameState narrow_zone() {
    nlohmann::json s = hand_scenario(12.0f, 24.0f, 2.5f, 3.0f, nlohmann::json::array({termagants(60), termagants(60), termagants(60)}),
                                     nlohmann::json::array({intercessors(90)}));
    s["result"] = "points_destroyed";
    return build(s);
}

// D-081 / D-007: touching zones (12" x 6", a 2" south zone and a 4" north one): once side B's Termagants (unit 2) stand
// on their zone's front, every spot in side A's zone is within 2" of them, so side A's Intercessors (unit 1) can't be
// set up at all.
GameState touching_zones() {
    return build(hand_scenario(12.0f, 6.0f, 2.0f, 4.0f, nlohmann::json::array({intercessors()}),
                               nlohmann::json::array({termagants()})));
}

// ---- play helpers ----

// Applies `action` for whoever must decide; the events.
std::vector<GameEvent> act(GameState& gs, const GameAction& action) { return apply_action(gs, decider(gs), action); }

// D-081: each DeployUnits answered with the first legal action (the lowest-id unit's proposal, or CannotSetUp) until
// the deployment is over; the events of each action.
std::vector<std::vector<GameEvent>> deploy_at_proposals(GameState& gs) {
    std::vector<std::vector<GameEvent>> out;
    for (int guard = 0; guard < 100 && gs.flow.stage == Stage::Deploy; ++guard) {
        out.push_back(act(gs, legal_actions(gs, decider(gs)).front()));
    }
    return out;
}

const Unit& unit_of(const GameState& gs, std::uint32_t id) {
    if (const Unit* u = gs.find_unit(UnitId{id})) return *u;
    return *find_undeployed(gs, UnitId{id});
}

// The proposal for `id`, which must exist.
action::DeployUnit proposal(const GameState& gs, std::uint32_t id) {
    const std::optional<action::DeployUnit> p = propose_set_up(gs, UnitId{id});
    EXPECT_TRUE(p.has_value()) << "unit " << id << " has no proposal";
    return p.value_or(action::DeployUnit{UnitId{id}, {}});
}

action::DeployUnit moved(action::DeployUnit a, float dx_mm, float dy_mm) {
    for (ModelPlacement& m : a.models) {
        m.position.x += dx_mm;
        m.position.y += dy_mm;
    }
    return a;
}

std::vector<std::string> reasons(const GameState& gs, Side side, const GameAction& a) {
    return validate_action(gs, side, a).reasons;
}

bool has_reason(const std::vector<std::string>& rs, const std::string& text) {
    return std::find(rs.begin(), rs.end(), text) != rs.end();
}

}  // namespace

// ==== D081_Loader: "deployment": "alternating" ====

TEST(D081_Loader, UnitsWaitToBeSetUpBuiltAsBefore) {
    const GameState hand = stock_battle(Side::A, DeploymentMode::Hand);
    const GameState placed = stock_battle(Side::A, DeploymentMode::Auto);
    EXPECT_EQ(hand.flow.stage, Stage::NotStarted);
    EXPECT_TRUE(hand.units.empty());
    ASSERT_EQ(hand.undeployed.size(), placed.units.size());
    for (std::size_t i = 0; i < hand.undeployed.size(); ++i) {
        // The same unit (ids, models, weapons, points, starting wounds and strength), only not placed.
        Unit u = hand.undeployed[i];
        const Unit& p = placed.units[i];
        ASSERT_EQ(u.models.size(), p.models.size());
        for (std::size_t m = 0; m < u.models.size(); ++m) {
            EXPECT_EQ(u.models[m].position, Position{});
            EXPECT_EQ(u.models[m].facing_deg, 0.0f);
            u.models[m].position = p.models[m].position;
            u.models[m].facing_deg = p.models[m].facing_deg;
        }
        EXPECT_EQ(u, p) << "unit " << p.id.v;
    }
    // Everything else is the auto-placed battle's.
    GameState same = hand;
    same.undeployed.clear();
    same.units = placed.units;
    EXPECT_EQ(same, placed);
}

TEST(D081_Loader, TheHandCustomBattleIsTheAutoOneWithoutFormations) {
    const auto& f = wh40k::test::factions();
    const auto& r = wh40k::test::army_resolver();
    const ArmyList a = stock(wh40k::test::csm());
    const ArmyList b = stock(wh40k::test::tyranids());
    nlohmann::json hand = custom_battle_scenario(a, b, Side::B, f, r, DeploymentMode::Hand);
    nlohmann::json autos = custom_battle_scenario(a, b, Side::B, f, r);
    EXPECT_EQ(autos, custom_battle_scenario(a, b, Side::B, f, r, DeploymentMode::Auto));
    EXPECT_FALSE(autos.contains("deployment"));
    EXPECT_EQ(hand.at("deployment"), "alternating");
    for (const char* side : {"A", "B"}) {
        for (nlohmann::json& u : autos["sides"][side]["units"]) u.erase("formation");
        for (const nlohmann::json& u : hand["sides"][side]["units"]) EXPECT_FALSE(u.contains("formation"));
    }
    hand.erase("deployment");
    hand.erase("notes");
    autos.erase("notes");
    EXPECT_EQ(hand, autos);
}

TEST(D081_Loader, AFormationIsRefusedWithHandDeployment) {
    nlohmann::json s = hand_scenario(24.0f, 25.0f, 12.0f, 12.0f, nlohmann::json::array({intercessors()}),
                                     nlohmann::json::array({termagants()}));
    s["sides"]["A"]["units"][0]["formation"] = {{"ranks", 1}, {"gap_in", 0}, {"centre_x_in", 12}, {"anchor", "zone_front"}};
    EXPECT_EQ(load_refusal(s), "scenario: side A unit 'Intercessor Squad': D-081: a unit set up by hand (deployment "
                               "\"alternating\") takes no formation");
}

TEST(D081_Loader, UnknownDeploymentAndEastWestZonesAreRefused) {
    nlohmann::json s = hand_scenario(24.0f, 25.0f, 12.0f, 12.0f, nlohmann::json::array({intercessors()}),
                                     nlohmann::json::array({termagants()}));
    nlohmann::json fixed = s;
    fixed["deployment"] = "fixed";
    EXPECT_EQ(load_refusal(fixed), "scenario: root: D-081: unknown deployment \"fixed\" (the only value is \"alternating\")");
    nlohmann::json east = s;
    east["sides"]["B"]["deployment_zone"]["edge"] = "east";
    EXPECT_EQ(load_refusal(east), "scenario: side B: D-081: deployment \"alternating\" needs a north or south deployment zone");
    EXPECT_EQ(load_refusal(s), "");
}

TEST(D081_Loader, AUnitThatCannotBeSetUpEvenAloneIsRefused) {
    // A 1" deep zone can't hold a Termagant (28.5 mm base) in any formation.
    const nlohmann::json s = hand_scenario(24.0f, 25.0f, 12.0f, 1.0f, nlohmann::json::array({intercessors()}),
                                           nlohmann::json::array({termagants()}));
    EXPECT_EQ(load_refusal(s), "scenario: side B unit 'Termagants': D-081: unit 2 cannot be set up in its deployment zone "
                               "even alone");
}

TEST(D081_Loader, PointsOfUnitsToSetUpAreChecked) {
    // D-076's check covers the units waiting to be set up (D-081).
    nlohmann::json s = hand_scenario(24.0f, 25.0f, 12.0f, 12.0f, nlohmann::json::array({intercessors(90)}),
                                     nlohmann::json::array({termagants(60)}));
    s["result"] = "points_destroyed";
    EXPECT_EQ(load_refusal(s), "");
    nlohmann::json missing = s;
    missing["sides"]["B"]["units"][0].erase("points");
    EXPECT_EQ(load_refusal(missing), "scenario: side B unit 'Termagants': D-076: missing field 'points' (result "
                                     "\"points_destroyed\" needs every unit's points)");
    GameState gs = build(s);
    gs.undeployed[1].points = -5;
    EXPECT_THROW(check_points_data(gs, "test"), DataError);
}

// ==== D081_Deploy: the sequence, the start of battle round 1, the proposals ====

TEST(D081_Deploy, TheSideTakingTheSecondTurnSetsUpFirst) {
    for (const Side first : {Side::A, Side::B}) {
        GameState gs = stock_battle(first);
        const std::vector<GameEvent> events = start_game(gs);
        EXPECT_EQ(gs.flow.stage, Stage::Deploy);
        EXPECT_EQ(gs.flow.battle_round, 0);
        ASSERT_EQ(events.size(), 1u);
        EXPECT_EQ(events.front(), GameEvent{(event::DeploymentStarted{opponent(first)})});
        EXPECT_EQ(event_rule(events.front()), "D-081");
        ASSERT_TRUE(gs.pending.has_value());
        EXPECT_EQ(*gs.pending, (PendingDecision{opponent(first), decision::DeployUnits{}}));
        EXPECT_EQ(gs.pending, compute_decision(gs));
        EXPECT_EQ(describe(gs, *gs.pending), std::string(opponent(first) == Side::A ? "Side A" : "Side B") +
                                                 ": set up one of your units (03.02)");
        // 01.03: while it is neither player's turn, the player who takes the first turn is the active player.
        EXPECT_EQ(active_side(gs), first);
    }
}

TEST(D081_Deploy, TheSidesAlternateThenTheSideNotYetDoneSetsUpTheRest) {
    // 5 Chaos Space Marine units (A) against 9 Tyranid units (B). Side A takes the first turn, so B sets up first.
    GameState gs = stock_battle(Side::A);
    start_game(gs);
    std::string order;
    for (int guard = 0; guard < 20 && gs.flow.stage == Stage::Deploy; ++guard) {
        order += decider(gs) == Side::A ? 'A' : 'B';
        EXPECT_EQ(active_side(gs), Side::A);  // 01.03, throughout
        EXPECT_EQ(side_to_deploy(gs), decider(gs));
        act(gs, legal_actions(gs, decider(gs)).front());
    }
    EXPECT_EQ(order, "BABABABABABBBB");
    // Side B taking the first turn: A sets up first, and B, with more units, sets up the rest.
    GameState other = stock_battle(Side::B);
    start_game(other);
    order.clear();
    for (int guard = 0; guard < 20 && other.flow.stage == Stage::Deploy; ++guard) {
        order += decider(other) == Side::A ? 'A' : 'B';
        act(other, legal_actions(other, decider(other)).front());
    }
    EXPECT_EQ(order, "ABABABABABBBBB");
}

TEST(D081_Deploy, BattleRoundOneStartsAfterTheLastUnitAsTheD003StartDoes) {
    for (const Side first : {Side::A, Side::B}) {
        GameState hand = stock_battle(first, DeploymentMode::Hand);
        start_game(hand);
        const std::vector<std::vector<GameEvent>> steps = deploy_at_proposals(hand);
        ASSERT_EQ(steps.size(), 14u);
        GameState placed = stock_battle(first, DeploymentMode::Auto);
        const std::vector<GameEvent> d003 = start_game(placed);
        // The same state: every unit where Auto-place puts it, battle round 1 at the same first decision.
        EXPECT_EQ(hand, placed);
        EXPECT_TRUE(hand.undeployed.empty());
        EXPECT_EQ(hand.flow.battle_round, 1);
        // The last set-up's events: the unit set up, then exactly start_game's events for the auto-placed battle.
        const std::vector<GameEvent>& last = steps.back();
        ASSERT_FALSE(last.empty());
        EXPECT_TRUE(std::holds_alternative<event::UnitSetUp>(last.front()));
        EXPECT_EQ(std::vector<GameEvent>(last.begin() + 1, last.end()), d003);
    }
}

TEST(D081_Deploy, ProposalsInIdOrderReproduceTheAutoLayoutPositions) {
    // The stock armies and random legal armies (D-079), both first turns: bots placing the lowest-id unit at its
    // proposal end exactly where Auto-place puts every unit, and the state is the auto-placed one.
    int battles = 0;
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        for (const Side first : {Side::A, Side::B}) {
            GameState hand = wh40k::test::random_armies_battle(seed, DeploymentMode::Hand, first);
            GameState placed = wh40k::test::random_armies_battle(seed, DeploymentMode::Auto, first);
            start_game(hand);
            (void)deploy_at_proposals(hand);
            start_game(placed);
            EXPECT_EQ(hand, placed) << "seed " << seed;
            ++battles;
        }
    }
    EXPECT_EQ(battles, 24);
}

TEST(D081_Deploy, UnitsWaitingToBeSetUpAreInvisibleToTheQueries) {
    GameState gs = stock_battle(Side::A);
    start_game(gs);
    // B's Termagants (unit 6) and A's Legionaries (unit 1) are set up; the rest wait.
    act(gs, legal_actions(gs, decider(gs)).front());
    act(gs, legal_actions(gs, decider(gs)).front());
    ASSERT_EQ(gs.units.size(), 2u);
    EXPECT_EQ(gs.units[0].id, UnitId{1});
    EXPECT_EQ(gs.units[1].id, UnitId{6});
    const Unit& waiting = *find_undeployed(gs, UnitId{2});
    EXPECT_EQ(gs.find_unit(UnitId{2}), nullptr);
    EXPECT_EQ(find_model(gs, waiting.models.front().id), nullptr);
    EXPECT_EQ(find_unit_of_model(gs, waiting.models.front().id), nullptr);
    EXPECT_EQ(enemy_models(gs, Side::A).size(), gs.units[1].models.size());
    EXPECT_EQ(posed_friendly_models(gs, Side::A).size(), gs.units[0].models.size());
    EXPECT_TRUE(engaged_enemy_units(gs, gs.units[0]).empty());
    const PointsTally tally = points_destroyed(gs);
    EXPECT_EQ(tally.destroyed[0], 0);
    EXPECT_EQ(tally.destroyed[1], 0);
}

TEST(D081_Deploy, ButtonsThenTemplatesForEachUnitWaiting) {
    GameState gs = stock_battle(Side::A);
    start_game(gs);
    EXPECT_TRUE(legal_actions(gs, Side::A).empty());  // B sets up first
    const std::vector<GameAction> legal = legal_actions(gs, Side::B);
    ASSERT_EQ(legal.size(), 18u);  // 9 Tyranid units: a proposal each, then a template each
    for (std::size_t i = 0; i < 9; ++i) {
        const auto* button = std::get_if<action::DeployUnit>(&legal[i]);
        const auto* tmpl = std::get_if<action::DeployUnit>(&legal[9 + i]);
        ASSERT_NE(button, nullptr);
        ASSERT_NE(tmpl, nullptr);
        EXPECT_EQ(button->unit, UnitId{static_cast<std::uint32_t>(6 + i)});
        EXPECT_EQ(tmpl->unit, button->unit);
        EXPECT_FALSE(is_template(legal[i]));
        EXPECT_TRUE(is_template(legal[9 + i]));
        wh40k::test::expect_valid(gs, Side::B, legal[i]);
        (void)rejection(gs, Side::B, legal[9 + i]);  // a template is never valid
        (void)rejection(gs, Side::A, legal[i]);      // not side A's decision
        EXPECT_EQ(button->models.size(), find_undeployed(gs, button->unit)->models.size());
    }
}

TEST(D081_Deploy, TheEngineScansForASpotWhenTheSlotIsTaken) {
    // Side B takes the first turn, so A sets up first. A sets its Legionaries (unit 1) up by hand where the Chaos Lord
    // (unit 2) would stand; the Lord's proposal then comes from the scan, and is legal.
    GameState gs = stock_battle(Side::B);
    const GameState placed = [] {
        GameState p = stock_battle(Side::B, DeploymentMode::Auto);
        start_game(p);
        return p;
    }();
    start_game(gs);
    const Unit& lord_slot = *placed.find_unit(UnitId{2});
    action::DeployUnit legionaries = proposal(gs, 1);
    const float dx = lord_slot.models.front().position.x - legionaries.models.front().position.x;
    legionaries = moved(legionaries, dx, 0.0f);
    wh40k::test::expect_valid(gs, Side::A, legionaries);
    act(gs, legionaries);
    act(gs, legal_actions(gs, decider(gs)).front());  // B
    ASSERT_EQ(decider(gs), Side::A);
    const action::DeployUnit lord = proposal(gs, 2);
    EXPECT_NE(lord.models.front().position, lord_slot.models.front().position);
    wh40k::test::expect_valid(gs, Side::A, lord);
    // At the front of the zone, as near the middle as it can be.
    EXPECT_FLOAT_EQ(lord.models.front().position.y, in(12.0f) - 20.0f);
}

TEST(D081_Deploy, CannotSetUpOnlyWhenNothingFits) {
    GameState gs = narrow_zone();
    start_game(gs);
    ASSERT_EQ(decider(gs), Side::B);
    act(gs, legal_actions(gs, Side::B).front());  // the Intercessors (unit 4)
    // A: the first line of Termagants at the zone's front, the second behind it; each could be set up.
    for (const std::uint32_t id : {1u, 2u}) {
        ASSERT_EQ(decider(gs), Side::A);
        EXPECT_EQ(rejection(gs, Side::A, action::CannotSetUp{UnitId{id}}),
                  std::vector<std::string>{"D-081: unit " + std::to_string(id) +
                                           " can be set up (the engine finds a legal spot), so it can't be counted as "
                                           "not set up"});
        const action::DeployUnit p = proposal(gs, id);
        act(gs, p);
    }
    EXPECT_FLOAT_EQ(gs.find_unit(UnitId{1})->models.front().position.y, in(2.5f) - 14.25f);
    EXPECT_FLOAT_EQ(gs.find_unit(UnitId{2})->models.front().position.y, in(2.5f) - in(1.25f) - 14.25f);
    // The third has no legal spot: no proposal, and CannotSetUp is offered (with its template).
    EXPECT_FALSE(propose_set_up(gs, UnitId{3}).has_value());
    const std::vector<GameAction> legal = legal_actions(gs, Side::A);
    ASSERT_EQ(legal.size(), 2u);
    EXPECT_EQ(legal[0], GameAction{(action::CannotSetUp{UnitId{3}})});
    EXPECT_EQ(legal[1], GameAction{(action::DeployUnit{UnitId{3}, {}})});
    wh40k::test::expect_valid(gs, Side::A, legal[0]);
    const std::vector<GameEvent> events = act(gs, legal[0]);
    ASSERT_GE(events.size(), 3u);
    EXPECT_EQ(events[0], GameEvent{(event::UnitNotSetUp{UnitId{3}, Side::A})});
    EXPECT_EQ(events[1], GameEvent{(event::UnitDestroyed{UnitId{3}})});
    EXPECT_EQ(events[2], GameEvent{(event::BattleRoundStarted{1})});
    // It counts as destroyed: in `units`, with no models, its points and starting strength as built.
    const Unit& gone = *gs.find_unit(UnitId{3});
    EXPECT_TRUE(gone.models.empty());
    EXPECT_EQ(gone.points, 60);
    EXPECT_EQ(gone.starting_strength, 10);
    EXPECT_TRUE(gs.undeployed.empty());
    EXPECT_FALSE(gs.result.has_value());  // A still has two units on the battlefield
}

TEST(D081_Deploy, AUnitNotSetUpScoresItsFullPointsForTheOpponent) {
    GameState gs = narrow_zone();
    start_game(gs);
    while (gs.flow.stage == Stage::Deploy) act(gs, legal_actions(gs, decider(gs)).front());
    const PointsTally tally = points_destroyed(gs);
    EXPECT_EQ(tally.destroyed[side_index(Side::A)], 0);
    EXPECT_EQ(tally.destroyed[side_index(Side::B)], 60 * tally.denominator);  // D-076: its full points
    EXPECT_EQ(tally.display[side_index(Side::B)], "60");
}

TEST(D081_Deploy, NoWipeOutDuringDeploymentD007FromBattleRoundOne) {
    GameState gs = touching_zones();
    const std::vector<GameEvent> start = start_game(gs);
    // At the start nobody has a model on the battlefield, and the game goes on (D-007 is not checked yet).
    EXPECT_TRUE(gs.units.empty());
    EXPECT_FALSE(gs.result.has_value());
    EXPECT_TRUE(events_of<event::GameOver>(start).empty());
    act(gs, legal_actions(gs, Side::B).front());  // B's Termagants, at their zone's front
    EXPECT_EQ(gs.flow.stage, Stage::Deploy);
    EXPECT_FALSE(gs.result.has_value());
    // Every spot in A's zone is within 2" of them: A's only answer is CannotSetUp (or a hand placement, refused).
    const std::vector<GameAction> legal = legal_actions(gs, Side::A);
    ASSERT_EQ(legal.size(), 2u);
    EXPECT_EQ(legal[0], GameAction{(action::CannotSetUp{UnitId{1}})});
    const std::vector<GameEvent> events = act(gs, legal[0]);
    // A has no model on the battlefield when battle round 1 starts: D-007, B wins at once.
    EXPECT_EQ(events_of<event::BattleRoundStarted>(events).size(), 1u);
    ASSERT_TRUE(gs.result.has_value());
    EXPECT_EQ(gs.result->winner, Side::B);
    EXPECT_EQ(gs.result->reason, "D-007: Side A has no models on the battlefield; Side B wins");
    EXPECT_EQ(gs.flow.stage, Stage::GameOver);
    EXPECT_EQ(gs.flow.battle_round, 1);
}

TEST(D081_Deploy, EveryProposalValidatesAfterRandomHandPlacements) {
    // Random hand placements (D-004 fills of the DeployUnit template, applied when legal), then at every DeployUnits
    // decision every listed set-up is legal: proposals from the slot and from the scan alike.
    int scanned = 0;
    int by_hand = 0;
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        GameState gs = wh40k::test::random_armies_battle(seed, DeploymentMode::Hand, seed % 2 == 0 ? Side::A : Side::B);
        GameState placed = wh40k::test::random_armies_battle(seed, DeploymentMode::Auto, seed % 2 == 0 ? Side::A : Side::B);
        start_game(gs);
        TestRng rng(seed);
        for (int guard = 0; guard < 40 && gs.flow.stage == Stage::Deploy; ++guard) {
            const Side side = decider(gs);
            const std::vector<GameAction> legal = legal_actions(gs, side);
            for (const GameAction& a : legal) {
                if (is_template(a)) continue;
                wh40k::test::expect_valid(gs, side, a);
                if (const auto* d = std::get_if<action::DeployUnit>(&a)) {
                    const Unit* slot = placed.find_unit(d->unit);
                    if (slot != nullptr && d->models.front().position != slot->models.front().position) ++scanned;
                }
            }
            std::optional<GameAction> pick;
            for (int attempt = 0; attempt < 6 && !pick; ++attempt) {
                const GameAction filled = wh40k::test::fill_template(gs, legal.back(), rng, false);
                if (validate_action(gs, side, filled).ok()) pick = filled;
            }
            if (pick) ++by_hand;
            act(gs, pick.value_or(legal.front()));
        }
        EXPECT_NE(gs.flow.stage, Stage::Deploy) << "seed " << seed;
    }
    EXPECT_GT(by_hand, 10);
    EXPECT_GT(scanned, 5);
}

// ==== 03.02 / 03.03 / 03.04 / D-009 / D-064: what a set-up needs ====

TEST(R03_02_SetUp, EveryModelOfTheUnitIsPlacedExactlyOnce) {
    GameState gs = stock_battle(Side::B);  // A sets up first
    start_game(gs);
    const action::DeployUnit p = proposal(gs, 1);  // 10 Legionaries
    action::DeployUnit missing = p;
    missing.models.pop_back();
    EXPECT_EQ(rejection(gs, Side::A, missing),
              std::vector<std::string>{"03.02: model " + std::to_string(p.models.back().model.v) +
                                       " of unit 1 is not placed (every model of the unit is set up)"});
    action::DeployUnit twice = p;
    twice.models.push_back(p.models.front());
    EXPECT_EQ(rejection(gs, Side::A, twice),
              std::vector<std::string>{"03.02: model " + std::to_string(p.models.front().model.v) +
                                       " is placed more than once"});
    action::DeployUnit foreign = p;
    foreign.models.front().model = ModelId{9999};
    const std::vector<std::string> r = rejection(gs, Side::A, foreign);
    EXPECT_TRUE(has_reason(r, "03.02: model 9999 is not a model of unit 1"));
    EXPECT_TRUE(has_reason(r, "03.02: model " + std::to_string(p.models.front().model.v) +
                                  " of unit 1 is not placed (every model of the unit is set up)"));
    // Any order is fine.
    action::DeployUnit reversed = p;
    std::reverse(reversed.models.begin(), reversed.models.end());
    wh40k::test::expect_valid(gs, Side::A, reversed);
}

TEST(R03_02_SetUp, OnTheBattlefieldAtARealPoint) {
    GameState gs = stock_battle(Side::B);
    start_game(gs);
    const action::DeployUnit p = proposal(gs, 2);  // the Chaos Lord, one model
    const std::string lord = "model " + std::to_string(p.models.front().model.v);
    EXPECT_EQ(rejection(gs, Side::A, moved(p, -in(30.0f), 0.0f)),
              std::vector<std::string>{"03.02: " + lord + " is not wholly on the battlefield"});
    action::DeployUnit nan = p;
    nan.models.front().position.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(rejection(gs, Side::A, nan),
              std::vector<std::string>{"03.02: " + lord + "'s position must be a finite point on the battlefield"});
    action::DeployUnit raised = p;
    raised.models.front().position.z = 1.0f;
    EXPECT_EQ(rejection(gs, Side::A, raised),
              std::vector<std::string>{"03.02: " + lord +
                                       " must be set up on the battlefield's surface (z 0: there is no terrain)"});
}

TEST(R03_02_SetUp, ARoundBaseHasNoFacing) {
    GameState gs = stock_battle(Side::B);
    start_game(gs);
    action::DeployUnit p = proposal(gs, 2);
    p.models.front().facing_deg = 15.0f;
    EXPECT_EQ(rejection(gs, Side::A, p),
              std::vector<std::string>{"D-074: model " + std::to_string(p.models.front().model.v) +
                                       " has a round base, which has no facing; its facing must be 0"});
    p.models.front().facing_deg = -0.0f;  // a zero is a zero
    wh40k::test::expect_valid(gs, Side::A, p);
    p.models.front().facing_deg = std::numeric_limits<float>::infinity();
    EXPECT_EQ(rejection(gs, Side::A, p),
              std::vector<std::string>{"D-074: model " + std::to_string(p.models.front().model.v) +
                                       "'s facing must be a finite number of degrees"});
}

TEST(R03_02_SetUp, OnlyAFriendlyUnitWaitingToBeSetUp) {
    GameState gs = stock_battle(Side::B);
    start_game(gs);
    const action::DeployUnit p = proposal(gs, 1);
    act(gs, p);  // A sets up unit 1; now B
    act(gs, legal_actions(gs, Side::B).front());
    EXPECT_EQ(rejection(gs, Side::A, p), std::vector<std::string>{"03.02: unit 1 has already been set up"});
    EXPECT_EQ(rejection(gs, Side::A, action::DeployUnit{UnitId{8}, {}}),
              std::vector<std::string>{"03.02: unit 8 is not a friendly unit"});
    EXPECT_EQ(rejection(gs, Side::A, action::CannotSetUp{UnitId{99}}),
              std::vector<std::string>{"03.02: unit 99 does not exist"});
    EXPECT_EQ(rejection(gs, Side::A, action::DeployUnit{UnitId{2}, {}}),
              std::vector<std::string>{"03.02: model " + std::to_string(unit_of(gs, 2).models.front().id.v) +
                                       " of unit 2 is not placed (every model of the unit is set up)"});
}

TEST(R03_03_Coherency, DeployedUnitMustBeCoherent) {
    GameState gs = stock_battle(Side::B);
    start_game(gs);
    action::DeployUnit p = proposal(gs, 3);  // 10 Chaos Terminators, two ranks of 5
    // The last model (the back rank's east end) 4" further back: more than 2" from every other model of the unit
    // (3" would leave it 1.8" from its neighbour, diagonally), and still within 9" of all of them.
    p.models.back().position.y -= in(4.0f);
    const std::string last = "model " + std::to_string(p.models.back().model.v);
    EXPECT_EQ(rejection(gs, Side::A, p),
              std::vector<std::string>{"03.03: " + last + " would be more than 2\" from every other model in unit 3"});
}

TEST(R03_04_Engagement, DeployedUnitMustBeUnengaged) {
    // Zones 1" apart: once B's Termagants stand on their zone's front, A's Intercessors can't stand on theirs.
    GameState gs = zones_one_inch_apart();
    start_game(gs);
    ASSERT_EQ(decider(gs), Side::B);
    act(gs, legal_actions(gs, Side::B).front());
    const Unit& termagants = *gs.find_unit(UnitId{2});
    EXPECT_FLOAT_EQ(termagants.models.front().position.y, in(13.0f) + 14.25f);  // touching their front line
    // At A's zone front (centred, as the auto-layout would put it) they would be 1" from the Termagants.
    action::DeployUnit front = proposal(gs, 1);
    for (ModelPlacement& m : front.models) m.position.y = in(12.0f) - 16.0f;
    const std::vector<std::string> r = rejection(gs, Side::A, front);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_TRUE(wh40k::test::starts_with(r.front(), "03.02: unit 1 would be set up engaged with unit 2 (03.04: model "))
        << r.front();
    // The proposal stands back on the scan's 1/4" steps until it is unengaged: at 3/4" back the closest Intercessor and
    // Termagant are within 2"; at 1" back the closest pair (their centres 7.25 mm apart across the battlefield, no
    // Termagant straight in front of an Intercessor) is 51.1 mm apart, just over 2" (50.8 mm + EPS).
    const action::DeployUnit p = proposal(gs, 1);
    for (const ModelPlacement& m : p.models) EXPECT_FLOAT_EQ(m.position.y, in(12.0f) - in(1.0f) - 16.0f);
    wh40k::test::expect_valid(gs, Side::A, p);
    const std::vector<std::string> closer = rejection(gs, Side::A, moved(p, 0.0f, in(0.25f)));
    ASSERT_EQ(closer.size(), 1u);
    EXPECT_TRUE(wh40k::test::starts_with(closer.front(), "03.02: unit 1 would be set up engaged with unit 2"));
}

TEST(D009_WhollyWithin, DeployedOvalAtAnAngle) {
    // The Tyrannofex (unit 11; a 92 x 120 mm oval) with its centre 50 mm behind B's zone front (y = 48"): at facing 0
    // its half-length (60 mm) reaches out of the zone, at 45 degrees too (53.5 mm), at 90 degrees (46 mm) it fits.
    GameState gs = stock_battle(Side::A);  // B sets up first
    start_game(gs);
    const ModelId model = find_undeployed(gs, UnitId{11})->models.front().id;
    const auto at = [&](float facing) {
        return action::DeployUnit{UnitId{11}, {ModelPlacement{model, Position{in(22.0f), in(48.0f) + 50.0f, 0.0f}, facing}}};
    };
    const std::string out = "D-009: model " + std::to_string(model.v) + " is not wholly within Side B's deployment zone";
    EXPECT_EQ(rejection(gs, Side::B, at(0.0f)), std::vector<std::string>{out});
    EXPECT_EQ(rejection(gs, Side::B, at(45.0f)), std::vector<std::string>{out});
    wh40k::test::expect_valid(gs, Side::B, at(90.0f));
    wh40k::test::expect_valid(gs, Side::B, at(270.0f));  // the same footprint (modulo 180, D-074)
    act(gs, at(270.0f));
    const Model& m = gs.find_unit(UnitId{11})->models.front();
    EXPECT_EQ(m.facing_deg, 90.0f);  // stored modulo 180
    EXPECT_EQ(m.position, (Position{in(22.0f), in(48.0f) + 50.0f, 0.0f}));
}

TEST(D064_SetUp, NoModelOnAnother) {
    GameState gs = stock_battle(Side::B);  // A sets up first
    start_game(gs);
    const action::DeployUnit legionaries = proposal(gs, 1);
    act(gs, legionaries);
    act(gs, legal_actions(gs, Side::B).front());
    const Model& front = gs.find_unit(UnitId{1})->models.front();
    const ModelId lord = unit_of(gs, 2).models.front().id;
    // The Chaos Lord (40 mm) on a Legionary (32 mm) is refused; in base contact beside it is fine.
    const action::DeployUnit on{UnitId{2}, {ModelPlacement{lord, front.position, 0.0f}}};
    const std::vector<std::string> r = rejection(gs, Side::A, on);
    EXPECT_TRUE(has_reason(r, "03.01: model " + std::to_string(lord.v) + " would be set up on model " +
                                  std::to_string(front.id.v) + " of unit 1 (D-064)"));
    // West of the front rank's first Legionary, touching the zone's front line (the Lord's radius is 20 mm, the
    // Legionary's 16 mm): centres 36 mm apart.
    const float dy = (in(12.0f) - 20.0f) - front.position.y;
    const action::DeployUnit touching{
        UnitId{2},
        {ModelPlacement{lord, Position{front.position.x - std::sqrt(36.0f * 36.0f - dy * dy), in(12.0f) - 20.0f, 0.0f},
                        0.0f}}};
    wh40k::test::expect_valid(gs, Side::A, touching);
    // Two models of one unit on each other.
    action::DeployUnit terminators = proposal(gs, 3);
    terminators.models[1].position = terminators.models[0].position;
    EXPECT_TRUE(has_reason(rejection(gs, Side::A, terminators),
                           "03.01: model " + std::to_string(terminators.models[0].model.v) + " would be set up on model " +
                               std::to_string(terminators.models[1].model.v) + " of its own unit (D-064)"));
}

// ==== D081_Json: the state and the files ====

TEST(D081_Json, AStateMidDeploymentRoundTrips) {
    GameState gs = stock_battle(Side::A);
    start_game(gs);
    for (int i = 0; i < 5; ++i) act(gs, legal_actions(gs, decider(gs)).front());
    ASSERT_EQ(gs.flow.stage, Stage::Deploy);
    const nlohmann::json j = gs;
    ASSERT_TRUE(j.contains("undeployed"));
    EXPECT_EQ(j.at("undeployed").size(), 9u);
    EXPECT_EQ(j.at("flow").at("stage"), "Deploy");
    EXPECT_EQ(j.at("pending").at("kind"), "DeployUnits");
    const GameState back = nlohmann::json::parse(j.dump()).get<GameState>();
    EXPECT_EQ(back, gs);
    EXPECT_EQ(nlohmann::json(back).dump(), j.dump());
    // A hand-deployment scenario as loaded (NotStarted) too.
    const GameState loaded = stock_battle(Side::B);
    EXPECT_EQ(nlohmann::json(loaded).get<GameState>(), loaded);
}

TEST(D081_Json, OlderFilesHaveNoNewKeyAndLoadTheSame) {
    const GameState v1 = wh40k::test::load_v1_scenario();
    nlohmann::json j = v1;
    EXPECT_FALSE(j.contains("undeployed"));
    j["undeployed"] = nlohmann::json::array();  // an empty list reads as none and is not written back
    const GameState back = j.get<GameState>();
    EXPECT_EQ(back, v1);
    EXPECT_FALSE(nlohmann::json(back).contains("undeployed"));
    // A DeployUnit's facing is written only when it isn't 0.
    nlohmann::json a;
    action::to_json(a, GameAction{action::DeployUnit{UnitId{1}, {ModelPlacement{ModelId{2}, Position{1.0f, 2.0f, 0.0f}, 0.0f}}}});
    EXPECT_FALSE(a.at("models").at(0).contains("facing_deg"));
}

TEST(D081_Json, UnitsWaitingOutsideTheDeploymentAreRefused) {
    GameState gs = stock_battle(Side::A);
    start_game(gs);
    act(gs, legal_actions(gs, decider(gs)).front());
    const nlohmann::json good = gs;
    ASSERT_EQ(state_refusal(good), "");
    nlohmann::json later = good;
    later["flow"]["stage"] = "MoveUnits";
    EXPECT_EQ(state_refusal(later), "state JSON: D-081: units waiting to be set up (\"undeployed\") are only possible "
                                    "before battle round 1 (stage NotStarted or Deploy), not in stage MoveUnits");
    nlohmann::json none = good;
    none.erase("undeployed");
    EXPECT_EQ(state_refusal(none), "state JSON: D-081: stage Deploy with no unit waiting to be set up (\"undeployed\" is "
                                   "empty)");
    nlohmann::json twice = good;
    twice["undeployed"][0]["id"] = twice["units"][0]["id"];
    EXPECT_EQ(state_refusal(twice), "state JSON: D-081: unit id " + twice["units"][0]["id"].dump() +
                                        " is used more than once");
    nlohmann::json model_twice = good;
    model_twice["undeployed"][0]["models"][0]["id"] = model_twice["units"][0]["models"][0]["id"];
    EXPECT_EQ(state_refusal(model_twice), "state JSON: D-081: model id " +
                                              model_twice["units"][0]["models"][0]["id"].dump() +
                                              " is used more than once");
    nlohmann::json order = good;
    std::swap(order["undeployed"][0], order["undeployed"][1]);
    EXPECT_EQ(state_refusal(order), "state JSON: D-081: \"undeployed\" must be in unit id order (unit " +
                                        order["undeployed"][1]["id"].dump() + " comes after unit " +
                                        order["undeployed"][0]["id"].dump() + ")");
    nlohmann::json empty = good;
    empty["undeployed"][0]["models"] = nlohmann::json::array();
    EXPECT_EQ(state_refusal(empty), "state JSON: D-081: unit " + empty["undeployed"][0]["id"].dump() +
                                        " is waiting to be set up but has no models");
}

// ==== Timing (run on purpose; M74 asked for legal_actions' cost during the deployment step) ====

namespace {

double legal_actions_ms(const GameState& gs, int runs, std::size_t* actions = nullptr) {
    const auto t0 = std::chrono::steady_clock::now();
    std::size_t n = 0;
    for (int i = 0; i < runs; ++i) n = legal_actions(gs, decider(gs)).size();
    const auto t1 = std::chrono::steady_clock::now();
    if (actions != nullptr) *actions = n;
    return std::chrono::duration<double, std::milli>(t1 - t0).count() / runs;
}

// Side `side`'s zone covered by a grid of large friendly bases (a test-only unit), so that no unit fits anywhere in
// it: the worst case, every proposal a full scan that finds nothing.
void cover_zone(GameState& gs, Side side) {
    Unit blocker;
    blocker.id = UnitId{1000};
    blocker.side = side;
    blocker.name = "Blocker";
    const Rect zone = zone_rect(gs.battlefield, gs.side(side).deployment_zone);
    const Base big{BaseShape::Round, 100.0f, 0.0f};
    std::uint32_t id = 10000;
    for (float y = zone.min_y; y <= zone.max_y + 1.0f; y += 85.0f) {
        for (float x = 0.0f; x <= gs.battlefield.width_mm + 1.0f; x += 85.0f) {
            Model m;
            m.id = ModelId{id++};
            m.base = big;
            m.position = Position{x, y, 0.0f};
            m.wounds_remaining = 1;
            blocker.models.push_back(m);
        }
    }
    gs.units.push_back(blocker);
}

}  // namespace

TEST(D081_Deploy, DISABLED_LegalActionsTiming) {
    // Typical: the stock custom battle at its first decision (9 Tyranid proposals from the auto-layout) and half way.
    GameState gs = stock_battle(Side::A);
    start_game(gs);
    std::size_t n = 0;
    const double first_ms = legal_actions_ms(gs, 50, &n);
    std::printf("typical, first decision (9 units waiting, slots legal): %.3f ms, %zu actions\n", first_ms, n);
    GameState half = gs;
    for (int i = 0; i < 7; ++i) act(half, legal_actions(half, decider(half)).front());
    std::printf("typical, half deployed (%zu waiting for %s): %.3f ms\n", half.undeployed.size(),
                decider(half) == Side::A ? "A" : "B", legal_actions_ms(half, 50));
    // A slot taken: B's first unit set up by hand on the next units' slots, so their proposals come from the scan.
    GameState scan = gs;
    {
        action::DeployUnit p = proposal(scan, 6);
        act(scan, moved(p, in(9.0f), 0.0f));  // the Termagants 9" east, over their neighbours' slots
        act(scan, legal_actions(scan, decider(scan)).front());  // A
    }
    int scanned = 0;
    const GameState placed = [] {
        GameState p = stock_battle(Side::A, DeploymentMode::Auto);
        start_game(p);
        return p;
    }();
    for (const SetUpProposal& p : deployment_proposals(scan, Side::B)) {
        if (p.action && p.action->models.front().position != placed.find_unit(p.unit)->models.front().position) ++scanned;
    }
    const double scan_ms = legal_actions_ms(scan, 20);
    std::printf("slots taken (%d of %zu proposals from the scan): %.3f ms\n", scanned,
                deployment_proposals(scan, Side::B).size(), scan_ms);
    // The worst: B's zone covered, so every one of B's 9 units scans its whole grid and finds nothing (CannotSetUp).
    GameState worst = gs;
    cover_zone(worst, Side::B);
    std::size_t m = 0;
    const double worst_ms = legal_actions_ms(worst, 2, &m);
    std::printf("worst, zone covered (9 full scans, all CannotSetUp): %.3f ms, %zu actions\n", worst_ms, m);
    GameState one = worst;
    std::printf("worst, one unit's CannotSetUp validated: %.3f ms\n", [&] {
        const auto t0 = std::chrono::steady_clock::now();
        const ValidationResult v = validate_action(one, Side::B, action::CannotSetUp{UnitId{6}});
        const auto t1 = std::chrono::steady_clock::now();
        EXPECT_TRUE(v.ok());
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
    }());
}
