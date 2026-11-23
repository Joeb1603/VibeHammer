// M92 (D-093): the app-only rules audit. 04.03.03 Target No Longer Eligible or Viable: weapons whose target is gone
// select new targets (replacing D-012's "lost"); 02.02.01: a BS worsened by cover stops at 6+. (13.11.01 Gone to
// Ground is tested in m88_terrain_test.cpp, 14.02.01 in m86_objectives_test.cpp, 24.11.01 in m60_abilities_test.cpp.)

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/fight.hpp"
#include "core/serialization/json.hpp"
#include "core/state/queries.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::add_unit_copy;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::in;
using wh40k::test::is_rule_cited;
using wh40k::test::model_ids;
using wh40k::test::select;
using wh40k::test::shoot_step_state;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;
constexpr std::size_t kBoltRifle = 0;
constexpr std::size_t kFocused = 0;
constexpr std::size_t kSaturation = 1;
const AttackContext kShooting{AttackType::Shooting, true};
const AttackContext kFighting{AttackType::Fighting, false};

// The V1 Shoot step with a second Termagant unit (a copy, unit `second`) and the first cut to one model. Intercessor 0
// fires Saturation and Intercessor 1 Focused Fire at the first unit (batches 0 and 1), Intercessor 2 Saturation at
// the second (batch 2). Batch 0 is resolved first and destroys the first unit, so batch 1's target is gone.
struct Retargeting {
    GameState gs;
    UnitId gants;
    UnitId second;
};

Retargeting first_unit_destroyed(ScriptedDice& dice) {
    Retargeting r{shoot_step_state(), {}, {}};
    r.gants = r.gs.units[kGants].id;
    r.second = add_unit_copy(r.gs, kGants);
    r.gs.units[kGants].models.resize(1);
    const std::vector<WeaponSelection> sel{
        select(r.gs, kMarines, 0, kBoltRifle, kSaturation, r.gants),
        select(r.gs, kMarines, 1, kBoltRifle, kFocused, r.gants),
        select(r.gs, kMarines, 2, kBoltRifle, kSaturation, r.second),
    };
    begin_attacks(r.gs, dice, r.gs.units[kMarines].id, kShooting, sel);
    apply_action(r.gs, Side::A, action::SelectAttackBatch{0}, dice);  // 3 hits, 3 wounds, 3 failed saves
    apply_action(r.gs, Side::B, action::OrderCasualties{r.gants, model_ids(r.gs.units[kGants])}, dice);
    return r;
}

}  // namespace

TEST(D093_TargetNoLongerEligible, TheAttackerIsAskedForNewTargetsWhenOneIsEligible) {
    ScriptedDice dice{3, 3, 3, 3, 3, 3, 1, 1, 1};
    Retargeting r = first_unit_destroyed(dice);
    ASSERT_TRUE(is_destroyed(*r.gs.find_unit(r.gants)));
    const UnitId marines = r.gs.units[kMarines].id;
    ASSERT_EQ(*r.gs.pending, (PendingDecision{Side::A, decision::RetargetAttacks{marines}}));
    EXPECT_EQ(decision_rule(r.gs, *r.gs.pending), "D-093");
    // D-004: one answer per unit that a lost weapon can target, then none.
    EXPECT_EQ(legal_actions(r.gs, Side::A), (std::vector<GameAction>{action::RetargetAttacks{{AttackRetarget{1, r.second}}},
                                                                     action::RetargetAttacks{}}));
    EXPECT_EQ(dice.remaining(), 0u);
}

TEST(D093_TargetNoLongerEligible, NewTargetsGatherNewBatchesWithTheirSelectTargetsFacts) {
    ScriptedDice dice{3, 3, 3, 3, 3, 3, 1, 1, 1};
    Retargeting r = first_unit_destroyed(dice);
    const auto events = apply_action(r.gs, Side::A, action::RetargetAttacks{{AttackRetarget{1, r.second}}}, dice);
    EXPECT_TRUE(events_of<event::AttacksLost>(events).empty());  // nothing is lost: the one lost weapon moved
    const auto moved = events_of<event::AttacksRetargeted>(events);
    ASSERT_EQ(moved.size(), 1u);
    EXPECT_EQ(moved[0].attacker, r.gs.units[kMarines].id);
    EXPECT_EQ(moved[0].retargets, (std::vector<AttackRetarget>{{1, r.second}}));
    EXPECT_EQ(moved[0].first_batch, 3u);  // appended: earlier batch indices stay
    ASSERT_EQ(moved[0].batches.size(), 1u);
    EXPECT_EQ(moved[0].batches[0].target, r.second);
    EXPECT_EQ(moved[0].batches[0].selections, (std::vector<std::size_t>{1}));
    EXPECT_EQ(moved[0].batches[0].attack_dice, 2);  // Focused Fire A1 + [RAPID FIRE 1]: within half range now
    const AttackFlow& f = *r.gs.flow.attack;
    ASSERT_EQ(f.batches.size(), 4u);
    EXPECT_TRUE(f.batch_done[1]);
    EXPECT_FALSE(f.batch_done[3]);
    EXPECT_EQ(f.selections[1].target, r.second);
    EXPECT_TRUE(f.selections[1].target_within_half_range);
    EXPECT_EQ(moved[0].selections, (std::vector<WeaponSelection>{f.selections[1]}));
    // Two batches at the second unit: the attacker chooses the order (04.03), then both are resolved.
    ASSERT_EQ(*r.gs.pending, (PendingDecision{Side::A, decision::SelectAttackBatch{r.gs.units[kMarines].id}}));
    ScriptedDice misses{1, 1, 1, 1, 1};  // batch 3's two hit rolls, then batch 2's three
    apply_action(r.gs, Side::A, action::SelectAttackBatch{3}, misses);
    EXPECT_EQ(misses.remaining(), 0u);
    EXPECT_EQ(*r.gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
}

TEST(D093_TargetNoLongerEligible, WithNoEligibleNewTargetTheAttacksAreLostWithoutAsking) {
    // The second unit is moved more than 24" away (out of the Bolt Rifles' range), so batch 1 has no new target.
    GameState gs = shoot_step_state();
    const UnitId gants = gs.units[kGants].id;
    const UnitId far = add_unit_copy(gs, kGants);
    gs.units[kGants].models.resize(1);
    for (Model& m : gs.find_unit(far)->models) m.position.y += in(30.0f);
    ASSERT_GT(*unit_distance(gs.units[kMarines], *gs.find_unit(far)), in(24.0f));
    const std::vector<WeaponSelection> sel{select(gs, kMarines, 0, kBoltRifle, kSaturation, gants),
                                           select(gs, kMarines, 1, kBoltRifle, kFocused, gants)};
    ScriptedDice dice{3, 3, 3, 3, 3, 3, 1, 1, 1};
    begin_attacks(gs, dice, gs.units[kMarines].id, kShooting, sel);
    apply_action(gs, Side::A, action::SelectAttackBatch{0}, dice);
    const auto events = apply_action(gs, Side::B, action::OrderCasualties{gants, model_ids(gs.units[kGants])}, dice);
    const auto lost = events_of<event::AttacksLost>(events);
    ASSERT_EQ(lost.size(), 1u);
    EXPECT_EQ(lost[0].batch, 1u);
    EXPECT_EQ(lost[0].attack_dice, 2);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
}

TEST(D093_TargetNoLongerEligible, ABadAnswerIsRefusedWithReasons) {
    ScriptedDice dice{3, 3, 3, 3, 3, 3, 1, 1, 1};
    Retargeting r = first_unit_destroyed(dice);
    const UnitId marines = r.gs.units[kMarines].id;
    const auto refused = [&](const action::RetargetAttacks& a) {
        const ValidationResult v = validate_action(r.gs, Side::A, a);
        EXPECT_FALSE(v.ok());
        for (const std::string& reason : v.reasons) EXPECT_TRUE(is_rule_cited(reason)) << reason;
        return v.reasons.empty() ? std::string() : v.reasons.front();
    };
    // Selection 0 was resolved, and selection 2's target is still there: neither is waiting on a gone target.
    EXPECT_NE(refused({{AttackRetarget{0, r.second}}}).find("weapon selection 0 is not waiting"), std::string::npos);
    EXPECT_NE(refused({{AttackRetarget{2, r.second}}}).find("weapon selection 2 is not waiting"), std::string::npos);
    // Not an enemy unit it could select (its own unit; the destroyed one).
    EXPECT_NE(refused({{AttackRetarget{1, marines}}}).find("is not an eligible target"), std::string::npos);
    EXPECT_NE(refused({{AttackRetarget{1, r.gants}}}).find("is not an eligible target"), std::string::npos);
    // The same weapon twice.
    EXPECT_NE(refused({{AttackRetarget{1, r.second}, AttackRetarget{1, r.second}}}).find("more than once"),
              std::string::npos);
    // Wrong player: the attacker answers.
    EXPECT_FALSE(validate_action(r.gs, Side::B, action::RetargetAttacks{}).ok());
    EXPECT_TRUE(validate_action(r.gs, Side::A, action::RetargetAttacks{}).ok());
}

TEST(D093_TargetNoLongerEligible, AMeleeWeaponCanTakeAnotherUnitItsModelIsEngagedWith) {
    // After the V1 charge, one Termagant (alone in its unit) is engaged with two Intercessors, A and B; a copy of the
    // Termagants (unit `second`) stands where the original unit stood, so both are engaged with them. B's close
    // combat weapon is given S5 so A's and B's attacks are not identical (two batches, D-006). A's batch destroys the
    // Termagant: B's weapon can take the other unit (04.02 While Fighting: one its model is engaged with).
    GameState gs = wh40k::test::after_successful_charge();
    const UnitId marines = gs.units[kMarines].id;
    const UnitId second = add_unit_copy(gs, kGants);
    std::optional<std::size_t> gant;
    std::vector<std::size_t> engaged;
    for (std::size_t g = 0; g < gs.units[kGants].models.size() && !gant; ++g) {
        engaged.clear();
        for (std::size_t i = 0; i < gs.units[kMarines].models.size(); ++i) {
            const float d = base_distance(pose_of(gs.units[kMarines].models[i]), pose_of(gs.units[kGants].models[g]));
            if (d <= in(2.0f)) engaged.push_back(i);
        }
        if (engaged.size() >= 2) gant = g;
    }
    ASSERT_TRUE(gant.has_value());
    const Model kept = gs.units[kGants].models[*gant];
    gs.units[kGants].models = {kept};
    gs.flow.stage = Stage::Fight;
    gs.pending.reset();
    const Model& a = gs.units[kMarines].models[engaged[0]];
    std::optional<MeleeDeclaration> option;
    for (const MeleeDeclaration& d : melee_options(gs, gs.units[kMarines])) {
        if (d.model == a.id && d.targets.front().unit == gs.units[kGants].id) option = d;
    }
    ASSERT_TRUE(option.has_value());
    Model& b = gs.units[kMarines].models[engaged[1]];
    b.weapons[option->weapon_index].profiles[option->profile_index].strength += 1;
    const UnitId gants = gs.units[kGants].id;
    const std::vector<WeaponSelection> sel{
        select_weapon_target(gs, a.id, option->weapon_index, option->profile_index, gants),
        select_weapon_target(gs, b.id, option->weapon_index, option->profile_index, gants),
    };
    ScriptedDice dice{6, 6, 6, 6, 6, 6, 1, 1, 1};  // A's three attacks hit, wound and are not saved
    begin_attacks(gs, dice, marines, kFighting, sel);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::SelectAttackBatch{marines}}));
    apply_action(gs, Side::A, action::SelectAttackBatch{0}, dice);
    apply_action(gs, Side::B, action::OrderCasualties{gants, model_ids(gs.units[kGants])}, dice);
    ASSERT_TRUE(is_destroyed(*gs.find_unit(gants)));
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::RetargetAttacks{marines}}));
    EXPECT_EQ(legal_actions(gs, Side::A),
              (std::vector<GameAction>{action::RetargetAttacks{{AttackRetarget{1, second}}}, action::RetargetAttacks{}}));
    EXPECT_TRUE(validate_action(gs, Side::A, action::RetargetAttacks{{AttackRetarget{1, second}}}).ok());
    const auto reasons = validate_action(gs, Side::A, action::RetargetAttacks{{AttackRetarget{1, marines}}}).reasons;
    ASSERT_FALSE(reasons.empty());
    EXPECT_NE(reasons.front().find("engaged with"), std::string::npos) << reasons.front();
}

TEST(D093_TargetNoLongerEligible, TheShootingTypeIsKeptAndRoundTrips) {
    ScriptedDice dice{3, 3, 3, 3, 3, 3, 1, 1, 1};
    Retargeting r = first_unit_destroyed(dice);
    AttackFlow f = *r.gs.flow.attack;
    EXPECT_EQ(f.shooting_type, ShootingType::Normal);
    EXPECT_FALSE(Json(f).contains("shooting_type"));  // omitted when Normal: older files load and save the same
    f.shooting_type = ShootingType::Assault;
    EXPECT_EQ(Json(f).at("shooting_type"), "Assault");
    EXPECT_EQ(Json(f).get<AttackFlow>(), f);
    EXPECT_EQ(Json(r.gs).get<GameState>(), r.gs);
}

TEST(R02_02_Modifiers, ABallisticSkillWorsenedByCoverStopsAt6Plus) {
    // 02.02.01 (app-only, D-093): after all modifiers BS cannot be 7+ or worse.
    GameState gs = shoot_step_state();
    const UnitId gants = gs.units[kGants].id;
    WeaponSelection s = select(gs, kMarines, 0, kBoltRifle, kSaturation, gants);
    s.benefit_of_cover = true;
    gs.units[kMarines].models[0].weapons[kBoltRifle].profiles[kSaturation].skill = 3;
    EXPECT_EQ(gather_attack_batches(gs, std::vector<WeaponSelection>{s}, kShooting).at(0).profile.skill, 4);
    gs.units[kMarines].models[0].weapons[kBoltRifle].profiles[kSaturation].skill = 5;
    EXPECT_EQ(gather_attack_batches(gs, std::vector<WeaponSelection>{s}, kShooting).at(0).profile.skill, 6);
    gs.units[kMarines].models[0].weapons[kBoltRifle].profiles[kSaturation].skill = 6;
    EXPECT_EQ(gather_attack_batches(gs, std::vector<WeaponSelection>{s}, kShooting).at(0).profile.skill, 6);
}
