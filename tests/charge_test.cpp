// M9: the Charge phase through the engine. 11.02 Declare Charge and the charge roll, 11.04 charge
// targets and the charge move (While/After Moving), D-018 (a reverted charge move), 24.13 Fights First.
// Every charge roll uses ScriptedDice.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/rules/charge.hpp"
#include "core/state/queries.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::add_unit_copy;
using wh40k::test::decider;
using wh40k::test::in;
using wh40k::test::is_rule_cited;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;
constexpr float kMarineRadius = 16.0f;
constexpr float kGantRadius = 14.25f;

template <typename E>
std::vector<E> events_of(const std::vector<GameEvent>& events) {
    std::vector<E> out;
    for (const auto& e : events) {
        if (const auto* x = std::get_if<E>(&e)) out.push_back(*x);
    }
    return out;
}

bool starts_with(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

std::vector<std::string> rejection(const GameState& gs, Side side, const GameAction& a) {
    const auto r = validate_action(gs, side, a);
    EXPECT_FALSE(r.ok()) << action_name(a) << " was accepted";
    for (const auto& reason : r.reasons) {
        EXPECT_TRUE(is_rule_cited(reason)) << reason;
    }
    return r.reasons;
}

void expect_valid(const GameState& gs, Side side, const GameAction& a) {
    const auto r = validate_action(gs, side, a);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.reasons.front());
}

// The V1 scenario at Side A's first Charge step. Nothing has moved: the front ranks are exactly 12"
// apart (D-008), so the Intercessors can declare a charge.
GameState charge_step_state() {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (!(gs.flow.stage == Stage::Charge && turn_side(gs) == Side::A)) {
        apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    }
    return gs;
}

float gap_in(const GameState& gs, std::size_t a = kMarines, std::size_t b = kGants) {
    return mm_to_inches(*unit_distance(gs.units[a], gs.units[b]));
}

// Moves the Termagants north/south so the two units are `gap` inches apart, edge to edge.
void set_gap(GameState& gs, float gap) {
    const float shift = in(gap_in(gs) - gap);
    for (Model& m : gs.units[kGants].models) m.position.y -= shift;
    ASSERT_NEAR(gap_in(gs), gap, 0.001f);
}

action::DeclareCharge declare(const GameState& gs) { return action::DeclareCharge{gs.units[kMarines].id}; }

action::StartChargeMove charge_at(const GameState& gs, std::vector<UnitId> targets) {
    return action::StartChargeMove{gs.units[kMarines].id, std::move(targets)};
}

// Moves every Intercessor straight north by `dy_in` from its start position (one ProposeModelMove each).
void move_marines_north(GameState& gs, float dy_in) {
    for (const Model& m : gs.units[kMarines].models) {
        const Position s = *move_start_position(gs, m.id);
        apply_action(gs, Side::A, action::ProposeModelMove{m.id, {Position{s.x, s.y + in(dy_in), 0.0f}}});
    }
}

std::vector<Position> positions(const Unit& u) {
    std::vector<Position> out;
    for (const Model& m : u.models) out.push_back(m.position);
    return out;
}

// The charge is over and Side A is back at the Charge decision.
void expect_back_at_charge_step(const GameState& gs) {
    EXPECT_FALSE(gs.flow.charge.has_value());
    EXPECT_FALSE(gs.flow.move.has_value());
    EXPECT_FALSE(gs.flow.activation.has_value());
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Charge{}}));
}

// A one-model copy of the Termagants, in line with their front rank, one slot east of it.
UnitId add_second_gant_unit(GameState& gs) {
    const UnitId id = add_unit_copy(gs, kGants);
    Unit& u = *gs.find_unit(id);
    Position east = u.models[0].position;
    for (const Model& m : u.models) {
        if (m.position.y < east.y - 0.01f || (std::abs(m.position.y - east.y) <= 0.01f && m.position.x > east.x)) {
            east = m.position;
        }
    }
    u.models.resize(1);
    u.models[0].position = Position{east.x + 2.0f * kGantRadius + in(1), east.y, 0.0f};
    return id;
}

}  // namespace

// ==== 11.02 Declare Charge ====

TEST(R11_02_Charge, LegalActionsAreEndChargeStepAndEachUnitThatCanDeclare) {
    const GameState gs = charge_step_state();
    ASSERT_NEAR(gap_in(gs), 12.0f, 0.01f);
    const auto legal = legal_actions(gs, Side::A);
    EXPECT_EQ(legal, (std::vector<GameAction>{action::EndChargeStep{}, declare(gs)}));
    EXPECT_TRUE(legal_actions(gs, Side::B).empty());
}

TEST(R11_02_Charge, NotEligibleUnlessWithin12InchesOfAnEnemyUnit) {
    GameState gs = charge_step_state();
    set_gap(gs, 12.1f);
    EXPECT_EQ(legal_actions(gs, Side::A), (std::vector<GameAction>{action::EndChargeStep{}}));
    EXPECT_EQ(rejection(gs, Side::A, declare(gs)).at(0), "11.02: unit 1 is not within 12\" of any enemy unit");
}

TEST(R11_02_Charge, NotEligibleWhileEngaged) {
    GameState gs = charge_step_state();
    set_gap(gs, 1.0f);
    EXPECT_EQ(rejection(gs, Side::A, declare(gs)).at(0), "11.02: unit 1 is engaged and cannot declare a charge");
}

TEST(R11_02_Charge, NotEligibleAfterAnAdvanceOrFallBackMove) {
    GameState gs = charge_step_state();
    gs.units[kMarines].turn_flags.advanced = true;
    EXPECT_EQ(rejection(gs, Side::A, declare(gs)).at(0),
              "11.02: unit 1 made an advance move this turn and cannot declare a charge");
    gs.units[kMarines].turn_flags.advanced = false;
    gs.units[kMarines].turn_flags.fell_back = true;
    EXPECT_EQ(rejection(gs, Side::A, declare(gs)).at(0),
              "11.02: unit 1 made a fall-back move this turn and cannot declare a charge");
}

TEST(R11_02_Charge, OnlyFriendlyUnitsDeclareCharges) {
    const GameState gs = charge_step_state();
    EXPECT_EQ(rejection(gs, Side::A, action::DeclareCharge{gs.units[kGants].id}).at(0),
              "11.02: unit 2 is not a friendly unit");
    rejection(gs, Side::B, declare(gs));
}

TEST(R11_02_Charge, AUnitDeclaresAChargeOnlyOncePerPhase) {
    GameState gs = charge_step_state();
    ScriptedDice dice{1, 1};
    apply_action(gs, Side::A, declare(gs), dice);
    EXPECT_TRUE(gs.units[kMarines].phase_flags.declared_charge);
    expect_back_at_charge_step(gs);
    EXPECT_EQ(legal_actions(gs, Side::A), (std::vector<GameAction>{action::EndChargeStep{}}));
    EXPECT_EQ(rejection(gs, Side::A, declare(gs)).at(0), "11.02: unit 1 has already declared a charge this phase");
}

TEST(R11_02_Charge, TheDeclarationIsResetWhenTheNextChargePhaseStarts) {
    GameState gs = charge_step_state();
    ScriptedDice dice{1, 1};
    apply_action(gs, Side::A, declare(gs), dice);
    while (!(gs.flow.stage == Stage::Charge && turn_side(gs) == Side::A && gs.flow.battle_round == 2)) {
        apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    }
    EXPECT_FALSE(gs.units[kMarines].phase_flags.declared_charge);
    EXPECT_FALSE(gs.units[kMarines].phase_flags.charge_roll.has_value());
    EXPECT_EQ(legal_actions(gs, Side::A).size(), 2u);
}

TEST(R11_02_Charge, TheChargeRollOffersTheTargetsWithinIt) {
    GameState gs = charge_step_state();
    set_gap(gs, 9.9f);
    ScriptedDice dice{4, 6};
    const auto events = apply_action(gs, Side::A, declare(gs), dice);
    ASSERT_EQ(events_of<event::ChargeDeclared>(events).size(), 1u);
    const auto rolled = events_of<event::ChargeRolled>(events);
    ASSERT_EQ(rolled.size(), 1u);
    EXPECT_EQ(rolled[0].roll.total(), 10);
    EXPECT_EQ(rolled[0].eligible_targets, (std::vector<UnitId>{gs.units[kGants].id}));
    EXPECT_EQ(*gs.units[kMarines].phase_flags.charge_roll, 10);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::SelectChargeTargets{gs.units[kMarines].id}}));
    EXPECT_EQ(active_side(gs), Side::A);
    EXPECT_EQ(legal_actions(gs, Side::A),
              (std::vector<GameAction>{charge_at(gs, {gs.units[kGants].id}),
                                       action::SkipChargeMove{gs.units[kMarines].id}}));
}

// ==== 11.04 Before Moving: charge targets within 12" and within the charge roll ====

TEST(R11_04_ChargeMove, EnemyAt11Point9WithRoll11CannotBeATargetSoTheChargeFails) {
    GameState gs = charge_step_state();
    set_gap(gs, 11.9f);
    const auto before = positions(gs.units[kMarines]);
    ScriptedDice dice{5, 6};
    const auto events = apply_action(gs, Side::A, declare(gs), dice);
    EXPECT_TRUE(events_of<event::ChargeRolled>(events).at(0).eligible_targets.empty());
    const auto resolved = events_of<event::ChargeResolved>(events);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_FALSE(resolved[0].charge_move_made);
    EXPECT_TRUE(starts_with(resolved[0].reason, "11.02: no enemy unit is within 12\" and within the charge roll (11\")"))
        << resolved[0].reason;
    EXPECT_EQ(positions(gs.units[kMarines]), before);  // the models don't move
    EXPECT_FALSE(gs.units[kMarines].turn_flags.made_charge_move);
    expect_back_at_charge_step(gs);
}

TEST(R11_04_ChargeMove, EnemyAt9Point9WithRoll9CannotBeATargetEvenThoughA9InchMoveWouldReachEngagement) {
    // 10th edition asked whether the move could reach within 2"; 11.04 asks whether the target is within
    // the charge roll.
    GameState gs = charge_step_state();
    set_gap(gs, 9.9f);
    ScriptedDice dice{4, 5};
    const auto events = apply_action(gs, Side::A, declare(gs), dice);
    EXPECT_TRUE(events_of<event::ChargeRolled>(events).at(0).eligible_targets.empty());
    EXPECT_FALSE(events_of<event::ChargeResolved>(events).at(0).charge_move_made);
    expect_back_at_charge_step(gs);
}

TEST(R11_04_ChargeMove, EnemyAt9Point9WithRoll10CanBeChargedAndTheChargeSucceeds) {
    GameState gs = charge_step_state();
    set_gap(gs, 9.9f);
    ScriptedDice dice{4, 6};
    apply_action(gs, Side::A, declare(gs), dice);
    const auto start = apply_action(gs, Side::A, charge_at(gs, {gs.units[kGants].id}));
    EXPECT_EQ(events_of<event::ChargeTargetsSelected>(start).size(), 1u);
    EXPECT_EQ(events_of<event::MoveStarted>(start).at(0).move_type, MoveType::Charge);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::MoveModels{gs.units[kMarines].id}}));
    EXPECT_NEAR(mm_to_inches(*max_move_distance_mm(gs, gs.units[kMarines].models[0].id)), 10.0f, 0.001f);

    move_marines_north(gs, 8.9f);  // ends about 1" from the Termagants' front rank
    EXPECT_TRUE(move_end_problems(gs).empty());
    const auto events = apply_action(gs, Side::A, action::CommitUnitMove{});
    EXPECT_EQ(events_of<event::UnitMoveCommitted>(events).at(0).move_type, MoveType::Charge);
    const auto resolved = events_of<event::ChargeResolved>(events);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_TRUE(resolved[0].charge_move_made);
    EXPECT_TRUE(is_engaged(gs, gs.units[kMarines]));
    EXPECT_TRUE(gs.units[kMarines].turn_flags.made_charge_move);
    EXPECT_TRUE(is_fights_first_unit(gs.units[kMarines]));  // 11.04 After Moving, 24.13
    EXPECT_EQ(dice.remaining(), 0u);
    expect_back_at_charge_step(gs);
}

TEST(R11_04_ChargeMove, ARollOf2NeverSucceeds) {
    // 11 sidebar "Failed charges": an unengaged unit is more than 2" from every enemy unit.
    GameState gs = charge_step_state();
    set_gap(gs, 2.05f);
    ASSERT_FALSE(is_engaged(gs, gs.units[kMarines]));
    ScriptedDice dice{1, 1};
    const auto events = apply_action(gs, Side::A, declare(gs), dice);
    EXPECT_FALSE(events_of<event::ChargeResolved>(events).at(0).charge_move_made);
    expect_back_at_charge_step(gs);
}

TEST(R11_04_ChargeMove, TargetsMustBeEligibleAndListedOnce) {
    GameState gs = charge_step_state();
    set_gap(gs, 9.9f);
    ScriptedDice dice{4, 6};
    apply_action(gs, Side::A, declare(gs), dice);
    EXPECT_EQ(rejection(gs, Side::A, charge_at(gs, {})).at(0), "11.04: select one or more enemy units as charge targets");
    EXPECT_EQ(rejection(gs, Side::A, charge_at(gs, {gs.units[kMarines].id})).at(0),
              "11.04: unit 1 is not an enemy unit within 12\" and within the charge roll (10\") of unit 1");
    EXPECT_EQ(rejection(gs, Side::A, charge_at(gs, {gs.units[kGants].id, gs.units[kGants].id})).at(0),
              "11.04: unit 2 is selected as a charge target more than once");
}

TEST(R11_04_ChargeMove, AModelCannotMoveFurtherThanTheChargeRoll) {
    GameState gs = charge_step_state();
    set_gap(gs, 9.9f);
    ScriptedDice dice{4, 6};
    apply_action(gs, Side::A, declare(gs), dice);
    apply_action(gs, Side::A, charge_at(gs, {gs.units[kGants].id}));
    const Model& m = gs.units[kMarines].models[0];
    const auto reasons =
        rejection(gs, Side::A, action::ProposeModelMove{m.id, {Position{m.position.x, m.position.y + in(10.5f), 0}}});
    EXPECT_TRUE(contains(reasons, "11.04: model 1 would move 10.50\" but the maximum distance is 10.00\""));
}

TEST(R11_04_ChargeMove, EachMovedModelMustEndCloserToAChargeTarget) {
    GameState gs = charge_step_state();
    set_gap(gs, 9.9f);
    ScriptedDice dice{4, 6};
    apply_action(gs, Side::A, declare(gs), dice);
    apply_action(gs, Side::A, charge_at(gs, {gs.units[kGants].id}));
    const Model& m = gs.units[kMarines].models[2];
    const auto away =
        rejection(gs, Side::A, action::ProposeModelMove{m.id, {Position{m.position.x, m.position.y - in(1), 0}}});
    EXPECT_EQ(away, (std::vector<std::string>{"11.04: model 3 must end its move closer to one or more charge targets"}));
    // Moving out and back to the start position is not closer either.
    const Position s = m.position;
    rejection(gs, Side::A, action::ProposeModelMove{m.id, {Position{s.x, s.y + in(2), 0}, s}});
    expect_valid(gs, Side::A, action::ProposeModelMove{m.id, {Position{s.x, s.y + in(1), 0}}});
}

TEST(R11_04_ChargeMove, TheUnitMustEndEngagedWithEveryChargeTarget) {
    GameState gs = charge_step_state();
    set_gap(gs, 9.9f);
    ScriptedDice dice{4, 6};
    apply_action(gs, Side::A, declare(gs), dice);
    apply_action(gs, Side::A, charge_at(gs, {gs.units[kGants].id}));
    move_marines_north(gs, 5.0f);  // closer, but not engaged
    EXPECT_EQ(move_end_problems(gs),
              (std::vector<std::string>{"11.04: unit 1 must end its charge move engaged with charge target unit 2"}));
    const auto events = apply_action(gs, Side::A, action::CommitUnitMove{});
    EXPECT_EQ(events_of<event::UnitMoveReverted>(events).size(), 1u);
}

TEST(R11_04_ChargeMove, ACommittedChargeCannotEndEngagedWithANonTargetUnit) {
    GameState gs = charge_step_state();
    set_gap(gs, 9.9f);
    const UnitId second = add_second_gant_unit(gs);  // 10.05" from the Intercessors
    ScriptedDice dice{5, 6};
    const auto declared = apply_action(gs, Side::A, declare(gs), dice);
    EXPECT_EQ(events_of<event::ChargeRolled>(declared).at(0).eligible_targets,
              (std::vector<UnitId>{gs.units[kGants].id, second}));
    EXPECT_EQ(legal_actions(gs, Side::A).size(), 4u);  // {2}, {3}, {2, 3}, skip

    apply_action(gs, Side::A, charge_at(gs, {gs.units[kGants].id}));
    move_marines_north(gs, 8.9f);  // the easternmost Intercessor also ends within 2" of the second unit
    const auto events = apply_action(gs, Side::A, action::CommitUnitMove{});
    const auto reverted = events_of<event::UnitMoveReverted>(events);
    ASSERT_EQ(reverted.size(), 1u);
    EXPECT_EQ(reverted[0].reasons,
              (std::vector<std::string>{
                  "11.04: unit 1 cannot end its charge move engaged with unit 3, which is not a charge target"}));

    // D-018: back to selecting targets with the same roll; charging both units works.
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::SelectChargeTargets{gs.units[kMarines].id}}));
    apply_action(gs, Side::A, charge_at(gs, {gs.units[kGants].id, second}));
    move_marines_north(gs, 8.9f);
    const auto again = apply_action(gs, Side::A, action::CommitUnitMove{});
    EXPECT_TRUE(events_of<event::ChargeResolved>(again).at(0).charge_move_made);
}

// ==== D-018: a reverted charge move keeps its roll and returns to target selection ====

TEST(D018_RevertedChargeMove, ARevertedMoveReturnsToTargetSelectionWithTheSameRoll) {
    GameState gs = charge_step_state();
    set_gap(gs, 9.9f);
    const auto start = positions(gs.units[kMarines]);
    ScriptedDice dice{4, 6};  // only the charge roll: no new dice may be rolled
    apply_action(gs, Side::A, declare(gs), dice);
    apply_action(gs, Side::A, charge_at(gs, {gs.units[kGants].id}), dice);
    move_marines_north(gs, 5.0f);
    apply_action(gs, Side::A, action::CommitUnitMove{}, dice);  // not engaged: reverts

    EXPECT_EQ(positions(gs.units[kMarines]), start);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::SelectChargeTargets{gs.units[kMarines].id}}));
    EXPECT_EQ(*gs.units[kMarines].phase_flags.charge_roll, 10);
    EXPECT_EQ(gs.flow.activation, (Activation{gs.units[kMarines].id, ActivationKind::Move}));

    const auto skipped = apply_action(gs, Side::A, action::SkipChargeMove{gs.units[kMarines].id}, dice);
    const auto resolved = events_of<event::ChargeResolved>(skipped);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_FALSE(resolved[0].charge_move_made);
    EXPECT_EQ(resolved[0].reason, "11.02: unit 1 does not make a charge move; the charge fails");
    EXPECT_EQ(positions(gs.units[kMarines]), start);
    EXPECT_FALSE(is_fights_first_unit(gs.units[kMarines]));
    EXPECT_EQ(dice.remaining(), 0u);
    expect_back_at_charge_step(gs);
}

TEST(D018_RevertedChargeMove, ResettingTheMoveAlsoReturnsToTargetSelection) {
    GameState gs = charge_step_state();
    set_gap(gs, 9.9f);
    ScriptedDice dice{4, 6};
    apply_action(gs, Side::A, declare(gs), dice);
    apply_action(gs, Side::A, charge_at(gs, {gs.units[kGants].id}));
    move_marines_north(gs, 3.0f);
    apply_action(gs, Side::A, action::ResetUnitMove{});
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::SelectChargeTargets{gs.units[kMarines].id}}));
}

TEST(R09_02_MoveUnits, AChargeMoveCannotBeSelectedInTheMovementPhase) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    const auto reasons =
        rejection(gs, Side::A, action::StartMove{gs.units[kMarines].id, MoveType::Charge, std::nullopt});
    EXPECT_EQ(reasons.at(0), "09.02: a Charge move is made in the Charge phase (11.02, 11.04)");
}

// ==== 24.13 Fights First ====

TEST(R24_13_FightsFirst, LastsUntilTheEndOfTheTurn) {
    GameState gs = charge_step_state();
    set_gap(gs, 9.9f);
    ScriptedDice dice{4, 6};
    apply_action(gs, Side::A, declare(gs), dice);
    apply_action(gs, Side::A, charge_at(gs, {gs.units[kGants].id}));
    move_marines_north(gs, 8.9f);
    apply_action(gs, Side::A, action::CommitUnitMove{});
    ASSERT_TRUE(is_fights_first_unit(gs.units[kMarines]));

    // The Fight phase follows (12.04); the end of the turn is covered in fight_test.cpp.
    apply_action(gs, Side::A, action::EndChargeStep{});
    EXPECT_EQ(gs.flow.stage, Stage::PileIn);
    EXPECT_TRUE(is_fights_first_unit(gs.units[kMarines]));
}
