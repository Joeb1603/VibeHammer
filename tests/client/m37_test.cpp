// M37: the shaded area's Engagement Range part (D-053) and dice pop-ups with nothing to roll (D-053).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <variant>
#include <vector>

#include "board_interaction.hpp"
#include "casualty_plan.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/state/queries.hpp"
#include "decision_view.hpp"
#include "dice_cards.hpp"
#include "drop_fix.hpp"
#include "reach_area.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::in;

namespace {

bool covered(const std::vector<ReachSpan>& spans, const Position& p, float step) {
    return std::any_of(spans.begin(), spans.end(), [&](const ReachSpan& s) {
        return std::fabs(s.y - p.y) <= step * 0.5f && p.x >= s.x0 && p.x <= s.x1;
    });
}

GameSession normal_move(float gap_in) {
    GameState gs = wh40k::test::load_v1_scenario();
    if (gap_in > 0.0f) {
        const float gap = *unit_distance(gs.units[0], gs.units[1]);
        for (Model& m : gs.units[1].models) m.position.y -= gap - in(gap_in);
    }
    GameSession session(gs);
    EXPECT_TRUE(session.submit(action::StartMove{session.state().units[0].id, MoveType::Normal, std::nullopt}).ok());
    return session;
}

}  // namespace

TEST(M37_EngagedArea, TheRedPartIsWithinEngagementRangeAndTheEngineAcceptsItButTheCommitDoesNot) {
    GameSession session = normal_move(3.0f);
    const GameState& gs = session.state();
    const Model& m = gs.units[0].models[2];
    const ReachRegion region = model_region(*move_reach(gs, m.id));
    const float step = 0.5f;
    const auto blue = reach_spans(region, step);
    const auto red = engaged_spans(region, step);
    ASSERT_FALSE(red.empty());

    // The Termagant straight ahead.
    const Model* t = &gs.units[1].models[0];
    for (const Model& x : gs.units[1].models) {
        if (centre_distance(m.position, x.position) < centre_distance(m.position, t->position)) t = &x;
    }
    const float touching = base_radius(m.base) + base_radius(t->base);
    const float dx = t->position.x - m.position.x, dy = t->position.y - m.position.y;
    const float d = std::hypot(dx, dy);
    const auto towards = [&](float gap_mm) {  // on the line to the Termagant, `gap_mm` from its base
        const float k = (d - touching - gap_mm) / d;
        return Position{m.position.x + dx * k, m.position.y + dy * k, 0.0f};
    };
    const Position engaged = towards(in(1.0f));
    EXPECT_TRUE(covered(red, engaged, step));
    EXPECT_FALSE(covered(blue, engaged, step));
    const Position free = towards(in(2.5f));
    EXPECT_TRUE(covered(blue, free, step));
    EXPECT_FALSE(covered(red, free, step));
    const Position on_base = t->position;
    EXPECT_FALSE(covered(red, on_base, step));
    EXPECT_FALSE(covered(blue, on_base, step));

    // The red spans don't overlap the blue ones, and their middles are accepted but end engaged.
    int checked = 0;
    for (std::size_t i = 0; i < red.size(); i += 7) {
        const ReachSpan& s = red[i];
        if (s.x1 - s.x0 < 1.0f) continue;
        const Position mid{(s.x0 + s.x1) * 0.5f, s.y, 0.0f};
        EXPECT_FALSE(covered(blue, mid, 0.01f));
        GameState g = gs;
        ASSERT_TRUE(validate_action(g, Side::A, action::ProposeModelMove{m.id, {mid}}).ok()) << mid.x << ", " << mid.y;
        apply_action(g, Side::A, action::ProposeModelMove{m.id, {mid}});
        const auto problems = move_end_problems(g);
        EXPECT_TRUE(std::any_of(problems.begin(), problems.end(),
                                [](const std::string& p) { return p.find("unengaged") != std::string::npos; }));
        ++checked;
    }
    EXPECT_GT(checked, 5);
}

TEST(M37_EngagedArea, NothingRedWithNoEnemyNear) {
    GameSession session = normal_move(0.0f);  // the V1 start: the Termagants about 12" away
    const Model& m = session.state().units[0].models[2];
    EXPECT_TRUE(engaged_spans(model_region(*move_reach(session.state(), m.id)), 0.5f).empty());
}

TEST(M37_DiceCards, AWoundCardWithNoHitsNeedsNoRoll) {
    GameState gs = wh40k::test::shoot_step_state();
    BoardInteraction board(gs, legal_actions(gs, Side::A));
    (void)board.click(gs, gs.units[0].id);
    const ClickResult r = board.click(gs, gs.units[1].id);
    ASSERT_FALSE(r.menu.empty());
    ScriptedDice misses(std::vector<int>(400, 1));  // every hit roll fails
    std::vector<LogEntry> log;
    log.push_back(LogEntry{"shoot", apply_action(gs, Side::A, r.menu.front().action, misses)});
    for (int i = 0; i < 50 && gs.pending && !std::holds_alternative<decision::Shoot>(gs.pending->kind); ++i) {
        const DecisionView view = make_decision_view(legal_actions(gs, gs.pending->side));
        GameAction a = view.buttons.empty() ? GameAction{} : view.buttons.front();
        for (const auto& t : view.templates) {
            if (const auto* c = std::get_if<action::OrderCasualties>(&t)) a = CasualtyPlan(gs, c->target).action();
        }
        log.push_back(LogEntry{"step", apply_action(gs, gs.pending->side, a, misses)});
    }
    const std::vector<DiceCard> cards = dice_cards(gs, log, 0);
    ASSERT_FALSE(cards.empty());
    EXPECT_EQ(cards[0].title, "Hit rolls");
    EXPECT_TRUE(needs_roll(cards[0]));
    const auto wound = std::find_if(cards.begin(), cards.end(), [](const DiceCard& c) { return c.title == "Wound rolls"; });
    ASSERT_NE(wound, cards.end());
    EXPECT_FALSE(needs_roll(*wound));
    EXPECT_EQ(wound->result, "No hits to roll for");
}
