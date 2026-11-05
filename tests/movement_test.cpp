// M7: Movement phase moves (09.04-09.07) through the engine, 03.01 paths and ending a move, D-005.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::events_of;
using wh40k::test::starts_with;
using wh40k::test::decider;
using wh40k::test::in;
using wh40k::test::load_v1_scenario;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

// Side A's first Move Units step.
GameState movement_state() {
    GameState gs = load_v1_scenario();
    start_game(gs);
    return gs;
}

Position shifted(const Position& p, float dx_in, float dy_in) {
    return Position{p.x + in(dx_in), p.y + in(dy_in), 0.0f};
}

action::StartMove start(const GameState& gs, MoveType type, std::optional<FallBackMode> mode = std::nullopt) {
    return action::StartMove{gs.units[kMarines].id, type, mode};
}

action::ProposeModelMove move_by(const GameState& gs, std::size_t model, float dx_in, float dy_in) {
    const Model& m = gs.units[kMarines].models[model];
    return action::ProposeModelMove{m.id, {shifted(*move_start_position(gs, m.id), dx_in, dy_in)}};
}

// Puts a Termagant 1" west of the first Intercessor: the units are engaged (03.04).
void engage_first_marine(GameState& gs) {
    const Position m = gs.units[kMarines].models[0].position;
    gs.units[kGants].models[0].position = Position{m.x - 16.0f - 14.25f - in(1), m.y, 0.0f};
}

}  // namespace

// ==== 09.05 Normal move ====

TEST(R09_05_NormalMove, WholeUnitMovesUpToItsMCharacteristic) {
    GameState gs = movement_state();
    apply_action(gs, Side::A, start(gs, MoveType::Normal));
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::MoveModels{gs.units[kMarines].id}}));
    EXPECT_EQ(active_side(gs), Side::A);  // 01.03
    std::vector<Position> expected;
    for (std::size_t i = 0; i < 5; ++i) {
        expected.push_back(shifted(gs.units[kMarines].models[i].position, 0, 6));
        apply_action(gs, Side::A, move_by(gs, i, 0, 6));  // exactly M = 6"
    }
    EXPECT_TRUE(move_end_problems(gs).empty());
    const auto events = apply_action(gs, Side::A, action::CommitUnitMove{});
    ASSERT_EQ(events_of<event::UnitMoveCommitted>(events).size(), 1u);
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(gs.units[kMarines].models[i].position, expected[i]);
    }
    EXPECT_TRUE(gs.units[kMarines].turn_flags.moved_over_3in);  // 24.16
    EXPECT_FALSE(gs.units[kMarines].turn_flags.advanced);
    EXPECT_FALSE(gs.flow.move.has_value());
    EXPECT_EQ(gs.flow.stage, Stage::Shoot);  // every unit has been selected (09.02)
}

TEST(R09_05_NormalMove, DistanceLimitIsTheMCharacteristic) {
    GameState gs = movement_state();
    apply_action(gs, Side::A, start(gs, MoveType::Normal));
    EXPECT_FLOAT_EQ(*max_move_distance_mm(gs, gs.units[kMarines].models[0].id), in(6));
    EXPECT_TRUE(validate_action(gs, Side::A, move_by(gs, 0, 0, 6)).ok());
    action::ProposeModelMove too_far = move_by(gs, 0, 0, 6);
    too_far.path[0].y += 0.05f;
    const auto r = validate_action(gs, Side::A, too_far);
    ASSERT_FALSE(r.ok());
    EXPECT_TRUE(starts_with(r.reasons[0], "09.05: model 1 would move 6.00\" but the maximum distance is 6.00\""))
        << r.reasons[0];
    // 03.01: the distance is the sum of the straight lines, not the displacement.
    const Position s = *move_start_position(gs, gs.units[kMarines].models[0].id);
    const action::ProposeModelMove dogleg{gs.units[kMarines].models[0].id, {shifted(s, 3, 0), shifted(s, 3, 3.1f)}};
    EXPECT_FALSE(validate_action(gs, Side::A, dogleg).ok());
}

TEST(R09_05_NormalMove, EndingEngagedRevertsTheWholeMove) {
    GameState gs = movement_state();
    for (auto& m : gs.units[kGants].models) m.position.y -= in(6);  // Termagants now 6" away
    std::vector<Position> before;
    for (const auto& m : gs.units[kMarines].models) before.push_back(m.position);
    apply_action(gs, Side::A, start(gs, MoveType::Normal));
    for (std::size_t i = 0; i < 5; ++i) apply_action(gs, Side::A, move_by(gs, i, 0, 5));  // 1" short
    const auto events = apply_action(gs, Side::A, action::CommitUnitMove{});
    const auto reverted = events_of<event::UnitMoveReverted>(events);
    ASSERT_EQ(reverted.size(), 1u);
    EXPECT_EQ(reverted[0].reasons,
              (std::vector<std::string>{"09.05: unit 1 must be unengaged after a Normal move"}));
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(gs.units[kMarines].models[i].position, before[i]);  // 03.01: returned to the start
    }
    // 03 sidebar: the unit has not been selected to make that move, so it can be selected again.
    EXPECT_FALSE(gs.units[kMarines].phase_flags.selected_to_move);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::MoveUnits{}}));
    EXPECT_TRUE(validate_action(gs, Side::A, start(gs, MoveType::Normal)).ok());
}

TEST(R03_01_EndingAMove, IncoherentEndRevertsEveryModel) {
    GameState gs = movement_state();
    const auto before = gs.units[kMarines].models;
    apply_action(gs, Side::A, start(gs, MoveType::Normal));
    apply_action(gs, Side::A, move_by(gs, 0, -5.5f, 0));  // 5.5" from its neighbour (bases touched, M14)
    apply_action(gs, Side::A, move_by(gs, 4, 0, 1));      // a valid move on its own
    // Model 1 is now more than 2" from every other model, and about 9.3" from model 5 (both clauses).
    EXPECT_EQ(move_end_problems(gs),
              (std::vector<std::string>{"03.03: model 1 is more than 2\" from every other model in unit 1",
                                        "03.03: model 1 is more than 9\" from another model in unit 1",
                                        "03.03: model 5 is more than 9\" from another model in unit 1"}));
    apply_action(gs, Side::A, action::CommitUnitMove{});
    EXPECT_EQ(gs.units[kMarines].models, before);  // both moved models are back
}

TEST(R03_01_EndingAMove, EndingOnAFriendlyModelReverts) {
    GameState gs = movement_state();
    const auto before = gs.units[kMarines].models;
    apply_action(gs, Side::A, start(gs, MoveType::Normal));
    // D-027 rejects such a proposal, so put model 1 on model 2 directly: the commit still checks 03.01
    // Ending a Move for the whole unit.
    gs.units[kMarines].models[0].position = gs.units[kMarines].models[1].position;
    const auto problems = move_end_problems(gs);
    ASSERT_FALSE(problems.empty());
    EXPECT_EQ(problems[0], "03.01: unit 1's model 1 is on model 2");
    const auto events = apply_action(gs, Side::A, action::CommitUnitMove{});
    ASSERT_EQ(events_of<event::UnitMoveReverted>(events).size(), 1u);
    EXPECT_EQ(gs.units[kMarines].models, before);
}

TEST(R03_01_MovingAModel, AProposalEndingOnAFriendlyModelIsRejected) {
    GameState gs = movement_state();
    apply_action(gs, Side::A, start(gs, MoveType::Normal));
    // D-027: ending on the next Intercessor is rejected when proposed, not only at the commit.
    const Model& second = gs.units[kMarines].models[1];
    const auto r = validate_action(gs, Side::A, action::ProposeModelMove{gs.units[kMarines].models[0].id,
                                                                         {second.position}});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.reasons, (std::vector<std::string>{"03.01: model 1 cannot end its move on model 2"}));
}

TEST(R03_01_MovingAModel, AModelMayMoveThroughFriendlyModelsAndEndInBaseContact) {
    GameState gs = movement_state();
    apply_action(gs, Side::A, start(gs, MoveType::Normal));
    // The bases touch in a line (M14). Model 3 steps 2" north, then model 1 moves straight east through
    // model 2 into model 3's old place, in base contact with model 2.
    apply_action(gs, Side::A, move_by(gs, 2, 0, 2));
    const Model& m1 = gs.units[kMarines].models[0];
    const Position s = *move_start_position(gs, m1.id);
    const Position east_of_2{s.x + 64.0f, s.y, 0.0f};  // in base contact with model 2, where model 3 was
    // A straight line through model 2 (03.01: friendly models can be moved through).
    const action::ProposeModelMove through{m1.id, {east_of_2}};
    EXPECT_TRUE(validate_action(gs, Side::A, through).ok());
    apply_action(gs, Side::A, through);
    EXPECT_TRUE(in_base_contact(base_distance(gs.units[kMarines].models[0].position, m1.base,
                                              gs.units[kMarines].models[1].position,
                                              gs.units[kMarines].models[1].base)));
    EXPECT_TRUE(move_end_problems(gs).empty());
}

TEST(D027_ProposalOverlap, ModelsCanSwapPlacesInStepsBecauseAProposalReplacesThePath) {
    GameState gs = movement_state();
    apply_action(gs, Side::A, start(gs, MoveType::Normal));
    const Position p1 = gs.units[kMarines].models[0].position;
    const Position p2 = gs.units[kMarines].models[1].position;
    const ModelId m1 = gs.units[kMarines].models[0].id;
    const ModelId m2 = gs.units[kMarines].models[1].id;
    EXPECT_FALSE(validate_action(gs, Side::A, action::ProposeModelMove{m1, {p2}}).ok());  // model 2 is there
    apply_action(gs, Side::A, action::ProposeModelMove{m1, {shifted(p1, 0, 2)}});        // step aside
    apply_action(gs, Side::A, action::ProposeModelMove{m2, {p1}});
    apply_action(gs, Side::A, action::ProposeModelMove{m1, {p2}});  // replaces model 1's path
    EXPECT_EQ(gs.units[kMarines].models[0].position, p2);
    EXPECT_EQ(gs.units[kMarines].models[1].position, p1);
    ASSERT_EQ(events_of<event::UnitMoveCommitted>(apply_action(gs, Side::A, action::CommitUnitMove{})).size(), 1u);
}

TEST(R03_01_MovingAModel, PathsAreCheckedAgainstEnemyModelsAndTheEdge) {
    GameState gs = movement_state();
    for (auto& m : gs.units[kGants].models) m.position.y -= in(8);  // front rank 4" from the marines
    apply_action(gs, Side::A, start(gs, MoveType::Normal));
    const Model& marine = gs.units[kMarines].models[2];  // the Termagant at x = 12" is straight ahead
    const action::ProposeModelMove through{marine.id, {shifted(marine.position, 0, 6)}};
    const auto r = validate_action(gs, Side::A, through);
    ASSERT_FALSE(r.ok());
    EXPECT_TRUE(starts_with(r.reasons[0], "03.01: model 3's base cannot be moved through enemy model")) << r.reasons[0];

    const action::ProposeModelMove off_board{marine.id, {Position{marine.position.x, 5.0f, 0.0f}}};
    const auto edge = validate_action(gs, Side::A, off_board);
    ASSERT_FALSE(edge.ok());
    EXPECT_EQ(edge.reasons[0], "03.01: model 3's base cannot cross the edge of the battlefield");
}

TEST(R03_01_MovingAModel, ProposalsMustBeForTheMovingUnitAndNotEmpty) {
    GameState gs = movement_state();
    apply_action(gs, Side::A, start(gs, MoveType::Normal));
    const auto other = validate_action(gs, Side::A, action::ProposeModelMove{gs.units[kGants].models[0].id,
                                                                              {Position{100.0f, 100.0f, 0.0f}}});
    ASSERT_FALSE(other.ok());
    EXPECT_EQ(other.reasons[0], "03.01: model 6 is not in the moving unit (unit 1)");
    const auto empty = validate_action(gs, Side::A, action::ProposeModelMove{gs.units[kMarines].models[0].id, {}});
    ASSERT_FALSE(empty.ok());  // the template itself is not an action
    EXPECT_TRUE(starts_with(empty.reasons[0], "03.01: the path is empty"));
}

TEST(R03_01_MovingAModel, ProposingAgainReplacesTheEarlierPath) {
    GameState gs = movement_state();
    apply_action(gs, Side::A, start(gs, MoveType::Normal));
    const ModelId id = gs.units[kMarines].models[0].id;
    const Position s = *move_start_position(gs, id);
    apply_action(gs, Side::A, move_by(gs, 0, 0, 4));
    apply_action(gs, Side::A, move_by(gs, 0, 0, 4));  // measured from the start again: 4", not 8"
    EXPECT_EQ(gs.units[kMarines].models[0].position, shifted(s, 0, 4));
    ASSERT_EQ(gs.flow.move->paths.size(), 1u);
    EXPECT_EQ(gs.flow.move->paths[0].points.size(), 2u);
}

TEST(R24_16_Heavy, MovingExactly3InchesDoesNotCount) {
    GameState gs = movement_state();
    apply_action(gs, Side::A, start(gs, MoveType::Normal));
    for (std::size_t i = 0; i < 5; ++i) apply_action(gs, Side::A, move_by(gs, i, 0, 3));
    apply_action(gs, Side::A, action::CommitUnitMove{});
    EXPECT_FALSE(gs.units[kMarines].turn_flags.moved_over_3in);
}

TEST(R03_01_MovingUnits, ResetReturnsModelsAndAllowsAnotherMoveType) {
    GameState gs = movement_state();
    const auto before = gs.units[kMarines].models;
    apply_action(gs, Side::A, start(gs, MoveType::Normal));
    apply_action(gs, Side::A, move_by(gs, 0, 0, 2));
    const auto events = apply_action(gs, Side::A, action::ResetUnitMove{});
    ASSERT_EQ(events_of<event::UnitMoveReverted>(events).size(), 1u);
    EXPECT_TRUE(events_of<event::UnitMoveReverted>(events)[0].reasons.empty());
    EXPECT_EQ(gs.units[kMarines].models, before);
    EXPECT_EQ(legal_actions(gs, Side::A).size(), 3u);
    apply_action(gs, Side::A, start(gs, MoveType::RemainStationary));
    EXPECT_EQ(gs.flow.stage, Stage::Shoot);
}

// ==== 09.06 Advance move ====

TEST(R09_06_AdvanceMove, MaximumIsMPlusTheAdvanceRoll) {
    GameState gs = movement_state();
    ScriptedDice dice{4};
    const auto events = apply_action(gs, Side::A, start(gs, MoveType::Advance), dice);
    EXPECT_EQ(events_of<event::AdvanceRolled>(events).at(0), (event::AdvanceRolled{gs.units[kMarines].id, 4, false}));
    EXPECT_FLOAT_EQ(*max_move_distance_mm(gs, gs.units[kMarines].models[0].id), in(10));
    EXPECT_TRUE(validate_action(gs, Side::A, move_by(gs, 0, 0, 10)).ok());
    const auto r = validate_action(gs, Side::A, move_by(gs, 0, 0, 10.05f));
    ASSERT_FALSE(r.ok());
    EXPECT_TRUE(starts_with(r.reasons[0], "09.06: ")) << r.reasons[0];
    for (std::size_t i = 0; i < 5; ++i) apply_action(gs, Side::A, move_by(gs, i, 0, 5));
    apply_action(gs, Side::A, action::CommitUnitMove{});
    EXPECT_TRUE(gs.units[kMarines].turn_flags.advanced);  // 09.06 After Moving
}

TEST(D005_RevertedMoveDice, AdvanceRollIsReusedAfterAReset) {
    GameState gs = movement_state();
    ScriptedDice first{3};
    apply_action(gs, Side::A, start(gs, MoveType::Advance), first);
    apply_action(gs, Side::A, action::ResetUnitMove{});
    ScriptedDice none(std::vector<int>{});
    const auto events = apply_action(gs, Side::A, start(gs, MoveType::Advance), none);
    EXPECT_EQ(events_of<event::AdvanceRolled>(events).at(0), (event::AdvanceRolled{gs.units[kMarines].id, 3, true}));
    EXPECT_EQ(none.consumed(), 0u);
    EXPECT_FLOAT_EQ(*max_move_distance_mm(gs, gs.units[kMarines].models[0].id), in(9));
}

TEST(D005_RevertedMoveDice, AdvanceRollIsForgottenInTheNextMovementPhase) {
    GameState gs = movement_state();
    ScriptedDice dice{3};
    apply_action(gs, Side::A, start(gs, MoveType::Advance), dice);
    apply_action(gs, Side::A, action::ResetUnitMove{});
    apply_action(gs, Side::A, start(gs, MoveType::RemainStationary));
    while (!(gs.flow.stage == Stage::MoveUnits && turn_side(gs) == Side::A)) {
        apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    }
    EXPECT_FALSE(gs.units[kMarines].phase_flags.advance_roll.has_value());
}

// ==== 09.07 Fall-back move ====

TEST(R09_07_FallBackMove, EligibilityDependsOnEngagement) {
    GameState gs = movement_state();
    const auto unengaged = validate_action(gs, Side::A, start(gs, MoveType::FallBack, FallBackMode::OrderedRetreat));
    ASSERT_FALSE(unengaged.ok());
    EXPECT_EQ(unengaged.reasons[0], "09.07: a Fall-back move needs the unit to be engaged");

    engage_first_marine(gs);
    gs.pending = compute_decision(gs);
    const UnitId unit = gs.units[kMarines].id;
    EXPECT_EQ(legal_actions(gs, Side::A),
              (std::vector<GameAction>{action::StartMove{unit, MoveType::RemainStationary, std::nullopt},
                                       action::StartMove{unit, MoveType::FallBack, FallBackMode::OrderedRetreat},
                                       action::StartMove{unit, MoveType::FallBack, FallBackMode::DesperateEscape}}));
    const auto normal = validate_action(gs, Side::A, start(gs, MoveType::Normal));
    ASSERT_FALSE(normal.ok());
    EXPECT_EQ(normal.reasons[0], "09.05: a Normal move needs the unit to be unengaged");
    const auto no_mode = validate_action(gs, Side::A, start(gs, MoveType::FallBack));
    ASSERT_FALSE(no_mode.ok());
    EXPECT_EQ(no_mode.reasons[0], "09.07: select a fall-back mode");
}

TEST(R09_07_FallBackMove, BattleShockedUnitsMustDesperatelyEscape) {
    GameState gs = movement_state();
    engage_first_marine(gs);
    gs.units[kMarines].battle_shocked = true;
    gs.pending = compute_decision(gs);
    const auto r = validate_action(gs, Side::A, start(gs, MoveType::FallBack, FallBackMode::OrderedRetreat));
    ASSERT_FALSE(r.ok());
    EXPECT_TRUE(starts_with(r.reasons[0], "09.07: a battle-shocked unit cannot select Ordered Retreat"));
    EXPECT_EQ(legal_actions(gs, Side::A).size(), 2u);  // remain stationary, desperate escape
}

TEST(R09_07_FallBackMove, DesperateEscapeMayMoveThroughEnemiesOrderedRetreatMayNot) {
    GameState gs = movement_state();
    gs.units[kMarines].models.resize(1);  // a one-model unit keeps coherency out of the way
    engage_first_marine(gs);
    gs.pending = compute_decision(gs);
    const ModelId marine = gs.units[kMarines].models[0].id;
    const Position s = gs.units[kMarines].models[0].position;
    const action::ProposeModelMove through_enemy{marine, {Position{s.x - in(5.9f), s.y, 0.0f}}};  // through it

    apply_action(gs, Side::A, start(gs, MoveType::FallBack, FallBackMode::OrderedRetreat));
    const auto blocked = validate_action(gs, Side::A, through_enemy);
    ASSERT_FALSE(blocked.ok());
    EXPECT_TRUE(starts_with(blocked.reasons[0], "03.01: model 1's base cannot be moved through enemy model"));
    apply_action(gs, Side::A, action::ResetUnitMove{});

    ScriptedDice dice{6,      // hazard roll: passed
                      4, 4};  // battle-shock roll after the move: 8 >= Ld 6+, passed
    const auto start_events =
        apply_action(gs, Side::A, start(gs, MoveType::FallBack, FallBackMode::DesperateEscape), dice);
    EXPECT_EQ(events_of<event::HazardRollsMade>(start_events).at(0).rolls.rolls, (std::vector<int>{6}));
    EXPECT_TRUE(validate_action(gs, Side::A, through_enemy).ok());  // 09.07 While Moving
    apply_action(gs, Side::A, through_enemy, dice);
    const auto end_events = apply_action(gs, Side::A, action::CommitUnitMove{}, dice);
    ASSERT_EQ(events_of<event::UnitMoveCommitted>(end_events).size(), 1u);
    const auto shock = events_of<event::BattleShockRolled>(end_events);
    ASSERT_EQ(shock.size(), 1u);  // 09.07 After Moving: the unit was not battle-shocked
    EXPECT_FALSE(shock[0].roll.becomes_battle_shocked);
    EXPECT_TRUE(gs.units[kMarines].turn_flags.fell_back);
    EXPECT_EQ(dice.remaining(), 0u);
}

TEST(R09_07_FallBackMove, DesperateEscapeHazardsAndFailedBattleShock) {
    GameState gs = movement_state();
    gs.units[kMarines].models.resize(1);
    engage_first_marine(gs);
    gs.pending = compute_decision(gs);
    ScriptedDice dice{3,      // hazard roll: passed (fails on 1-2)
                      1, 2};  // battle-shock roll: 3 < 6, failed
    apply_action(gs, Side::A, start(gs, MoveType::FallBack, FallBackMode::DesperateEscape), dice);
    const ModelId marine = gs.units[kMarines].models[0].id;
    apply_action(gs, Side::A, action::ProposeModelMove{marine, {shifted(*move_start_position(gs, marine), 3, 0)}});
    apply_action(gs, Side::A, action::CommitUnitMove{}, dice);
    EXPECT_TRUE(gs.units[kMarines].battle_shocked);  // 01.07
}

TEST(R06_03_HazardRolls, DesperateEscapeCasualtiesAreChosenByTheOwner) {
    GameState gs = movement_state();
    engage_first_marine(gs);
    gs.units[kMarines].battle_shocked = true;  // must Desperate Escape; no roll after (already shocked)
    gs.pending = compute_decision(gs);
    ScriptedDice dice{1, 6, 6, 6, 6};  // one hazard roll per model; one fails: 1 mortal wound
    const auto events = apply_action(gs, Side::A, start(gs, MoveType::FallBack, FallBackMode::DesperateEscape), dice);
    const auto hazards = events_of<event::HazardRollsMade>(events);
    ASSERT_EQ(hazards.size(), 1u);
    EXPECT_EQ(hazards[0].rolls.failures, 1);
    EXPECT_EQ(hazards[0].rolls.mortal_wounds, 1);
    // 06.02: five unwounded models, so the controlling player chooses (D-013).
    const UnitId unit = gs.units[kMarines].id;
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{unit}}));
    const ModelId chosen = gs.units[kMarines].models[3].id;
    apply_action(gs, Side::A, action::OrderCasualties{unit, {chosen, gs.units[kMarines].models[0].id,
                                                             gs.units[kMarines].models[1].id,
                                                             gs.units[kMarines].models[2].id,
                                                             gs.units[kMarines].models[4].id}});
    EXPECT_EQ(gs.units[kMarines].models[3].wounds_remaining, 1);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::MoveModels{unit}}));
}

TEST(D005_RevertedMoveDice, DesperateEscapeHazardsAreNotRerolledAndWoundsStand) {
    GameState gs = movement_state();
    engage_first_marine(gs);
    gs.units[kMarines].battle_shocked = true;
    gs.units[kMarines].models.resize(2);  // 2 models: a failed roll needs a casualty choice
    gs.units[kMarines].models[1].wounds_remaining = 1;  // a wounded model takes the mortal wound (06.02)
    gs.pending = compute_decision(gs);
    ScriptedDice dice{2, 5};
    apply_action(gs, Side::A, start(gs, MoveType::FallBack, FallBackMode::DesperateEscape), dice);
    ASSERT_EQ(gs.units[kMarines].models.size(), 1u);  // the wounded model was destroyed
    apply_action(gs, Side::A, action::ResetUnitMove{});

    ScriptedDice none(std::vector<int>{});
    const auto again =
        apply_action(gs, Side::A, start(gs, MoveType::FallBack, FallBackMode::DesperateEscape), none);
    EXPECT_TRUE(events_of<event::HazardRollsMade>(again).empty());
    EXPECT_EQ(none.consumed(), 0u);
    EXPECT_EQ(gs.units[kMarines].models.size(), 1u);  // destroyed models are not returned
}

TEST(D007_GameEnd, HazardsDestroyingTheLastModelEndTheGame) {
    GameState gs = movement_state();
    gs.units[kMarines].models.resize(1);
    gs.units[kMarines].models[0].wounds_remaining = 1;
    engage_first_marine(gs);
    gs.pending = compute_decision(gs);
    ScriptedDice dice{1};
    apply_action(gs, Side::A, start(gs, MoveType::FallBack, FallBackMode::DesperateEscape), dice);
    EXPECT_EQ(gs.flow.stage, Stage::GameOver);
    ASSERT_TRUE(gs.result.has_value());
    EXPECT_EQ(gs.result->winner, Side::B);
}
