// The M4 attack sequence driven through the engine: PendingDecisions for 04.03 (attacker), 05.03 and
// 05.04 (opposing player, D-013), D-012 and D-007.

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/casualties.hpp"
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

// D-051 (amends D-013): a forced casualty still asks, so no model is removed without the player picking it.
TEST(D051_CasualtyOrder, ForcedCasualtiesStillAskForTheOrder) {
    GameState gs = shoot_step_state(Side::B);
    gs.units[kMarines].models[1].wounds_remaining = 1;
    const ModelId wounded = gs.units[kMarines].models[1].id;
    const UnitId marines = gs.units[kMarines].id;
    const WeaponSelection s = select(gs, kGants, 0, kFleshborer, 0, marines);
    ScriptedDice dice{4, 4, 1};  // hit, wound (S5 vs T5: 4+), save 1: damage
    begin_attacks(gs, dice, gs.units[kGants].id, AttackContext{}, {s});
    EXPECT_EQ(dice.remaining(), 0u);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{marines}}));
    EXPECT_EQ(gs.units[kMarines].models.size(), 5u);  // nothing removed yet
    // Any order: 05.04 still forces the wounded model.
    const auto events = apply_action(gs, Side::A, action::OrderCasualties{marines, model_ids(gs.units[kMarines])}, dice);
    EXPECT_EQ(events_of<event::ModelDestroyed>(events).at(0).model, wounded);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::Shoot{}}));
}

TEST(D051_CasualtyOrder, NoDecisionWhenEverySaveIsPassed) {
    GameState gs = shoot_step_state(Side::B);
    const WeaponSelection s = select(gs, kGants, 0, kFleshborer, 0, gs.units[kMarines].id);
    ScriptedDice dice{4, 4, 6};  // hit, wound, save 6: the attack fails
    begin_attacks(gs, dice, gs.units[kGants].id, AttackContext{}, {s});
    EXPECT_EQ(dice.remaining(), 0u);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::Shoot{}}));  // nothing to select
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

// ==== D-051: preview_casualties, picking the models one at a time ====

namespace {

// Side B's Termagants shoot the Intercessors with `n` fleshborers: n hits, n wounds, n failed saves (D1).
GameState marines_take_failed_saves(int n, std::function<void(GameState&)> setup = {}) {
    GameState gs = shoot_step_state(Side::B);
    if (setup) setup(gs);
    std::vector<WeaponSelection> sel;
    std::vector<int> rolls;
    for (int i = 0; i < n; ++i) sel.push_back(select(gs, kGants, static_cast<std::size_t>(i), kFleshborer, 0, gs.units[kMarines].id));
    for (int i = 0; i < n; ++i) rolls.push_back(4);  // hits
    for (int i = 0; i < n; ++i) rolls.push_back(4);  // wounds (S5 vs T5: 4+)
    for (int i = 0; i < n; ++i) rolls.push_back(1);  // saves: 1 always inflicts damage
    ScriptedDice dice(rolls);
    begin_attacks(gs, dice, gs.units[kGants].id, AttackContext{}, sel);
    EXPECT_EQ(dice.remaining(), 0u);
    return gs;
}

ModelId mid(const GameState& gs, std::size_t unit, std::size_t i) { return gs.units[unit].models[i].id; }

// D-051's guarantee: applying OrderCasualties(picks + the rest, unit order) gives the preview's results.
void expect_preview_matches_resolution(const GameState& gs, const std::vector<ModelId>& picks) {
    const auto preview = preview_casualties(gs, picks);
    ASSERT_TRUE(preview.has_value());
    ASSERT_TRUE(preview->done());
    const UnitId target = std::get<decision::OrderCasualties>(gs.pending->kind).target;
    std::vector<ModelId> priority = picks;
    for (const Model& m : gs.find_unit(target)->models) {
        if (std::find(priority.begin(), priority.end(), m.id) == priority.end()) priority.push_back(m.id);
    }
    GameState after = gs;
    std::vector<DamageResult> resolved;
    for (const auto& e : apply_action(after, gs.pending->side, action::OrderCasualties{target, priority})) {
        if (const auto* s = std::get_if<event::SavesResolved>(&e)) {
            for (const SaveStep& step : s->result.steps) {
                if (step.damage) resolved.push_back(*step.damage);
            }
        } else if (const auto* m = std::get_if<event::MortalWoundsInflicted>(&e)) {
            resolved.insert(resolved.end(), m->result.steps.begin(), m->result.steps.end());
        }
    }
    EXPECT_EQ(resolved, preview->results);
}

}  // namespace

TEST(D051_CasualtyPreview, WithNoPicksTheCurrentGroupIsOffered) {
    const GameState gs = marines_take_failed_saves(2);
    const auto p = preview_casualties(gs, {});
    ASSERT_TRUE(p.has_value());
    EXPECT_TRUE(p->results.empty());
    EXPECT_EQ(p->options, model_ids(gs.units[kMarines]));
    EXPECT_EQ(p->next_damage, 1);
    EXPECT_FALSE(p->done());
}

TEST(D051_CasualtyPreview, AClickedModelKeepsTakingItsForcedDamage) {
    // 3 damage into fresh W2 Intercessors: model 1 takes 2 (the second is forced, 05.04), then a new pick.
    const GameState gs = marines_take_failed_saves(3);
    const ModelId first = mid(gs, kMarines, 1);
    const auto p = preview_casualties(gs, std::vector<ModelId>{first});
    ASSERT_TRUE(p.has_value());
    ASSERT_EQ(p->results.size(), 2u);
    EXPECT_EQ(p->results[0], (DamageResult{first, 1, 1, false}));
    EXPECT_EQ(p->results[1], (DamageResult{first, 1, 1, true}));
    std::vector<ModelId> rest = model_ids(gs.units[kMarines]);
    std::erase(rest, first);
    EXPECT_EQ(p->options, rest);

    const ModelId second = mid(gs, kMarines, 3);
    const auto done = preview_casualties(gs, std::vector<ModelId>{first, second});
    ASSERT_TRUE(done.has_value());
    EXPECT_TRUE(done->done());
    ASSERT_EQ(done->results.size(), 3u);
    EXPECT_EQ(done->results[2], (DamageResult{second, 1, 1, false}));
    expect_preview_matches_resolution(gs, {first, second});
}

TEST(D051_CasualtyPreview, AnAlreadyWoundedModelStillNeedsItsClick) {
    const GameState gs = marines_take_failed_saves(1, [](GameState& g) { g.units[kMarines].models[2].wounds_remaining = 1; });
    const ModelId wounded = mid(gs, kMarines, 2);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{gs.units[kMarines].id}}));
    const auto p = preview_casualties(gs, {});
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->options, (std::vector<ModelId>{wounded}));  // forced (05.04), but not taken without a click
    EXPECT_FALSE(preview_casualties(gs, std::vector<ModelId>{mid(gs, kMarines, 0)}).has_value());  // not legal
    const auto done = preview_casualties(gs, std::vector<ModelId>{wounded});
    ASSERT_TRUE(done.has_value());
    EXPECT_TRUE(done->done());
    EXPECT_EQ(done->results, (std::vector<DamageResult>{{wounded, 1, 1, true}}));
    expect_preview_matches_resolution(gs, {wounded});
}

TEST(D051_CasualtyPreview, AClickedModelKeepsTheDamageEvenWithAnotherWoundedModel) {
    // W3 Intercessors, models 0 and 1 both on 2/3: the clicked one takes both damage, the other none.
    const GameState gs = marines_take_failed_saves(2, [](GameState& g) {
        for (Model& m : g.units[kMarines].models) {
            m.characteristics.wounds = 3;
            m.wounds_remaining = 3;
        }
        g.units[kMarines].models[0].wounds_remaining = 2;
        g.units[kMarines].models[1].wounds_remaining = 2;
    });
    const ModelId a = mid(gs, kMarines, 0);
    const ModelId b = mid(gs, kMarines, 1);
    EXPECT_EQ(preview_casualties(gs, {})->options, (std::vector<ModelId>{a, b}));
    const auto p = preview_casualties(gs, std::vector<ModelId>{b});
    ASSERT_TRUE(p.has_value());
    EXPECT_TRUE(p->done());
    EXPECT_EQ(p->results, (std::vector<DamageResult>{{b, 1, 1, false}, {b, 1, 1, true}}));
    expect_preview_matches_resolution(gs, {b});
}

TEST(D051_CasualtyPreview, IllegalOrSurplusPicksGiveNothing) {
    const GameState gs = marines_take_failed_saves(1);
    EXPECT_FALSE(preview_casualties(gs, std::vector<ModelId>{ModelId{999}}).has_value());
    EXPECT_FALSE(preview_casualties(gs, std::vector<ModelId>{mid(gs, kMarines, 0), mid(gs, kMarines, 1)}).has_value());
    EXPECT_FALSE(preview_casualties(shoot_step_state(), {}).has_value());  // no casualty decision pending
}

TEST(D051_CasualtyPreview, OnlyTheCurrentAllocationGroupIsOffered) {
    GameState gs = shoot_step_state();
    for (std::size_t i = 0; i < 5; ++i) gs.units[kGants].models[i].characteristics.save = 6;  // two groups
    const UnitId target = gs.units[kGants].id;
    ScriptedDice dice{3, 3, 3, 3, 3, 3, 1, 1, 1};
    begin_attacks(gs, dice, gs.units[kMarines].id, kShooting, {select(gs, kMarines, 0, kBoltRifle, kSaturation, target)});
    apply_action(gs, Side::B, action::DeclareAllocationOrder{target, {1, 0}}, dice);  // the Sv5+ group first
    const auto p = preview_casualties(gs, {});
    ASSERT_TRUE(p.has_value());
    const std::vector<ModelId> all = model_ids(gs.units[kGants]);
    const std::vector<ModelId> sv5(all.begin() + 5, all.end());
    EXPECT_EQ(p->options, sv5);
    EXPECT_FALSE(preview_casualties(gs, std::vector<ModelId>{mid(gs, kGants, 0)}).has_value());  // a later group
    expect_preview_matches_resolution(gs, {sv5[4], sv5[0], sv5[2]});
}

TEST(D051_CasualtyPreview, ExcessAttacksAreLostWhenTheUnitIsWipedOut) {
    GameState gs = shoot_step_state();
    shrink(gs, kGants, 1);
    const UnitId gants = gs.units[kGants].id;
    ScriptedDice dice{3, 3, 3, 3, 3, 3, 1, 1, 1};  // 3 failed saves, one W1 model
    begin_attacks(gs, dice, gs.units[kMarines].id, kShooting, {select(gs, kMarines, 0, kBoltRifle, kSaturation, gants)});
    const auto p = preview_casualties(gs, std::vector<ModelId>{mid(gs, kGants, 0)});
    ASSERT_TRUE(p.has_value());
    EXPECT_TRUE(p->done());
    EXPECT_EQ(p->results.size(), 1u);
    EXPECT_EQ(p->attacks_lost, 2);
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
    apply_action(gs, Side::A, action::SelectAttackBatch{0}, dice);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::B, decision::OrderCasualties{gants}}));  // D-051
    const auto events =
        apply_action(gs, Side::B, action::OrderCasualties{gants, model_ids(gs.units[kGants])}, dice);

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
    const UnitId gants = gs.units[kGants].id;
    begin_attacks(gs, dice, gs.units[kMarines].id, kShooting, {select(gs, kMarines, 0, kBoltRifle, kSaturation, gants)});
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::B, decision::OrderCasualties{gants}}));  // D-051
    const auto events =
        apply_action(gs, Side::B, action::OrderCasualties{gants, model_ids(gs.units[kGants])}, dice);
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
