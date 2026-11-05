// The M4 attack sequence driven through the engine: PendingDecisions for 04.03 (attacker), 05.03 and
// 05.04 (opposing player, D-013), D-012 and D-007.

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/serialization/json.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::events_of;
using wh40k::test::add_unit_copy;
using wh40k::test::decider;
using wh40k::test::is_rule_cited;
using wh40k::test::load_v1_scenario;
using wh40k::test::model_ids;
using wh40k::test::select;
using wh40k::test::shoot_step_state;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;
constexpr std::size_t kBoltRifle = 0;
constexpr std::size_t kBoltPistol = 1;
constexpr std::size_t kFocused = 0;
constexpr std::size_t kSaturation = 1;
constexpr std::size_t kFleshborer = 0;
const AttackContext kShooting{AttackType::Shooting, true};

std::vector<WeaponSelection> saturation_volley(const GameState& gs) {
    std::vector<WeaponSelection> sel;
    for (std::size_t i = 0; i < 5; ++i) {
        sel.push_back(select(gs, kMarines, i, kBoltRifle, kSaturation, gs.units[kGants].id));
    }
    return sel;
}

// Keeps only the first `n` models of units[index].
void shrink(GameState& gs, std::size_t index, std::size_t n) {
    gs.units[index].models.resize(n);
}

}  // namespace

// ==== D-013: one casualty decision per batch, owned by the opposing player ====

TEST(D013_CasualtyOrder, SaturationVolleyAsksTheOpposingPlayerOnceForCasualties) {
    GameState gs = shoot_step_state();
    const UnitId gants = gs.units[kGants].id;
    ScriptedDice dice{1, 2, 3, 4, 5, 6, 3, 3, 2, 1, 6, 5, 4, 2, 3,  // hits: 10
                      2, 3, 1, 6, 4, 5, 2, 3, 3, 1,                 // wounds: 6
                      6, 5, 1, 4, 6, 2};                            // saves: 4 fail
    const auto begin = begin_attacks(gs, dice, gs.units[kMarines].id, kShooting, saturation_volley(gs));
    ASSERT_EQ(events_of<event::AttackBatchesGathered>(begin).size(), 1u);
    EXPECT_EQ(events_of<event::SaveRollsMade>(begin).at(0).rolls, (std::vector<int>{6, 5, 1, 4, 6, 2}));

    ASSERT_TRUE(gs.pending.has_value());
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::OrderCasualties{gants}}));
    EXPECT_EQ(active_side(gs), Side::A);  // 01.03: the attacker stays active
    EXPECT_TRUE(legal_actions(gs, Side::A).empty());
    const auto legal = legal_actions(gs, Side::B);
    ASSERT_EQ(legal.size(), 1u);
    EXPECT_TRUE(is_template(legal[0]));
    EXPECT_FALSE(validate_action(gs, Side::B, legal[0]).ok());

    std::vector<ModelId> priority = model_ids(gs.units[kGants]);
    std::reverse(priority.begin(), priority.end());  // the rear models die first
    const auto events = apply_action(gs, Side::B, action::OrderCasualties{gants, priority}, dice);

    EXPECT_EQ(dice.remaining(), 0u);
    ASSERT_EQ(gs.units[kGants].models.size(), 6u);
    const std::vector<ModelId> survivors(priority.rbegin(), priority.rbegin() + 6);
    EXPECT_EQ(model_ids(gs.units[kGants]), survivors);
    EXPECT_EQ(events_of<event::ModelDestroyed>(events).size(), 4u);
    EXPECT_EQ(events_of<event::SavesResolved>(events).at(0).result.models_destroyed, 4);
    // The attack is resolved: back to Side A's Shoot step.
    EXPECT_FALSE(gs.flow.attack.has_value());
    EXPECT_FALSE(gs.flow.activation.has_value());
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
}

TEST(D013_CasualtyOrder, ForcedCasualtiesNeedNoDecision) {
    GameState gs = shoot_step_state(Side::B);
    gs.units[kMarines].models[1].wounds_remaining = 1;
    const ModelId wounded = gs.units[kMarines].models[1].id;
    const WeaponSelection s = select(gs, kGants, 0, kFleshborer, 0, gs.units[kMarines].id);
    ScriptedDice dice{4, 4, 1};  // hit, wound (S5 vs T5: 4+), save 1: damage
    const auto events = begin_attacks(gs, dice, gs.units[kGants].id, AttackContext{}, {s});
    EXPECT_EQ(dice.remaining(), 0u);
    EXPECT_EQ(events_of<event::ModelDestroyed>(events).at(0).model, wounded);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::Shoot{}}));  // no prompt was needed
}

TEST(D013_CasualtyOrder, CasualtyOrderValidationCitesTheRule) {
    GameState gs = shoot_step_state();
    const UnitId gants = gs.units[kGants].id;
    ScriptedDice dice{3, 3, 3, 3, 3, 3, 1, 1, 1};
    begin_attacks(gs, dice, gs.units[kMarines].id, kShooting,
                  {select(gs, kMarines, 0, kBoltRifle, kSaturation, gants)});
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::B, decision::OrderCasualties{gants}}));
    const auto all = model_ids(gs.units[kGants]);

    auto check = [&](action::OrderCasualties a, const std::string& prefix) {
        const auto r = validate_action(gs, Side::B, a);
        ASSERT_FALSE(r.ok());
        EXPECT_EQ(r.reasons[0].rfind(prefix, 0), 0u) << r.reasons[0];
        EXPECT_TRUE(is_rule_cited(r.reasons[0]));
    };
    check({gs.units[kMarines].id, all}, "05.04: casualties are being allocated in unit 2");
    check({gants, {}}, "05.04: the casualty order is empty");
    auto missing = all;
    missing.pop_back();
    check({gants, missing}, "05.04: the casualty order must list every model");
    auto duplicate = all;
    duplicate.back() = duplicate.front();
    check({gants, duplicate}, "05.04: the casualty order must list every model");
    auto foreign = all;
    foreign.back() = ModelId{999};
    check({gants, foreign}, "05.04: the casualty order must list every model");
    EXPECT_TRUE(validate_action(gs, Side::B, action::OrderCasualties{gants, all}).ok());
}

// ==== 05.03 Allocation order decision ====

TEST(R05_03_AllocationOrder, TwoAllocationGroupsGiveTheOpposingPlayerAnOrderDecision) {
    GameState gs = shoot_step_state();
    Unit& gants = gs.units[kGants];
    for (std::size_t i = 0; i < 5; ++i) {
        gants.models[i].characteristics.save = 6;  // group 0: Sv6+ (models 0-4); group 1: Sv5+
    }
    const UnitId target = gants.id;
    ScriptedDice dice{3, 3, 3, 3, 3, 3, 1, 1, 1};  // 3 hits, 3 wounds, 3 failed saves
    begin_attacks(gs, dice, gs.units[kMarines].id, kShooting,
                  {select(gs, kMarines, 0, kBoltRifle, kSaturation, target)});
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::B, decision::DeclareAllocationOrder{target}}));
    EXPECT_EQ(legal_actions(gs, Side::B).size(), 2u);
    EXPECT_EQ(dice.consumed(), 6u);  // save rolls wait for the order (05.03 step 2 before step 3)

    const auto bad = validate_action(gs, Side::B, action::DeclareAllocationOrder{target, {0, 0}});
    ASSERT_FALSE(bad.ok());
    EXPECT_EQ(bad.reasons[0].rfind("05.03: ", 0), 0u);

    const auto events = apply_action(gs, Side::B, action::DeclareAllocationOrder{target, {1, 0}}, dice);
    const auto declared = events_of<event::AllocationOrderDeclared>(events);
    ASSERT_EQ(declared.size(), 1u);
    EXPECT_EQ(declared[0].groups[0].save, 5);  // the Sv5+ group first
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::B, decision::OrderCasualties{target}}));
    apply_action(gs, Side::B, action::OrderCasualties{target, model_ids(gs.units[kGants])}, dice);
    // The three casualties came from the Sv5+ group (models 5-9).
    EXPECT_EQ(gs.units[kGants].models.size(), 7u);
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(gs.units[kGants].models[i].characteristics.save, 6);
    }
}

// ==== 04.03 batch order decision ====

TEST(R04_03_ResolveAttacks, AttackerChoosesBatchOrderAndFinishesATargetFirst) {
    GameState gs = shoot_step_state();
    const UnitId gants = gs.units[kGants].id;
    const UnitId gants2 = add_unit_copy(gs, kGants);
    gs.units[kMarines].models[2].weapons[kBoltPistol].profiles[0].strength = 4;  // a distinct profile
    const std::vector<WeaponSelection> sel{
        select(gs, kMarines, 0, kBoltRifle, kSaturation, gants),  // batch 0
        select(gs, kMarines, 1, kBoltRifle, kFocused, gants),     // batch 1
        select(gs, kMarines, 2, kBoltPistol, 0, gants),           // batch 2
        select(gs, kMarines, 3, kBoltRifle, kSaturation, gants2), // batch 3
    };
    ScriptedDice dice{1, 1, 1,   // batch 0: 3 misses
                      1,         // batch 2: 1 miss
                      1, 1,      // batch 1: 2 misses
                      1, 1, 1};  // batch 3: 3 misses
    begin_attacks(gs, dice, gs.units[kMarines].id, kShooting, sel);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::SelectAttackBatch{gs.units[kMarines].id}}));
    EXPECT_EQ(legal_actions(gs, Side::A).size(), 4u);
    EXPECT_TRUE(legal_actions(gs, Side::B).empty());

    apply_action(gs, Side::A, action::SelectAttackBatch{0}, dice);
    // Unit 2 still has batches 1 and 2: they come before unit 3's batch.
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::SelectAttackBatch{gs.units[kMarines].id}}));
    EXPECT_EQ(legal_actions(gs, Side::A).size(), 2u);
    const auto other_target = validate_action(gs, Side::A, action::SelectAttackBatch{3});
    ASSERT_FALSE(other_target.ok());
    EXPECT_EQ(other_target.reasons[0],
              "04.03: resolve the remaining attacks against unit 2 before selecting another enemy unit");
    const auto used = validate_action(gs, Side::A, action::SelectAttackBatch{0});
    ASSERT_FALSE(used.ok());
    EXPECT_EQ(used.reasons[0].rfind("04.03: ", 0), 0u);

    const auto events = apply_action(gs, Side::A, action::SelectAttackBatch{2}, dice);
    std::vector<std::size_t> order;
    for (const auto& e : events_of<event::AttackBatchSelected>(events)) order.push_back(e.batch);
    EXPECT_EQ(order, (std::vector<std::size_t>{2, 1, 3}));  // 1 and 3 had no alternatives
    EXPECT_EQ(dice.remaining(), 0u);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
}

// ==== D-012 and D-007 ====

TEST(D012_TargetNoLongerEligible, AttacksAgainstADestroyedTargetAreLost) {
    GameState gs = shoot_step_state();
    const UnitId gants = gs.units[kGants].id;
    const UnitId gants2 = add_unit_copy(gs, kGants);
    shrink(gs, kGants, 1);  // a one-model target
    const std::vector<WeaponSelection> sel{
        select(gs, kMarines, 0, kBoltRifle, kSaturation, gants),  // batch 0
        select(gs, kMarines, 1, kBoltRifle, kFocused, gants),     // batch 1: lost
        select(gs, kMarines, 2, kBoltRifle, kSaturation, gants2), // batch 2
    };
    ScriptedDice dice{3, 3, 3, 3, 3, 3, 1, 1, 1,  // batch 0: 3 failed saves, the only model dies
                      1, 1, 1};                    // batch 2: 3 misses
    begin_attacks(gs, dice, gs.units[kMarines].id, kShooting, sel);
    ASSERT_EQ(decider(gs), Side::A);  // three batches, two targets: the attacker chooses
    const auto events = apply_action(gs, Side::A, action::SelectAttackBatch{0}, dice);

    const auto resolved = events_of<event::SavesResolved>(events);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_EQ(resolved[0].result.attacks_lost, 2);  // 05.04: excess attacks lost
    EXPECT_EQ(events_of<event::UnitDestroyed>(events).at(0).unit, gants);
    const auto lost = events_of<event::AttacksLost>(events);
    ASSERT_EQ(lost.size(), 1u);
    EXPECT_EQ(lost[0].batch, 1u);
    EXPECT_EQ(lost[0].attack_dice, 2);
    EXPECT_EQ(dice.remaining(), 0u);  // no dice rolled for the lost batch
    EXPECT_FALSE(gs.result.has_value());  // Side B still has unit 3
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
}

TEST(D007_GameEnd, WipingOutASideEndsTheGameImmediately) {
    GameState gs = shoot_step_state();
    shrink(gs, kGants, 1);
    ScriptedDice dice{3, 1, 1, 3, 1};  // one hit, one wound, failed save
    const auto events = begin_attacks(gs, dice, gs.units[kMarines].id, kShooting,
                                      {select(gs, kMarines, 0, kBoltRifle, kSaturation, gs.units[kGants].id)});
    EXPECT_EQ(gs.flow.stage, Stage::GameOver);
    ASSERT_TRUE(gs.result.has_value());
    EXPECT_EQ(gs.result->winner, Side::A);
    EXPECT_EQ(gs.result->reason, "D-007: Side B has no models on the battlefield; Side A wins");
    ASSERT_TRUE(std::holds_alternative<event::GameOver>(events.back()));
    EXPECT_FALSE(gs.pending.has_value());
    EXPECT_TRUE(legal_actions(gs, Side::A).empty());
    EXPECT_EQ(validate_action(gs, Side::A, action::EndShootStep{}).reasons.at(0), "D-007: the game is over");
}

// ==== Determinism: save and load in the middle of a decision ====

TEST(Determinism_Engine, SaveAndLoadMidDecisionContinuesIdentically) {
    GameState gs = shoot_step_state(Side::A, 42);
    ScriptedDice dice{3, 3, 3, 3, 3, 3, 1, 1, 1};
    begin_attacks(gs, dice, gs.units[kMarines].id, kShooting,
                  {select(gs, kMarines, 0, kBoltRifle, kSaturation, gs.units[kGants].id)});
    ASSERT_TRUE(std::holds_alternative<decision::OrderCasualties>(gs.pending->kind));

    GameState loaded = Json::parse(Json(gs).dump()).get<GameState>();
    ASSERT_EQ(loaded, gs);
    const GameAction answer = action::OrderCasualties{gs.units[kGants].id, model_ids(gs.units[kGants])};
    std::vector<GameEvent> a = apply_action(gs, Side::B, answer);
    std::vector<GameEvent> b = apply_action(loaded, Side::B, answer);
    while (gs.pending) {
        const GameAction next = legal_actions(gs, decider(gs)).front();
        auto ea = apply_action(gs, decider(gs), next);
        auto eb = apply_action(loaded, decider(loaded), next);
        a.insert(a.end(), ea.begin(), ea.end());
        b.insert(b.end(), eb.begin(), eb.end());
    }
    EXPECT_EQ(a, b);
    EXPECT_EQ(Json(gs).dump(), Json(loaded).dump());
}

// ==== begin_attacks preconditions (programmer errors) ====

TEST(AttackFlow_BeginAttacks, Preconditions) {
    GameState gs = load_v1_scenario();
    start_game(gs);  // Move Units step
    ScriptedDice dice{1};
    const WeaponSelection s = select(gs, kMarines, 0, kBoltRifle, kSaturation, gs.units[kGants].id);
    EXPECT_THROW((void)begin_attacks(gs, dice, gs.units[kMarines].id, kShooting, {s}), std::logic_error);

    GameState shoot = shoot_step_state();
    const WeaponSelection gant_shot = select(shoot, kGants, 0, kFleshborer, 0, shoot.units[kMarines].id);
    EXPECT_THROW((void)begin_attacks(shoot, dice, shoot.units[kGants].id, AttackContext{}, {gant_shot}),
                 std::logic_error);  // not Side B's turn
    WeaponSelection friendly = select(shoot, kMarines, 0, kBoltRifle, kSaturation, shoot.units[kGants].id);
    friendly.target = shoot.units[kMarines].id;
    EXPECT_THROW((void)begin_attacks(shoot, dice, shoot.units[kMarines].id, kShooting, {friendly}),
                 std::logic_error);
}
