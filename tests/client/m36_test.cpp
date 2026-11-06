// M36: "Fix illegal moves" (D-052): a drop the engine would refuse goes to the nearest spot it can go, along
// the drag when its line meets an enemy model. Engagement Range is never fixed (user's choice).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/state/queries.hpp"
#include "drop_fix.hpp"
#include "move_plan.hpp"
#include "reach_area.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::in;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

Position plus(const Position& p, float dx_mm, float dy_mm) { return Position{p.x + dx_mm, p.y + dy_mm, 0.0f}; }

const Model& model_of(const GameState& gs, ModelId id) {
    for (const Unit& u : gs.units) {
        for (const Model& m : u.models) {
            if (m.id == id) return m;
        }
    }
    throw std::logic_error("no such model");
}

// The fix region for the model's next straight line (D-052).
ReachRegion region_of(const GameState& gs, ModelId id) {
    const std::optional<MoveReach> reach = move_reach(gs, id);
    EXPECT_TRUE(reach.has_value());
    return reach ? fix_region(model_region(*reach)) : ReachRegion{};
}

std::optional<FixedDrop> fix(const GameState& gs, ModelId id, const Position& drop) {
    return fix_drop(region_of(gs, id), model_of(gs, id).position, drop);
}

// Edge-to-edge gap between two models' bases, negative when they overlap.
float signed_gap(const GameState& gs, const Position& a, ModelId ma, ModelId mb) {
    return centre_distance(a, model_of(gs, mb).position) - base_radius(model_of(gs, ma).base) -
           base_radius(model_of(gs, mb).base);
}

// How far `p` is from the line through `a` and `b`.
float off_line(const Position& p, const Position& a, const Position& b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    return std::fabs((p.x - a.x) * dy - (p.y - a.y) * dx) / std::hypot(dx, dy);
}

bool accepted(const GameState& gs, ModelId id, const Position& to) {
    return validate_action(gs, gs.pending->side, extend_path(gs, id, to)).ok();
}

// The V1 scenario with the Termagants `gap_in` inches from the Intercessors, Side A's Normal move started.
GameSession normal_move(float gap_in = 0.0f, float marines_dx_in = 0.0f) {
    GameState gs = wh40k::test::load_v1_scenario();
    if (gap_in > 0.0f) {
        const float gap = *unit_distance(gs.units[kMarines], gs.units[kGants]);
        for (Model& m : gs.units[kGants].models) m.position.y -= gap - in(gap_in);
    }
    for (Model& m : gs.units[kMarines].models) m.position.x += in(marines_dx_in);
    GameSession session(gs);
    EXPECT_TRUE(session.submit(action::StartMove{session.state().units[kMarines].id, MoveType::Normal, std::nullopt}).ok());
    return session;
}

std::size_t rightmost(const Unit& u) {
    std::size_t best = 0;
    for (std::size_t i = 0; i < u.models.size(); ++i) {
        if (u.models[i].position.x > u.models[best].position.x) best = i;
    }
    return best;
}

std::size_t middle(const Unit& u) {
    std::vector<std::size_t> order(u.models.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return u.models[a].position.x < u.models[b].position.x; });
    return order[order.size() / 2];
}

const Model& closest_gant(const GameState& gs, const Position& p) {
    const Model* best = nullptr;
    for (const Model& t : gs.units[kGants].models) {
        if (best == nullptr || centre_distance(p, t.position) < centre_distance(p, best->position)) best = &t;
    }
    return *best;
}

// The V1 charge after the M30 setup: the Termagants 9.9" away, a charge roll of 10.
GameState charge_move() {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (!(gs.flow.stage == Stage::Charge && turn_side(gs) == Side::A)) {
        apply_action(gs, wh40k::test::decider(gs), legal_actions(gs, wh40k::test::decider(gs)).front());
    }
    const float gap = mm_to_inches(*unit_distance(gs.units[kMarines], gs.units[kGants]));
    for (auto& m : gs.units[kGants].models) m.position.y -= in(gap - 9.9f);
    ScriptedDice dice{4, 6};
    apply_action(gs, Side::A, action::DeclareCharge{gs.units[kMarines].id}, dice);
    apply_action(gs, Side::A, action::StartChargeMove{gs.units[kMarines].id, {gs.units[kGants].id}});
    return gs;
}

// Side A's pile-in after after_successful_charge (as M34).
GameState pile_in() {
    GameState gs = wh40k::test::after_successful_charge();
    apply_action(gs, Side::A, action::EndChargeStep{});
    apply_action(gs, Side::A, action::StartPileIn{gs.units[kMarines].id, {gs.units[kGants].id}});
    return gs;
}

}  // namespace

// ==== The region: Engagement Range is not part of it ====

TEST(M36_FixRegion, EngagementCirclesBecomeNotOnTheModelAndStillEngagedGroupsGo) {
    const float touching = 30.25f;
    MoveReach r;
    r.start = Position{100.0f, 100.0f, 0.0f};
    r.from = r.start;
    r.max_mm = r.remaining_mm = 150.0f;
    r.centre_bounds = Rect{0.0f, 0.0f, 600.0f, 600.0f};
    r.forbidden.push_back(ReachCircle{Position{200.0f, 100.0f, 0.0f}, touching + kEngagementRangeMm + EPS_MM, true, ModelId{7}});
    r.forbidden.push_back(ReachCircle{Position{100.0f, 200.0f, 0.0f}, 32.0f, false, ModelId{2}});
    r.required_any.push_back({ReachCircle{Position{200.0f, 100.0f, 0.0f}, 90.0f, false, ModelId{7}}});  // closer
    r.required_any.push_back({ReachCircle{Position{200.0f, 100.0f, 0.0f}, 81.0f, true, ModelId{7}}});   // still engaged
    const ReachRegion full = model_region(r);
    const ReachRegion region = fix_region(full);
    ASSERT_EQ(region.outside_all.size(), 2u);
    EXPECT_NEAR(region.outside_all[0].radius_mm, touching, 1e-3f);
    EXPECT_FALSE(region.outside_all[0].engagement);
    EXPECT_EQ(region.outside_all[1], full.outside_all[1]);
    ASSERT_EQ(region.inside_any.size(), 1u);  // the closer group stays
    EXPECT_FALSE(region.inside_any[0][0].engagement);

    // Within 2" of the enemy model but not on it, closer: fine for the fix, outside the shaded area.
    const Position near_enemy{200.0f - touching - 10.0f, 100.0f, 0.0f};
    EXPECT_FALSE(in_region(full, near_enemy));
    EXPECT_FALSE(drop_problem(region, near_enemy).has_value());
    EXPECT_FALSE(fix_drop(region, r.from, near_enemy).has_value());
}

TEST(M36_FixRegion, ProblemsMatchTheRegionTest) {
    MoveReach r;
    r.start = Position{100.0f, 100.0f, 0.0f};
    r.from = r.start;
    r.max_mm = r.remaining_mm = 50.0f;
    r.centre_bounds = Rect{16.0f, 16.0f, 130.0f, 600.0f};
    r.forbidden.push_back(ReachCircle{Position{100.0f, 130.0f, 0.0f}, 20.0f, false, ModelId{3}});
    r.blocking.push_back(ReachCircle{Position{70.0f, 100.0f, 0.0f}, 10.0f, false, ModelId{9}});
    const ReachRegion region = fix_region(model_region(r));
    EXPECT_EQ(drop_problem(region, Position{100.0f, 70.0f, 0.0f}), std::nullopt);
    EXPECT_EQ(drop_problem(region, Position{100.0f, 135.0f, 0.0f}), DropProblem::OnModel);
    EXPECT_EQ(drop_problem(region, Position{100.0f, 40.0f, 0.0f}), DropProblem::TooFar);
    EXPECT_EQ(drop_problem(region, Position{135.0f, 100.0f, 0.0f}), DropProblem::OffBattlefield);
    EXPECT_EQ(drop_problem(region, Position{60.0f, 100.0f, 0.0f}), DropProblem::ThroughEnemyModel);  // behind it
    for (float x = 40.0f; x <= 160.0f; x += 2.5f) {
        for (float y = 40.0f; y <= 160.0f; y += 2.5f) {
            const Position p{x, y, 0.0f};
            EXPECT_EQ(in_region(region, p), !drop_problem(region, p).has_value()) << x << ", " << y;
        }
    }
    MoveReach stuck = r;
    stuck.immobile = true;  // 12.03 / 12.08: in base contact
    const ReachRegion none = fix_region(model_region(stuck));
    EXPECT_EQ(drop_problem(none, Position{100.0f, 90.0f, 0.0f}), DropProblem::CannotMove);
    EXPECT_FALSE(fix_drop(none, r.from, Position{100.0f, 40.0f, 0.0f}).has_value());
}

// ==== Engagement Range is left alone ====

TEST(M36_FixDrop, ANormalDropWithinEngagementRangeIsLeftForTheCommit) {
    GameSession session = normal_move(5.0f);
    const Model& m = session.state().units[kMarines].models[middle(session.state().units[kMarines])];
    const Position drop = plus(m.position, 0.0f, in(4.0f));  // about 1" from the Termagants
    EXPECT_FALSE(drop_problem(region_of(session.state(), m.id), drop).has_value());
    EXPECT_FALSE(fix(session.state(), m.id, drop).has_value());
    ASSERT_TRUE(session.submit(extend_path(session.state(), m.id, drop)).ok());
    const auto problems = move_end_problems(session.state());
    EXPECT_TRUE(std::any_of(problems.begin(), problems.end(),
                            [](const std::string& p) { return p.find("unengaged") != std::string::npos; }));
}

TEST(M36_FixDrop, ADragThroughEngagementRangeIsUntouchedAndAFallBackTooFarStopsAtItsMaximum) {
    // A Termagant beside the right-hand Intercessor's path: the line passes within 2" of it, not over it.
    GameState gs = wh40k::test::load_v1_scenario();
    const std::size_t r = rightmost(gs.units[kMarines]);
    const Position start = gs.units[kMarines].models[r].position;
    gs.units[kGants].models[0].position = plus(start, in(1.8f), in(3.0f));
    GameSession session(gs);
    const UnitId marines = session.state().units[kMarines].id;
    ASSERT_TRUE(session.submit(action::StartMove{marines, MoveType::Normal, std::nullopt}).ok());
    const ModelId id = session.state().units[kMarines].models[r].id;
    const Position beyond = plus(start, 0.0f, in(5.9f));
    EXPECT_TRUE(in_region(model_region(*move_reach(session.state(), id)), beyond));  // ends unengaged
    EXPECT_FALSE(fix(session.state(), id, beyond).has_value());
    EXPECT_TRUE(accepted(session.state(), id, beyond));

    // A Fall Back from engaged, dragged 8" east: it stops at its 6" on the line.
    GameState engaged = wh40k::test::load_v1_scenario();
    engaged.units[kGants].models[0].position = plus(start, 0.0f, in(1.9f));
    GameSession fall_back(engaged);
    ASSERT_TRUE(fall_back.submit(action::StartMove{marines, MoveType::FallBack, FallBackMode::OrderedRetreat}).ok());
    const Position east = plus(start, in(8.0f), 0.0f);
    const auto fixed = fix(fall_back.state(), id, east);
    ASSERT_TRUE(fixed.has_value());
    EXPECT_EQ(fixed->problem, DropProblem::TooFar);
    EXPECT_NEAR(centre_distance(start, fixed->to), in(6.0f), 0.01f);
    EXPECT_LT(off_line(fixed->to, start, east), 0.01f);
    EXPECT_TRUE(accepted(fall_back.state(), id, fixed->to));
}

// ==== Along the drag ====

TEST(M36_FixDrop, TooFarStopsAtTheMaximumAlongTheDragAndASecondLineUsesWhatIsLeft) {
    GameSession session = normal_move();
    const Model& m = session.state().units[kMarines].models[middle(session.state().units[kMarines])];
    const ModelId id = m.id;
    const Position start = m.position;
    const Position drop = plus(start, in(3.0f), in(7.4f));  // about 8"
    const auto fixed = fix(session.state(), id, drop);
    ASSERT_TRUE(fixed.has_value());
    EXPECT_EQ(fixed->problem, DropProblem::TooFar);
    EXPECT_NEAR(centre_distance(start, fixed->to), in(6.0f), 0.01f);
    EXPECT_LT(off_line(fixed->to, start, drop), 0.01f);
    EXPECT_TRUE(accepted(session.state(), id, fixed->to));
    EXPECT_EQ(fixed->how, FixedDrop::How::AlongDrag);  // the nearest spot is 2" away, more than 1"
    EXPECT_EQ(fix_message(*fixed), "Moved as far as it can along the drag: it was further than the model can move");
    // Just too far: the nearest spot, which is the same point on the line.
    const Position slightly = plus(start, 0.0f, in(6.5f));
    const auto close_fix = fix(session.state(), id, slightly);
    ASSERT_TRUE(close_fix.has_value());
    EXPECT_EQ(close_fix->how, FixedDrop::How::Nearest);
    EXPECT_NEAR(close_fix->to.x, start.x, 0.01f);
    EXPECT_NEAR(close_fix->to.y, start.y + in(6.0f), 0.01f);
    EXPECT_EQ(fix_message(*close_fix), "Moved to the nearest spot it can go: it was further than the model can move");

    // 4" first, then a 4" line: it stops after 2" more.
    ASSERT_TRUE(session.submit(extend_path(session.state(), id, plus(start, 0.0f, in(4.0f)))).ok());
    const Position here = model_of(session.state(), id).position;
    const Position second = plus(here, in(4.0f), 0.0f);
    const auto again = fix(session.state(), id, second);
    ASSERT_TRUE(again.has_value());
    EXPECT_NEAR(centre_distance(here, again->to), in(2.0f), 0.01f);
    EXPECT_LT(off_line(again->to, here, second), 0.01f);
    ASSERT_TRUE(session.submit(extend_path(session.state(), id, again->to)).ok());
    const auto ruled = ruler(session.state(), id);
    ASSERT_TRUE(ruled.has_value());
    EXPECT_NEAR(ruled->moved_mm, in(6.0f), 0.02f);
    EXPECT_FALSE(ruled->over);
}

TEST(M36_FixDrop, ANormalDragOntoATermagantEndsInBaseContactOnTheLine) {
    GameSession session = normal_move(3.0f);
    const GameState& gs = session.state();
    const Model& m = gs.units[kMarines].models[middle(gs.units[kMarines])];
    const Model& target = closest_gant(gs, m.position);
    const auto fixed = fix(gs, m.id, target.position);
    ASSERT_TRUE(fixed.has_value());
    EXPECT_EQ(fixed->problem, DropProblem::ThroughEnemyModel);
    EXPECT_EQ(fixed->how, FixedDrop::How::AlongDrag);
    const float gap = signed_gap(gs, fixed->to, m.id, target.id);
    EXPECT_GE(gap, 0.0f);
    EXPECT_TRUE(in_base_contact(gap)) << gap;
    EXPECT_LT(off_line(fixed->to, m.position, target.position), 0.01f);
    EXPECT_TRUE(accepted(gs, m.id, fixed->to));  // ending engaged is the commit's business
    EXPECT_EQ(fix_message(*fixed), "Moved as far as it can along the drag: its line ran into an enemy model (03.01)");
}

TEST(M36_FixDrop, AChargeOntoATargetEndsInBaseContactAndThroughTheFrontRankStopsAtIt) {
    const GameState gs = charge_move();
    const Model& m = gs.units[kMarines].models[middle(gs.units[kMarines])];
    const Model& front = closest_gant(gs, m.position);
    const auto onto = fix(gs, m.id, front.position);
    ASSERT_TRUE(onto.has_value());
    EXPECT_EQ(onto->how, FixedDrop::How::AlongDrag);
    EXPECT_TRUE(in_base_contact(signed_gap(gs, onto->to, m.id, front.id)));
    EXPECT_GE(signed_gap(gs, onto->to, m.id, front.id), 0.0f);
    EXPECT_TRUE(accepted(gs, m.id, onto->to));  // 11.04: closer to the target

    // The Termagant behind the front one: the line stops at the first base it meets.
    const Model* back = nullptr;
    for (const Model& t : gs.units[kGants].models) {
        if (t.position.y > front.position.y + 10.0f &&
            (back == nullptr || std::fabs(t.position.x - front.position.x) < std::fabs(back->position.x - front.position.x))) {
            back = &t;
        }
    }
    ASSERT_NE(back, nullptr);
    const auto through = fix(gs, m.id, back->position);
    ASSERT_TRUE(through.has_value());
    EXPECT_EQ(through->how, FixedDrop::How::AlongDrag);
    EXPECT_LT(off_line(through->to, m.position, back->position), 0.01f);
    EXPECT_LT(through->to.y, front.position.y);  // in front of the front rank
    bool contact = false;
    for (const Model& t : gs.units[kGants].models) {
        const float g = signed_gap(gs, through->to, m.id, t.id);
        EXPECT_GE(g, 0.0f) << "on model " << t.id.v;
        contact = contact || in_base_contact(g);
    }
    EXPECT_TRUE(contact);
    EXPECT_TRUE(accepted(gs, m.id, through->to));
}

TEST(M36_FixDrop, AGapTooNarrowForTheBaseStopsTheModelBeforeIt) {
    GameState gs = wh40k::test::load_v1_scenario();
    const std::size_t i = middle(gs.units[kMarines]);
    const Position start = gs.units[kMarines].models[i].position;
    const float half_gap = 12.5f;  // a 25 mm gap for a 32 mm base
    const float offset = base_radius(gs.units[kGants].models[0].base) + half_gap;
    gs.units[kGants].models[0].position = plus(start, -offset, in(4.0f));
    gs.units[kGants].models[1].position = plus(start, offset, in(4.0f));
    GameSession session(gs);
    ASSERT_TRUE(session.submit(action::StartMove{gs.units[kMarines].id, MoveType::Normal, std::nullopt}).ok());
    const ModelId id = session.state().units[kMarines].models[i].id;
    const Position drop = plus(start, 0.0f, in(5.5f));  // beyond the gap
    const auto fixed = fix(session.state(), id, drop);
    ASSERT_TRUE(fixed.has_value());
    EXPECT_EQ(fixed->how, FixedDrop::How::AlongDrag);
    EXPECT_NEAR(fixed->to.x, start.x, 0.01f);  // on the line, not beside the gap
    EXPECT_LT(fixed->to.y, start.y + in(4.0f) - 10.0f);  // before the gap, not beyond it
    const GameState& s = session.state();
    const float g0 = signed_gap(s, fixed->to, id, s.units[kGants].models[0].id);
    const float g1 = signed_gap(s, fixed->to, id, s.units[kGants].models[1].id);
    EXPECT_GE(std::min(g0, g1), 0.0f);
    EXPECT_TRUE(in_base_contact(std::min(g0, g1)));
    EXPECT_TRUE(accepted(s, id, fixed->to));
}

TEST(M36_FixDrop, ADropOnAFriendlyModelsCentreStopsOnTheNearSide) {
    GameSession session = normal_move();
    const Unit& u = session.state().units[kMarines];
    const ModelId id = u.models[middle(u)].id;
    const ModelId other = u.models[rightmost(u)].id;
    const Position start = model_of(session.state(), id).position;
    ASSERT_TRUE(session.submit(extend_path(session.state(), other, plus(start, 0.0f, in(3.0f)))).ok());
    const Position friend_at = model_of(session.state(), other).position;
    const auto fixed = fix(session.state(), id, friend_at);
    ASSERT_TRUE(fixed.has_value());
    EXPECT_EQ(fixed->problem, DropProblem::OnModel);
    EXPECT_EQ(fixed->how, FixedDrop::How::AlongDrag);  // the nearest spot is more than 1" away
    EXPECT_LT(off_line(fixed->to, start, friend_at), 0.01f);
    EXPECT_LT(fixed->to.y, friend_at.y);
    const float gap = signed_gap(session.state(), fixed->to, id, other);
    EXPECT_GE(gap, 0.0f);
    EXPECT_TRUE(in_base_contact(gap));
    EXPECT_TRUE(accepted(session.state(), id, fixed->to));
}

// ==== The nearest spot ====

TEST(M36_FixDrop, OffTheEdgeStaysAtTheEdge) {
    GameSession session = normal_move(0.0f, 8.0f);  // the Intercessors 8" further east
    const GameState& gs = session.state();
    const Model& m = gs.units[kMarines].models[rightmost(gs.units[kMarines])];
    const Position drop = plus(m.position, in(1.2f), in(2.0f));  // its base 0.35" over the edge
    ASSERT_EQ(drop_problem(region_of(gs, m.id), drop), DropProblem::OffBattlefield);
    const auto fixed = fix(gs, m.id, drop);
    ASSERT_TRUE(fixed.has_value());
    EXPECT_EQ(fixed->problem, DropProblem::OffBattlefield);
    EXPECT_EQ(fixed->how, FixedDrop::How::Nearest);
    EXPECT_NEAR(fixed->to.y, drop.y, 0.01f);
    EXPECT_NEAR(fixed->to.x, gs.battlefield.width_mm - base_radius(m.base), 0.01f);
    EXPECT_TRUE(accepted(gs, m.id, fixed->to));
}

TEST(M36_FixDrop, AGlancingDropOnAFriendIsPushedStraightOut) {
    GameSession session = normal_move();
    const Unit& u = session.state().units[kMarines];
    const ModelId id = u.models[middle(u)].id;
    const ModelId other = u.models[rightmost(u)].id;
    const Position start = model_of(session.state(), id).position;
    ASSERT_TRUE(session.submit(extend_path(session.state(), other, plus(start, 0.0f, in(3.0f)))).ok());
    const Position friend_at = model_of(session.state(), other).position;
    const Position drop = plus(friend_at, 20.0f, 0.0f);  // 20 mm right of its centre
    const auto fixed = fix(session.state(), id, drop);
    ASSERT_TRUE(fixed.has_value());
    EXPECT_EQ(fixed->how, FixedDrop::How::Nearest);
    EXPECT_NEAR(fixed->to.y, friend_at.y, 0.01f);  // beside it, straight out from its centre
    EXPECT_NEAR(centre_distance(fixed->to, drop), 12.0f, 0.02f);
    EXPECT_TRUE(in_base_contact(signed_gap(session.state(), fixed->to, id, other)));
    EXPECT_TRUE(accepted(session.state(), id, fixed->to));
}

TEST(M36_FixDrop, APileInThatIsntCloserGoesToTheNearestCloserSpotAndOntoTheTargetGivesBaseContact) {
    const GameState gs = pile_in();
    const Unit& u = gs.units[kMarines];
    const Model& m = u.models[middle(u)];
    const Position back = plus(m.position, 0.0f, -in(0.3f));  // straight back, further from the Termagants
    ASSERT_FALSE(accepted(gs, m.id, back));
    const auto fixed = fix(gs, m.id, back);
    ASSERT_TRUE(fixed.has_value());
    EXPECT_EQ(fixed->problem, DropProblem::NotCloser);
    EXPECT_EQ(fixed->how, FixedDrop::How::Nearest);
    EXPECT_LE(centre_distance(fixed->to, back), kNearestSpotMaxMm);
    EXPECT_TRUE(accepted(gs, m.id, fixed->to));  // 12.03: ends closer

    const Model& target = closest_gant(gs, m.position);
    const auto onto = fix(gs, m.id, target.position);
    ASSERT_TRUE(onto.has_value());
    const float gap = signed_gap(gs, onto->to, m.id, target.id);
    EXPECT_GE(gap, 0.0f);
    EXPECT_TRUE(in_base_contact(gap));
    EXPECT_TRUE(accepted(gs, m.id, onto->to));
}

// ==== When nothing changes ====

TEST(M36_FixDrop, ALegalDropIsUntouchedAndNoSpotGivesNoFix) {
    GameSession session = normal_move();
    const Model& m = session.state().units[kMarines].models[middle(session.state().units[kMarines])];
    const Position legal = plus(m.position, in(1.0f), in(3.0f));
    EXPECT_FALSE(drop_problem(region_of(session.state(), m.id), legal).has_value());
    EXPECT_FALSE(fix(session.state(), m.id, legal).has_value());

    // A model against the east edge dragged straight east has nowhere to go.
    GameState gs = wh40k::test::load_v1_scenario();
    const std::size_t r = rightmost(gs.units[kMarines]);
    const float shift = gs.battlefield.width_mm - base_radius(gs.units[kMarines].models[r].base) -
                        gs.units[kMarines].models[r].position.x;
    for (Model& x : gs.units[kMarines].models) x.position.x += shift;
    GameSession edge(gs);
    ASSERT_TRUE(edge.submit(action::StartMove{gs.units[kMarines].id, MoveType::Normal, std::nullopt}).ok());
    const Model& e = edge.state().units[kMarines].models[r];
    const Position out = plus(e.position, in(2.0f), 0.0f);
    EXPECT_EQ(drop_problem(region_of(edge.state(), e.id), out), DropProblem::OffBattlefield);
    EXPECT_FALSE(fix(edge.state(), e.id, out).has_value());
}

// ==== Whole-unit drags ====

TEST(M36_FixDrop, AWholeUnitDragTooFarOrOffTheEdgeIsAcceptedForEveryModel) {
    for (const bool off_edge : {false, true}) {
        GameSession session = normal_move(0.0f, off_edge ? 8.0f : 0.0f);
        const Unit& u = session.state().units[kMarines];
        const Position grab = u.models[middle(u)].position;
        const Position drop = off_edge ? plus(grab, in(3.0f), 0.0f) : plus(grab, 0.0f, in(8.0f));
        const ReachRegion region = fix_region(unit_region(session.state(), u.id, grab));
        const auto fixed = fix_drop(region, grab, drop);
        ASSERT_TRUE(fixed.has_value()) << off_edge;
        EXPECT_EQ(fixed->problem, off_edge ? DropProblem::OffBattlefield : DropProblem::TooFar);
        EXPECT_LT(off_line(fixed->to, grab, drop), 0.01f);
        if (!off_edge) EXPECT_NEAR(centre_distance(grab, fixed->to), in(6.0f), 0.01f);
        const UnitId unit = u.id;
        for (const auto& a : translate_unit(session.state(), unit, fixed->to.x - grab.x, fixed->to.y - grab.y)) {
            EXPECT_TRUE(session.submit(a).ok()) << off_edge;
        }
        if (off_edge) {
            const Unit& moved = session.state().units[kMarines];
            const Model& e = moved.models[rightmost(moved)];
            EXPECT_NEAR(e.position.x, session.state().battlefield.width_mm - base_radius(e.base), 0.01f);
        }
    }
}

// ==== Over grids of drops ====

namespace {

// Every drop round each model: a fix is accepted by the engine, lies within 1" of the drop (nearest spot) or
// on the drag line (along the drag), and is the same when asked again. A drop the fix leaves alone is
// accepted too, unless the fix found no spot.
void check_grid(const GameState& gs, int* fixes) {
    for (const Model& m : gs.units[kMarines].models) {
        const auto reach = move_reach(gs, m.id);
        if (!reach) continue;
        const ReachRegion region = fix_region(model_region(*reach));
        for (float dx = -8.0f; dx <= 8.0f; dx += 0.5f) {
            for (float dy = -8.0f; dy <= 12.0f; dy += 0.5f) {
                const Position drop = plus(m.position, in(dx), in(dy));
                const auto fixed = fix_drop(region, m.position, drop);
                if (!fixed) {
                    if (!drop_problem(region, drop)) {
                        EXPECT_TRUE(accepted(gs, m.id, drop)) << m.id.v << ": " << dx << ", " << dy;
                    }
                    continue;
                }
                ++*fixes;
                EXPECT_TRUE(accepted(gs, m.id, fixed->to)) << m.id.v << ": " << dx << ", " << dy;
                if (fixed->how == FixedDrop::How::Nearest) {
                    EXPECT_LE(centre_distance(fixed->to, drop), kNearestSpotMaxMm + 0.01f) << dx << ", " << dy;
                } else {
                    EXPECT_LT(off_line(fixed->to, m.position, drop), 0.01f) << dx << ", " << dy;
                    EXPECT_LE(centre_distance(m.position, fixed->to), centre_distance(m.position, drop) + 0.01f);
                }
                const auto again = fix_drop(region, m.position, drop);
                ASSERT_TRUE(again.has_value());
                EXPECT_EQ(again->to, fixed->to);
            }
        }
    }
}

}  // namespace

TEST(M36_FixDrop, EveryFixInANormalMoveNearTheTermagantsIsAccepted) {
    GameSession session = normal_move(3.0f);
    int fixes = 0;
    check_grid(session.state(), &fixes);
    EXPECT_GT(fixes, 1000);
}

TEST(M36_FixDrop, EveryFixInAChargeIsAccepted) {
    int fixes = 0;
    check_grid(charge_move(), &fixes);
    EXPECT_GT(fixes, 1000);
}

TEST(M36_FixDrop, EveryFixInAPileInIsAccepted) {
    int fixes = 0;
    check_grid(pile_in(), &fixes);
    EXPECT_GT(fixes, 1000);
}
