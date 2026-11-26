// D-095 (M94): engine-paced dice rolls. The engine stops with a RollDice decision at every roll point; apply_action
// answers them inline (the M93 behaviour), apply_action_paced stops at them.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/data/load_error.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/distance.hpp"
#include "core/model/keywords.hpp"
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

// D-096 (M95): declines every reaction window waiting, adding its events to `events`. These tests follow M94's flow, in
// which no stratagem is used (D096 tests the windows themselves); declining draws no dice.
void decline_windows(GameState& gs, std::vector<GameEvent>* events = nullptr) {
    ScriptedDice none{std::vector<int>{}};
    while (window_pending(gs)) {
        const std::vector<GameEvent> e = apply_action_paced(gs, gs.pending->side, action::DeclineStratagems{}, none);
        if (events != nullptr) events->insert(events->end(), e.begin(), e.end());
    }
}

// Answers the pending RollDice (paced: up to the next decision) from `dice`, with the windows before and after it
// declined (D-096).
std::vector<GameEvent> roll(GameState& gs, DiceSource& dice) {
    decline_windows(gs);
    const decision::RollDice d = pending_roll(gs);
    std::vector<GameEvent> events = apply_action_paced(gs, gs.pending->side, action::RollDice{d.roll, d.unit}, dice);
    decline_windows(gs, &events);
    return events;
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
    decline_windows(paced, &e2);  // D-096: every window declined
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
// M95 (D-096): WH40K_EVENT_DUMP_WITHOUT=<event kind> leaves that kind out (DamageRolled, the one event M95 adds to a game
// without stratagems), so an M95 dump compares with an M94 one. The filter is here, not in a script, so every number
// keeps the dump's own format.
TEST(D095_Closure, DISABLED_DumpReplayEvents) {
    const char* dir = std::getenv("WH40K_EVENT_DUMP_DIR");
    if (dir == nullptr) GTEST_SKIP() << "set WH40K_EVENT_DUMP_DIR";
    const char* without = std::getenv("WH40K_EVENT_DUMP_WITHOUT");
    std::filesystem::create_directories(dir);
    for (const auto& file : replay_files()) {
        SCOPED_TRACE(file.filename().string());
        const Replay replay = load_replay(file);
        const ReplayResult r = run_replay(replay, repo_root());
        ASSERT_TRUE(r.ok()) << "step " << *r.failed_step << ": " << r.reasons.front();
        std::vector<GameEvent> events = r.events;
        if (without != nullptr) {
            std::erase_if(events, [without](const GameEvent& e) { return event_name(e) == std::string_view(without); });
        }
        std::ofstream out(std::filesystem::path(dir) / file.filename(), std::ios::binary);
        out << Json{{"events", events}, {"state", r.final_state}}.dump() << '\n';
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
    decline_windows(gs);  // D-096: Insane Bravery's window (15.04), declined
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

// ---- 04.03 random A (D-069), 05.01 hit, 05.02 wound, 05.03 save, 05.04 damage rolls; 24.15 hazard rolls ----

namespace {

// Side B's Shoot step in V1 (the Termagants 1" closer) and the Shoot action firing all ten fleshborers at the
// Intercessors, each fleshborer changed by `change`. Fleshborer: BS4+, S5 against T4, AP0 against Sv3+, D1.
struct Volley {
    GameState gs;
    action::Shoot shoot;
};
template <typename Change>
Volley fleshborer_volley(Change change) {
    Volley v{wh40k::test::shoot_step_state(Side::B), {}};
    for (Model& m : v.gs.units[1].models) change(m.weapons[0].profiles[0]);
    v.shoot = action::Shoot{v.gs.units[1].id, ShootingType::Normal, {}};
    for (const Model& m : v.gs.units[1].models) {
        v.shoot.shots.push_back(ShotDeclaration{m.id, 0, 0, v.gs.units[0].id});
    }
    return v;
}
Volley fleshborer_volley() {
    return fleshborer_volley([](WeaponProfile&) {});
}

const DiceExpr kD3{1, 3, 0};

}  // namespace

TEST(D095_RollPoints, AnAttacksRollsWaitForTheirRollersInTurn) {
    // Hit and wound rolls are the attacking unit's player's (Side B); save rolls the opposing player's (Side A, 05.03);
    // a random D's damage rolls the attacking unit's player's again (D-069), after the casualty order (D-013).
    Volley v = fleshborer_volley([](WeaponProfile& p) { p.damage = kD3; });
    GameState& gs = v.gs;
    const UnitId marines = gs.units[0].id;
    const UnitId gants = gs.units[1].id;
    ScriptedDice none{std::vector<int>{}};
    const auto shot = apply_action_paced(gs, Side::B, v.shoot, none);
    EXPECT_EQ(none.consumed(), 0u);
    EXPECT_EQ(events_of<event::AttackBatchSelected>(shot).size(), 1u);  // gathered: no random A
    EXPECT_TRUE(events_of<event::HitRollsMade>(shot).empty());
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Hit, gants}}));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "05.01");
    RollDiceInfo info = roll_dice_info(gs).value();
    EXPECT_EQ(info.dice, 10);
    EXPECT_EQ(info.target, marines);

    ScriptedDice hits{6, 6, 6, 1, 1, 1, 1, 1, 1, 1};
    const auto hit = roll(gs, hits);
    EXPECT_EQ(hits.consumed(), 10u);
    ASSERT_EQ(events_of<event::HitRollsMade>(hit).size(), 1u);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Wound, gants}}));
    EXPECT_EQ(roll_dice_info(gs)->dice, 3);

    ScriptedDice wounds{6, 6, 6};
    const auto wound = roll(gs, wounds);
    ASSERT_EQ(events_of<event::WoundRollsMade>(wound).size(), 1u);
    // One allocation group (05.03): its order is declared without asking; the save rolls are the defender's.
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::RollDice{RollKind::Save, marines}}));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "05.03");
    info = roll_dice_info(gs).value();
    EXPECT_EQ(info.roller, Side::A);
    EXPECT_EQ(info.dice, 3);
    EXPECT_EQ(info.target, marines);

    ScriptedDice saves{1, 1, 1};
    const auto saved = roll(gs, saves);
    ASSERT_EQ(events_of<event::SaveRollsMade>(saved).size(), 1u);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{marines}}));
    apply_action_paced(gs, Side::A, action::OrderCasualties{marines, wh40k::test::model_ids(gs.units[0])}, none);
    EXPECT_EQ(none.consumed(), 0u);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Damage, gants}}));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "05.04");
    info = roll_dice_info(gs).value();
    EXPECT_EQ(info.expression, "D3");
    EXPECT_EQ(info.dice, 1);  // D-096 (M95): one attack's D3 (D-069), for the model the casualty order selects
    EXPECT_EQ(info.model, gs.units[0].models.front().id);
    EXPECT_EQ(info.target, marines);
    EXPECT_EQ(compute_decision(gs), gs.pending);

    ScriptedDice damage{6, 6, 6};  // D3 = 3 each: three W2 Intercessors destroyed
    std::vector<GameEvent> resolved;
    for (int i = 0; i < 3; ++i) {
        // D-096: a Damage roll per attack that inflicts damage, each its own step; the batch resolves after the last.
        ASSERT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Damage, gants}}));
        EXPECT_EQ(compute_decision(gs), gs.pending);
        const auto more = roll(gs, damage);
        resolved.insert(resolved.end(), more.begin(), more.end());
    }
    EXPECT_EQ(damage.consumed(), 3u);
    EXPECT_EQ(events_of<event::DamageRolled>(resolved).size(), 3u);
    ASSERT_EQ(events_of<event::SavesResolved>(resolved).size(), 1u);
    EXPECT_EQ(events_of<event::ModelDestroyed>(resolved).size(), 3u);
    EXPECT_EQ(gs.units[0].models.size(), 2u);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::Shoot{}}));
}

TEST(D095_RollPoints, AFixedDamageAsksForNoDamageRoll) {
    Volley v = fleshborer_volley();
    GameState& gs = v.gs;
    const UnitId marines = gs.units[0].id;
    ScriptedDice dice{6, 6, 6, 1, 1, 1, 1, 1, 1, 1, 6, 6, 6, 1, 1, 1};
    apply_action_paced(gs, Side::B, v.shoot, dice);
    roll(gs, dice);  // hits
    roll(gs, dice);  // wounds
    roll(gs, dice);  // saves
    ASSERT_EQ(gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{marines}}));
    const auto resolved =
        apply_action_paced(gs, Side::A, action::OrderCasualties{marines, wh40k::test::model_ids(gs.units[0])}, dice);
    EXPECT_EQ(dice.remaining(), 0u);
    ASSERT_EQ(events_of<event::SavesResolved>(resolved).size(), 1u);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::Shoot{}}));
}

TEST(D095_RollPoints, NoHitRollForTorrentAndNoWoundRollWithoutHits) {
    // [TORRENT] hits automatically (24.37): its hit rolls are made at once, with no dice, and the wound rolls wait.
    {
        Volley v = fleshborer_volley([](WeaponProfile& p) { p.abilities.push_back(ability::Torrent{}); });
        ScriptedDice none{std::vector<int>{}};
        const auto shot = apply_action_paced(v.gs, Side::B, v.shoot, none);
        EXPECT_EQ(none.consumed(), 0u);
        ASSERT_EQ(events_of<event::HitRollsMade>(shot).size(), 1u);
        EXPECT_EQ(events_of<event::HitRollsMade>(shot)[0].rolls.hits(), 10);
        EXPECT_EQ(v.gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Wound, v.gs.units[1].id}}));
    }
    // No hits: the wound rolls are made at once (none), with their required roll, and the attacks are over.
    {
        Volley v = fleshborer_volley();
        ScriptedDice dice{1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        apply_action_paced(v.gs, Side::B, v.shoot, dice);
        const auto missed = roll(v.gs, dice);
        const auto wounds = events_of<event::WoundRollsMade>(missed);
        ASSERT_EQ(wounds.size(), 1u);
        EXPECT_TRUE(wounds[0].rolls.rolls.empty());
        EXPECT_EQ(wounds[0].rolls.required, 4);  // S4 against T4 (05.02)
        EXPECT_EQ(v.gs.pending, (PendingDecision{Side::B, decision::Shoot{}}));
    }
}

TEST(D095_RollPoints, ARandomAIsRolledWhenTheBatchIsGathered) {
    // D-069: the batch's random A (one D3 per fleshborer here) waits for the attacking unit's player; the batch is
    // announced once gathered, then its hit rolls wait.
    Volley v = fleshborer_volley([](WeaponProfile& p) { p.attacks = kD3; });
    GameState& gs = v.gs;
    const UnitId gants = gs.units[1].id;
    ScriptedDice none{std::vector<int>{}};
    const auto shot = apply_action_paced(gs, Side::B, v.shoot, none);
    EXPECT_TRUE(events_of<event::AttackBatchSelected>(shot).empty());
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::RandomAttacks, gants}}));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "04.03");
    const RollDiceInfo info = roll_dice_info(gs).value();
    EXPECT_EQ(info.dice, 10);
    EXPECT_EQ(info.expression, "D3");
    ScriptedDice attacks{1, 1, 1, 1, 1, 1, 1, 1, 1, 6};  // nine 1s (D3 = 1) and a 6 (D3 = 3): 12 attack dice
    const auto gathered = roll(gs, attacks);
    ASSERT_EQ(events_of<event::AttackDiceRolled>(gathered).size(), 1u);
    const auto selected = events_of<event::AttackBatchSelected>(gathered);
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected[0].attack_dice, 12);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Hit, gants}}));
    EXPECT_EQ(roll_dice_info(gs)->dice, 12);
}

TEST(D095_RollPoints, HazardRollsWaitAfterTheAttacksUnlessNoEnemyIsLeft) {
    // 24.15: ten [HAZARDOUS] fleshborers (one batch): after the attacks, their hazard rolls (one per weapon, made
    // together) wait for the attacking unit's player.
    const auto hazardous = [](WeaponProfile& p) { p.abilities.push_back(ability::Hazardous{}); };
    {
        Volley v = fleshborer_volley(hazardous);
        GameState& gs = v.gs;
        ScriptedDice misses{1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        apply_action_paced(gs, Side::B, v.shoot, misses);
        roll(gs, misses);  // every hit roll fails: the attacks are over
        EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Hazard, gs.units[1].id}}));
        EXPECT_EQ(decision_rule(gs, *gs.pending), "24.15");
        EXPECT_EQ(roll_dice_info(gs)->dice, 10);
        ScriptedDice hazard{1, 6, 6, 6, 6, 6, 6, 6, 6, 6};  // one fails: a mortal wound for Side B to allocate (06.02)
        const auto rolled = roll(gs, hazard);
        ASSERT_EQ(events_of<event::HazardRollsMade>(rolled).size(), 1u);
        EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::OrderCasualties{gs.units[1].id}}));
    }
    // D-071: none is made once no enemy model is left.
    {
        Volley v = fleshborer_volley(hazardous);
        v.gs.units[0].models.resize(1);
        GameState& gs = v.gs;
        const UnitId marines = gs.units[0].id;
        ScriptedDice dice{6, 6, 6, 1, 1, 1, 1, 1, 1, 1, 6, 6, 6, 1, 1, 1};
        apply_action_paced(gs, Side::B, v.shoot, dice);
        roll(gs, dice);  // hits
        roll(gs, dice);  // wounds
        roll(gs, dice);  // saves
        apply_action_paced(gs, Side::A, action::OrderCasualties{marines, wh40k::test::model_ids(gs.units[0])}, dice);
        EXPECT_TRUE(is_destroyed(gs.units[0]));
        EXPECT_FALSE(roll_pending(gs));
        EXPECT_EQ(gs.flow.stage, Stage::GameOver);  // D-007
    }
}

TEST(D095_RollPoints, ATwinLinkedBatchsWoundRollIsMadeBeforeItsRerollsAreChosen) {
    Volley v = fleshborer_volley([](WeaponProfile& p) { p.abilities.push_back(ability::TwinLinked{}); });
    GameState& gs = v.gs;
    ScriptedDice dice{6, 6, 6, 1, 1, 1, 1, 1, 1, 1, 6, 1, 1};
    apply_action_paced(gs, Side::B, v.shoot, dice);
    roll(gs, dice);  // hits
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Wound, gs.units[1].id}}));
    roll(gs, dice);  // wounds: two fail
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RerollWoundRolls{gs.units[1].id}}));
}

TEST(D095_Closure, AnAttacksRollsAreMadeAsBefore) {
    {
        const Volley v = fleshborer_volley([](WeaponProfile& p) { p.damage = kD3; });
        EXPECT_EQ(expect_closure(v.gs, Side::B, v.shoot, {6, 6, 6, 1, 1, 1, 1, 1, 1, 1, 6, 6, 6, 1, 1, 1}),
                  (std::vector<RollKind>{RollKind::Hit, RollKind::Wound, RollKind::Save}));
        GameState gs = v.gs;
        ScriptedDice dice{6, 6, 6, 1, 1, 1, 1, 1, 1, 1, 6, 6, 6, 1, 1, 1};
        apply_action(gs, Side::B, v.shoot, dice);
        const UnitId marines = gs.units[0].id;
        // D-096 (M95): one Damage roll per attack that inflicts damage (three here).
        EXPECT_EQ(expect_closure(gs, Side::A, action::OrderCasualties{marines, wh40k::test::model_ids(gs.units[0])},
                                 {4, 2, 6}),
                  (std::vector<RollKind>(3, RollKind::Damage)));
    }
    {
        const Volley v = fleshborer_volley([](WeaponProfile& p) { p.attacks = kD3; });
        EXPECT_EQ(expect_closure(v.gs, Side::B, v.shoot, std::vector<int>(40, 1)),
                  (std::vector<RollKind>{RollKind::RandomAttacks, RollKind::Hit}));
    }
    {
        const Volley v = fleshborer_volley([](WeaponProfile& p) { p.abilities.push_back(ability::Hazardous{}); });
        EXPECT_EQ(expect_closure(v.gs, Side::B, v.shoot, std::vector<int>(20, 1)),
                  (std::vector<RollKind>{RollKind::Hit, RollKind::Hazard}));
    }
}

// ---- The closure over whole games ----

namespace {

// The steps a format-1 file records: without the rolls (D-095) and the declined windows (D-096).
std::vector<ReplayStep> without_rolls(const std::vector<ReplayStep>& steps) {
    std::vector<ReplayStep> out;
    for (const ReplayStep& s : steps) {
        if (!std::holds_alternative<action::RollDice>(s.action) &&
            !std::holds_alternative<action::DeclineStratagems>(s.action)) {
            out.push_back(s);
        }
    }
    return out;
}

}  // namespace

TEST(D095_Closure, EverySavedReplayPlaysTheSamePacedAndInline) {
    // Each saved replay as format 2 (each roll a step: the file itself, or with_roll_steps of a format-1 file), run
    // paced, and its steps without the rolls run inline: the same events and the same final state, and its expect block.
    int files = 0;
    int with_stratagems = 0;
    std::size_t rolls = 0;
    for (const auto& file : replay_files()) {
        SCOPED_TRACE(file.filename().string());
        ++files;
        const Replay saved = load_replay(file);
        const Replay paced = with_roll_steps(saved, repo_root());
        EXPECT_EQ(paced.format, kReplayFormatWindowsRecorded);  // D-096 (M95): format 3
        // D-096: inline play declines every window, so a game that uses a stratagem has no inline twin; it is checked
        // paced only (m95_stratagems.json, D096_Replay).
        const bool uses_stratagems = std::any_of(paced.steps.begin(), paced.steps.end(), [](const ReplayStep& s) {
            return std::holds_alternative<action::UseStratagem>(s.action);
        });
        if (uses_stratagems) {
            ++with_stratagems;
            const ReplayResult p = run_replay(paced, repo_root());
            ASSERT_TRUE(p.ok()) << "step " << *p.failed_step << ": " << p.reasons.front();
            EXPECT_EQ(replay_expectation(p.final_state), saved.expect);
            continue;
        }
        Replay at_once = paced;
        at_once.format = kReplayFormatRollsInline;
        at_once.steps = without_rolls(paced.steps);
        if (saved.format == kReplayFormatRollsInline) EXPECT_EQ(at_once.steps, saved.steps);
        const ReplayResult p = run_replay(paced, repo_root());
        const ReplayResult i = run_replay(at_once, repo_root());
        ASSERT_TRUE(p.ok()) << "step " << *p.failed_step << ": " << p.reasons.front();
        ASSERT_TRUE(i.ok()) << "step " << *i.failed_step << ": " << i.reasons.front();
        EXPECT_EQ(p.events, i.events);
        EXPECT_EQ(p.final_state, i.final_state);
        EXPECT_EQ(replay_expectation(p.final_state), saved.expect);
        rolls += paced.steps.size() - at_once.steps.size();
    }
    EXPECT_GE(files, 30);
    EXPECT_EQ(with_stratagems, 1);
    EXPECT_GT(rolls, 500u);  // the bot games alone roll hundreds of times
}

TEST(D095_Closure, RandomGamesPlayTheSamePacedAndInline) {
    // Random legal play through a paced match (each roll answered as it comes), then its choices replayed inline.
    const std::vector<GameState> scenarios{wh40k::test::load_v1_scenario(), wh40k::test::load_rosters_scenario(),
                                           load_scenario(repo_root() / "data/scenarios/strike_44x60.json", repo_root())};
    for (std::size_t n = 0; n < scenarios.size(); ++n) {
        for (std::uint64_t seed = 1; seed <= 4; ++seed) {
            SCOPED_TRACE("scenario " + std::to_string(n) + " seed " + std::to_string(seed));
            GameState initial = scenarios[n];
            initial.dice = DiceState{seed, 0};
            Match paced(initial, RollMode::Paced);
            wh40k::test::TestRng rng(seed * 7);
            int rolls = 0;
            for (int i = 0; i < 400 && paced.state().pending; ++i) {
                // D-096: every window declined (the roll closure; D096 plays games that use stratagems).
                const GameAction a = window_pending(paced.state()) ? GameAction{action::DeclineStratagems{}}
                                                                   : wh40k::test::random_action(paced.state(), rng);
                rolls += std::holds_alternative<action::RollDice>(a) ? 1 : 0;
                ASSERT_TRUE(paced.submit(decider(paced.state()), a).ok());
            }
            // Stopped at a roll or a window: the inline game is past it.
            if (roll_pending(paced.state()) || window_pending(paced.state())) continue;
            EXPECT_GT(rolls, 0);
            Match at_once(initial);
            for (const ReplayStep& s : without_rolls(paced.steps())) ASSERT_TRUE(at_once.submit(s.side, s.action).ok());
            EXPECT_EQ(at_once.state(), paced.state());
            EXPECT_EQ(at_once.events(), paced.events());
            EXPECT_EQ(with_roll_steps(initial, at_once.steps()), paced.steps());
        }
    }
}

TEST(D095_Closure, AStateWaitingForEachRollRoundTripsAndTheNewFieldsAreOmittedOtherwise) {
    const auto round_trip = [](const GameState& gs) { return Json(gs).get<GameState>(); };
    std::vector<GameState> waiting;
    {
        Volley v = fleshborer_volley([](WeaponProfile& p) {
            p.damage = kD3;
            p.attacks = kD3;
        });
        GameState& gs = v.gs;
        ScriptedDice dice{std::vector<int>(10, 6)};
        apply_action_paced(gs, Side::B, v.shoot, dice);
        waiting.push_back(gs);  // RandomAttacks: rolling, no hits
        roll(gs, dice);
        waiting.push_back(gs);  // Hit
        ScriptedDice hits{std::vector<int>(30, 6)};
        roll(gs, hits);
        waiting.push_back(gs);  // Wound: rolling with its hit rolls
        ScriptedDice wounds{std::vector<int>(30, 6)};
        roll(gs, wounds);
        waiting.push_back(gs);  // Save
        ScriptedDice saves{std::vector<int>(30, 1)};
        roll(gs, saves);
        apply_action_paced(gs, Side::A, action::OrderCasualties{gs.units[0].id, wh40k::test::model_ids(gs.units[0])});
        waiting.push_back(gs);  // Damage: the casualty order held
        EXPECT_TRUE(gs.flow.attack->current->priority.has_value());
    }
    {
        GameState gs = engaged_marines(2);
        apply_action_paced(gs, Side::A,
                           action::StartMove{gs.units[0].id, MoveType::FallBack, FallBackMode::DesperateEscape});
        waiting.push_back(gs);  // Hazard (Desperate Escape)
    }
    {
        GameState gs = charge_step_state(9.9f);
        apply_action_paced(gs, Side::A, action::DeclareCharge{gs.units[0].id});
        waiting.push_back(gs);  // Charge
    }
    ASSERT_EQ(waiting.size(), 7u);
    for (const GameState& gs : waiting) {
        EXPECT_TRUE(roll_pending(gs));
        EXPECT_EQ(round_trip(gs), gs) << describe(gs, *gs.pending);
    }
    EXPECT_TRUE(Json(waiting[1]).at("flow").at("attack").contains("rolling"));
    EXPECT_TRUE(Json(waiting[2]).at("flow").at("attack").at("rolling").contains("hits"));
    EXPECT_TRUE(Json(waiting[4]).at("flow").at("attack").at("current").contains("priority"));
    // No roll waiting: none of the new fields is written.
    GameState none = wh40k::test::shoot_step_state();
    const std::string text = Json(none).dump();
    EXPECT_EQ(text.find("rolling"), std::string::npos);
    EXPECT_EQ(text.find("priority"), std::string::npos);
    EXPECT_EQ(text.find("battle_shock_tests"), std::string::npos);
    GameState shock = wh40k::test::load_v1_scenario();
    shock.flow.battle_shock_tests = {UnitId{2}};
    EXPECT_EQ(round_trip(shock), shock);
}

// ---- The M94 replay: tests/replays/m94_paced_rolls.json (format 2, every kind of roll a step of its own) ----

namespace {

constexpr std::uint64_t kM94Seed = 1;  // picked by D095_Replay.DISABLED_SearchM94Seed

// The milestone's replay, played through a paced match: each roll is a RollDice step answered by its roller. `ok` says
// whether the seed shows what the replay is about (every kind of roll, a Desperate Escape, and the stop at Side A's
// second-round Advance); `problem`, the first refused step.
Replay m94_replay(std::uint64_t seed, bool& ok, std::string& problem) {
    ok = false;
    Replay r;
    r.format = kReplayFormatWindowsRecorded;  // M95 (D-096): format 3
    r.description =
        "M94 (D-095): engine-paced dice rolls, every roll a RollDice step answered by its roller. The V1 scenario with "
        "4 of the 10 Termagants (below half-strength), 1\" from the Intercessors (engaged), their fleshborers a "
        "stand-in A D3, D D3 and [HAZARDOUS]. Round 1, Side A: the Intercessors make a Desperate Escape (09.07): Side A "
        "makes its hazard rolls before the move and its battle-shock roll after it, then passes. Side B: the "
        "Termagants' battle-shock roll in the Battle-shock step (08.03); they remain stationary and fire every "
        "fleshborer at the Intercessors: Side B rolls the random A (D-069), the hit and wound rolls, Side A the save "
        "rolls (05.03) and orders its casualties (D-013), Side B the damage rolls (05.04, one per attack since M95, "
        "D-096) and then its "
        "hazard rolls (24.15). The Termagants declare a charge and make the charge roll (11.02), then no charge move. "
        "Round 2, Side A: any battle-shock roll, then the Intercessors Advance (09.06). Stops at Side A's move of the "
        "advancing models. Casualties in unit order.";
    GameState init = wh40k::test::load_v1_scenario();
    init.units[1].models.resize(4);
    const float gap = mm_to_inches(*unit_distance(init.units[0], init.units[1]));
    for (Model& m : init.units[1].models) {
        m.position.y -= in(gap - 1.0f);
        WeaponProfile& p = m.weapons[0].profiles[0];
        p.attacks = kD3;
        p.damage = kD3;
        p.abilities.push_back(ability::Hazardous{});
    }
    init.dice = DiceState{seed, 0};
    r.initial.state = init;
    Match match(init, RollMode::Paced);
    bool failed = false;
    auto act = [&](const GameAction& a) {
        if (failed) return;
        const ValidationResult v = match.submit(decider(match.state()), a);
        if (!v.ok()) {
            failed = true;
            problem = v.reasons.front();
        }
    };
    // Neutral answers (rolls made, casualties in unit order, steps ended) until `done`.
    auto play_until = [&](auto done) {
        for (int i = 0; i < 300 && !failed && match.state().pending && !done(match.state()); ++i) {
            act(wh40k::test::neutral_action(match.state()));
        }
    };
    const UnitId marines = init.units[0].id;
    const UnitId gants = init.units[1].id;
    // Round 1, Side A: a Desperate Escape, 3" south.
    act(action::StartMove{marines, MoveType::FallBack, FallBackMode::DesperateEscape});
    play_until([](const GameState& gs) { return gs.pending && std::holds_alternative<decision::MoveModels>(gs.pending->kind); });
    for (const Model& m : match.state().units[0].models) {
        const Position s = *move_start_position(match.state(), m.id);
        act(action::ProposeModelMove{m.id, {Position{s.x, s.y - in(3), 0.0f}}});
    }
    act(action::CommitUnitMove{});
    // Side B's turn: its Battle-shock step, then the volley.
    play_until([](const GameState& gs) {
        return gs.flow.stage == Stage::Shoot && turn_side(gs) == Side::B && !roll_pending(gs) && !window_pending(gs);
    });
    if (!failed) {
        action::Shoot volley{gants, ShootingType::Normal, {}};
        for (const Model& m : match.state().units[1].models) {
            volley.shots.push_back(ShotDeclaration{m.id, 0, 0, marines});
        }
        act(volley);
    }
    // D-096 (M95): a reaction window waits like a roll (neutral_action declines it).
    const auto waiting = [](const GameState& gs) { return roll_pending(gs) || window_pending(gs); };
    play_until([&](const GameState& gs) { return gs.flow.stage == Stage::Charge && !waiting(gs); });
    if (!failed && match.state().flow.stage == Stage::Charge) {
        const auto legal = legal_actions(match.state(), Side::B);
        if (std::find(legal.begin(), legal.end(), GameAction{action::DeclareCharge{gants}}) == legal.end()) return r;
        act(action::DeclareCharge{gants});
        play_until([&](const GameState& gs) { return !waiting(gs); });
        if (!failed && match.state().pending &&
            std::holds_alternative<decision::SelectChargeTargets>(match.state().pending->kind)) {
            act(action::SkipChargeMove{gants});
        }
    }
    // Round 2, Side A: on to its Move Units step, then an Advance.
    play_until([](const GameState& gs) {
        return gs.flow.battle_round == 2 && gs.pending && std::holds_alternative<decision::MoveUnits>(gs.pending->kind);
    });
    if (failed || !match.state().pending || turn_side(match.state()) != Side::A) return r;
    act(action::StartMove{marines, MoveType::Advance, std::nullopt});
    play_until([&](const GameState& gs) { return !waiting(gs); });
    r.steps = match.steps();
    r.expect = replay_expectation(match.state());
    std::vector<RollKind> kinds;
    for (const ReplayStep& s : r.steps) {
        if (const auto* roll = std::get_if<action::RollDice>(&s.action)) kinds.push_back(roll->roll);
    }
    const auto has = [&](RollKind k) { return std::find(kinds.begin(), kinds.end(), k) != kinds.end(); };
    ok = !failed && has(RollKind::Advance) && has(RollKind::Charge) && has(RollKind::BattleShock) &&
         has(RollKind::Hazard) && has(RollKind::RandomAttacks) && has(RollKind::Hit) && has(RollKind::Wound) &&
         has(RollKind::Save) && has(RollKind::Damage) && match.state().pending &&
         std::holds_alternative<decision::MoveModels>(match.state().pending->kind);
    return r;
}

const std::filesystem::path kM94Replay = kReplayDir / "m94_paced_rolls.json";

}  // namespace

TEST(D095_Replay, ThePacedRollsReplayAnswersEveryKindOfRollAsAStep) {
    const Replay saved = load_replay(kM94Replay);
    EXPECT_EQ(saved.format, kReplayFormatWindowsRecorded);  // regenerated as format 3 in M95 (D-096)
    const ReplayResult run = run_replay(saved, repo_root());
    ASSERT_TRUE(run.ok()) << "step " << *run.failed_step << ": " << run.reasons.front();
    EXPECT_EQ(replay_expectation(run.final_state), saved.expect);
    // Every kind of roll, each made by the controlling player of the unit it is for (the target's for a save, 05.03);
    // the Desperate Escape's two kinds of roll.
    std::vector<RollKind> kinds;
    for (const ReplayStep& s : saved.steps) {
        if (const auto* roll = std::get_if<action::RollDice>(&s.action)) {
            kinds.push_back(roll->roll);
            EXPECT_EQ(s.side, run.final_state.find_unit(roll->unit)->side) << roll_kind_name(roll->roll);
            if (roll->roll == RollKind::Save) EXPECT_EQ(s.side, Side::A);  // the Termagants' volley: the defender rolls
        }
    }
    for (const RollKind k : {RollKind::Advance, RollKind::Charge, RollKind::BattleShock, RollKind::Hazard,
                             RollKind::RandomAttacks, RollKind::Hit, RollKind::Wound, RollKind::Save, RollKind::Damage}) {
        EXPECT_NE(std::find(kinds.begin(), kinds.end(), k), kinds.end()) << roll_kind_name(k);
    }
    EXPECT_EQ(saved.steps[0].action,
              (GameAction{action::StartMove{UnitId{1}, MoveType::FallBack, FallBackMode::DesperateEscape}}));
    EXPECT_EQ(saved.steps[1].action, (GameAction{action::RollDice{RollKind::Hazard, UnitId{1}}}));
    // The same game with every roll made inside its step (format 1): the same events and state.
    Replay at_once = saved;
    at_once.format = kReplayFormatRollsInline;
    at_once.steps = without_rolls(saved.steps);
    const ReplayResult inline_run = run_replay(at_once, repo_root());
    ASSERT_TRUE(inline_run.ok());
    EXPECT_EQ(inline_run.events, run.events);
    EXPECT_EQ(inline_run.final_state, run.final_state);
    // The file is what the builder plays now.
    bool ok = false;
    std::string problem;
    const Replay again = m94_replay(kM94Seed, ok, problem);
    EXPECT_TRUE(problem.empty()) << problem;
    EXPECT_TRUE(ok);
    EXPECT_EQ(Json(again).dump(2), Json(saved).dump(2));
}

TEST(D095_Replay, DISABLED_SearchM94Seed) {
    for (std::uint64_t seed = 1; seed <= 60; ++seed) {
        bool ok = false;
        std::string problem;
        (void)m94_replay(seed, ok, problem);
        if (!problem.empty()) std::printf("seed %llu: %s\n", static_cast<unsigned long long>(seed), problem.c_str());
        if (ok) {
            std::printf("M94 seed %llu\n", static_cast<unsigned long long>(seed));
            return;
        }
    }
    ADD_FAILURE() << "no seed in 1..60";
}

// Writes tests/replays/m94_paced_rolls.json. Run on purpose only:
//   wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D095_Replay.DISABLED_RegenerateM94Replay
TEST(D095_Replay, DISABLED_RegenerateM94Replay) {
    bool ok = false;
    std::string problem;
    const Replay r = m94_replay(kM94Seed, ok, problem);
    ASSERT_TRUE(problem.empty()) << problem;
    ASSERT_TRUE(ok);
    const ReplayResult run = run_replay(r, repo_root());
    ASSERT_TRUE(run.ok()) << "step " << *run.failed_step << ": " << run.reasons.front();
    save_replay(r, kM94Replay);
}

// ---- Format 1 (every roll inside its step) still loads; with_roll_steps makes it format 2 ----

TEST(D095_Replays, AFormatOneReplayStillRunsAndConvertsToItsFormatTwoFile) {
    // tests/data/legacy/ keeps two replays as M93 wrote them (format 1) and as M94 wrote them (format 2, D-096). Each runs
    // as before, and with_roll_steps turns either into exactly the regenerated file (format 3 since M95).
    for (const std::string name : {"m8_shooting", "m62_random_characteristics"}) {
        SCOPED_TRACE(name);
        const Replay current = load_replay(kReplayDir / (name + ".json"));
        EXPECT_EQ(current.format, kReplayFormatWindowsRecorded);
        EXPECT_EQ(with_roll_steps(current, repo_root()), current);  // format 3: as it is
        for (const std::string format : {"format1", "format2"}) {
            SCOPED_TRACE(format);
            const Replay legacy = load_replay(repo_root() / "tests/data/legacy" / (name + "_" + format + ".json"));
            EXPECT_EQ(legacy.format, format == "format1" ? kReplayFormatRollsInline : kReplayFormatRollsRecorded);
            const ReplayResult run = run_replay(legacy, repo_root());
            ASSERT_TRUE(run.ok()) << "step " << *run.failed_step << ": " << run.reasons.front();
            EXPECT_EQ(replay_expectation(run.final_state), legacy.expect);
            EXPECT_EQ(Json(with_roll_steps(legacy, repo_root())).dump(2), Json(current).dump(2));
        }
    }
}

TEST(D095_Replays, AnUnknownFormatIsRefusedAndARollStepCantStandInAFormatOneReplay) {
    Json j = Json(load_replay(kReplayDir / "m8_shooting.json"));
    j["format"] = 4;  // D-096 (M95): 3 is the current format
    EXPECT_THROW((void)j.get<Replay>(), DataError);
    // Format 1 makes every roll inside its step: a RollDice step there answers nothing (05.01: no roll is waiting).
    Replay r = load_replay(kReplayDir / "m8_shooting.json");
    r.format = kReplayFormatRollsInline;
    const ReplayResult run = run_replay(r, repo_root());
    ASSERT_FALSE(run.ok());
    EXPECT_TRUE(std::holds_alternative<action::RollDice>(r.steps.at(*run.failed_step).action));
}
