// M25: quality-of-life fixes (D-038): Cancel on move menus, the end-of-shooting warning's query,
// automatic damage allocation and the victory pop-up's text.

#include <gtest/gtest.h>

#include <variant>
#include <vector>

#include "auto_actions.hpp"
#include "board_interaction.hpp"
#include "core/engine/engine.hpp"
#include "core/state/queries.hpp"
#include "phase_track.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::in;

namespace {

// The V1 scenario with the Termagants moved to 1" from the Intercessors: both units start engaged.
GameState engaged_at_start() {
    GameState gs = wh40k::test::load_v1_scenario_closer();
    const float gap = *unit_distance(gs.units[0], gs.units[1]);
    for (auto& m : gs.units[1].models) m.position.y -= gap - in(1);
    return gs;
}

// Side A's Intercessors shoot the Termagants with the first weapon choice (Bolt Rifle - Focused Fire).
void shoot_first_choice(GameSession& session) {
    const GameState& gs = session.state();
    BoardInteraction board(gs, session.legal());
    (void)board.click(gs, gs.units[0].id);
    const ClickResult r = board.click(gs, gs.units[1].id);
    ASSERT_FALSE(r.menu.empty());
    ASSERT_TRUE(session.submit(r.menu.front().action).ok());
}

}  // namespace

TEST(M25_MoveMenu, CancelPicksNothingAndDropsTheUnit) {
    GameSession session(wh40k::test::load_v1_scenario());
    BoardInteraction board(session.state(), session.legal());
    const ClickResult r = board.click(session.state(), session.state().units[0].id);
    ASSERT_FALSE(r.menu.empty());
    ASSERT_TRUE(r.menu.back().cancel);
    const ClickResult picked = board.choose(session.state(), r.menu.back());
    EXPECT_FALSE(picked.submit.has_value());
    EXPECT_TRUE(picked.menu.empty());
    EXPECT_FALSE(board.actor().has_value());
    EXPECT_TRUE(session.history().empty());
}

TEST(M25_MoveMenu, AnEngagedUnitOffersFallBackAndCancelInsteadOfMovingAtOnce) {
    GameSession session(engaged_at_start());
    ASSERT_TRUE(std::holds_alternative<decision::MoveUnits>(session.pending()->kind));
    ASSERT_TRUE(is_engaged(session.state(), session.state().units[0]));
    BoardInteraction board(session.state(), session.legal());
    const ClickResult r = board.click(session.state(), session.state().units[0].id);
    EXPECT_FALSE(r.submit.has_value());  // a Fall Back move is not made by the click itself
    ASSERT_GE(r.menu.size(), 2u);
    for (std::size_t i = 0; i + 1 < r.menu.size(); ++i) {
        const auto* m = std::get_if<action::StartMove>(&r.menu[i].action);
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->move_type, MoveType::FallBack);
        EXPECT_FALSE(r.menu[i].cancel);
    }
    EXPECT_TRUE(r.menu.back().cancel);
    EXPECT_EQ(r.menu.back().label, "Cancel");
}

TEST(M25_EndShootingWarning, ListsTheUnitsThatStillHaveAShot) {
    const GameState gs = wh40k::test::shoot_step_state();
    EXPECT_EQ(units_that_can_still_shoot(legal_actions(gs, Side::A)), (std::vector<UnitId>{gs.units[0].id}));
    GameState far = gs;
    for (auto& m : far.units[1].models) m.position.y += in(30);  // out of range: nothing can shoot
    EXPECT_TRUE(units_that_can_still_shoot(legal_actions(far, Side::A)).empty());
}

TEST(M25_AutoAllocate, AnAttackResolvesWithoutTheDefendersAllocationAndCasualtyDecisions) {
    GameSession session(wh40k::test::shoot_step_state());
    shoot_first_choice(session);
    const AutoSettings settings{false, true};
    for (int guard = 0; guard < 50 && session.pending(); ++guard) {
        (void)run_auto_actions(session, settings);
        const auto& kind = session.pending()->kind;
        EXPECT_FALSE(std::holds_alternative<decision::DeclareAllocationOrder>(kind));
        EXPECT_FALSE(std::holds_alternative<decision::OrderCasualties>(kind));
        if (std::holds_alternative<decision::Shoot>(kind)) break;  // back to the attacker's next choice
        // The attacker still picks which batch to resolve (04.03): that is not damage allocation.
        ASSERT_TRUE(std::holds_alternative<decision::SelectAttackBatch>(kind));
        ASSERT_TRUE(session.submit(session.legal().front()).ok());
    }
    ASSERT_TRUE(std::holds_alternative<decision::Shoot>(session.pending()->kind));
    const GameSession rebuilt = GameSession::from_steps(session.initial(), session.history());
    EXPECT_EQ(rebuilt.state(), session.state());
}

TEST(M25_AutoAllocate, OffLeavesTheDecisionsToThePlayer) {
    GameState gs = wh40k::test::shoot_step_state();
    const auto legal = legal_actions(gs, Side::A);
    EXPECT_FALSE(next_auto_action(gs, legal, AutoSettings{false, false}).has_value());
}

TEST(M25_Victory, TheTextNamesTheWinnerAndTheReason) {
    GameState gs = wh40k::test::load_v1_scenario();
    EXPECT_FALSE(game_over_text(gs).has_value());
    gs.result = GameResult{Side::B, "D-007: every enemy unit has been destroyed"};
    const auto win = game_over_text(gs);
    ASSERT_TRUE(win.has_value());
    EXPECT_EQ(win->title, "Side B (" + gs.side(Side::B).name + ") wins!");
    EXPECT_EQ(win->reason, "D-007: every enemy unit has been destroyed");
    EXPECT_EQ(win->winner, Side::B);
    gs.result = GameResult{std::nullopt, "D-007: the last battle round ended"};
    const auto draw = game_over_text(gs);
    ASSERT_TRUE(draw.has_value());
    EXPECT_EQ(draw->title, "Draw");
    EXPECT_FALSE(draw->winner.has_value());
}
