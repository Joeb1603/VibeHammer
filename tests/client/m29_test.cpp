// M29: the selection follows the board (D-043); the shaded area leaves out spots behind enemy models (D-044).

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/state/queries.hpp"
#include "board_interaction.hpp"
#include "reach_area.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::in;

TEST(M29_Selection, StaysOnlyOnTheActorTheMovingUnitOrTheMenusUnit) {
    const UnitId a{1}, b{2};
    EXPECT_FALSE(keep_selection(std::nullopt, a, a, a));
    EXPECT_TRUE(keep_selection(a, a, std::nullopt, std::nullopt));   // the picked shooter, charger, ...
    EXPECT_TRUE(keep_selection(a, std::nullopt, a, std::nullopt));   // the unit moving
    EXPECT_TRUE(keep_selection(b, a, std::nullopt, b));              // an enemy whose weapon menu is open
    EXPECT_FALSE(keep_selection(b, a, std::nullopt, std::nullopt));  // an enemy clicked for nothing
    EXPECT_FALSE(keep_selection(a, std::nullopt, std::nullopt, std::nullopt));  // the actor was cancelled
}

TEST(M29_Selection, CancellingTheShooterDropsItAsTheActor) {
    GameSession session(wh40k::test::shoot_step_state());
    const GameState& gs = session.state();
    BoardInteraction board(gs, session.legal());
    const UnitId marines = gs.units[0].id;
    (void)board.click(gs, marines);
    EXPECT_TRUE(keep_selection(marines, board.actor(), std::nullopt, std::nullopt));
    (void)board.click(gs, marines);  // clicking the shooter again picks another unit
    EXPECT_FALSE(keep_selection(marines, board.actor(), std::nullopt, std::nullopt));
}

TEST(M29_Shadow, TheAreaBehindADiscIsLeftOutButBesideItIsNot) {
    ReachRegion r;
    r.bounds = Rect{0.0f, 0.0f, 1000.0f, 1000.0f};
    r.inside_all.push_back(ReachCircle{Position{100.0f, 100.0f, 0.0f}, 80.0f, false, {}});
    r.outside_shadows.push_back(Shadow{Position{100.0f, 100.0f, 0.0f}, ReachCircle{Position{140.0f, 100.0f, 0.0f}, 10.0f, false, {}}});
    EXPECT_TRUE(in_region(r, Position{120.0f, 100.0f, 0.0f}));   // in front of the disc
    EXPECT_FALSE(in_region(r, Position{140.0f, 100.0f, 0.0f}));  // the disc
    EXPECT_FALSE(in_region(r, Position{170.0f, 100.0f, 0.0f}));  // behind it
    EXPECT_FALSE(in_region(r, Position{170.0f, 108.0f, 0.0f}));  // behind it, off-centre
    EXPECT_TRUE(in_region(r, Position{170.0f, 125.0f, 0.0f}));   // past it, outside its shadow
    EXPECT_TRUE(in_region(r, Position{140.0f, 115.0f, 0.0f}));   // beside it
    EXPECT_TRUE(in_region(r, Position{60.0f, 100.0f, 0.0f}));    // the other way
    const float step = 0.5f;
    const auto spans = reach_spans(r, step);
    ASSERT_FALSE(spans.empty());
    for (const ReachSpan& s : spans) {
        const float mid = (s.x0 + s.x1) * 0.5f;
        EXPECT_TRUE(in_region(r, Position{mid, s.y, 0.0f})) << mid << ", " << s.y;
    }
}

namespace {

// Every grid point round `model` whose neighbourhood is wholly in (or out of) its region: those in it are
// accepted by the engine as the model's next straight line; those left out only by a shadow are refused.
struct Agreement {
    int inside = 0;
    int shadowed = 0;
};
Agreement check_against_engine(const GameState& gs, ModelId model) {
    Agreement out;
    const auto reach = move_reach(gs, model);
    EXPECT_TRUE(reach.has_value());
    if (!reach) return out;
    const ReachRegion region = model_region(*reach);
    ReachRegion no_shadows = region;
    no_shadows.outside_shadows.clear();
    const auto stable = [](const ReachRegion& r, const Position& p) {
        const bool in = in_region(r, p);
        for (const auto& d : {std::pair{0.1f, 0.0f}, {-0.1f, 0.0f}, {0.0f, 0.1f}, {0.0f, -0.1f}}) {
            if (in_region(r, Position{p.x + d.first, p.y + d.second, 0.0f}) != in) return false;
        }
        return true;
    };
    for (float dx = -3.2f; dx <= 3.2f; dx += 0.2f) {
        for (float dy = -3.2f; dy <= 3.2f; dy += 0.2f) {
            const Position p{reach->from.x + in(dx), reach->from.y + in(dy), 0.0f};
            if (!stable(region, p) || !stable(no_shadows, p)) continue;
            const bool ok = validate_action(gs, Side::A, action::ProposeModelMove{model, {p}}).ok();
            if (in_region(region, p)) {
                EXPECT_TRUE(ok) << "shaded but refused: " << dx << ", " << dy;
                ++out.inside;
            } else if (in_region(no_shadows, p)) {
                EXPECT_FALSE(ok) << "behind an enemy but accepted: " << dx << ", " << dy;
                ++out.shadowed;
            }
        }
    }
    return out;
}

}  // namespace

TEST(M29_ReachArea, EveryShadedPileInSpotIsAcceptedAndSpotsBehindEnemiesAreLeftOut) {
    GameState gs = wh40k::test::after_successful_charge();
    apply_action(gs, Side::A, action::EndChargeStep{});
    apply_action(gs, Side::A, action::StartPileIn{gs.units[0].id, {gs.units[1].id}});
    int shadowed = 0;
    for (const Model& m : gs.units[0].models) {
        const Agreement a = check_against_engine(gs, m.id);
        EXPECT_GT(a.inside, 0) << "model " << m.id.v;
        shadowed += a.shadowed;
    }
    EXPECT_GT(shadowed, 0);  // the Termagants do hide some spots the pile-in rules alone would allow
}

TEST(M29_ReachArea, EveryShadedNormalMoveSpotIsAccepted) {
    GameState gs = wh40k::test::load_v1_scenario();
    const float gap = *unit_distance(gs.units[0], gs.units[1]);
    for (auto& m : gs.units[1].models) m.position.y -= gap - in(5);
    start_game(gs);
    apply_action(gs, Side::A, action::StartMove{gs.units[0].id, MoveType::Normal, std::nullopt});
    for (const Model& m : gs.units[0].models) EXPECT_GT(check_against_engine(gs, m.id).inside, 0);
}
