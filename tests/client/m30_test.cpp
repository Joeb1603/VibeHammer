// M30: Cancel on every click menu and before a charge roll (D-045); a kept advance roll is not shown as a
// new roll (D-045); the shaded area for charge moves (D-046).

#include <gtest/gtest.h>

#include <variant>
#include <vector>

#include "action_text.hpp"
#include "board_interaction.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/state/queries.hpp"
#include "dice_cards.hpp"
#include "reach_area.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::events_of;
using wh40k::test::in;

TEST(M30_Cancel, AWeaponMenusCancelKeepsPickingATargetForTheSameShooter) {
    GameSession session(wh40k::test::shoot_step_state());
    const GameState& gs = session.state();
    BoardInteraction board(gs, session.legal());
    (void)board.click(gs, gs.units[0].id);
    const ClickResult r = board.click(gs, gs.units[1].id);
    ASSERT_FALSE(r.menu.empty());
    ASSERT_TRUE(r.menu.back().cancel);
    EXPECT_EQ(r.menu.back().label, "Cancel");
    const ClickResult picked = board.choose(gs, r.menu.back());
    EXPECT_FALSE(picked.submit.has_value());
    EXPECT_EQ(board.actor(), gs.units[0].id);  // still the shooter
    EXPECT_TRUE(board.picking_target());
    EXPECT_TRUE(session.history().empty());
}

TEST(M30_Cancel, AMeleeWeaponMenuEndsWithCancelAndKeepsPickingATarget) {
    GameSession session(wh40k::test::after_successful_charge());
    ASSERT_TRUE(session.submit(action::EndChargeStep{}).ok());
    while (session.state().flow.stage == Stage::PileIn) ASSERT_TRUE(session.submit(action::EndPileIns{}).ok());
    ASSERT_TRUE(session.submit(action::SelectToFight{session.state().units[0].id, FightType::Normal}).ok());
    const GameState& gs = session.state();
    BoardInteraction board(gs, session.legal());
    const ClickResult r = board.click(gs, gs.units[1].id);
    ASSERT_FALSE(r.menu.empty());
    ASSERT_TRUE(r.menu.back().cancel);
    (void)board.choose(gs, r.menu.back());
    EXPECT_TRUE(board.picking_target());
    EXPECT_EQ(board.actor(), gs.units[0].id);
}

TEST(M30_Cancel, DeclaringAChargeAsksFirstAndCancelDeclaresNothing) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (!(gs.flow.stage == Stage::Charge && turn_side(gs) == Side::A)) {
        apply_action(gs, wh40k::test::decider(gs), legal_actions(gs, wh40k::test::decider(gs)).front());
    }
    for (auto& m : gs.units[1].models) m.position.y -= in(3);
    BoardInteraction board(gs, legal_actions(gs, Side::A));
    const ClickResult r = board.click(gs, gs.units[0].id);
    EXPECT_FALSE(r.submit.has_value());  // no charge roll from the click itself
    ASSERT_EQ(r.menu.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<action::DeclareCharge>(r.menu[0].action));
    ASSERT_TRUE(r.menu[1].cancel);
    const ClickResult cancelled = board.choose(gs, r.menu[1]);
    EXPECT_FALSE(cancelled.submit.has_value());
    EXPECT_FALSE(board.actor().has_value());
}

TEST(M30_KeptAdvanceRoll, AdvancingAgainAfterAResetShowsNoNewRoll) {
    GameSession session(wh40k::test::load_v1_scenario());
    const UnitId marines = session.state().units[0].id;
    const action::StartMove advance{marines, MoveType::Advance, std::nullopt};
    EXPECT_EQ(action_label(session.state(), advance), "Advance: Intercessor Squad (1)");
    std::size_t from = session.log().size();
    ASSERT_TRUE(session.submit(advance).ok());
    ASSERT_EQ(dice_cards(session.state(), session.log(), from).size(), 1u);  // the roll
    const float first_max = *max_move_distance_mm(session.state(), session.state().units[0].models[0].id);
    const int roll = *session.state().units[0].phase_flags.advance_roll;
    ASSERT_TRUE(session.submit(action::ResetUnitMove{}).ok());

    // The menu says the roll is kept (D-005) before it is picked again.
    EXPECT_EQ(action_label(session.state(), advance),
              "Advance: Intercessor Squad (1) (advance roll kept: +" + std::to_string(roll) + "\")");
    from = session.log().size();
    ASSERT_TRUE(session.submit(advance).ok());
    EXPECT_TRUE(dice_cards(session.state(), session.log(), from).empty());  // nothing rolled again
    const auto rolled = events_of<event::AdvanceRolled>(session.log().back().events);
    ASSERT_EQ(rolled.size(), 1u);
    EXPECT_TRUE(rolled[0].reused);
    EXPECT_EQ(rolled[0].roll, roll);
    EXPECT_FLOAT_EQ(*max_move_distance_mm(session.state(), session.state().units[0].models[0].id), first_max);
}

TEST(M30_ChargeArea, EveryShadedChargeSpotIsAccepted) {
    // The Termagants 9.9" away, a charge roll of 10 (as wh40k::test::after_successful_charge).
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (!(gs.flow.stage == Stage::Charge && turn_side(gs) == Side::A)) {
        apply_action(gs, wh40k::test::decider(gs), legal_actions(gs, wh40k::test::decider(gs)).front());
    }
    const float gap = mm_to_inches(*unit_distance(gs.units[0], gs.units[1]));
    for (auto& m : gs.units[1].models) m.position.y -= in(gap - 9.9f);
    ScriptedDice dice{4, 6};
    apply_action(gs, Side::A, action::DeclareCharge{gs.units[0].id}, dice);
    apply_action(gs, Side::A, action::StartChargeMove{gs.units[0].id, {gs.units[1].id}});

    int shaded = 0;
    for (const Model& m : gs.units[0].models) {
        const auto reach = move_reach(gs, m.id);
        ASSERT_TRUE(reach.has_value());
        const ReachRegion region = model_region(*reach);
        for (float dx = -10.0f; dx <= 10.0f; dx += 0.5f) {
            for (float dy = -10.0f; dy <= 10.0f; dy += 0.5f) {
                const Position p{reach->from.x + in(dx), reach->from.y + in(dy), 0.0f};
                bool stable = true;
                for (const auto& d : {std::pair{0.1f, 0.0f}, {-0.1f, 0.0f}, {0.0f, 0.1f}, {0.0f, -0.1f}}) {
                    stable = stable && in_region(region, Position{p.x + d.first, p.y + d.second, 0}) ==
                                           in_region(region, p);
                }
                if (!stable || !in_region(region, p)) continue;
                EXPECT_TRUE(validate_action(gs, Side::A, action::ProposeModelMove{m.id, {p}}).ok())
                    << "model " << m.id.v << " shaded but refused: " << dx << ", " << dy;
                ++shaded;
            }
        }
    }
    EXPECT_GT(shaded, 50);
    // The whole unit can be dragged in a charge too.
    const Unit& u = gs.units[0];
    EXPECT_FALSE(unit_region(gs, u.id, u.models[2].position).empty);
}
