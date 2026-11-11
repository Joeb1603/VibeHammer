// M79 (D-083 part 1): the army plan. Hand-built roster positions (and one Strike position, without points): the
// groups that pay and the ones that don't (each member's lone value checked), an engaged unit as a group's seed, the
// no-op cases (one unit a side, one unit on our side, the M77 / M55 switch), a group on an enemy side's last unit, the
// same plan from the same state (repeated, copied, a fresh cache, the middle of a move), and the plan's cost (printed).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "arena.hpp"
#include "auto_actions.hpp"
#include "bot.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/actions.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/state/queries.hpp"
#include "normal/army_plan.hpp"
#include "normal/value.hpp"
#include "session.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using namespace wh40k::ai::normal;
using wh40k::ai::analysis::OddsCache;
namespace client = wh40k::client;
using wh40k::test::in;
namespace mf = wh40k::test::monsters;

namespace {

// The roster scenario's units (D-076: Side A first, each side west to east), by unit id: the chainsword Legionaries (1),
// the Chaos Lord (2), the boltgun Legionaries (5), Termagants (6), Genestealers (7, 13), a Screamer-killer (8), the
// Tyrant Guard (9), the Hive Tyrant (10).
constexpr UnitId kChainswords{1};
constexpr UnitId kLord{2};
constexpr UnitId kBoltguns{5};
constexpr UnitId kTermagants{6};
constexpr UnitId kGenestealers{7};
constexpr UnitId kScreamer{8};
constexpr UnitId kGuard{9};
constexpr UnitId kTyrant{10};
constexpr UnitId kGenestealers2{13};

// The roster scenario with only the units `ids` (in unit order).
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

float radius_in(const GameState& gs, UnitId id) { return mm_to_inches(base_radius(gs.find_unit(id)->models.front().base)); }

// Started, at the first turn's Move Units decision (the rosters: Side A's).
GameState at_move_units(GameState gs) {
    start_game(gs);
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind);
    });
    return gs;
}

// The Screamer-killer at (22, 34) and both Legionaries ranks `gap_in` inches from it, edge to edge, south-west and
// south-east (each rank's end model on the diagonal); the Tyrant Guard far north-east (Side B's other unit).
GameState screamer_fixture(float gap_in, bool with_guard = true, bool with_boltguns = true) {
    GameState gs = with_guard ? rosters_with({kChainswords, kBoltguns, kScreamer, kGuard})
                              : rosters_with({kChainswords, kBoltguns, kScreamer});
    mf::place_rank(unit(gs, kScreamer), 22.0f, 34.0f);
    const float half = 6.3f - radius_in(gs, kChainswords);  // a rank of ten 32 mm bases: its end model's centre
    const float c = (gap_in + radius_in(gs, kScreamer) + radius_in(gs, kChainswords)) / 1.41421356f;
    mf::place_rank(unit(gs, kChainswords), 22.0f - c - half, 34.0f - c);
    mf::place_rank(unit(gs, kBoltguns), 22.0f + c + half, 34.0f - c);
    if (with_guard) mf::place_rank(unit(gs, kGuard), 40.0f, 58.0f);
    if (!with_boltguns) gs.units.erase(gs.units.begin() + 1);
    return at_move_units(gs);
}

// A lone chainsword Legionaries unit at (22, 20) (the Chaos Lord far south-east), the Genestealers 12" north-west of
// it, the Termagants 15" north-east and the Hive Tyrant 14" north, with `tyrant_wounds` wounds left.
GameState tyrant_fixture(int tyrant_wounds) {
    GameState gs = rosters_with({kChainswords, kLord, kTermagants, kGenestealers, kTyrant});
    mf::place_rank(unit(gs, kChainswords), 22.0f, 20.0f);
    mf::place_rank(unit(gs, kLord), 40.0f, 2.0f);
    const float r = radius_in(gs, kChainswords);
    mf::place_rank(unit(gs, kGenestealers), 15.0f, 20.0f + 12.0f + r + radius_in(gs, kGenestealers));
    mf::place_rank(unit(gs, kTermagants), 29.0f, 20.0f + 15.0f + r + radius_in(gs, kTermagants));
    mf::place_rank(unit(gs, kTyrant), 22.0f, 20.0f + 14.0f + r + radius_in(gs, kTyrant));
    unit(gs, kTyrant).models.front().wounds_remaining = tyrant_wounds;
    return at_move_units(gs);
}

std::vector<UnitId> members_of(const PlanGroup& g) {
    std::vector<UnitId> out;
    for (const PlanMember& m : g.members) out.push_back(m.unit);
    return out;
}

double lone_value(const GameState& gs, UnitId ours, UnitId enemy) {
    OddsCache cache;
    return lone_exchange(cache, gs, ours, enemy).value;
}

double joint_value(const GameState& gs, std::initializer_list<UnitId> members, UnitId target) {
    OddsCache cache;
    const std::vector<UnitId> m(members);
    return joint_exchange(cache, gs, m, target).value;
}

// Each of `side`'s units: its closest enemy unit, no partners, no group.
void expect_closest_no_partners(const GameState& gs, const ArmyPlan& plan, Side side) {
    EXPECT_FALSE(plan.active);
    EXPECT_TRUE(plan.groups.empty());
    std::size_t n = 0;
    for (const Unit& u : gs.units) {
        if (u.side != side || u.models.empty()) continue;
        ++n;
        const UnitPlan* p = plan.find(u.id);
        ASSERT_NE(p, nullptr);
        const Unit* closest = closest_enemy(gs, u);
        ASSERT_NE(closest, nullptr);
        EXPECT_EQ(p->focus, closest->id);
        EXPECT_TRUE(p->partners.empty());
        EXPECT_FALSE(p->group.has_value());
    }
    EXPECT_EQ(plan.units.size(), n);
}

}  // namespace

// ---- Groups that pay, groups that don't ----

TEST(M79_ArmyPlan, TwoLegionariesUnitsTeamUpOnAScreamerKiller) {
    // Both Legionaries units 16" from a Screamer-killer (PLAN.md said 18"; there the joint exchange is +0.06, just over
    // the margin, so the test stands where it is +0.22; D-083 part 1). Alone each loses: the Screamer-killer charges it
    // and out-fights it. Together they win: it can answer only one of them.
    const GameState gs = screamer_fixture(16.0f);
    OddsCache cache;
    for (const UnitId l : {kChainswords, kBoltguns}) {
        const LoneExchange lone = lone_exchange(cache, gs, l, kScreamer);
        EXPECT_NEAR(mm_to_inches(lone.gap_mm), 16.0f, 0.05f);
        EXPECT_EQ(lone.arrival, 0);  // it can attack this turn
        EXPECT_LT(lone.value, 0.0) << "alone it doesn't pay";
    }
    const ArmyPlan plan = army_plan(cache, gs, Side::A);
    EXPECT_TRUE(plan.active);
    ASSERT_EQ(plan.groups.size(), 1u);
    const PlanGroup& g = plan.groups.front();
    EXPECT_EQ(g.target, kScreamer);
    EXPECT_EQ(members_of(g), (std::vector<UnitId>{kChainswords, kBoltguns}));
    for (const PlanMember& m : g.members) {
        EXPECT_LT(m.lone, 0.0);
        EXPECT_DOUBLE_EQ(m.alone, 0.0);  // apart, each holds back: neither pays alone (nor against the far Tyrant Guard)
        EXPECT_DOUBLE_EQ(m.lone, lone_value(gs, m.unit, kScreamer));
    }
    EXPECT_GT(g.value, 0.15);  // +0.22 measured
    EXPECT_DOUBLE_EQ(g.alone, 0.0);
    EXPECT_DOUBLE_EQ(g.gain, g.value - g.alone);
    EXPECT_GT(g.gain, kMinPlanGain);
    EXPECT_DOUBLE_EQ(g.value, joint_value(gs, {kChainswords, kBoltguns}, kScreamer));
    EXPECT_EQ(g.first_contact, 0);
    ASSERT_TRUE(g.reply_to.has_value());
    EXPECT_NE(g.member(*g.reply_to), nullptr);
    // Each Legionaries unit: the Screamer-killer, the other as its partner.
    for (const auto& [u, partner] : {std::pair{kChainswords, kBoltguns}, std::pair{kBoltguns, kChainswords}}) {
        const UnitPlan* p = plan.find(u);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p->focus, kScreamer);
        EXPECT_EQ(p->partners, std::vector<UnitId>{partner});
        EXPECT_EQ(plan.group_of(u), &g);
    }
    // The reasons' text: display names (D-065).
    EXPECT_EQ(describe_unit(gs, plan, kChainswords).rfind("with Legionaries 2 on Screamer-killer 1 (joint +0.", 0), 0u)
        << describe_unit(gs, plan, kChainswords);
    EXPECT_EQ(describe(gs, plan).rfind("Legionaries 1 + Legionaries 2 on Screamer-killer 1 (joint +0.", 0), 0u)
        << describe(gs, plan);
}

TEST(M79_ArmyPlan, AFullStrengthHiveTyrantNeedsNoPartners) {
    // A Hive Tyrant 14" from a lone Legionaries unit wins alone, and nobody adds to it: the Termagants' share is below
    // the margin, and the Genestealers lower the joint exchange (the Legionaries' strike-back kills them). Without it the
    // Genestealers and Termagants lose together. So no group forms and the Tyrant keeps its closest unit.
    const GameState gs = tyrant_fixture(10);
    const double tyrant = lone_value(gs, kTyrant, kChainswords);
    EXPECT_GT(tyrant, 0.5);  // +1.16 measured
    EXPECT_LT(lone_value(gs, kGenestealers, kChainswords), 0.0);
    EXPECT_LT(lone_value(gs, kTermagants, kChainswords), 0.0);
    EXPECT_LT(joint_value(gs, {kTermagants, kTyrant}, kChainswords) - tyrant, kMinPlanGain);
    EXPECT_LT(joint_value(gs, {kGenestealers, kTyrant}, kChainswords), tyrant);
    EXPECT_LT(joint_value(gs, {kTermagants, kGenestealers}, kChainswords), 0.0);
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::B);
    EXPECT_TRUE(plan.active);
    EXPECT_TRUE(plan.groups.empty()) << describe(gs, plan);
    const UnitPlan* t = plan.find(kTyrant);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->focus, kChainswords);
    EXPECT_TRUE(t->partners.empty());
}

TEST(M79_ArmyPlan, AWoundedHiveTyrantPairsWithGenestealers) {
    // The same position with the Tyrant at 3 wounds: it no longer wins alone by much (what is left of it is counted by
    // its wounds, D-083 part 1), and with the Genestealers the joint exchange beats what both get apart. The Termagants
    // add less than the margin, so they stay out.
    const GameState gs = tyrant_fixture(3);
    const GameState fresh = tyrant_fixture(10);
    EXPECT_LT(lone_value(gs, kTyrant, kChainswords), lone_value(fresh, kTyrant, kChainswords));
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::B);
    ASSERT_EQ(plan.groups.size(), 1u) << describe(gs, plan);
    const PlanGroup& g = plan.groups.front();
    EXPECT_EQ(g.target, kChainswords);
    EXPECT_EQ(members_of(g), (std::vector<UnitId>{kGenestealers, kTyrant}));
    EXPECT_LT(g.member(kGenestealers)->lone, 0.0);
    EXPECT_GT(g.gain, kMinPlanGain);
    EXPECT_GT(g.value, g.alone);
    EXPECT_EQ(plan.find(kTyrant)->partners, std::vector<UnitId>{kGenestealers});
    EXPECT_EQ(plan.find(kGenestealers)->partners, std::vector<UnitId>{kTyrant});
    const UnitPlan* termagants = plan.find(kTermagants);
    ASSERT_NE(termagants, nullptr);
    EXPECT_TRUE(termagants->partners.empty());
    EXPECT_FALSE(termagants->group.has_value());
}

TEST(M79_ArmyPlan, ALosingGroupIsNotFormed) {
    // Both Genestealers units 12" from the boltgun Legionaries (M68: a lone Genestealers charge deals little and takes
    // much back): each loses alone and the pair still loses, so no group forms on it (nor on the far Chaos Lord).
    GameState gs = rosters_with({kLord, kBoltguns, kGenestealers, kGenestealers2});
    mf::place_rank(unit(gs, kBoltguns), 22.0f, 20.0f);
    mf::place_rank(unit(gs, kLord), 40.0f, 2.0f);
    const float r = radius_in(gs, kBoltguns) + radius_in(gs, kGenestealers);
    mf::place_rank(unit(gs, kGenestealers), 15.0f, 20.0f + 12.0f + r);
    mf::place_rank(unit(gs, kGenestealers2), 29.0f, 20.0f + 12.0f + r);
    gs = at_move_units(gs);
    EXPECT_LT(lone_value(gs, kGenestealers, kBoltguns), 0.0);
    EXPECT_LT(lone_value(gs, kGenestealers2, kBoltguns), 0.0);
    EXPECT_LT(joint_value(gs, {kGenestealers, kGenestealers2}, kBoltguns), 0.0);
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::B);
    EXPECT_TRUE(plan.active);
    EXPECT_TRUE(plan.groups.empty()) << describe(gs, plan);
    EXPECT_EQ(describe(gs, plan), "no group pays");
    for (const UnitPlan& u : plan.units) EXPECT_TRUE(u.partners.empty());
}

TEST(M79_ArmyPlan, AnEngagedUnitJoinsAsASeed) {
    // The chainsword Legionaries engaged with the Screamer-killer (1" south of it), the boltgun Legionaries 10" east of
    // it: a group on the unit the engaged one fights, the engaged unit a member (arrival 0, Engaged).
    GameState gs = rosters_with({kChainswords, kBoltguns, kScreamer, kGuard});
    mf::place_rank(unit(gs, kScreamer), 22.0f, 34.0f);
    mf::place_rank(unit(gs, kChainswords), 22.0f,
                   34.0f - 1.0f - radius_in(gs, kScreamer) - radius_in(gs, kChainswords));
    mf::place_rank(unit(gs, kBoltguns), 22.0f + 10.0f + radius_in(gs, kScreamer) + 6.3f, 34.0f);
    mf::place_rank(unit(gs, kGuard), 40.0f, 58.0f);
    gs = at_move_units(gs);
    ASSERT_EQ(engaged_enemy_units(gs, *gs.find_unit(kChainswords)), std::vector<UnitId>{kScreamer});
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::A);
    ASSERT_EQ(plan.groups.size(), 1u) << describe(gs, plan);
    const PlanGroup& g = plan.groups.front();
    EXPECT_EQ(g.target, kScreamer);
    EXPECT_EQ(members_of(g), (std::vector<UnitId>{kChainswords, kBoltguns}));
    const PlanMember* engaged = g.member(kChainswords);
    EXPECT_EQ(engaged->mode, AttackMode::Engaged);
    EXPECT_EQ(engaged->arrival, 0);
    EXPECT_DOUBLE_EQ(engaged->alone, engaged->lone);  // engaged, it can't hold back
    EXPECT_GT(g.gain, kMinPlanGain);
    EXPECT_EQ(plan.find(kChainswords)->focus, kScreamer);
}

TEST(M79_ArmyPlan, AGroupGangsUpOnTheEnemysLastUnit) {
    // The Screamer-killer as Side B's only unit: ganging up on it is what the plan is for (D-083 part 1: only our side
    // having fewer than two units makes the plan a no-op).
    const GameState gs = screamer_fixture(16.0f, /*with_guard=*/false);
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::A);
    EXPECT_TRUE(plan.active);
    ASSERT_EQ(plan.groups.size(), 1u);
    EXPECT_EQ(plan.groups.front().target, kScreamer);
    EXPECT_EQ(members_of(plan.groups.front()), (std::vector<UnitId>{kChainswords, kBoltguns}));
    // Side B's plan (one unit) is the no-op.
    expect_closest_no_partners(gs, army_plan(cache, gs, Side::B), Side::B);
}

TEST(M79_ArmyPlan, ASideWithOneUnitHasNoPlan) {
    const GameState gs = screamer_fixture(16.0f, /*with_guard=*/true, /*with_boltguns=*/false);
    ASSERT_EQ(gs.find_unit(kBoltguns), nullptr);
    OddsCache cache;
    expect_closest_no_partners(gs, army_plan(cache, gs, Side::A), Side::A);
}

// ---- Determinism ----

TEST(M79_ArmyPlan, TheSameStateGivesTheSamePlan) {
    // Repeated calls with one cache, a copied state with a fresh cache, at the rosters' start (both sides), the hand-built
    // positions, and a played Normal vs Normal rosters game at Side B's round 2 Move Units decision.
    std::vector<GameState> states{at_move_units(wh40k::test::load_rosters_scenario()), screamer_fixture(16.0f),
                                  tyrant_fixture(3)};
    {
        client::GameSession session = client::GameSession::new_game(wh40k::test::load_rosters_scenario(), 3);
        const BotConfig normal{};
        std::array<Bot, 2> bots{Bot(normal), Bot(normal)};
        client::AutoSettings autos;
        autos.skip_empty_steps = true;
        autos.auto_allocate = false;
        autos.bot_controlled = {true, true};
        for (int step = 0; step < 2000 && session.pending(); ++step) {
            (void)client::run_auto_actions(session, autos);
            const GameState& st = session.state();
            if (!st.pending) break;
            if (st.flow.battle_round == 2 && st.pending->side == Side::B &&
                std::holds_alternative<decision::MoveUnits>(st.pending->kind)) {
                states.push_back(st);
                break;
            }
            ASSERT_TRUE(session.submit(bots[side_index(st.pending->side)].decide(st, st.pending->side).action).ok());
        }
        ASSERT_EQ(states.size(), 4u);
    }
    for (const GameState& gs : states) {
        for (const Side side : {Side::A, Side::B}) {
            OddsCache cache;
            const ArmyPlan first = army_plan(cache, gs, side);
            EXPECT_EQ(army_plan(cache, gs, side), first);  // the same cache, warm
            const GameState copy = gs;
            OddsCache fresh;
            EXPECT_EQ(army_plan(fresh, copy, side), first);
        }
    }
    // A plan from the middle of a move is the plan of its start (the moving unit taken at its starting positions).
    GameState gs = screamer_fixture(16.0f);
    OddsCache cache;
    const ArmyPlan before = army_plan(cache, gs, Side::A);
    ASSERT_FALSE(before.groups.empty());
    const GameAction start = action::StartMove{kBoltguns, MoveType::Normal, std::nullopt};
    ASSERT_TRUE(validate_action(gs, Side::A, start).ok());
    (void)apply_action(gs, Side::A, start);
    EXPECT_EQ(army_plan(cache, gs, Side::A), before);
    const Model m = gs.find_unit(kBoltguns)->models.front();
    const GameAction step = action::ProposeModelMove{m.id, {Position{m.position.x, m.position.y + in(3.0f), 0.0f}}};
    ASSERT_TRUE(validate_action(gs, Side::A, step).ok()) << validate_action(gs, Side::A, step).reasons.front();
    (void)apply_action(gs, Side::A, step);
    ASSERT_NE(gs.find_unit(kBoltguns)->models.front().position, m.position);  // the model stands 3" further north
    EXPECT_EQ(army_plan(cache, gs, Side::A), before);
}

// ---- The no-op cases ----

TEST(M79_ArmyPlan, OneUnitASideIsTheClosestWithNoPartners) {
    const GameState gs = at_move_units(wh40k::test::load_v1_scenario());
    OddsCache cache;
    for (const Side side : {Side::A, Side::B}) {
        const ArmyPlan plan = army_plan(cache, gs, side);
        expect_closest_no_partners(gs, plan, side);
        EXPECT_EQ(describe(gs, plan), "no plan");
    }
}

TEST(M79_ArmyPlan, BeforeTheBattleThereIsNoPlan) {
    // Before the battle (not started) every unit has its closest enemy unit and no partners. (D-085, M85: this was part
    // of TheM77SwitchGivesTheClosestWithNoPartners, whose Generation::M77 / M55 cases went with the switch.)
    const GameState not_started = wh40k::test::load_rosters_scenario();
    OddsCache cache;
    expect_closest_no_partners(not_started, army_plan(cache, not_started, Side::A), Side::A);
}

// ---- Shares without points ----

TEST(M79_ArmyPlan, StrikeWithoutPointsUsesShares) {
    // Strike (no points, D-007): three damaged Termagant units (7, 3 and 8 models of 10) gang up on the Intercessor
    // Squad in front of them, as they did at round 1 of a Normal vs Normal Strike game (seed 1). The values are shares of
    // a unit (weight 1): the group's risk is what is left of its members, 0.7 + 0.3 + 0.8.
    GameState gs = load_scenario(wh40k::test::repo_root() / "data/scenarios/strike_44x60.json", wh40k::test::repo_root());
    gs.first_turn = Side::B;
    const auto place = [&](std::size_t i, std::initializer_list<std::pair<float, float>> at) {
        Unit& u = gs.units[i];
        u.models.resize(at.size());
        std::size_t k = 0;
        for (const auto& [x, y] : at) u.models[k++].position = wh40k::test::at(x, y);
    };
    place(0, {{20.97f, 19.86f}, {22.23f, 19.86f}, {23.49f, 19.86f}, {24.75f, 19.86f}, {26.00f, 19.86f}});
    place(1, {{27.48f, 18.37f}, {28.74f, 18.37f}, {30.00f, 18.37f}, {31.26f, 18.37f}, {32.52f, 18.37f}});
    place(2, {{42.48f, 19.37f}, {43.74f, 19.37f}, {45.00f, 19.37f}, {46.26f, 19.37f}, {47.52f, 19.37f}});
    place(3, {{7.76f, 32.56f}, {8.88f, 32.56f}, {10.00f, 32.56f}, {7.76f, 33.68f}, {8.88f, 33.68f}, {10.00f, 33.68f},
              {11.12f, 33.68f}});
    place(4, {{23.00f, 33.68f}, {24.12f, 33.68f}, {25.24f, 33.68f}});
    place(5, {{34.76f, 35.56f}, {35.88f, 35.56f}, {37.00f, 35.56f}, {34.76f, 36.68f}, {35.88f, 36.68f}, {37.00f, 36.68f},
              {38.12f, 36.68f}, {39.24f, 36.68f}});
    gs = at_move_units(gs);
    ASSERT_EQ(gs.pending->side, Side::B);
    for (const Unit& u : gs.units) {
        EXPECT_EQ(u.points, 0);
        EXPECT_DOUBLE_EQ(share_weight(gs, u), 1.0);
    }
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::B);
    ASSERT_EQ(plan.groups.size(), 1u) << describe(gs, plan);
    const PlanGroup& g = plan.groups.front();
    EXPECT_EQ(g.target, gs.units[0].id);
    EXPECT_EQ(members_of(g), (std::vector<UnitId>{gs.units[3].id, gs.units[4].id, gs.units[5].id}));
    EXPECT_NEAR(g.risk, 0.7 + 0.3 + 0.8, 1e-12);
    for (const PlanMember& m : g.members) EXPECT_LT(m.lone, 0.0);
    EXPECT_GT(g.gain, kMinPlanGain);
    EXPECT_LT(g.value, 1.0);  // at most the squad (a whole unit is one share)
    // The Marines: each squad wins alone, so they take no partners.
    for (const UnitPlan& u : army_plan(cache, gs, Side::A).units) EXPECT_TRUE(u.partners.empty());
}

// ---- The cost ----

TEST(M79_ArmyPlan, TheCostIsMeasured) {
    // Printed, not asserted (Debug builds are slow; the Release numbers are in D-083 part 1 and PLAN.md M79): the plan at
    // the rosters' start (both sides) and at the start of the largest random battle of seeds 1-400 (seed 63, 7 v 7).
    const ArmySource source = load_army_source(wh40k::test::repo_root());
    const std::vector<std::pair<std::string, GameState>> states{
        {"rosters", at_move_units(wh40k::test::load_rosters_scenario())},
        {"random armies seed 63", at_move_units(random_armies_battle(source, 63, Side::A))}};
    for (const auto& [name, gs] : states) {
        for (const Side side : {Side::A, Side::B}) {
            constexpr int kCalls = 5;
            double total = 0.0;
            double slowest = 0.0;
            std::optional<ArmyPlan> first;
            for (int i = 0; i < kCalls; ++i) {
                OddsCache cache;
                const auto t0 = std::chrono::steady_clock::now();
                ArmyPlan plan = army_plan(cache, gs, side);
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                total += ms;
                slowest = std::max(slowest, ms);
                if (!first) first = std::move(plan);
                else EXPECT_EQ(plan, *first);
            }
            std::printf("[ M79 cost ] %s, side %c (%zu units v %zu): mean %.2f ms, max %.2f ms per plan; %s\n",
                        name.c_str(), side == Side::A ? 'A' : 'B', first->units.size(), gs.units.size() - first->units.size(),
                        total / kCalls, slowest, describe(gs, *first).c_str());
        }
    }
}
