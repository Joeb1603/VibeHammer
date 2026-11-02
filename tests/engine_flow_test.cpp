#include <gtest/gtest.h>

#include <stdexcept>
#include <type_traits>
#include <vector>

#include "core/engine/engine.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::add_unit_copy;
using wh40k::test::decider;
using wh40k::test::load_v1_scenario;

namespace {

template <typename E>
std::vector<E> events_of(const std::vector<GameEvent>& events) {
    std::vector<E> out;
    for (const auto& e : events) {
        if (const auto* x = std::get_if<E>(&e)) out.push_back(*x);
    }
    return out;
}

// Plays every decision with its only legal action until the game ends. Returns all events.
std::vector<GameEvent> play_all_pass(GameState& gs, int& actions) {
    std::vector<GameEvent> all = start_game(gs);
    actions = 0;
    while (gs.pending) {
        const Side side = decider(gs);
        const auto legal = legal_actions(gs, side);
        EXPECT_FALSE(legal.empty()) << describe(*gs.pending);
        if (legal.empty()) break;
        // "Pass": the first legal action is Remain Stationary / End Shoot Step / End Charge Step.
        const auto events = apply_action(gs, side, legal.front());
        all.insert(all.end(), events.begin(), events.end());
        ++actions;
        EXPECT_EQ(gs.pending, compute_decision(gs));  // pending invariant
    }
    return all;
}

}  // namespace

// ==== 07 The battle round ====

TEST(R07_BattleRound, AllPassGameReachesGameOverAfterFiveRoundsAsADraw) {
    GameState gs = load_v1_scenario();
    int actions = 0;
    const auto events = play_all_pass(gs, actions);
    EXPECT_EQ(actions, 30);  // per turn: move the unit, end shooting, end charges
    EXPECT_EQ(gs.flow.stage, Stage::GameOver);
    EXPECT_EQ(gs.flow.battle_round, 5);
    ASSERT_TRUE(gs.result.has_value());
    EXPECT_FALSE(gs.result->winner.has_value());  // D-007: a draw
    EXPECT_EQ(gs.result->reason.rfind("D-007: ", 0), 0u) << gs.result->reason;
    EXPECT_FALSE(gs.pending.has_value());
    EXPECT_EQ(events_of<event::BattleRoundStarted>(events).size(), 5u);
    EXPECT_EQ(events_of<event::BattleRoundEnded>(events).size(), 5u);
    EXPECT_EQ(events_of<event::TurnStarted>(events).size(), 10u);
    EXPECT_EQ(events_of<event::TurnEnded>(events).size(), 10u);
    ASSERT_TRUE(std::holds_alternative<event::GameOver>(events.back()));
}

TEST(R07_02_PlayerTurns, SameSideTakesTheFirstTurnEveryBattleRound) {
    GameState gs = load_v1_scenario();
    int actions = 0;
    const auto turns = events_of<event::TurnStarted>(play_all_pass(gs, actions));
    ASSERT_EQ(turns.size(), 10u);
    for (std::size_t i = 0; i < turns.size(); ++i) {
        EXPECT_EQ(turns[i].round, static_cast<int>(i / 2) + 1);
        EXPECT_EQ(turns[i].side, i % 2 == 0 ? Side::A : Side::B);
    }
}

TEST(R07_02_PlayerTurns, FirstTurnFollowsTheScenario) {
    GameState gs = load_v1_scenario();
    gs.first_turn = Side::B;
    start_game(gs);
    EXPECT_EQ(turn_side(gs), Side::B);
    EXPECT_EQ(decider(gs), Side::B);
}

TEST(R07_02_PlayerTurns, PhasesStartAndEndInOrderEachTurn) {
    GameState gs = load_v1_scenario();
    int actions = 0;
    const auto events = play_all_pass(gs, actions);
    std::vector<Phase> started;
    std::vector<Phase> ended;
    for (const auto& e : events) {
        if (const auto* p = std::get_if<event::PhaseStarted>(&e)) started.push_back(p->phase);
        if (const auto* p = std::get_if<event::PhaseEnded>(&e)) ended.push_back(p->phase);
    }
    const std::vector<Phase> turn{Phase::Command, Phase::Movement, Phase::Shooting, Phase::Charge, Phase::Fight};
    ASSERT_EQ(started.size(), 50u);
    ASSERT_EQ(ended, started);
    for (std::size_t i = 0; i < started.size(); ++i) {
        EXPECT_EQ(started[i], turn[i % 5]);
    }
}

TEST(R07_01_StartOfBattleRound, StartGameRunsToTheFirstDecision) {
    GameState gs = load_v1_scenario();
    EXPECT_FALSE(gs.pending.has_value());
    const auto events = start_game(gs);
    ASSERT_GE(events.size(), 5u);
    EXPECT_EQ(events[0], GameEvent{event::BattleRoundStarted{1}});
    EXPECT_EQ(events[1], GameEvent{(event::TurnStarted{1, Side::A})});
    EXPECT_EQ(events[2], GameEvent{(event::PhaseStarted{Phase::Command, Side::A})});
    EXPECT_EQ(events[3], GameEvent{(event::PhaseEnded{Phase::Command, Side::A})});
    EXPECT_EQ(events[4], GameEvent{(event::PhaseStarted{Phase::Movement, Side::A})});
    EXPECT_EQ(gs.flow.stage, Stage::MoveUnits);
    ASSERT_TRUE(gs.pending.has_value());
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::MoveUnits{}}));
    EXPECT_THROW((void)start_game(gs), std::logic_error);
}

// ==== 01.03 Active player ====

TEST(R01_03_ActivePlayer, FollowsTheTurnAndIsTheFirstPlayerBetweenTurns) {
    GameState gs = load_v1_scenario();
    gs.first_turn = Side::B;
    EXPECT_EQ(active_side(gs), Side::B);  // not started: neither player's turn
    start_game(gs);
    EXPECT_EQ(active_side(gs), Side::B);
    // Play B's turn; A becomes active in its own turn.
    while (turn_side(gs) == Side::B) {
        apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    }
    EXPECT_EQ(gs.flow.stage, Stage::MoveUnits);
    EXPECT_EQ(active_side(gs), Side::A);
}

// ==== 09.02 Move Units ====

TEST(R09_02_MoveUnits, OnlyTheActivePlayersUnselectedUnitsAreOffered) {
    GameState gs = load_v1_scenario();
    start_game(gs);
    const auto legal = legal_actions(gs, Side::A);
    const UnitId unit = gs.units[0].id;
    // 09.04 any unit; 09.05 / 09.06 unengaged units. Not engaged, so no Fall-back move (09.07).
    EXPECT_EQ(legal, (std::vector<GameAction>{action::StartMove{unit, MoveType::RemainStationary, std::nullopt},
                                              action::StartMove{unit, MoveType::Normal, std::nullopt},
                                              action::StartMove{unit, MoveType::Advance, std::nullopt}}));
    EXPECT_TRUE(legal_actions(gs, Side::B).empty());
}

TEST(R09_04_RemainStationary, EmitsEventAndMarksTheUnitSelected) {
    GameState gs = load_v1_scenario();
    start_game(gs);
    const auto before = gs.units[0].models;
    const auto events = apply_action(gs, Side::A, action::StartMove{gs.units[0].id, MoveType::RemainStationary});
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events[0], GameEvent{event::UnitRemainedStationary{gs.units[0].id}});
    EXPECT_EQ(gs.units[0].models, before);  // no models moved
    EXPECT_EQ(gs.flow.stage, Stage::Shoot);  // every unit selected: the step ended
}

TEST(R09_02_MoveUnits, EveryUnitMustBeSelectedBeforeTheStepEnds) {
    GameState gs = load_v1_scenario();
    const UnitId second = add_unit_copy(gs, 0);  // a second Side A unit
    start_game(gs);
    apply_action(gs, Side::A, action::StartMove{gs.units[0].id});
    EXPECT_EQ(gs.flow.stage, Stage::MoveUnits);
    const auto legal = legal_actions(gs, Side::A);
    ASSERT_EQ(legal.size(), 3u);
    for (const auto& a : legal) {
        EXPECT_EQ(std::get<action::StartMove>(a).unit, second);  // only the unselected unit
    }

    const auto again = validate_action(gs, Side::A, action::StartMove{gs.units[0].id});
    ASSERT_FALSE(again.ok());
    EXPECT_EQ(again.reasons[0], "09.02: unit 1 has already been selected to move this phase");

    apply_action(gs, Side::A, action::StartMove{second});
    EXPECT_EQ(gs.flow.stage, Stage::Shoot);
}

TEST(R09_02_MoveUnits, SelectionsResetEachMovementPhase) {
    GameState gs = load_v1_scenario();
    start_game(gs);
    apply_action(gs, Side::A, action::StartMove{gs.units[0].id});
    EXPECT_TRUE(gs.units[0].phase_flags.selected_to_move);
    apply_action(gs, Side::A, action::EndShootStep{});
    apply_action(gs, Side::A, action::EndChargeStep{});
    // Side B's Movement phase.
    EXPECT_EQ(decider(gs), Side::B);
    EXPECT_FALSE(gs.units[0].phase_flags.selected_to_move);
}

TEST(R09_02_MoveUnits, EnemyAndDestroyedUnitsCannotBeSelected) {
    GameState gs = load_v1_scenario();
    const UnitId second = add_unit_copy(gs, 0);
    gs.find_unit(second)->models.clear();  // destroyed
    start_game(gs);
    const auto enemy = validate_action(gs, Side::A, action::StartMove{gs.units[1].id});
    ASSERT_FALSE(enemy.ok());
    EXPECT_EQ(enemy.reasons[0], "09.02: unit 2 is not a friendly unit");
    const auto destroyed = validate_action(gs, Side::A, action::StartMove{second});
    ASSERT_FALSE(destroyed.ok());
    EXPECT_NE(destroyed.reasons[0].find("destroyed"), std::string::npos);
    const auto missing = validate_action(gs, Side::A, action::StartMove{UnitId{77}});
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.reasons[0], "09.02: unit 77 does not exist");
    EXPECT_EQ(legal_actions(gs, Side::A).size(), 3u);  // the destroyed unit is not offered
}

TEST(R07_02_PlayerTurns, TurnFlagsAreClearedAtTheEndOfTheTurn) {
    GameState gs = load_v1_scenario();
    start_game(gs);
    gs.units[0].turn_flags.advanced = true;
    apply_action(gs, Side::A, action::StartMove{gs.units[0].id});
    apply_action(gs, Side::A, action::EndShootStep{});
    EXPECT_TRUE(gs.units[0].turn_flags.advanced);
    apply_action(gs, Side::A, action::EndChargeStep{});  // through the Fight phase to End of Turn
    EXPECT_FALSE(gs.units[0].turn_flags.advanced);
}
