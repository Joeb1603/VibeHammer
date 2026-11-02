// JSON round-trips for every new M5 state field, action and event (CLAUDE.md: mandatory).

#include <gtest/gtest.h>

#include <variant>
#include <vector>

#include "core/data/load_error.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::select;
using wh40k::test::shoot_step_state;

namespace {

template <typename T>
T round_trip(const T& value) {
    return Json::parse(Json(value).dump()).get<T>();
}

GameAction round_trip_action(const GameAction& a) {
    Json j;
    action::to_json(j, a);
    GameAction out;
    action::from_json(Json::parse(j.dump()), out);
    return out;
}

GameEvent round_trip_event(const GameEvent& e) {
    Json j;
    event::to_json(j, e);
    GameEvent out;
    event::from_json(Json::parse(j.dump()), out);
    return out;
}

}  // namespace

TEST(Serialization, EveryStageRoundTrips) {
    for (std::size_t i = 0; i < kStageCount; ++i) {
        FlowState f;
        f.stage = static_cast<Stage>(i);
        f.battle_round = 3;
        f.turn_in_round = 1;
        EXPECT_EQ(round_trip(f), f) << stage_name(f.stage);
    }
    for (const Phase p : {Phase::None, Phase::Command, Phase::Movement, Phase::Shooting, Phase::Charge, Phase::Fight}) {
        EXPECT_EQ(round_trip(p), p);
    }
}

TEST(Serialization, EveryDecisionKindRoundTrips) {
    const std::vector<PendingDecision> all{
        {Side::A, decision::MoveUnits{}},
        {Side::A, decision::MoveModels{UnitId{1}}},
        {Side::B, decision::Shoot{}},
        {Side::A, decision::Charge{}},
        {Side::A, decision::SelectAttackBatch{UnitId{1}}},
        {Side::B, decision::DeclareAllocationOrder{UnitId{2}}},
        {Side::B, decision::OrderCasualties{UnitId{2}}},
    };
    ASSERT_EQ(all.size(), std::variant_size_v<DecisionKind>);
    for (const auto& d : all) {
        EXPECT_EQ(round_trip(d), d) << describe(d);
    }
}

TEST(Serialization, StateMidAttackRoundTrips) {
    GameState gs = shoot_step_state();
    ScriptedDice dice{3, 3, 3, 3, 3, 3, 1, 1, 1};
    begin_attacks(gs, dice, gs.units[0].id, AttackContext{AttackType::Shooting, true},
                  {select(gs, 0, 0, 0, 1, gs.units[1].id)});
    ASSERT_TRUE(gs.flow.attack.has_value());
    ASSERT_TRUE(gs.flow.attack->current.has_value());
    ASSERT_TRUE(gs.flow.activation.has_value());
    gs.units[0].phase_flags.selected_to_move = true;
    EXPECT_EQ(round_trip(gs), gs);
    EXPECT_EQ(round_trip(*gs.flow.attack), *gs.flow.attack);
}

TEST(Serialization, StateMidMoveRoundTrips) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    apply_action(gs, Side::A, action::StartMove{gs.units[0].id, MoveType::Advance, std::nullopt});
    const ModelId m = gs.units[0].models[0].id;
    const Position start = gs.units[0].models[0].position;
    apply_action(gs, Side::A, action::ProposeModelMove{m, {Position{start.x, start.y + 20.0f, 0.0f}}});
    ASSERT_TRUE(gs.flow.move.has_value());
    ASSERT_TRUE(gs.units[0].phase_flags.advance_roll.has_value());
    gs.flow.move->mode = FallBackMode::OrderedRetreat;  // exercise every field
    gs.flow.move->pending_mortal_wounds = 2;
    gs.units[0].phase_flags.desperate_escape_hazards_made = true;
    EXPECT_EQ(round_trip(gs), gs);
}

TEST(Serialization, GameResultWithAndWithoutWinner) {
    const GameResult win{Side::A, "D-007: Side B has no models on the battlefield; Side A wins"};
    const GameResult draw{std::nullopt, "D-007: draw"};
    EXPECT_EQ(round_trip(win), win);
    EXPECT_EQ(round_trip(draw), draw);
    GameState gs = wh40k::test::load_v1_scenario();
    gs.result = win;
    EXPECT_EQ(round_trip(gs), gs);
}

TEST(Serialization, EveryActionKindRoundTrips) {
    const std::vector<GameAction> all{
        action::StartMove{UnitId{1}, MoveType::FallBack, FallBackMode::DesperateEscape},
        action::ProposeModelMove{ModelId{3}, {Position{10.5f, 20.25f, 0.0f}, Position{30.0f, 40.0f, 0.0f}}},
        action::CommitUnitMove{},
        action::ResetUnitMove{},
        action::EndShootStep{},
        action::EndChargeStep{},
        action::SelectAttackBatch{3},
        action::DeclareAllocationOrder{UnitId{2}, {1, 0}},
        action::OrderCasualties{UnitId{2}, {ModelId{9}, ModelId{6}}},
    };
    ASSERT_EQ(all.size(), std::variant_size_v<GameAction>);
    for (const auto& a : all) {
        EXPECT_EQ(round_trip_action(a), a) << action_name(a);
    }
    EXPECT_EQ(round_trip_action(action::StartMove{UnitId{1}, MoveType::Advance, std::nullopt}),
              GameAction{(action::StartMove{UnitId{1}, MoveType::Advance, std::nullopt})});
    // M5 replays wrote StartMove without a fall_back_mode field: still readable.
    GameAction old_form;
    action::from_json(Json::parse(R"({"kind":"StartMove","unit":1,"move_type":"RemainStationary"})"), old_form);
    EXPECT_EQ(old_form, GameAction{(action::StartMove{UnitId{1}, MoveType::RemainStationary, std::nullopt})});
    EXPECT_EQ(round_trip_action(action::OrderCasualties{UnitId{2}, {}}),
              GameAction{(action::OrderCasualties{UnitId{2}, {}})});  // a template
}

TEST(Serialization, EveryEventKindRoundTrips) {
    HitRolls hits;
    hits.rolls = {HitRoll{std::nullopt, std::nullopt, HitOutcome::AutoHit}, HitRoll{6, 7, HitOutcome::CriticalHit}};
    WoundRolls wounds;
    wounds.required = 3;
    wounds.rolls = {WoundRoll{2, 2, WoundOutcome::Fail}, WoundRoll{6, 6, WoundOutcome::CriticalWound}};
    InflictDamageResult damage;
    damage.steps = {SaveStep{0, SaveCheck{1, 1, false, SaveOutcome::InflictsDamage}, DamageResult{ModelId{7}, 2, 1, true}},
                    SaveStep{std::nullopt, std::nullopt, std::nullopt}};
    damage.models_destroyed = 1;
    damage.attacks_lost = 1;
    AttackBatch batch;
    batch.target = UnitId{2};
    batch.profile.skill = 3;
    batch.profile.strength = 5;
    batch.profile.damage = DiceExpr::fixed(1);
    batch.profile.attack_time_abilities = {ability::Unknown{"Lethal Hits"}};
    batch.selections = {0, 2};
    batch.attack_dice = 7;
    const AllocationGroup group{{ModelId{7}, ModelId{8}}, false, 1, 5, std::nullopt};

    const std::vector<GameEvent> all{
        event::BattleRoundStarted{2},
        event::BattleRoundEnded{2},
        event::TurnStarted{2, Side::B},
        event::TurnEnded{2, Side::B},
        event::PhaseStarted{Phase::Shooting, Side::A},
        event::PhaseEnded{Phase::Fight, Side::B},
        event::UnitRemainedStationary{UnitId{1}},
        event::MoveStarted{UnitId{1}, MoveType::FallBack, FallBackMode::OrderedRetreat},
        event::AdvanceRolled{UnitId{1}, 4, true},
        event::HazardRollsMade{UnitId{1}, HazardRolls{{1, 5}, 1, 1}},
        event::MortalWoundsInflicted{UnitId{1}, MortalWoundsResult{{DamageResult{ModelId{2}, 1, 1, false}}, 0, 0}},
        event::ModelMoveProposed{UnitId{1}, ModelId{2}, {Position{1.0f, 2.0f, 0.0f}, Position{3.0f, 4.0f, 0.0f}},
                                 2.5f},
        event::UnitMoveCommitted{UnitId{1}, MoveType::Advance},
        event::UnitMoveReverted{UnitId{1}, {"03.03: model 2 is more than 2\" from every other model in unit 1"}},
        event::BattleShockRolled{UnitId{1}, BattleShockRoll{LeadershipRoll{TwoD6{{1, 2}}, 6, false}, true}},
        event::AttackBatchesGathered{UnitId{1}, {batch}},
        event::AttackBatchSelected{UnitId{1}, 0, UnitId{2}, 7},
        event::HitRollsMade{0, hits},
        event::WoundRollsMade{0, wounds},
        event::AllocationOrderDeclared{UnitId{2}, {group}},
        event::SaveRollsMade{UnitId{2}, {1, 6}},
        event::SavesResolved{UnitId{2}, damage},
        event::ModelDestroyed{UnitId{2}, ModelId{7}},
        event::UnitDestroyed{UnitId{2}},
        event::AttacksLost{UnitId{2}, 1, 2},
        event::GameOver{Side::A, "D-007: Side B has no models on the battlefield; Side A wins"},
    };
    ASSERT_EQ(all.size(), std::variant_size_v<GameEvent>);
    for (const auto& e : all) {
        EXPECT_EQ(round_trip_event(e), e) << event_name(e);
        Json j;
        event::to_json(j, e);
        EXPECT_EQ(j.at("rule").get<std::string>(), std::string(event_rule(e)));
    }
    EXPECT_EQ(round_trip_event(event::GameOver{std::nullopt, "draw"}), GameEvent{(event::GameOver{std::nullopt, "draw"})});
}

TEST(Serialization, UnknownKindsAreDataErrors) {
    const Json bad_action = Json::parse(R"({"kind": "Teleport"})");
    const Json bad_event = Json::parse(R"({"kind": "Earthquake"})");
    const Json bad_decision = Json::parse(R"({"side": "A", "kind": "Dance"})");
    GameAction a;
    EXPECT_THROW(action::from_json(bad_action, a), DataError);
    GameEvent e;
    EXPECT_THROW(event::from_json(bad_event, e), DataError);
    EXPECT_THROW((void)bad_decision.get<PendingDecision>(), DataError);
    EXPECT_THROW((void)Json("NotAStage").get<Stage>(), DataError);
}
