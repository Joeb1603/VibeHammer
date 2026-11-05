#include <gtest/gtest.h>

#include <stdexcept>

#include "core/engine/engine.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::decider;
using wh40k::test::is_rule_cited;
using wh40k::test::load_v1_scenario;

TEST(EngineValidation, NothingIsLegalBeforeTheGameStarts) {
    const GameState gs = load_v1_scenario();
    EXPECT_TRUE(legal_actions(gs, Side::A).empty());
    const auto r = validate_action(gs, Side::A, action::StartMove{gs.units[0].id});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.reasons[0].rfind("07.01: ", 0), 0u) << r.reasons[0];
}

TEST(EngineValidation, WrongSideIsRejectedWithTheDecisionsRule) {
    GameState gs = load_v1_scenario();
    start_game(gs);
    const auto r = validate_action(gs, Side::B, action::StartMove{gs.units[1].id});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.reasons[0],
              "09.02: waiting for Side A: select a unit to move (09.02); Side B cannot act now");
    EXPECT_TRUE(is_rule_cited(r.reasons[0]));
}

TEST(EngineValidation, OutOfPhaseActionIsRejectedWithItsRule) {
    GameState gs = load_v1_scenario();
    start_game(gs);
    apply_action(gs, Side::A, action::StartMove{gs.units[0].id});
    ASSERT_EQ(gs.flow.stage, Stage::Shoot);
    const auto r = validate_action(gs, Side::A, action::EndChargeStep{});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.reasons[0],
              "11.02: EndChargeStep is not possible now; waiting for Side A: select a unit to shoot, or end the "
              "Shoot step (10.02)");
    const auto move = validate_action(gs, Side::A, action::StartMove{gs.units[0].id});
    ASSERT_FALSE(move.ok());
    EXPECT_EQ(move.reasons[0].rfind("09.02: StartMove is not possible now", 0), 0u) << move.reasons[0];
    const auto alloc = validate_action(gs, Side::A, action::OrderCasualties{gs.units[1].id, {ModelId{7}}});
    ASSERT_FALSE(alloc.ok());
    EXPECT_TRUE(is_rule_cited(alloc.reasons[0])) << alloc.reasons[0];
}

TEST(EngineValidation, EverythingIsRejectedAfterGameOver) {
    GameState gs = load_v1_scenario();
    start_game(gs);
    while (gs.pending) {
        apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    }
    ASSERT_EQ(gs.flow.stage, Stage::GameOver);
    EXPECT_TRUE(legal_actions(gs, Side::A).empty());
    EXPECT_TRUE(legal_actions(gs, Side::B).empty());
    for (const Side s : {Side::A, Side::B}) {
        const auto r = validate_action(gs, s, action::EndShootStep{});
        ASSERT_FALSE(r.ok());
        EXPECT_EQ(r.reasons[0], "D-007: the game is over");
    }
}

TEST(EngineValidation, ApplyingAnInvalidActionIsAProgrammerError) {
    GameState gs = load_v1_scenario();
    start_game(gs);
    const GameState before = gs;
    EXPECT_THROW((void)apply_action(gs, Side::B, action::EndShootStep{}), std::logic_error);
    EXPECT_EQ(gs, before);  // rejected before anything changed
}

TEST(EngineApi, AnEngineExceptionLeavesTheStateUnchanged) {
    // Programmer errors (here: scripted dice running out mid-attack) throw, but apply_action works on a
    // copy and commits only on success, so the caller's state is never half-applied.
    GameState gs = wh40k::test::shoot_step_state();
    const GameState before = gs;
    action::Shoot volley{gs.units[0].id, ShootingType::Normal, {}};
    for (const Model& m : gs.units[0].models) volley.shots.push_back(ShotDeclaration{m.id, 0, 0, gs.units[1].id});
    ScriptedDice too_few{3, 3};
    EXPECT_THROW((void)apply_action(gs, Side::A, volley, too_few), std::logic_error);
    EXPECT_EQ(gs, before);
    EXPECT_TRUE(validate_action(gs, Side::A, volley).ok());  // still at the same decision
}
