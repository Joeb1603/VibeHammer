// M28: the movement area follows each step and covers whole-unit drags (D-041); Cancel when picking a
// shooting target; removing a model for coherency by clicking it (D-042).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <variant>
#include <vector>

#include "board_interaction.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/state/queries.hpp"
#include "move_plan.hpp"
#include "reach_area.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::in;

namespace {

// The V1 scenario with the Termagants 5" from the Intercessors, Side A's Normal move started.
GameSession normal_move_near_enemies() {
    GameState gs = wh40k::test::load_v1_scenario();
    const float gap = *unit_distance(gs.units[0], gs.units[1]);
    for (auto& m : gs.units[1].models) m.position.y -= gap - in(5);
    GameSession session(gs);
    EXPECT_TRUE(session.submit(action::StartMove{session.state().units[0].id, MoveType::Normal, std::nullopt}).ok());
    return session;
}

Position centroid(const Unit& u) {
    Position c{0, 0, 0};
    for (const Model& m : u.models) {
        c.x += m.position.x;
        c.y += m.position.y;
    }
    c.x /= static_cast<float>(u.models.size());
    c.y /= static_cast<float>(u.models.size());
    return c;
}

// Drops the whole unit with the mouse at `to` after grabbing it at `grab`: every proposal is valid, and
// whether the move would then end engaged.
struct UnitDrop {
    bool all_valid = true;
    bool ends_engaged = false;
};
UnitDrop drop_unit(const GameSession& session, const Position& grab, const Position& to) {
    GameState g = session.state();
    UnitDrop out;
    for (const auto& a : translate_unit(g, g.units[0].id, to.x - grab.x, to.y - grab.y)) {
        if (!validate_action(g, Side::A, a).ok()) {
            out.all_valid = false;
            continue;
        }
        apply_action(g, Side::A, a);
    }
    for (const std::string& p : move_end_problems(g)) {
        if (p.find("engaged") != std::string::npos) out.ends_engaged = true;
    }
    return out;
}

}  // namespace

TEST(M28_ReachRegion, AnInsideAnyGroupKeepsOnlyWhatIsInOneOfItsCircles) {
    ReachRegion r;
    r.bounds = Rect{0.0f, 0.0f, 1000.0f, 1000.0f};
    r.inside_all.push_back(ReachCircle{Position{100.0f, 100.0f, 0.0f}, 50.0f, false, {}});
    r.inside_any.push_back({ReachCircle{Position{70.0f, 100.0f, 0.0f}, 10.0f, false, {}},
                            ReachCircle{Position{130.0f, 100.0f, 0.0f}, 10.0f, false, {}}});
    const auto spans = reach_spans(r, 1.0f);
    ASSERT_FALSE(spans.empty());
    for (const ReachSpan& s : spans) {
        const float mid = (s.x0 + s.x1) * 0.5f;
        EXPECT_TRUE(std::fabs(mid - 70.0f) < 10.0f || std::fabs(mid - 130.0f) < 10.0f) << mid;
        EXPECT_TRUE(in_region(r, Position{mid, s.y, 0.0f}));
    }
    EXPECT_FALSE(in_region(r, Position{100.0f, 100.0f, 0.0f}));  // in the circle, but in neither group circle
    r.empty = true;
    EXPECT_TRUE(reach_spans(r, 1.0f).empty());
}

TEST(M28_ReachRegion, AModelsAreaMovesWithItAfterEachDrag) {
    GameSession session = normal_move_near_enemies();
    const Model& m = session.state().units[0].models[0];
    const ModelId id = m.id;
    const Position before = m.position;
    ASSERT_TRUE(session.submit(extend_path(session.state(), id, Position{before.x - in(2), before.y, 0})).ok());
    const auto reach = move_reach(session.state(), id);
    ASSERT_TRUE(reach.has_value());
    const ReachRegion region = model_region(*reach);
    ASSERT_EQ(region.inside_all.size(), 1u);
    const Position now = session.state().units[0].models[0].position;
    EXPECT_EQ(region.inside_all[0].centre, now);
    EXPECT_NEAR(region.inside_all[0].radius_mm, *max_move_distance_mm(session.state(), id) - in(2), 0.01f);
    // 4.5" further west is beyond what's left (6" - 2"); 3.5" is within it.
    EXPECT_FALSE(in_region(region, Position{now.x - in(4.5f), now.y, 0}));
    EXPECT_TRUE(in_region(region, Position{now.x - in(3.5f), now.y, 0}));
}

TEST(M28_ReachRegion, AWholeUnitDragsAreaAgreesWithTheEngine) {
    GameSession session = normal_move_near_enemies();
    const Unit& u = session.state().units[0];
    const Position grab = centroid(u);
    const ReachRegion region = unit_region(session.state(), u.id, grab);
    ASSERT_FALSE(region.empty);
    const auto spans = reach_spans(region, in(0.1f));
    ASSERT_FALSE(spans.empty());
    EXPECT_TRUE(in_region(region, grab));  // not moving at all is fine
    // The furthest drop straight towards the Termagants (+y) still in the area.
    float best_y = grab.y;
    for (const ReachSpan& s : spans) {
        if (s.x0 <= grab.x && grab.x <= s.x1) best_y = std::max(best_y, s.y);
    }
    ASSERT_GT(best_y, grab.y + in(2));
    const UnitDrop ok = drop_unit(session, grab, Position{grab.x, best_y, 0});
    EXPECT_TRUE(ok.all_valid);
    EXPECT_FALSE(ok.ends_engaged);
    // 4.5" straight on would leave the front models within 2" of the Termagants.
    const Position too_far{grab.x, grab.y + in(4.5f), 0};
    EXPECT_FALSE(in_region(region, too_far));
    const UnitDrop bad = drop_unit(session, grab, too_far);
    EXPECT_TRUE(!bad.all_valid || bad.ends_engaged);
    // Sideways beyond the move distance: some model can't go that far.
    const Position sideways{grab.x - in(6.5f), grab.y, 0};
    EXPECT_FALSE(in_region(region, sideways));
    EXPECT_FALSE(drop_unit(session, grab, sideways).all_valid);
}

TEST(M28_ShootingCancel, PickingATargetCanBeCancelled) {
    GameSession session(wh40k::test::shoot_step_state());
    const GameState& gs = session.state();
    BoardInteraction board(gs, session.legal());
    EXPECT_FALSE(board.can_cancel());
    (void)board.click(gs, gs.units[0].id);
    ASSERT_TRUE(board.picking_target());
    EXPECT_TRUE(board.can_cancel());
    board.cancel();
    EXPECT_FALSE(board.actor().has_value());
    EXPECT_FALSE(board.picking_target());
    EXPECT_FALSE(board.can_cancel());
    EXPECT_TRUE(board.clickable(gs.units[0].id));  // back to picking a shooter
}

namespace {

// R03_03's setup: the two westernmost Intercessors wander off, so Side A must remove models at the end
// of its turn.
GameSession out_of_coherency() {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (gs.flow.stage != Stage::Charge) {
        apply_action(gs, gs.pending->side, legal_actions(gs, gs.pending->side).front());
    }
    for (std::size_t i = 0; i < 2; ++i) {
        gs.units[0].models[i].position.x -= in(5);
        gs.units[0].models[i].position.y += in(6);
    }
    GameSession session(gs);
    EXPECT_TRUE(session.submit(action::EndChargeStep{}).ok());
    return session;
}

}  // namespace

TEST(M28_CoherencyRemoval, AModelIsClickedThenConfirmedWithNoModelButtons) {
    GameSession session = out_of_coherency();
    const GameState& gs = session.state();
    ASSERT_TRUE(std::holds_alternative<decision::RestoreCoherency>(session.pending()->kind));
    const UnitId marines = gs.units[0].id;
    BoardInteraction board(gs, session.legal());
    ASSERT_TRUE(board.active());
    EXPECT_TRUE(board.picking_model());
    EXPECT_TRUE(board.step_buttons().empty());  // no button per model
    EXPECT_TRUE(board.clickable(marines));
    EXPECT_FALSE(board.clickable(gs.units[1].id));
    EXPECT_FALSE(board.confirm().has_value());  // nothing chosen yet
    EXPECT_NE(board.hint(gs).find("click a model to remove"), std::string::npos) << board.hint(gs);

    const ModelId first = gs.units[0].models[0].id;
    const ModelId second = gs.units[0].models[1].id;
    (void)board.click_model(gs, marines, first);
    EXPECT_EQ(board.chosen_model(), first);
    (void)board.click_model(gs, marines, second);  // a second click changes the choice
    EXPECT_EQ(board.chosen_model(), second);
    const auto confirm = board.confirm();
    ASSERT_TRUE(confirm.has_value());
    EXPECT_EQ(*confirm, (GameAction{action::RemoveModelForCoherency{marines, second}}));
    EXPECT_TRUE(session.submit(*confirm).ok());
}
