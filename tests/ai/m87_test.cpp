// M87 (D-086): the bot plays for objectives. Under the D-084 mission the army plan sends sets of units to objective
// areas, a unit with an area and no group heads for it, and the move score adds the objective terms: the enemy scoring
// a move denies by being there at the end of our turn, and ours it holds through the enemy's turn, at the VP rate
// (vp_share). Without a mission, or with both objective weights 0, the bot plays as before M87.
// Hand-built positions on the objective scenario (the rosters' units; ids as in the rosters).

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "analysis/threat.hpp"
#include "arena.hpp"
#include "auto_actions.hpp"
#include "bot.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/actions.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/distance.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/objectives.hpp"
#include "core/state/queries.hpp"
#include "normal/army_plan.hpp"
#include "normal/charge.hpp"
#include "normal/move_score.hpp"
#include "normal/movement.hpp"
#include "normal/objectives.hpp"
#include "normal/value.hpp"
#include "session.hpp"
#include "simulator.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using namespace wh40k::ai::normal;
using wh40k::ai::analysis::OddsCache;
namespace mf = wh40k::test::monsters;
using wh40k::test::in;

namespace {

constexpr const char* kObjectiveScenario = "data/scenarios/incursion_objectives_44x60.json";

// The objective scenario's units (the rosters' ids): Side A, then Side B.
constexpr UnitId kChainswords{1};  // Legionaries, OC 2 x 10
constexpr UnitId kLord{2};
constexpr UnitId kTerminators{3};  // OC 1 x 10
constexpr UnitId kHavocs{4};       // OC 1 x 5
constexpr UnitId kBoltguns{5};     // Legionaries, OC 2 x 10
constexpr UnitId kTermagants{6};   // OC 2 x 10
constexpr UnitId kGenestealers{7};
constexpr UnitId kScreamer{8};     // OC 3
constexpr UnitId kGuard{9};
constexpr UnitId kTermagants2{14};

// The areas (D-084): the Centre (22", 30") 12"; A's home (22", 6") and B's home (22", 54") 6".
constexpr std::size_t kCentre = 0;
constexpr std::size_t kHomeA = 1;
constexpr std::size_t kHomeB = 2;

GameState objectives() {
    return load_scenario(wh40k::test::repo_root() / kObjectiveScenario, wh40k::test::repo_root());
}

GameState objectives_with(std::initializer_list<UnitId> ids, int first_scoring_round = 2) {
    const GameState all = objectives();
    GameState gs = all;
    gs.units.clear();
    for (const Unit& u : all.units) {
        if (std::find(ids.begin(), ids.end(), u.id) != ids.end()) gs.units.push_back(u);
    }
    gs.mission->first_scoring_round = first_scoring_round;
    return gs;
}

Unit& unit(GameState& gs, UnitId id) {
    Unit* u = gs.find_unit(id);
    if (u == nullptr) throw std::logic_error("no such unit");
    return *u;
}

// A unit of more than 6 models in two touching ranks centred on (x_in, y_in) (03.03: a single rank of 7 or more is out
// of coherency at its ends); 6 or fewer, one rank.
void place_block(Unit& u, float x_in, float y_in) {
    if (u.models.size() <= 6) {
        mf::place_rank(u, x_in, y_in);
        return;
    }
    const std::size_t front = (u.models.size() + 1) / 2;
    const float d = mm_to_inches(2.0f * base_radius(u.models.front().base));
    for (std::size_t i = 0; i < u.models.size(); ++i) {
        const std::size_t rank = i < front ? 0 : 1;
        const std::size_t count = rank == 0 ? front : u.models.size() - front;
        const std::size_t k = rank == 0 ? i : i - front;
        const float x = x_in + (static_cast<float>(k) - 0.5f * static_cast<float>(count - 1)) * d;
        u.models[i].position = Position{in(x), in(y_in + (rank == 0 ? 0.0f : -d)), 0.0f};
    }
}

GameState at_move_units(GameState gs, Side first = Side::A) {
    gs.first_turn = first;
    start_game(gs);
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind);
    });
    return gs;
}

// The state after `id`'s StartMove of `type` (dice 4).
GameState started(const GameState& gs, UnitId id, MoveType type = MoveType::Normal) {
    Simulator sim(gs, 0);
    ScriptedDice dice{4};
    EXPECT_TRUE(sim.step(gs.pending->side, action::StartMove{id, type, std::nullopt}, dice).ok());
    return sim.state();
}

MoveWeights zero_objectives() {
    MoveWeights w;
    w.objective = 0.0;
    w.objective_approach = 0.0;
    return w;
}

std::vector<Position> positions_of(const Unit& u) {
    std::vector<Position> out;
    for (const Model& m : u.models) out.push_back(m.position);
    return out;
}

// `u`'s models moved by (dx, dy) inches.
std::vector<Position> shifted(const Unit& u, float dx, float dy) {
    std::vector<Position> out = positions_of(u);
    for (Position& p : out) {
        p.x += in(dx);
        p.y += in(dy);
    }
    return out;
}

// The side to move's whole Movement phase, played by the bot (each StartMove's reason in `reasons`).
GameState play_movement(GameState gs, const MoveWeights& weights, std::vector<std::string>* reasons = nullptr) {
    BotConfig config;
    config.move = weights;
    Bot bot(config);
    const Side side = gs.pending.value().side;
    for (int i = 0; i < 400 && gs.pending && gs.pending->side == side &&
                    (std::holds_alternative<decision::MoveUnits>(gs.pending->kind) ||
                     std::holds_alternative<decision::MoveModels>(gs.pending->kind));
         ++i) {
        const BotChoice c = bot.decide(gs, side);
        if (reasons != nullptr && std::holds_alternative<action::StartMove>(c.action)) reasons->push_back(c.reason);
        EXPECT_TRUE(validate_action(gs, side, c.action).ok()) << c.reason;
        (void)apply_action(gs, side, c.action);
    }
    return gs;
}

}  // namespace

// ==== The VP rate ====

TEST(M87_Score, TheVpRateIsKillPointsPerVpOverTheMeanUnitPoints) {
    // 14 units, 960 + 955 points: a mean of 136.79; 50 points per kill VP.
    GameState gs = objectives();
    EXPECT_NEAR(vp_share(gs), 50.0 / (1915.0 / 14.0), 1e-9);
    gs.mission->kill_vp = 2;  // 25 points per VP
    EXPECT_NEAR(vp_share(gs), 25.0 / (1915.0 / 14.0), 1e-9);
    gs.mission->kill_vp = 0;  // no kill VP: still kill_points per VP
    EXPECT_NEAR(vp_share(gs), 50.0 / (1915.0 / 14.0), 1e-9);
    EXPECT_EQ(vp_share(wh40k::test::load_rosters_scenario()), 0.0);  // no mission
}

// ==== The outlook ====

TEST(M87_Outlook, WhoScoresNextFollowsTheTurnOrder) {
    GameState gs = objectives();
    gs.first_turn = Side::A;
    const auto at = [&](int round, int turn) {
        GameState s = gs;
        s.flow.battle_round = round;
        s.flow.turn_in_round = turn;
        return s;
    };
    // Round 1, A first: B's round-1 Command phase scores nothing (round 2 is the first), A's round-2 one does.
    {
        const GameState s = at(1, 0);
        const NextScorings n = next_scorings(s, Side::A, turns_left(s));
        EXPECT_FALSE(n.theirs);
        EXPECT_TRUE(n.ours);
        EXPECT_FALSE(next_scorings(s, Side::B, turns_left(s)).ours) << "not B's turn";
    }
    // Round 1, B second: A's round-2 Command phase and B's round-2 one both score.
    {
        const GameState s = at(1, 1);
        const NextScorings n = next_scorings(s, Side::B, turns_left(s));
        EXPECT_TRUE(n.theirs);
        EXPECT_TRUE(n.ours);
    }
    // Round 5 (the last), A first: B scores once more, A never again; B second: nobody.
    {
        const GameState s = at(5, 0);
        const NextScorings n = next_scorings(s, Side::A, turns_left(s));
        EXPECT_TRUE(n.theirs);
        EXPECT_FALSE(n.ours);
        const GameState t = at(5, 1);
        const NextScorings m = next_scorings(t, Side::B, turns_left(t));
        EXPECT_FALSE(m.theirs);
        EXPECT_FALSE(m.ours);
    }
    // Without a mission nothing scores.
    GameState r = wh40k::test::load_rosters_scenario();
    r.flow.battle_round = 3;
    EXPECT_FALSE(next_scorings(r, Side::A, 2).ours);
}

TEST(M87_Outlook, TheEnemyLevelCountsUnitsInRangeWithinAMoveAndByAdvance) {
    // Termagants: Move 6".
    const GameState gs = objectives();
    const analysis::ThreatRanges th = analysis::threat_ranges(*gs.find_unit(kTermagants));
    EXPECT_EQ(p_reaches_area(th, 0.0f), 1.0);
    EXPECT_EQ(p_reaches_area(th, in(5.9f)), 1.0);
    // 9": an advance of 3 or more (4 in 6), or a charge of 3" or more after a Normal move (35 in 36): the likelier.
    EXPECT_NEAR(p_reaches_area(th, in(9.0f)), 35.0 / 36.0, 1e-9);
    // 20": out of reach.
    EXPECT_EQ(p_reaches_area(th, in(20.0f)), 0.0);
    // The distribution: OC 20 at 1/2 and OC 3 for certain.
    LevelPmf pmf;
    pmf.add(20, 0.5);
    pmf.add(3, 1.0);
    EXPECT_DOUBLE_EQ(pmf.p_below(0.0), 0.0);  // a level of 0 controls nothing
    EXPECT_DOUBLE_EQ(pmf.p_below(3.0), 0.0);  // equal levels control nothing (14.02)
    EXPECT_DOUBLE_EQ(pmf.p_below(4.0), 0.5);
    EXPECT_DOUBLE_EQ(pmf.p_below(24.0), 1.0);
    // One place: a unit within range of an area stays there; otherwise its chances sum to at most 1.
    std::vector<double> p{1.0, 1.0, 0.5};
    one_place(p, {false, true, false});
    EXPECT_EQ(p, (std::vector<double>{0.0, 1.0, 0.0}));
    std::vector<double> q{1.0, 0.5, 0.5};
    one_place(q, {false, false, false});
    EXPECT_EQ(q, (std::vector<double>{0.5, 0.25, 0.25}));
}

TEST(M87_Outlook, ABattleShockedUnitAddsNothing) {
    GameState gs = objectives_with({kBoltguns, kLord, kTermagants, kGuard});
    mf::place_rank(unit(gs, kTermagants), 22.0f, 30.0f);  // in the Centre
    const ObjectiveArea& centre = gs.mission->objectives[kCentre];
    EXPECT_EQ(oc_within(*gs.find_unit(kTermagants), centre), 20);
    unit(gs, kTermagants).battle_shocked = true;  // 01.07: OC '-'
    EXPECT_EQ(oc_within(*gs.find_unit(kTermagants), centre), 0);
    EXPECT_EQ(unit_oc(*gs.find_unit(kTermagants)), 0);
}

// ==== The objective terms ====

TEST(M87_MoveScore, StandingOnAnAreaTheEnemyHoldsDeniesIt) {
    // The first scoring round is 1, so B's round-1 Command phase (after A's turn) scores: Termagants (OC 20) hold the
    // Centre; the boltgun Legionaries (OC 20) stand 3" south of it. Ending with every model in it denies B's 5 VP;
    // a few models (less OC than the Termagants') deny nothing, and neither does staying outside.
    GameState gs = objectives_with({kBoltguns, kLord, kTermagants, kGuard}, 1);
    mf::place_rank(unit(gs, kTermagants), 22.0f, 33.0f);
    mf::place_rank(unit(gs, kBoltguns), 22.0f, 21.0f);
    mf::place_rank(unit(gs, kLord), 40.0f, 4.0f);
    mf::place_rank(unit(gs, kGuard), 40.0f, 58.0f);
    gs = at_move_units(gs);
    OddsCache cache;
    const ArmyPlan plan = no_plan(gs, Side::A);  // no promises: the others are where they stand
    const Unit& us = *gs.find_unit(kBoltguns);
    const std::optional<ObjectiveOutlook> o = objective_outlook(gs, us, plan, turns_left(gs), 1.0);
    ASSERT_TRUE(o);
    EXPECT_TRUE(o->next.theirs);
    const AreaOutlook& c = o->areas[kCentre];
    EXPECT_EQ(c.them_now, 20);
    EXPECT_EQ(c.others, 0);
    EXPECT_NEAR(c.value_them, 5.0 * vp_share(gs), 1e-12);
    const ObjectiveTerms stay = objective_terms(*o, gs, us, positions_of(us), 1.0);
    EXPECT_EQ(stay.deny, 0.0);
    const ObjectiveTerms all_in = objective_terms(*o, gs, us, shifted(us, 0.0f, 6.0f), 1.0);
    EXPECT_NEAR(all_in.deny, c.value_them, 1e-12);  // 20 against 20: nobody controls it
    std::vector<Position> some = positions_of(us);
    for (std::size_t i = 0; i < 4; ++i) some[i].y += in(6.0f);  // 4 models: OC 8 < 20
    EXPECT_EQ(objective_terms(*o, gs, us, some, 1.0).deny, 0.0);
}

TEST(M87_MoveScore, TheScoreTermFallsWithTheEnemysReach) {
    // The Terminators (OC 10) on A's home; A scores it in round 2 if it holds through B's turn. Nothing in reach: the
    // whole 2 VP; a Screamer-killer (OC 3) within its Move: still certain; Termagants (OC 20) within their Move: less than
    // half (they could also make the Centre: one_place splits their chances); Termagants within range of it now
    // (they stay): nothing.
    double two_vp = 0.0;
    const auto score_with = [&two_vp](std::optional<UnitId> enemy, float enemy_y = 12.5f) {
        GameState gs = objectives_with({kTerminators, kLord, kGuard, kScreamer, kTermagants});
        mf::place_rank(unit(gs, kTerminators), 22.0f, 6.0f);
        mf::place_rank(unit(gs, kLord), 40.0f, 4.0f);
        mf::place_rank(unit(gs, kGuard), 40.0f, 58.0f);
        mf::place_rank(unit(gs, kScreamer), 4.0f, 58.0f);
        mf::place_rank(unit(gs, kTermagants), 30.0f, 58.0f);
        if (enemy) mf::place_rank(unit(gs, *enemy), 22.0f, enemy_y);
        gs = at_move_units(gs);
        two_vp = 2.0 * vp_share(gs);
        const ArmyPlan plan = no_plan(gs, Side::A);
        const Unit& us = *gs.find_unit(kTerminators);
        const std::optional<ObjectiveOutlook> o = objective_outlook(gs, us, plan, turns_left(gs), 1.0);
        EXPECT_TRUE(o && o->next.ours);
        return o ? objective_terms(*o, gs, us, positions_of(us), 1.0).score : -1.0;
    };
    const double none = score_with(std::nullopt);
    EXPECT_NEAR(none, two_vp, 1e-12);
    const double screamer = score_with(kScreamer);
    EXPECT_NEAR(screamer, two_vp, 1e-12);
    const double termagants = score_with(kTermagants);
    EXPECT_GT(termagants, 0.0);
    EXPECT_LT(termagants, 0.5 * two_vp);
    EXPECT_NEAR(score_with(kTermagants, 8.6f), 0.0, 1e-12);  // their front rank's bases within the home's 3"
}

TEST(M87_MoveScore, WithoutAMissionOrWithZeroWeightsNothingChanges) {
    // The same armies at the start: the rosters (no mission) and the objective scenario with both objective weights 0
    // plan the same moves with the same scores; the outlook and the area spots are off.
    const GameState obj = at_move_units(objectives());
    const GameState ros = at_move_units(wh40k::test::load_rosters_scenario());
    OddsCache cache;
    for (const UnitId id : {kTerminators, kHavocs, kChainswords}) {
        const GameState a = started(obj, id);
        const GameState b = started(ros, id);
        const MoveScoreContext off(cache, a, id, zero_objectives());
        EXPECT_FALSE(off.objectives());
        EXPECT_FALSE(off.area_spots());
        const MoveScoreContext none(cache, b, id);
        EXPECT_FALSE(none.objectives());
        EXPECT_FALSE(none.area_spots());
        const std::optional<PlannedMove> x = plan_unit_move(cache, a, nullptr, zero_objectives());
        const std::optional<PlannedMove> y = plan_unit_move(cache, b, nullptr, MoveWeights{});
        ASSERT_TRUE(x && y);
        EXPECT_EQ(x->candidate.ends, y->candidate.ends);
        EXPECT_EQ(x->candidate.label, y->candidate.label);
        EXPECT_DOUBLE_EQ(x->score.total, y->score.total);
        EXPECT_EQ(x->score.objective, 0.0);
        // With the default weights the objective scenario's context has both.
        const MoveScoreContext on(cache, a, id);
        EXPECT_TRUE(on.objectives());
        EXPECT_TRUE(on.area_spots());
    }
}

TEST(M87_MoveScore, TheShareLeftChangesNoValue) {
    // hold_line and engaged_line give the same value with the share-left read-out as without it, and the share is
    // between nothing and what is left of the unit now.
    const GameState gs = started(at_move_units(objectives()), kBoltguns);
    OddsCache cache;
    MoveScoreContext ctx(cache, gs, kBoltguns);
    const ScoreLine line = ctx.new_line();
    const Position c{in(22.0f), in(10.0f), 0.0f};
    for (const float gap : {in(4.0f), in(12.0f), in(24.0f)}) {
        double left = -1.0;
        const double with = ctx.hold_line(line, gap, c, nullptr, nullptr, nullptr, nullptr, false, &left);
        const double without = ctx.hold_line(line, gap, c);
        EXPECT_DOUBLE_EQ(with, without);
        EXPECT_GE(left, 0.0);
        EXPECT_LE(left, line.us() + 1e-12);
    }
    double left = -1.0;
    EXPECT_DOUBLE_EQ(ctx.engaged_line(line, 1.0, nullptr, nullptr, &left), ctx.engaged_line(line));
    EXPECT_GE(left, 0.0);
    EXPECT_LE(left, line.us() + 1e-12);
}

// ==== The plan's area sets ====

TEST(M87_Plan, WithoutAMissionThereAreNoAreasAndTheGroupsAreUnchanged) {
    const GameState obj = at_move_units(objectives());
    const GameState ros = at_move_units(wh40k::test::load_rosters_scenario());
    OddsCache cache;
    const ArmyPlan with = army_plan(cache, obj, Side::A);
    const ArmyPlan without = army_plan(cache, ros, Side::A);
    EXPECT_TRUE(without.areas.empty());
    EXPECT_FALSE(with.areas.empty()) << describe(obj, with);
    EXPECT_EQ(with.groups, without.groups);
    ASSERT_EQ(with.units.size(), without.units.size());
    for (std::size_t i = 0; i < with.units.size(); ++i) {
        EXPECT_EQ(with.units[i].focus, without.units[i].focus);
        EXPECT_EQ(with.units[i].partners, without.units[i].partners);
        EXPECT_EQ(with.units[i].group, without.units[i].group);
        EXPECT_FALSE(without.units[i].area);
    }
    EXPECT_EQ(describe_groups(obj, with), describe(ros, without));
}

TEST(M87_Plan, TheCheapestUnitThatHoldsAnAreaIsChosenAndTheBestAreaGoesFirst) {
    // A to move, the enemy far north: the boltgun Legionaries (OC 20, 180 points) and the Havocs (OC 5, 135 points),
    // both within a Normal move of A's home and the Legionaries able to reach the Centre. Nothing can contest either
    // area soon, so one unit holds each: the Centre (5 VP a scoring) takes the Legionaries first, and the Havocs take the
    // home. With the Centre worth nothing to A the home's set is the Havocs alone: of the units that hold it for certain,
    // the one with the least at stake (value per risk).
    const auto plan_of = [](int centre_vp_a) {
        GameState gs = objectives_with({kBoltguns, kHavocs, kGuard, kGenestealers});
        gs.mission->objectives[kCentre].vp[0] = centre_vp_a;
        mf::place_rank(unit(gs, kBoltguns), 14.0f, 14.0f);
        mf::place_rank(unit(gs, kHavocs), 30.0f, 10.0f);
        mf::place_rank(unit(gs, kGuard), 40.0f, 58.0f);
        mf::place_rank(unit(gs, kGenestealers), 4.0f, 58.0f);
        gs = at_move_units(gs);
        OddsCache cache;
        return std::pair{gs, army_plan(cache, gs, Side::A)};
    };
    const auto set_on = [](const ArmyPlan& plan, std::size_t area) -> const AreaSet* {
        for (const AreaSet& s : plan.areas) {
            if (s.area == area) return &s;
        }
        return nullptr;
    };
    {
        const auto [gs, plan] = plan_of(5);
        ASSERT_TRUE(plan.groups.empty()) << describe(gs, plan);
        const AreaSet* centre = set_on(plan, kCentre);
        const AreaSet* home = set_on(plan, kHomeA);
        ASSERT_TRUE(centre != nullptr && home != nullptr) << describe(gs, plan);
        EXPECT_EQ(&plan.areas.front(), centre) << describe(gs, plan);
        ASSERT_EQ(centre->members.size(), 1u);
        EXPECT_EQ(centre->members.front().unit, kBoltguns);
        ASSERT_EQ(home->members.size(), 1u);
        EXPECT_EQ(home->members.front().unit, kHavocs);
        EXPECT_EQ(plan.area_of(kBoltguns), centre);
        EXPECT_EQ(plan.area_of(kHavocs), home);
        EXPECT_NE(describe(gs, plan).find("Havocs 1 to take A home"), std::string::npos) << describe(gs, plan);
    }
    {
        const auto [gs, plan] = plan_of(0);
        EXPECT_EQ(set_on(plan, kCentre), nullptr) << describe(gs, plan);
        const AreaSet* home = set_on(plan, kHomeA);
        ASSERT_NE(home, nullptr) << describe(gs, plan);
        ASSERT_EQ(home->members.size(), 1u) << describe(gs, plan);
        EXPECT_EQ(home->members.front().unit, kHavocs) << describe(gs, plan);
        EXPECT_EQ(plan.area_of(kBoltguns), nullptr) << describe(gs, plan);
    }
}

TEST(M87_Plan, AStronglyHeldAreaGetsASetThatOutnumbersIt) {
    // Termagants (OC 20) hold the Centre; both Legionaries units (OC 20 each) and the Chaos Lord (OC 1) are within a
    // Normal move of it. One Legionaries unit only ties (no control); the set has enough OC to beat 20 for certain.
    GameState gs = objectives_with({kChainswords, kBoltguns, kLord, kTermagants, kGuard});
    mf::place_rank(unit(gs, kTermagants), 22.0f, 32.0f);
    place_block(unit(gs, kChainswords), 14.0f, 21.0f);
    place_block(unit(gs, kBoltguns), 30.0f, 21.0f);
    mf::place_rank(unit(gs, kLord), 22.0f, 20.0f);
    mf::place_rank(unit(gs, kGuard), 40.0f, 58.0f);
    gs = at_move_units(gs);
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::A);
    const AreaSet* centre = nullptr;
    for (const AreaSet& s : plan.areas) {
        if (s.area == kCentre) centre = &s;
    }
    ASSERT_NE(centre, nullptr) << describe(gs, plan);
    int oc = 0;
    for (const AreaMember& m : centre->members) {
        EXPECT_EQ(m.arrival, 0) << describe(gs, plan);
        oc += m.oc;
    }
    EXPECT_GT(oc, 20) << describe(gs, plan);
    EXPECT_GE(centre->members.size(), 2u) << describe(gs, plan);
}

TEST(M87_Plan, TheSameStateGivesTheSamePlan) {
    // Repeated calls, a copy and a fresh cache; and from the middle of a move (one model moved) the plan of its start.
    const GameState gs = at_move_units(objectives());
    OddsCache c1;
    OddsCache c2;
    const ArmyPlan p1 = army_plan(c1, gs, Side::A);
    const GameState copy = gs;
    EXPECT_EQ(army_plan(c2, copy, Side::A), p1);
    EXPECT_EQ(army_plan(c1, gs, Side::A), p1);
    // Mid-move: the moving unit taken at its move's start, so the plan is the one at Move Units.
    GameState s = started(gs, kTerminators);
    const OddsCache fresh;
    OddsCache c3;
    const ArmyPlan at_start = army_plan(c3, s, Side::A);
    const Unit& t = *s.find_unit(kTerminators);
    const ModelId first = t.models.front().id;
    Position to = t.models.front().position;
    to.y += in(2.0f);
    ASSERT_TRUE(validate_action(s, Side::A, action::ProposeModelMove{first, {to}}).ok());
    (void)apply_action(s, Side::A, action::ProposeModelMove{first, {to}});
    EXPECT_EQ(army_plan(c3, s, Side::A), at_start);
    (void)fresh;
}

// ==== Candidates ====

TEST(M87_Candidates, ObjectiveSpotsOnlyUnderTheMission) {
    // The Havocs start 3" from A's home: with the area spots there are candidates straight at its centre, and the
    // candidates without them are the same list as before (appended, not interleaved).
    const GameState gs = started(at_move_units(objectives()), kHavocs);
    const std::vector<MoveCandidate> before = move_candidates(gs);
    const std::vector<MoveCandidate> with = move_candidates(gs, nullptr, true);
    ASSERT_GT(with.size(), before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(with[i].ends, before[i].ends);
        EXPECT_EQ(with[i].label, before[i].label);
    }
    // Without a mission the flag adds nothing.
    const GameState ros = started(at_move_units(wh40k::test::load_rosters_scenario()), kHavocs);
    EXPECT_EQ(move_candidates(ros, nullptr, true).size(), move_candidates(ros).size());
}

// ==== The arena ====

TEST(M87_Arena, TheLargestClusterCountsConnectedEngagements) {
    // A Termagants unit engaged with both Legionaries units: one group of three; the rest unengaged.
    GameState gs = objectives_with({kChainswords, kBoltguns, kLord, kTermagants, kGuard});
    mf::place_rank(unit(gs, kTermagants), 22.0f, 30.0f);
    const float r = mm_to_inches(base_radius(gs.find_unit(kTermagants)->models.front().base)) +
                    mm_to_inches(base_radius(gs.find_unit(kBoltguns)->models.front().base));
    mf::place_rank(unit(gs, kChainswords), 22.0f, 30.0f - r - 1.0f);
    mf::place_rank(unit(gs, kBoltguns), 22.0f, 30.0f + r + 1.0f);
    mf::place_rank(unit(gs, kLord), 40.0f, 4.0f);
    mf::place_rank(unit(gs, kGuard), 40.0f, 58.0f);
    const EngagementSnapshot snap = engagement_snapshot(gs);
    EXPECT_EQ(snap.largest_cluster, 3);
    EXPECT_EQ(snap.on_battlefield, 5);
}

TEST(M87_Arena, TheSummaryHasVpByAreaAndTheLargestMelee) {
    const GameState scenario = objectives();
    const ArenaResult r = run_arena(scenario, 1, 1, BotConfig{}, BotConfig{});
    ASSERT_EQ(r.games.size(), 1u);
    EXPECT_TRUE(r.clean());
    const std::string text = format_arena(r, scenario);
    EXPECT_NE(text.find("mean objective VP by area (M87): Centre A "), std::string::npos) << text;
    EXPECT_NE(text.find("; A home A "), std::string::npos) << text;
    EXPECT_NE(text.find("largest melee (M87), per game: "), std::string::npos) << text;
    // The VP by area add up to the objective VP.
    int total = 0;
    for (const auto& a : r.teamwork.area_vp) total += a[0] + a[1];
    EXPECT_EQ(total, r.games[0].objective_vp[0] + r.games[0].objective_vp[1]);
    // Without a mission there is no area line.
    const GameState rosters = wh40k::test::load_rosters_scenario();
    EXPECT_EQ(format_arena(run_arena(rosters, 0, 1, BotConfig{}, BotConfig{}), rosters).find("by area"),
              std::string::npos);
}

// ==== Play before M87 ====

namespace {

// The field after `key` in a --per-game line ("actions 0123..." -> "0123...").
std::string per_game_field(const std::string& line, const std::string& key) {
    const std::size_t at = line.find(key + " ");
    if (at == std::string::npos) return "";
    const std::size_t start = at + key.size() + 1;
    return line.substr(start, line.find_first_of(" \n", start) - start);
}

// M86's Release arena, seed 1 (`--per-game 1`): the rosters, and the objective scenario (the same actions and trace).
constexpr const char* kM86Actions = "96386b34f58c8447";
constexpr const char* kM86Trace = "e034d41cdcc42bd7";
constexpr const char* kM86RostersState = "154669e688551ac1";
constexpr const char* kM86ObjectivesState = "3301929fd89fbaf9";

}  // namespace

TEST(M87_Regression, TheRostersAndTheObjectiveScenarioWithoutObjectiveWeightsPlayAsAtM86) {
    // Without a mission nothing M87 adds is on; with both objective weights 0 a mission game plays as at M86 too.
    const PerGameReport rosters = run_per_game(wh40k::test::load_rosters_scenario(), 1, 1, BotConfig{}, BotConfig{});
    EXPECT_TRUE(rosters.clean);
    EXPECT_EQ(per_game_field(rosters.text, "actions"), kM86Actions);
    EXPECT_EQ(per_game_field(rosters.text, "state"), kM86RostersState);
    EXPECT_EQ(per_game_field(rosters.text, "trace"), kM86Trace);
    BotConfig m86;
    m86.move = zero_objectives();
    const PerGameReport mission = run_per_game(objectives(), 1, 1, m86, m86);
    EXPECT_TRUE(mission.clean);
    EXPECT_EQ(per_game_field(mission.text, "actions"), kM86Actions);
    EXPECT_EQ(per_game_field(mission.text, "state"), kM86ObjectivesState);
    EXPECT_EQ(per_game_field(mission.text, "trace"), kM86Trace);
}

TEST(M87_Regression, TheDefaultWeightsPlayTheObjectiveScenarioDifferently) {
    const PerGameReport mission = run_per_game(objectives(), 1, 1, BotConfig{}, BotConfig{});
    EXPECT_TRUE(mission.clean);
    EXPECT_NE(per_game_field(mission.text, "actions"), kM86Actions);
}

// ==== The bot ====

TEST(M87_Normal, AUnitWithAnAreaApproachesItAndTheReasonNamesIt) {
    // The chainsword Legionaries in A's south-west corner, the Chaos Lord on A's home, the enemy far north: the plan
    // sends the Legionaries to an area beyond their Move; they head for it (the approach term), and the reason names it.
    GameState gs = objectives_with({kChainswords, kLord, kGuard, kGenestealers});
    place_block(unit(gs, kChainswords), 8.0f, 5.5f);
    mf::place_rank(unit(gs, kLord), 22.0f, 6.0f);
    mf::place_rank(unit(gs, kGuard), 40.0f, 58.0f);
    mf::place_rank(unit(gs, kGenestealers), 4.0f, 58.0f);
    gs = at_move_units(gs);
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::A);
    const AreaSet* set = plan.area_of(kChainswords);
    ASSERT_NE(set, nullptr) << describe(gs, plan);
    ASSERT_GE(set->member(kChainswords)->arrival, 1) << describe(gs, plan);
    const ObjectiveArea& area = gs.mission->objectives[set->area];
    const GameState s = started(gs, kChainswords);
    const MoveScoreContext ctx(cache, s, kChainswords, MoveWeights{}, &plan);
    ASSERT_NE(ctx.team(), nullptr);
    EXPECT_EQ(ctx.team()->area, set->area);
    EXPECT_FALSE(ctx.team()->line);
    EXPECT_TRUE(ctx.team()->partners.empty());
    MoveWeights no_approach;
    no_approach.objective_approach = 0.0;
    const MoveScoreContext without(cache, s, kChainswords, no_approach, &plan);
    EXPECT_TRUE(without.team() == nullptr || !without.team()->area);
    std::vector<std::string> reasons;
    const GameState after = play_movement(gs, MoveWeights{}, &reasons);
    const float before_gap = centroid_gap_mm(positions_of(*gs.find_unit(kChainswords)), area);
    const float after_gap = centroid_gap_mm(positions_of(*after.find_unit(kChainswords)), area);
    EXPECT_LT(after_gap, before_gap - in(5.0f)) << describe(gs, plan) << "\n" << ::testing::PrintToString(reasons);
    const bool named = std::any_of(reasons.begin(), reasons.end(), [&](const std::string& r) {
        return r.rfind("Legionaries", 0) == 0 && r.find(area.name + " (") != std::string::npos;
    });
    EXPECT_TRUE(named) << ::testing::PrintToString(reasons);
}

TEST(M87_Normal, ASetMemberCountsItsPartnersPromise) {
    // The Centre held by Termagants (OC 20), both Legionaries units able to reach it this turn: the set's first member to
    // move counts its partners' OC there before they have moved (the promise), so it doesn't wait for them; after A's
    // Movement phase A's level there is above the Termagants'.
    GameState gs = objectives_with({kChainswords, kBoltguns, kLord, kTermagants, kGuard});
    mf::place_rank(unit(gs, kTermagants), 22.0f, 32.0f);
    place_block(unit(gs, kChainswords), 14.0f, 21.0f);
    place_block(unit(gs, kBoltguns), 30.0f, 21.0f);
    mf::place_rank(unit(gs, kLord), 22.0f, 20.0f);
    mf::place_rank(unit(gs, kGuard), 40.0f, 58.0f);
    gs = at_move_units(gs);
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::A);
    const AreaSet* centre = nullptr;
    for (const AreaSet& set : plan.areas) {
        if (set.area == kCentre) centre = &set;
    }
    ASSERT_TRUE(centre != nullptr && centre->members.size() >= 2) << describe(gs, plan);
    const AreaMember& first = centre->members.front();
    int partners = 0;
    for (const AreaMember& m : centre->members) partners += m.unit == first.unit ? 0 : m.oc;
    const std::optional<ObjectiveOutlook> o =
        objective_outlook(gs, *gs.find_unit(first.unit), plan, turns_left(gs), 1.0);
    ASSERT_TRUE(o);
    EXPECT_EQ(o->areas[kCentre].others, partners);
    const GameState after = play_movement(gs, MoveWeights{});
    const std::array<int, 2> levels = level_of_control(after, after.mission->objectives[kCentre]);
    EXPECT_GT(levels[0], levels[1]);
}

TEST(M87_Normal, TheLastUnitPlaysOnTheVpTally) {
    // Side A's last unit, the Chaos Lord, against a Hive Tyrant (it loses the exchange): A is behind on points destroyed
    // but ahead on VP. Under the mission its survival counts as a win's (D-084); with the objective weights 0, as at
    // M86, on points destroyed: nothing.
    GameState gs = objectives();
    for (Unit& u : gs.units) {
        if (u.side == Side::A && u.id != kLord) u.models.clear();
    }
    mf::place_rank(unit(gs, kLord), 22.0f, 20.0f);
    mf::place_rank(*gs.find_unit(UnitId{10}), 22.0f, 32.0f);  // the Hive Tyrant
    gs = at_move_units(gs);
    gs.flow.battle_round = 3;
    gs.mission->objective_vp = {40, 0};
    const GameState s = started(gs, kLord);
    OddsCache cache;
    const MoveScoreContext vp(cache, s, kLord);
    const MoveScoreContext points(cache, s, kLord, zero_objectives());
    ASSERT_LT(vp.lean(), 0.0);
    EXPECT_GT(vp.survival_weight(), 0.0);
    EXPECT_EQ(points.survival_weight(), 0.0);
}

TEST(M87_Normal, AFreshBotGivesTheMemosMoveOnTheObjectiveScenario) {
    // D-054: a plan is a function of the state, so a fresh bot (no memo) at any decision of a game on the objective
    // scenario gives the action the playing bot (with its memo) gave. Two battle rounds.
    wh40k::client::GameSession s = wh40k::client::GameSession::new_game(objectives(), 3);
    std::array<Bot, 2> bots{Bot(BotConfig{}), Bot(BotConfig{})};
    wh40k::client::AutoSettings autos;
    autos.bot_controlled = {true, true};
    std::size_t checked = 0;
    while (s.pending() && s.history().size() < 5000 && s.state().flow.battle_round <= 2) {
        (void)wh40k::client::run_auto_actions(s, autos);
        if (!s.pending()) break;
        const Side side = s.pending()->side;
        const GameState before = s.state();
        const BotChoice played = bots[side_index(side)].decide(before, side);
        if (std::holds_alternative<decision::MoveModels>(before.pending->kind) ||
            std::holds_alternative<decision::MoveUnits>(before.pending->kind)) {
            const BotChoice fresh = Bot(BotConfig{}).decide(before, side);
            EXPECT_EQ(fresh.action, played.action) << played.reason << " / " << fresh.reason;
            ++checked;
        }
        if (played.stopped || !s.submit(played.action).ok()) break;
    }
    EXPECT_GT(checked, 40u);
    EXPECT_GT(bots[0].memo_hits() + bots[1].memo_hits(), 0u);
}


