// M83 (D-083 part 5): tuning. A unit that follows no group closes in on its closest enemy unit beyond the line's
// horizon (MoveWeights::approach_alone), unless that unit outreaches it; within the horizon, and for an outreached
// unit, the score is as before. The follow gate is 0.1 (was 0.2). Hard ranks its moves without the lone approach.
// Hand-built roster positions (the M79/M80 fixtures' units).

#include <gtest/gtest.h>

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "analysis/threat.hpp"
#include "bot.hpp"
#include "core/engine/actions.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/state/queries.hpp"
#include "generation.hpp"
#include "hard/weights.hpp"
#include "normal/army_plan.hpp"
#include "normal/move_score.hpp"
#include "normal/movement.hpp"
#include "simulator.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using namespace wh40k::ai::normal;
using wh40k::ai::analysis::OddsCache;
namespace mf = wh40k::test::monsters;

namespace {

// The roster scenario's units by id (D-076: Side A first): the Chaos Lord (2), the Havocs (4), the boltgun
// Legionaries (5), Termagants (6), the Tyrant Guard (9).
constexpr UnitId kLord{2};
constexpr UnitId kHavocs{4};
constexpr UnitId kBoltguns{5};
constexpr UnitId kTermagants{6};
constexpr UnitId kGuard{9};

GameState rosters_with(std::initializer_list<UnitId> ids) {
    const GameState all = wh40k::test::load_rosters_scenario();
    GameState gs = all;
    gs.units.clear();
    for (const Unit& u : all.units) {
        if (std::find(ids.begin(), ids.end(), u.id) != ids.end()) gs.units.push_back(u);
    }
    return gs;
}

Unit& unit(GameState& gs, UnitId id) {
    Unit* u = gs.find_unit(id);
    if (u == nullptr) throw std::logic_error("no such unit");
    return *u;
}

// Started with `first` taking the first turn, at its first Move Units decision.
GameState at_move_units(GameState gs, Side first = Side::A) {
    gs.first_turn = first;
    start_game(gs);
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind);
    });
    return gs;
}

// Edge to edge, in inches.
float gap_in(const GameState& gs, UnitId a, UnitId b) {
    return mm_to_inches(unit_distance(*gs.find_unit(a), *gs.find_unit(b)).value_or(-1.0f));
}

BotConfig normal_at(Generation g) {
    BotConfig c;
    c.level = BotLevel::Normal;
    c.generation = g;
    return c;
}

// The side to move's whole Movement phase, played by Normal at `g` (each StartMove's reason in `reasons`).
GameState play_movement(GameState gs, Generation g, std::vector<std::string>* reasons = nullptr) {
    Bot bot(normal_at(g));
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

// The boltgun Legionaries and the Chaos Lord at the south edge, a Termagants unit `gap` inches north of the
// Legionaries, the Tyrant Guard far north-east (the Lord's closest enemy unit). Side A to move.
GameState far_apart(float gap) {
    GameState gs = rosters_with({kLord, kBoltguns, kTermagants, kGuard});
    mf::place_rank(unit(gs, kBoltguns), 15.0f, 4.0f);
    mf::place_rank(unit(gs, kLord), 36.0f, 4.0f);
    const float r = mm_to_inches(base_radius(unit(gs, kBoltguns).models.front().base)) +
                    mm_to_inches(base_radius(unit(gs, kTermagants).models.front().base));
    mf::place_rank(unit(gs, kTermagants), 15.0f, 4.0f + gap + r);
    mf::place_rank(unit(gs, kGuard), 38.0f, 58.0f);
    return at_move_units(gs);
}

}  // namespace

TEST(M83_Normal, AUnitWithoutAGroupClosesInBeyondTheHorizon) {
    // The boltgun Legionaries 50" from a Termagants unit and the Chaos Lord about 50" from the Tyrant Guard, beyond
    // their next turn's reach even after an Advance: every move scores about the same on the line there. Neither has
    // a group, and neither is outreached by its closest enemy unit (the Legionaries reach 6" + 24" against the
    // Termagants' 6" + 18"; the Lord 6" + its 12" pistol against the Guard's 6" + 7"), so both now close in. Before
    // (M77) the Lord drifted.
    const GameState gs = far_apart(50.0f);
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::A);
    ASSERT_TRUE(plan.active);
    for (const auto& [id, closest] : {std::pair{kBoltguns, kTermagants}, std::pair{kLord, kGuard}}) {
        ASSERT_TRUE(plan.find(id)->partners.empty()) << describe(gs, plan);
        Simulator sim(gs, 0);
        wh40k::core::ScriptedDice dice{4};
        ASSERT_TRUE(sim.step(Side::A, action::StartMove{id, MoveType::Normal, std::nullopt}, dice).ok());
        const MoveScoreContext ctx(cache, sim.state(), id, MoveWeights{}, Generation::Current, &plan);
        ASSERT_NE(ctx.team(), nullptr);
        EXPECT_TRUE(ctx.team()->alone);
        EXPECT_EQ(ctx.team()->focus, closest);
        EXPECT_FALSE(ctx.team()->line);
        EXPECT_TRUE(ctx.team()->partners.empty());
        EXPECT_FALSE(ctx.supported());
        const MoveScoreContext before(cache, sim.state(), id, MoveWeights{}, Generation::M77, nullptr);
        EXPECT_EQ(before.team(), nullptr);
        MoveWeights off;
        off.approach_alone = 0.0;
        const MoveScoreContext zero(cache, sim.state(), id, off, Generation::Current, &plan);
        EXPECT_EQ(zero.team(), nullptr);
        const std::optional<PlannedMove> now = plan_unit_move(cache, sim.state(), Generation::Current);
        ASSERT_TRUE(now);
        EXPECT_GT(now->score.approach, 0.0);
    }
    std::vector<std::string> reasons;
    const GameState now = play_movement(gs, Generation::Current, &reasons);
    const GameState then = play_movement(gs, Generation::M77);
    const float lord_start = gap_in(gs, kLord, kGuard);
    EXPECT_LT(gap_in(now, kLord, kGuard), lord_start - 5.0f);
    EXPECT_GT(gap_in(then, kLord, kGuard), lord_start - 1.0f) << "M77: the Lord drifts";
    EXPECT_LT(gap_in(now, kBoltguns, kTermagants), 50.0f - 5.0f);
    const bool named = std::any_of(reasons.begin(), reasons.end(), [](const std::string& r) {
        return r.find("Chaos Lord") != std::string::npos && r.find(" alone") != std::string::npos;
    });
    EXPECT_TRUE(named) << ::testing::PrintToString(reasons);
}

TEST(M83_Normal, AnOutreachedUnitKeepsItsMove) {
    // A Termagants unit 45" from the Havocs: beyond its reach (24"), but the Havocs' lascannons outreach it (Move 6" +
    // 48"), so it gets no approach term and its move is the pre-batch move.
    GameState gs = rosters_with({kLord, kHavocs, kTermagants, kGuard});
    mf::place_rank(unit(gs, kHavocs), 15.0f, 4.0f);
    mf::place_rank(unit(gs, kLord), 42.0f, 2.0f);
    mf::place_rank(unit(gs, kTermagants), 12.0f, 50.0f);
    mf::place_rank(unit(gs, kGuard), 38.0f, 58.0f);
    gs = at_move_units(gs, Side::B);
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::B);
    ASSERT_TRUE(plan.active);
    ASSERT_TRUE(plan.find(kTermagants)->partners.empty()) << describe(gs, plan);
    for (const MoveType type : {MoveType::Normal, MoveType::Advance}) {
        Simulator sim(gs, 0);
        wh40k::core::ScriptedDice dice{4};
        ASSERT_TRUE(sim.step(Side::B, action::StartMove{kTermagants, type, std::nullopt}, dice).ok());
        const MoveScoreContext ctx(cache, sim.state(), kTermagants, MoveWeights{}, Generation::Current, &plan);
        EXPECT_EQ(ctx.team(), nullptr);
        const std::optional<PlannedMove> now = plan_unit_move(cache, sim.state(), Generation::Current);
        const std::optional<PlannedMove> before = plan_unit_move(cache, sim.state(), Generation::M77);
        ASSERT_TRUE(now && before);
        EXPECT_EQ(now->candidate.ends, before->candidate.ends);
        EXPECT_EQ(now->candidate.label, before->candidate.label);
        EXPECT_DOUBLE_EQ(now->score.total, before->score.total);
    }
}

TEST(M83_Normal, WithinTheHorizonTheScoreIsAsBefore) {
    // The Havocs (lascannons 48") 30" from the Tyrant Guard: within their next turn's reach, so the approach term adds
    // nothing whatever the move (the turns beyond the horizon are 0 before and after).
    GameState gs = rosters_with({kLord, kHavocs, kTermagants, kGuard});
    mf::place_rank(unit(gs, kHavocs), 22.0f, 4.0f);
    mf::place_rank(unit(gs, kLord), 40.0f, 4.0f);
    mf::place_rank(unit(gs, kGuard), 22.0f, 36.0f);
    mf::place_rank(unit(gs, kTermagants), 8.0f, 58.0f);
    gs = at_move_units(gs);
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::A);
    ASSERT_TRUE(plan.find(kHavocs)->partners.empty()) << describe(gs, plan);
    Simulator sim(gs, 0);
    wh40k::core::ScriptedDice dice{4};
    ASSERT_TRUE(sim.step(Side::A, action::StartMove{kHavocs, MoveType::Normal, std::nullopt}, dice).ok());
    const std::optional<PlannedMove> now = plan_unit_move(cache, sim.state(), Generation::Current);
    ASSERT_TRUE(now);
    EXPECT_DOUBLE_EQ(now->score.approach, 0.0);
    EXPECT_DOUBLE_EQ(now->score.cohesion, 0.0);
}

TEST(M83_Normal, OneUnitASideHasNoApproach) {
    // V1: the plan is inactive, so no unit gets the term (the one-unit scenarios play as before).
    GameState gs = wh40k::test::load_v1_scenario();
    gs = at_move_units(gs);
    const Unit& u = gs.units.front();
    Simulator sim(gs, 0);
    wh40k::core::ScriptedDice dice{4};
    ASSERT_TRUE(sim.step(u.side, action::StartMove{u.id, MoveType::Normal, std::nullopt}, dice).ok());
    OddsCache cache;
    const MoveScoreContext ctx(cache, sim.state(), u.id, MoveWeights{}, Generation::Current, nullptr);
    EXPECT_EQ(ctx.team(), nullptr);
}

TEST(M83_Hard, RanksItsMovesWithoutTheLoneApproach) {
    // Hard's move score is Normal's except for the approach of a unit without a group (D-083 part 5: Hard's own search
    // decides those moves; the term measured worse for it). The follow gate and every other weight are Normal's.
    const hard::BotWeights w{};
    const MoveWeights n{};
    EXPECT_DOUBLE_EQ(w.move.approach_alone, 0.0);
    EXPECT_GT(n.approach_alone, 0.0);
    EXPECT_DOUBLE_EQ(w.move.follow, n.follow);
    EXPECT_DOUBLE_EQ(w.move.approach, n.approach);
    EXPECT_DOUBLE_EQ(w.move.cohesion, n.cohesion);
    EXPECT_DOUBLE_EQ(w.move.delay, n.delay);
    EXPECT_DOUBLE_EQ(w.move.survival, n.survival);
    EXPECT_DOUBLE_EQ(w.move.edge, n.edge);
    EXPECT_DOUBLE_EQ(w.move.coherency, n.coherency);
}
