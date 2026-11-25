// D-095 (M94): engine-paced dice rolls. The engine stops with a RollDice decision at every roll point; apply_action
// answers them inline (the M93 behaviour), apply_action_paced stops at them.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/distance.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::in;
using wh40k::test::repo_root;

namespace {

const std::filesystem::path kReplayDir{WH40K_REPLAY_DIR};

// The pending decision as a RollDice (a test failure, and a default one, when it isn't).
decision::RollDice pending_roll(const GameState& gs) {
    EXPECT_TRUE(roll_pending(gs)) << (gs.pending ? describe(gs, *gs.pending) : std::string("no decision"));
    if (!roll_pending(gs)) return {};
    return std::get<decision::RollDice>(gs.pending->kind);
}

// Answers the pending RollDice (paced: up to the next decision) from `dice`.
std::vector<GameEvent> roll(GameState& gs, DiceSource& dice) {
    const decision::RollDice d = pending_roll(gs);
    return apply_action_paced(gs, gs.pending->side, action::RollDice{d.roll, d.unit}, dice);
}

// D-095, the roll closure: `a` applied inline (apply_action) equals it applied paced with every roll then answered,
// all from the same scripted dice: the same state, the same events, the same dice. Returns the kinds of the rolls
// answered, in order.
std::vector<RollKind> expect_closure(const GameState& gs, Side side, const GameAction& a, const std::vector<int>& rolls) {
    GameState at_once = gs;
    ScriptedDice d1{rolls};
    const std::vector<GameEvent> e1 = apply_action(at_once, side, a, d1);
    GameState paced = gs;
    ScriptedDice d2{rolls};
    std::vector<GameEvent> e2 = apply_action_paced(paced, side, a, d2);
    std::vector<RollKind> kinds;
    while (roll_pending(paced)) {
        kinds.push_back(pending_roll(paced).roll);
        const std::vector<GameEvent> more = roll(paced, d2);
        e2.insert(e2.end(), more.begin(), more.end());
    }
    EXPECT_EQ(Json(paced).dump(), Json(at_once).dump());
    EXPECT_EQ(paced, at_once);
    EXPECT_EQ(e2, e1);
    EXPECT_EQ(d2.consumed(), d1.consumed());
    return kinds;
}

// The V1 scenario at Side A's Charge step, the Termagants moved to `gap_in` from the Intercessors.
GameState charge_step_state(float gap_in) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (!(gs.flow.stage == Stage::Charge && turn_side(gs) == Side::A)) {
        apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    }
    const float gap = mm_to_inches(*unit_distance(gs.units[0], gs.units[1]));
    for (auto& m : gs.units[1].models) m.position.y -= in(gap - gap_in);
    return gs;
}

std::vector<std::filesystem::path> replay_files() {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(kReplayDir)) {
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace

// The closure check's reference: every saved replay's events and final state, written to $WH40K_EVENT_DUMP_DIR (one
// file per replay). M94 ran it with the M93 engine before any roll point moved, then with the M94 engine on the
// regenerated format-2 files; the two dumps must be identical. Run on purpose only:
//   WH40K_EVENT_DUMP_DIR=<dir> wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D095_Closure.DISABLED_DumpReplayEvents
TEST(D095_Closure, DISABLED_DumpReplayEvents) {
    const char* dir = std::getenv("WH40K_EVENT_DUMP_DIR");
    if (dir == nullptr) GTEST_SKIP() << "set WH40K_EVENT_DUMP_DIR";
    std::filesystem::create_directories(dir);
    for (const auto& file : replay_files()) {
        SCOPED_TRACE(file.filename().string());
        const Replay replay = load_replay(file);
        const ReplayResult r = run_replay(replay, repo_root());
        ASSERT_TRUE(r.ok()) << "step " << *r.failed_step << ": " << r.reasons.front();
        std::ofstream out(std::filesystem::path(dir) / file.filename(), std::ios::binary);
        out << Json{{"events", r.events}, {"state", r.final_state}}.dump() << '\n';
    }
}

// ---- 11.02 Make Charge Roll ----

TEST(D095_RollPoints, TheChargeRollWaitsForTheChargingUnitsPlayer) {
    GameState gs = charge_step_state(9.9f);
    const UnitId marines = gs.units[0].id;
    const UnitId gants = gs.units[1].id;
    ScriptedDice none{std::vector<int>{}};
    const auto declared = apply_action_paced(gs, Side::A, action::DeclareCharge{marines}, none);
    EXPECT_EQ(none.consumed(), 0u);
    EXPECT_EQ(events_of<event::ChargeDeclared>(declared).size(), 1u);
    EXPECT_TRUE(events_of<event::ChargeRolled>(declared).empty());
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::RollDice{RollKind::Charge, marines}}));
    EXPECT_EQ(legal_actions(gs, Side::A), (std::vector<GameAction>{action::RollDice{RollKind::Charge, marines}}));
    EXPECT_TRUE(legal_actions(gs, Side::B).empty());
    EXPECT_EQ(decision_rule(gs, *gs.pending), "11.02");
    EXPECT_EQ(compute_decision(gs), gs.pending);
    const RollDiceInfo info = roll_dice_info(gs).value();
    EXPECT_EQ(info.roller, Side::A);
    EXPECT_EQ(info.dice, 2);
    EXPECT_EQ(info.expression, "2D6");

    ScriptedDice dice{4, 6};
    const auto rolled = roll(gs, dice);
    EXPECT_EQ(dice.consumed(), 2u);
    const auto charge = events_of<event::ChargeRolled>(rolled);
    ASSERT_EQ(charge.size(), 1u);
    EXPECT_EQ(charge[0].roll.total(), 10);
    EXPECT_EQ(charge[0].eligible_targets, (std::vector<UnitId>{gants}));
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::SelectChargeTargets{marines}}));
}

TEST(D095_RollPoints, AChargeRollWithNoTargetWithinItFailsTheCharge) {
    GameState gs = charge_step_state(9.9f);
    const UnitId marines = gs.units[0].id;
    ScriptedDice dice{1, 2};
    apply_action_paced(gs, Side::A, action::DeclareCharge{marines}, dice);
    const auto rolled = roll(gs, dice);
    const auto resolved = events_of<event::ChargeResolved>(rolled);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_FALSE(resolved[0].charge_move_made);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::Charge{}}));
    EXPECT_FALSE(gs.flow.charge.has_value());
}

TEST(D095_RollPoints, OnlyTheRollWaitedForIsAccepted) {
    GameState gs = charge_step_state(9.9f);
    const UnitId marines = gs.units[0].id;
    const UnitId gants = gs.units[1].id;
    ScriptedDice none{std::vector<int>{}};
    apply_action_paced(gs, Side::A, action::DeclareCharge{marines}, none);
    // The other side, another roll, another unit, another action: each refused with a rule-cited reason.
    EXPECT_FALSE(wh40k::test::rejection(gs, Side::B, action::RollDice{RollKind::Charge, marines}).empty());
    EXPECT_FALSE(wh40k::test::rejection(gs, Side::A, action::RollDice{RollKind::Advance, marines}).empty());
    EXPECT_FALSE(wh40k::test::rejection(gs, Side::A, action::RollDice{RollKind::Charge, gants}).empty());
    EXPECT_FALSE(wh40k::test::rejection(gs, Side::A, action::SkipChargeMove{marines}).empty());
    EXPECT_FALSE(wh40k::test::rejection(gs, Side::A, action::EndChargeStep{}).empty());
}

TEST(D095_Closure, TheChargeRollIsMadeAsBefore) {
    const GameState gs = charge_step_state(9.9f);
    EXPECT_EQ(expect_closure(gs, Side::A, action::DeclareCharge{gs.units[0].id}, {4, 6}),
              (std::vector<RollKind>{RollKind::Charge}));
    EXPECT_EQ(expect_closure(gs, Side::A, action::DeclareCharge{gs.units[0].id}, {1, 1}),
              (std::vector<RollKind>{RollKind::Charge}));
}

// ---- 09.06 Advance roll ----

TEST(D095_RollPoints, TheAdvanceRollWaitsForTheMovingUnitsPlayer) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);  // Side A's Move Units step
    const UnitId marines = gs.units[0].id;
    const ModelId first = gs.units[0].models[0].id;
    ScriptedDice none{std::vector<int>{}};
    const auto started =
        apply_action_paced(gs, Side::A, action::StartMove{marines, MoveType::Advance, std::nullopt}, none);
    EXPECT_EQ(none.consumed(), 0u);
    EXPECT_TRUE(events_of<event::AdvanceRolled>(started).empty());
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::RollDice{RollKind::Advance, marines}}));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "09.06");
    EXPECT_EQ(max_move_distance_mm(gs, first), std::nullopt);  // not known before the roll
    EXPECT_EQ(roll_dice_info(gs)->dice, 1);

    ScriptedDice dice{3};
    const auto rolled = roll(gs, dice);
    const auto advance = events_of<event::AdvanceRolled>(rolled);
    ASSERT_EQ(advance.size(), 1u);
    EXPECT_EQ(advance[0].roll, 3);
    EXPECT_FALSE(advance[0].reused);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::MoveModels{marines}}));
    EXPECT_FLOAT_EQ(*max_move_distance_mm(gs, first), *gs.units[0].models[0].characteristics.move_mm + in(3));
}

TEST(D095_RollPoints, AReusedAdvanceRollIsNotRolledAgain) {
    // D-005: the roll of a reverted Advance stands for the phase, so a second Advance asks for no roll.
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    const UnitId marines = gs.units[0].id;
    ScriptedDice dice{5};
    apply_action_paced(gs, Side::A, action::StartMove{marines, MoveType::Advance, std::nullopt}, dice);
    roll(gs, dice);
    apply_action_paced(gs, Side::A, action::ResetUnitMove{});
    const auto again = apply_action_paced(gs, Side::A, action::StartMove{marines, MoveType::Advance, std::nullopt});
    const auto advance = events_of<event::AdvanceRolled>(again);
    ASSERT_EQ(advance.size(), 1u);
    EXPECT_EQ(advance[0].roll, 5);
    EXPECT_TRUE(advance[0].reused);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::MoveModels{marines}}));
}

TEST(D095_Closure, TheAdvanceRollIsMadeAsBefore) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    EXPECT_EQ(expect_closure(gs, Side::A, action::StartMove{gs.units[0].id, MoveType::Advance, std::nullopt}, {4}),
              (std::vector<RollKind>{RollKind::Advance}));
    EXPECT_TRUE(expect_closure(gs, Side::A, action::StartMove{gs.units[0].id, MoveType::Normal, std::nullopt}, {})
                    .empty());
}

// ---- 09.07 Desperate Escape: hazard rolls before the move, a battle-shock roll after it ----

namespace {

// Side A's Move Units step with a Termagant 1" west of the first Intercessor (engaged, 03.04), the Intercessors cut
// to `models` models.
GameState engaged_marines(std::size_t models) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    gs.units[0].models.resize(models);
    const Position m = gs.units[0].models[0].position;
    gs.units[1].models[0].position = Position{m.x - 16.0f - 14.25f - in(1), m.y, 0.0f};
    gs.pending = compute_decision(gs);
    return gs;
}

}  // namespace

TEST(D095_RollPoints, ADesperateEscapeRollsItsHazardsBeforeTheMoveAndItsBattleShockAfter) {
    GameState gs = engaged_marines(2);
    const UnitId marines = gs.units[0].id;
    ScriptedDice none{std::vector<int>{}};
    apply_action_paced(gs, Side::A, action::StartMove{marines, MoveType::FallBack, FallBackMode::DesperateEscape}, none);
    EXPECT_EQ(none.consumed(), 0u);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::RollDice{RollKind::Hazard, marines}}));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "09.07");
    EXPECT_EQ(roll_dice_info(gs)->dice, 2);  // one hazard roll per model

    ScriptedDice hazards{1, 6};  // one fails: a mortal wound for the unit's player to allocate
    const auto rolled = roll(gs, hazards);
    ASSERT_EQ(events_of<event::HazardRollsMade>(rolled).size(), 1u);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{marines}}));
    apply_action_paced(gs, Side::A, action::OrderCasualties{marines, wh40k::test::model_ids(gs.units[0])});
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::MoveModels{marines}}));

    const ModelId first = gs.units[0].models[0].id;
    const Position start = *move_start_position(gs, first);
    apply_action_paced(gs, Side::A, action::ProposeModelMove{first, {Position{start.x + in(3), start.y, 0.0f}}});
    const auto committed = apply_action_paced(gs, Side::A, action::CommitUnitMove{}, none);
    EXPECT_EQ(none.consumed(), 0u);
    EXPECT_EQ(events_of<event::UnitMoveCommitted>(committed).size(), 1u);
    EXPECT_TRUE(events_of<event::BattleShockRolled>(committed).empty());
    EXPECT_FALSE(gs.flow.move.has_value());  // the move is over; the roll is owed
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::RollDice{RollKind::BattleShock, marines}}));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "09.07");

    ScriptedDice shock{1, 2};  // 3: below Ld 6+, battle-shocked (01.07)
    const auto tested = roll(gs, shock);
    ASSERT_EQ(events_of<event::BattleShockRolled>(tested).size(), 1u);
    EXPECT_TRUE(gs.units[0].battle_shocked);
    EXPECT_TRUE(gs.flow.battle_shock_tests.empty());
    // The Intercessors were Side A's only unit to move: the Move Units step ends after the roll (and not before it).
    EXPECT_EQ(gs.flow.stage, Stage::Shoot);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
}

TEST(D095_Closure, ADesperateEscapeRollsAsBefore) {
    GameState gs = engaged_marines(2);
    const UnitId marines = gs.units[0].id;
    EXPECT_EQ(expect_closure(gs, Side::A, action::StartMove{marines, MoveType::FallBack, FallBackMode::DesperateEscape},
                             {6, 6}),
              (std::vector<RollKind>{RollKind::Hazard}));
    apply_action(gs, Side::A, action::StartMove{marines, MoveType::FallBack, FallBackMode::DesperateEscape},
                 *std::make_unique<ScriptedDice>(std::vector<int>{6, 6}));
    const ModelId first = gs.units[0].models[0].id;
    const Position start = *move_start_position(gs, first);
    apply_action(gs, Side::A, action::ProposeModelMove{first, {Position{start.x + in(3), start.y, 0.0f}}});
    EXPECT_EQ(expect_closure(gs, Side::A, action::CommitUnitMove{}, {5, 4}),
              (std::vector<RollKind>{RollKind::BattleShock}));
}

// ---- 08.03 Battle-shock step ----

TEST(D095_RollPoints, TheBattleShockStepAsksForEachUnitsRollInUnitOrder) {
    // Side B's two Termagant units both below half-strength: entering Side B's Battle-shock step owes two rolls, in
    // unit order, each for Side B (01.03: the active player), each made only when answered.
    GameState gs = wh40k::test::load_v1_scenario();
    gs.units[0].models.resize(2);  // 2 of 5 Intercessors (Side A's: not tested in Side B's step)
    Unit second = gs.units[1];     // a second Termagant unit, 12" east of the first
    second.id = UnitId{50};
    for (std::size_t i = 0; i < second.models.size(); ++i) {
        second.models[i].id = ModelId{static_cast<std::uint32_t>(500 + i)};
        second.models[i].position.x += in(12);
    }
    second.models.resize(3);       // 3 of 10
    gs.units[1].models.resize(4);  // 4 of 10
    gs.units.push_back(second);
    start_game(gs);
    // Side A's turn passes: its Battle-shock step owes Side A's Intercessors a roll first (made inline here).
    ScriptedDice none{std::vector<int>{}};
    while (!(gs.flow.stage == Stage::BattleShock && turn_side(gs) == Side::B)) {
        ASSERT_FALSE(roll_pending(gs)) << "Side A owes no roll after its own step";
        apply_action_paced(gs, decider(gs), wh40k::test::neutral_action(gs), none);
    }
    EXPECT_EQ(none.consumed(), 0u);
    const UnitId first = gs.units[1].id;
    EXPECT_EQ(gs.flow.battle_shock_tests, (std::vector<UnitId>{first, UnitId{50}}));
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::BattleShock, first}}));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "08.03");
    EXPECT_EQ(roll_dice_info(gs)->expression, "2D6");
    ScriptedDice dice{6, 6, 1, 1};
    const auto tested = roll(gs, dice);
    ASSERT_EQ(events_of<event::BattleShockRolled>(tested).size(), 1u);
    EXPECT_EQ(dice.consumed(), 2u);
    EXPECT_FALSE(gs.units[1].battle_shocked);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::BattleShock, UnitId{50}}}));
    roll(gs, dice);
    EXPECT_TRUE(gs.find_unit(UnitId{50})->battle_shocked);
    EXPECT_TRUE(gs.flow.battle_shock_tests.empty());
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::MoveUnits{}}));
}

TEST(D095_Closure, TheBattleShockStepRollsAsBefore) {
    // The same units, every roll made at once: the end of Side A's turn leads into Side B's two battle-shock rolls.
    GameState gs = wh40k::test::load_v1_scenario();
    Unit second = gs.units[1];
    second.id = UnitId{50};
    for (std::size_t i = 0; i < second.models.size(); ++i) {
        second.models[i].id = ModelId{static_cast<std::uint32_t>(500 + i)};
        second.models[i].position.x += in(12);
    }
    second.models.resize(3);
    gs.units[1].models.resize(4);
    gs.units.push_back(second);
    start_game(gs);
    while (!(gs.flow.stage == Stage::Charge && turn_side(gs) == Side::A)) {
        apply_action(gs, decider(gs), wh40k::test::neutral_action(gs));
    }
    // EndChargeStep: the Fight phase, the end of the turn, then Side B's Command phase and its rolls.
    EXPECT_EQ(expect_closure(gs, Side::A, action::EndChargeStep{}, {6, 6, 1, 1}),
              (std::vector<RollKind>{RollKind::BattleShock, RollKind::BattleShock}));
}
