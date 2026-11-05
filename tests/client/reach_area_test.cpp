// M27: the shaded area a moving model can end in (D-040): core's move_reach as spans.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/state/queries.hpp"
#include "reach_area.hpp"
#include "session.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::in;

namespace {

bool covered(const std::vector<ReachSpan>& spans, float x, float y, float step) {
    return std::any_of(spans.begin(), spans.end(), [&](const ReachSpan& s) {
        return std::fabs(s.y - y) <= step * 0.5f && x >= s.x0 && x <= s.x1;
    });
}

MoveReach simple_reach() {
    MoveReach r;
    r.start = Position{100.0f, 100.0f, 0.0f};
    r.max_mm = 50.0f;
    r.centre_bounds = Rect{0.0f, 0.0f, 130.0f, 1000.0f};  // the east edge cuts the circle at x = 130
    r.forbidden.push_back(ReachCircle{Position{100.0f, 130.0f, 0.0f}, 10.0f, true});
    return r;
}

}  // namespace

TEST(M27_ReachSpans, TheCircleMinusTheForbiddenCirclesInsideTheBoard) {
    const MoveReach r = simple_reach();
    const float step = 1.0f;
    const auto spans = reach_spans(r, step);
    ASSERT_FALSE(spans.empty());
    EXPECT_TRUE(covered(spans, 100.0f, 100.0f, step));    // the start
    EXPECT_TRUE(covered(spans, 60.0f, 100.0f, step));     // 40 mm west: inside the circle
    EXPECT_FALSE(covered(spans, 40.0f, 100.0f, step));    // 60 mm west: beyond the maximum distance
    EXPECT_FALSE(covered(spans, 135.0f, 100.0f, step));   // past the board edge
    EXPECT_FALSE(covered(spans, 100.0f, 130.0f, step));   // inside the forbidden circle
    EXPECT_TRUE(covered(spans, 100.0f, 141.0f, step));    // just beyond it, still within reach
    for (const ReachSpan& s : spans) {
        EXPECT_LT(s.x0, s.x1);
        EXPECT_LE(s.x1, 130.0f);
        const float mid = (s.x0 + s.x1) * 0.5f;
        EXPECT_TRUE(in_reach(r, Position{mid, s.y, 0.0f})) << mid << ", " << s.y;
    }
}

TEST(M27_ReachSpans, ANonPositiveStepGivesNothing) {
    EXPECT_TRUE(reach_spans(simple_reach(), 0.0f).empty());
    EXPECT_TRUE(reach_spans(simple_reach(), -1.0f).empty());
}

// The area agrees with the engine: a point in it can be reached in a straight line and ends unengaged; a
// point in an enemy's engagement circle is refused at the end of the move.
TEST(M27_ReachArea, PointsInTheAreaEndUnengagedAndPointsInAnEngagementCircleDoNot) {
    GameState gs = wh40k::test::load_v1_scenario();
    const float gap = *unit_distance(gs.units[0], gs.units[1]);
    for (auto& m : gs.units[1].models) m.position.y -= gap - in(5);  // the Termagants 5" away
    GameSession session(gs);
    const UnitId marines = session.state().units[0].id;
    ASSERT_TRUE(session.submit(action::StartMove{marines, MoveType::Normal, std::nullopt}).ok());
    const Model& m = session.state().units[0].models[2];  // the middle Intercessor
    const auto reach = move_reach(session.state(), m.id);
    ASSERT_TRUE(reach.has_value());
    const auto spans = reach_spans(*reach, in(0.25f));
    ASSERT_FALSE(spans.empty());

    const auto ends_engaged = [&](const Position& to) {
        GameState g = session.state();
        EXPECT_TRUE(validate_action(g, Side::A, action::ProposeModelMove{m.id, {to}}).ok());
        apply_action(g, Side::A, action::ProposeModelMove{m.id, {to}});
        const auto problems = move_end_problems(g);
        return std::any_of(problems.begin(), problems.end(),
                           [](const std::string& p) { return p.find("engaged") != std::string::npos; });
    };
    // The furthest point straight towards the Termagants (+y) that is still in the area.
    const ReachSpan* best = nullptr;
    for (const ReachSpan& s : spans) {
        if (s.x0 <= m.position.x && m.position.x <= s.x1 && (best == nullptr || s.y > best->y)) best = &s;
    }
    ASSERT_NE(best, nullptr);
    ASSERT_GT(best->y, m.position.y + in(2));  // it really moves towards them
    EXPECT_FALSE(ends_engaged(Position{m.position.x, best->y, 0.0f}));
    // 6" straight on is past the edge of the area: within 2" of a Termagant.
    const Position too_far{m.position.x, m.position.y + in(4.5f), 0.0f};
    EXPECT_FALSE(in_reach(*reach, too_far));
    EXPECT_TRUE(ends_engaged(too_far));
}
