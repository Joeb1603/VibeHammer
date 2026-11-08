// M8: the Shooting phase through the engine. 10.02 Shoot, the shooting types (10.04-10.06), 04.01/04.02
// weapon and target selection, 06.01 visibility, and the shooting abilities 24.04, 24.07, 24.16, 24.27
// and 24.30. Every attack uses ScriptedDice.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/rules/shooting.hpp"
#include "core/state/queries.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::events_of;
using wh40k::test::starts_with;
using wh40k::test::rejection;
using wh40k::test::expect_valid;
using wh40k::test::add_unit_copy;
using wh40k::test::decider;
using wh40k::test::in;
using wh40k::test::is_rule_cited;
using wh40k::test::shoot_step_state;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;
constexpr std::size_t kBoltRifle = 0;
constexpr std::size_t kBoltPistol = 1;
constexpr std::size_t kKnives = 2;
constexpr std::size_t kFocused = 0;
constexpr std::size_t kSaturation = 1;
constexpr std::size_t kFleshborer = 0;
constexpr float kMarineRadius = 16.0f;
constexpr float kGantRadius = 14.25f;

ShotDeclaration shot(const GameState& gs, std::size_t unit, std::size_t model, std::size_t weapon,
                     std::size_t profile, std::size_t target) {
    return ShotDeclaration{gs.units[unit].models[model].id, weapon, profile, gs.units[target].id};
}

// Every Intercessor fires the same weapon profile at the Termagants.
action::Shoot marines_fire(const GameState& gs, ShootingType type, std::size_t weapon, std::size_t profile) {
    action::Shoot a{gs.units[kMarines].id, type, {}};
    for (std::size_t i = 0; i < gs.units[kMarines].models.size(); ++i) {
        a.shots.push_back(shot(gs, kMarines, i, weapon, profile, kGants));
    }
    return a;
}

// Puts the Termagants' front rank 1" from the Intercessors: the two units are engaged (03.04).
void engage(GameState& gs) {
    float front_y = gs.units[kGants].models[0].position.y;
    for (const Model& m : gs.units[kGants].models) front_y = std::min(front_y, m.position.y);
    const float marine_y = gs.units[kMarines].models[0].position.y;
    const float shift = front_y - (marine_y + kMarineRadius + kGantRadius + in(1));
    for (Model& m : gs.units[kGants].models) m.position.y -= shift;
    ASSERT_TRUE(is_engaged(gs, gs.units[kMarines]));
}

std::vector<GameAction> shoot_templates(const GameState& gs, Side side) {
    std::vector<GameAction> out;
    for (const auto& a : legal_actions(gs, side)) {
        if (std::holds_alternative<action::Shoot>(a)) out.push_back(a);
    }
    return out;
}

// Side A's first Movement phase, then a move of the Intercessors `dy_in` north (towards the Termagants)
// of the given type, ending at the Shoot step. The advance roll (if any) is `advance_roll`.
GameState after_marine_move(MoveType type, float dy_in, int advance_roll = 1) {
    GameState gs = wh40k::test::load_v1_scenario_closer();
    start_game(gs);
    ScriptedDice dice{advance_roll};
    apply_action(gs, Side::A, action::StartMove{gs.units[kMarines].id, type, std::nullopt}, dice);
    for (const Model& m : gs.units[kMarines].models) {
        const Position s = *move_start_position(gs, m.id);
        apply_action(gs, Side::A, action::ProposeModelMove{m.id, {Position{s.x, s.y + in(dy_in), 0.0f}}});
    }
    apply_action(gs, Side::A, action::CommitUnitMove{});
    EXPECT_EQ(gs.flow.stage, Stage::Shoot);
    return gs;
}

// Hit rolls of 1 always fail (05.01), so the volley ends after the hit rolls.
ScriptedDice all_misses(int n) { return ScriptedDice(std::vector<int>(static_cast<std::size_t>(n), 1)); }

}  // namespace

// ==== 10.02 Shoot ====

TEST(R10_02_Shoot, LegalActionsAreEndShootStepAndATemplatePerEligibleShootingType) {
    const GameState gs = shoot_step_state();
    const auto legal = legal_actions(gs, Side::A);
    ASSERT_EQ(legal.size(), 2u);
    EXPECT_EQ(legal[0], GameAction{action::EndShootStep{}});
    EXPECT_EQ(legal[1], GameAction{(action::Shoot{gs.units[kMarines].id, ShootingType::Normal, {}})});
    EXPECT_TRUE(is_template(legal[1]));
    EXPECT_TRUE(legal_actions(gs, Side::B).empty());
}

TEST(R10_02_Shoot, AnEmptyDeclarationIsTheTemplateAndIsNotValid) {
    const GameState gs = shoot_step_state();
    const auto reasons = rejection(gs, Side::A, action::Shoot{gs.units[kMarines].id, ShootingType::Normal, {}});
    ASSERT_EQ(reasons.size(), 1u);
    EXPECT_TRUE(starts_with(reasons[0], "D-016: ")) << reasons[0];
}

TEST(R10_02_Shoot, OnlyTheActivePlayerShootsAndOnlyWithFriendlyUnits) {
    const GameState gs = shoot_step_state();
    rejection(gs, Side::B, marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused));
    action::Shoot enemy{gs.units[kGants].id, ShootingType::Normal, {shot(gs, kGants, 0, kFleshborer, 0, kMarines)}};
    const auto reasons = rejection(gs, Side::A, enemy);
    EXPECT_EQ(reasons.at(0), "10.02: unit 2 is not a friendly unit");
}

TEST(R10_02_Shoot, AUnitIsSelectedToShootOnlyOncePerPhase) {
    GameState gs = shoot_step_state();
    const action::Shoot volley = marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused);
    ScriptedDice dice = all_misses(10);
    const auto events = apply_action(gs, Side::A, volley, dice);

    ASSERT_EQ(events_of<event::ShootingStarted>(events).size(), 1u);
    EXPECT_EQ(dice.remaining(), 0u);
    EXPECT_TRUE(gs.units[kMarines].phase_flags.selected_to_shoot);
    EXPECT_FALSE(gs.flow.activation.has_value());  // 01.03: the attacks are resolved
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
    EXPECT_EQ(legal_actions(gs, Side::A), (std::vector<GameAction>{action::EndShootStep{}}));
    const auto reasons = rejection(gs, Side::A, volley);
    EXPECT_EQ(reasons.at(0), "10.02: unit 1 has already been selected to shoot this phase");
}

TEST(R10_02_Shoot, SelectionIsResetWhenTheNextShootingPhaseStarts) {
    GameState gs = shoot_step_state();
    ScriptedDice dice = all_misses(10);
    apply_action(gs, Side::A, marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused), dice);
    // Pass every decision until Side A's next Shoot step (battle round 2).
    while (!(gs.flow.stage == Stage::Shoot && turn_side(gs) == Side::A && gs.flow.battle_round == 2)) {
        apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    }
    EXPECT_FALSE(gs.units[kMarines].phase_flags.selected_to_shoot);
    EXPECT_EQ(shoot_templates(gs, Side::A).size(), 1u);
}

TEST(R10_02_Shoot, ShootingStartedRecordsTheSelectionsAndTheAttacksFollow) {
    GameState gs = shoot_step_state();
    ScriptedDice dice = all_misses(10);
    const auto events = apply_action(gs, Side::A, marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused), dice);
    const auto started = events_of<event::ShootingStarted>(events);
    ASSERT_EQ(started.size(), 1u);
    EXPECT_EQ(started[0].type, ShootingType::Normal);
    ASSERT_EQ(started[0].selections.size(), 5u);
    for (const auto& s : started[0].selections) {
        EXPECT_EQ(s.target, gs.units[kGants].id);
        EXPECT_TRUE(s.target_within_half_range);  // 24.30, recorded at Select Targets
        EXPECT_EQ(s.target_models_at_selection, 10);
    }
    ASSERT_EQ(events_of<event::AttackBatchesGathered>(events).size(), 1u);
    EXPECT_EQ(events_of<event::HitRollsMade>(events).size(), 1u);
}

// ==== 10.04 Normal Shooting ====

TEST(R10_04_NormalShooting, FocusedFireVolleyIsResolvedByTheAttackSequence) {
    GameState gs = shoot_step_state();
    // 24.30: within 12", so A1 + [RAPID FIRE 1] = 2 dice each. 24.16: the unit remained stationary.
    ScriptedDice dice{6, 6, 1, 1, 1, 1, 1, 1, 1, 1,  // hits: 2 (6s; +1 can't turn a 1 into a hit)
                      3, 1,                          // wounds (S6 is twice T3: 2+): 1
                      1};                            // save: fails
    const auto events = apply_action(gs, Side::A, marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused), dice);
    const auto gathered = events_of<event::AttackBatchesGathered>(events);
    ASSERT_EQ(gathered.size(), 1u);
    ASSERT_EQ(gathered[0].batches.size(), 1u);
    EXPECT_EQ(gathered[0].batches[0].attack_dice, 10);
    EXPECT_EQ(gathered[0].batches[0].profile.hit_modifier, 1);  // [HEAVY]
    EXPECT_EQ(events_of<event::HitRollsMade>(events).at(0).rolls.hits(), 2);
    EXPECT_EQ(events_of<event::WoundRollsMade>(events).at(0).rolls.wounds(), 1);
    // One D2 wound on a 1-wound Termagant: the opposing player picks the casualty (D-013).
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::OrderCasualties{gs.units[kGants].id}}));
    EXPECT_EQ(dice.remaining(), 0u);
}

TEST(R10_04_NormalShooting, ModelsCanFireDifferentProfilesOfTheSameWeapon) {
    // D-001: profiles are chosen per model. Focused Fire and Saturation are not identical (D-006).
    GameState gs = shoot_step_state();
    action::Shoot a = marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused);
    a.shots[3].profile_index = kSaturation;
    a.shots[4].profile_index = kSaturation;
    ScriptedDice dice = all_misses(6 + 6);
    const auto events = apply_action(gs, Side::A, a, dice);
    const auto gathered = events_of<event::AttackBatchesGathered>(events);
    ASSERT_EQ(gathered.size(), 1u);
    ASSERT_EQ(gathered[0].batches.size(), 2u);
    EXPECT_EQ(gathered[0].batches[0].attack_dice, 6);  // 3 x (1 + 1)
    EXPECT_EQ(gathered[0].batches[1].attack_dice, 6);  // 2 x (2 + 1)
}

TEST(R10_04_NormalShooting, NotEligibleAfterAnAdvanceMove) {
    GameState gs = shoot_step_state();
    gs.units[kMarines].turn_flags.advanced = true;
    const auto reasons = rejection(gs, Side::A, marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused));
    EXPECT_EQ(reasons.at(0), "10.04: unit 1 made an advance move this turn");
}

TEST(R10_04_NormalShooting, NotEligibleWhileEngaged) {
    GameState gs = shoot_step_state();
    engage(gs);
    const auto reasons = rejection(gs, Side::A, marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused));
    EXPECT_EQ(reasons.at(0), "10.04: unit 1 is engaged; normal shooting needs an unengaged unit");
}

// ==== 04.01 Select Weapons ====

TEST(R04_01_SelectWeapons, AWeaponCanBeSelectedOnlyOnce) {
    GameState gs = shoot_step_state();
    action::Shoot a = marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused);
    a.shots.push_back(shot(gs, kMarines, 0, kBoltRifle, kSaturation, kGants));  // the same rifle again
    const auto reasons = rejection(gs, Side::A, a);
    ASSERT_EQ(reasons.size(), 1u);
    EXPECT_TRUE(starts_with(reasons[0], "04.01: model 1 selected weapon 0 more than once")) << reasons[0];
}

TEST(R04_01_SelectWeapons, OnlyRangedWeaponsAreSelectedWhileShooting) {
    GameState gs = shoot_step_state();
    const action::Shoot a{gs.units[kMarines].id, ShootingType::Normal, {shot(gs, kMarines, 0, kKnives, 0, kGants)}};
    const auto reasons = rejection(gs, Side::A, a);
    EXPECT_TRUE(starts_with(reasons.at(0), "04.01: 'Knives and Fists' is not a ranged weapon")) << reasons[0];
}

TEST(R04_01_SelectWeapons, TheModelMustBeInTheShootingUnit) {
    GameState gs = shoot_step_state();
    action::Shoot a = marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused);
    a.shots[0].model = gs.units[kGants].models[0].id;
    const auto reasons = rejection(gs, Side::A, a);
    EXPECT_EQ(reasons.at(0), "04.01: model 6 is not a model in unit 1");
}

// ==== 24.07 [CLOSE-QUARTERS] / 24.27 [PISTOL] ====

TEST(R24_07_CloseQuarters, InNormalShootingAModelFiresItsPistolOrItsRifleNotBoth) {
    GameState gs = shoot_step_state();
    action::Shoot both = marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused);
    both.shots.push_back(shot(gs, kMarines, 2, kBoltPistol, 0, kGants));
    const auto reasons = rejection(gs, Side::A, both);
    ASSERT_EQ(reasons.size(), 1u);
    EXPECT_EQ(reasons[0],
              "24.07: model 3 can select either its [CLOSE-QUARTERS] weapons or its other ranged weapons, not both");

    // Pistols for some models and rifles for others is fine; the Bolt pistol and a Saturation Bolt
    // Rifle make identical attacks (D-006).
    action::Shoot mixed = marines_fire(gs, ShootingType::Normal, kBoltRifle, kSaturation);
    mixed.shots[2] = shot(gs, kMarines, 2, kBoltPistol, 0, kGants);
    expect_valid(gs, Side::A, mixed);
}

TEST(R24_27_Pistol, PistolIsTreatedAsCloseQuarters) {
    // A Termagant spinefist is [ASSAULT, PISTOL]; give one Termagant a [PISTOL] weapon and the engaged
    // Termagants can make close-quarters shooting.
    GameState gs = shoot_step_state(Side::B);
    engage(gs);
    Model& gant = gs.units[kGants].models[0];
    Weapon spinefist = gant.weapons[kFleshborer];
    spinefist.name = "Pistol test weapon";
    spinefist.profiles[0].abilities = {parse_weapon_ability("Pistol")};
    gant.weapons.push_back(spinefist);
    EXPECT_EQ(eligible_shooting_types(gs, gs.units[kGants]), (std::vector<ShootingType>{ShootingType::CloseQuarters}));
}

// ==== 10.05 Assault Shooting ====

TEST(R10_05_AssaultShooting, AfterAnAdvanceOnlyAssaultShootingWithAssaultProfiles) {
    GameState gs = after_marine_move(MoveType::Advance, 1.0f);
    ASSERT_TRUE(gs.units[kMarines].turn_flags.advanced);
    const auto templates = shoot_templates(gs, Side::A);
    ASSERT_EQ(templates.size(), 1u);
    EXPECT_EQ(std::get<action::Shoot>(templates[0]).type, ShootingType::Assault);

    rejection(gs, Side::A, marines_fire(gs, ShootingType::Normal, kBoltRifle, kSaturation));
    // [ASSAULT] is per profile (24.04): Saturation may fire, Focused Fire may not.
    expect_valid(gs, Side::A, marines_fire(gs, ShootingType::Assault, kBoltRifle, kSaturation));
    auto reasons = rejection(gs, Side::A, marines_fire(gs, ShootingType::Assault, kBoltRifle, kFocused));
    EXPECT_EQ(reasons.size(), 5u);
    EXPECT_TRUE(starts_with(reasons.at(0), "10.05: 'Bolt Rifle - Focused Fire' is not an [ASSAULT] weapon"))
        << reasons[0];
    reasons = rejection(gs, Side::A, marines_fire(gs, ShootingType::Assault, kBoltPistol, 0));
    EXPECT_TRUE(starts_with(reasons.at(0), "10.05: 'Bolt pistol' is not an [ASSAULT] weapon")) << reasons[0];
}

TEST(R10_05_AssaultShooting, NotEligibleWithoutAnAdvanceMove) {
    GameState gs = shoot_step_state();
    const auto reasons = rejection(gs, Side::A, marines_fire(gs, ShootingType::Assault, kBoltRifle, kSaturation));
    EXPECT_EQ(reasons.at(0), "10.05: unit 1 did not make an advance move this turn");
}

TEST(R10_05_AssaultShooting, NotEligibleWithoutAssaultWeapons) {
    GameState gs = shoot_step_state();
    gs.units[kMarines].turn_flags.advanced = true;
    for (Model& m : gs.units[kMarines].models) m.weapons[kBoltRifle].profiles.pop_back();  // no Saturation
    EXPECT_TRUE(eligible_shooting_types(gs, gs.units[kMarines]).empty());
    EXPECT_EQ(legal_actions(gs, Side::A), (std::vector<GameAction>{action::EndShootStep{}}));
    const auto reasons = rejection(gs, Side::A, marines_fire(gs, ShootingType::Assault, kBoltRifle, kFocused));
    EXPECT_EQ(reasons.at(0), "10.05: unit 1 has no [ASSAULT] weapons");
}

// ==== 10.06 Close-quarters Shooting ====

TEST(R10_06_CloseQuartersShooting, EngagedIntercessorsFireBoltPistolsAtTheUnitTheyAreEngagedWith) {
    GameState gs = shoot_step_state();
    engage(gs);
    const auto templates = shoot_templates(gs, Side::A);
    ASSERT_EQ(templates.size(), 1u);
    EXPECT_EQ(std::get<action::Shoot>(templates[0]).type, ShootingType::CloseQuarters);

    const action::Shoot pistols = marines_fire(gs, ShootingType::CloseQuarters, kBoltPistol, 0);
    ScriptedDice dice = all_misses(5);
    const auto events = apply_action(gs, Side::A, pistols, dice);
    const auto gathered = events_of<event::AttackBatchesGathered>(events);
    ASSERT_EQ(gathered.size(), 1u);
    EXPECT_EQ(gathered[0].batches.at(0).attack_dice, 5);
    EXPECT_EQ(gathered[0].batches.at(0).profile.hit_modifier, 0);
    EXPECT_EQ(dice.remaining(), 0u);
}

TEST(R10_06_CloseQuartersShooting, NonCloseQuartersWeaponsCannotBeSelected) {
    GameState gs = shoot_step_state();
    engage(gs);
    const auto reasons = rejection(gs, Side::A, marines_fire(gs, ShootingType::CloseQuarters, kBoltRifle, kFocused));
    EXPECT_EQ(reasons.size(), 5u);
    EXPECT_TRUE(starts_with(reasons.at(0), "10.06: 'Bolt Rifle - Focused Fire' is not a [CLOSE-QUARTERS] weapon"))
        << reasons[0];
}

TEST(R10_06_CloseQuartersShooting, EngagedTermagantsCannotShoot) {
    GameState gs = shoot_step_state(Side::B);
    engage(gs);
    EXPECT_TRUE(eligible_shooting_types(gs, gs.units[kGants]).empty());
    EXPECT_EQ(legal_actions(gs, Side::B), (std::vector<GameAction>{action::EndShootStep{}}));
    const action::Shoot a{gs.units[kGants].id, ShootingType::CloseQuarters,
                          {shot(gs, kGants, 0, kFleshborer, 0, kMarines)}};
    const auto reasons = rejection(gs, Side::B, a);
    EXPECT_EQ(reasons.at(0), "10.06: unit 2 has no [CLOSE-QUARTERS] weapons and is not a MONSTER/VEHICLE unit");
    const auto normal = rejection(gs, Side::B, action::Shoot{gs.units[kGants].id, ShootingType::Normal, a.shots});
    EXPECT_EQ(normal.at(0), "10.04: unit 2 is engaged; normal shooting needs an unengaged unit");
}

TEST(R10_06_CloseQuartersShooting, NotEligibleWhenUnengaged) {
    const GameState gs = shoot_step_state();
    const auto reasons = rejection(gs, Side::A, marines_fire(gs, ShootingType::CloseQuarters, kBoltPistol, 0));
    EXPECT_EQ(reasons.at(0), "10.06: unit 1 is unengaged; close-quarters shooting needs an engaged unit");
}

TEST(R10_06_CloseQuartersShooting, OnlyUnitsEngagedWithTheShooterCanBeTargeted) {
    GameState gs = shoot_step_state();
    engage(gs);
    // A second Termagant unit, unengaged, 6" north of the first marine.
    const UnitId other = add_unit_copy(gs, kGants);
    Unit& u = *gs.find_unit(other);
    u.models.resize(1);
    const Position m = gs.units[kMarines].models[0].position;
    u.models[0].position = Position{m.x - in(3), m.y + in(6), 0.0f};
    ASSERT_FALSE(is_engaged(gs, u));
    const action::Shoot a{gs.units[kMarines].id, ShootingType::CloseQuarters,
                          {ShotDeclaration{gs.units[kMarines].models[0].id, kBoltPistol, 0, other}}};
    const auto reasons = rejection(gs, Side::A, a);
    EXPECT_EQ(reasons.at(0),
              "10.06: model 1 can only target enemy units engaged with unit 1 in close-quarters shooting");
}

TEST(R10_06_CloseQuartersShooting, MonsterModelsMayFireAnyWeaponWithMinusOneToHit) {
    GameState gs = shoot_step_state();
    engage(gs);
    gs.units[kMarines].keywords.push_back("Monster");  // a stand-in MONSTER unit
    for (Model& m : gs.units[kMarines].models) m.weapons.erase(m.weapons.begin() + kBoltPistol);  // no pistols
    EXPECT_EQ(eligible_shooting_types(gs, gs.units[kMarines]),
              (std::vector<ShootingType>{ShootingType::CloseQuarters}));

    action::Shoot a = marines_fire(gs, ShootingType::CloseQuarters, kBoltRifle, kSaturation);
    expect_valid(gs, Side::A, a);
    ScriptedDice dice = all_misses(5 * 3);
    const auto events = apply_action(gs, Side::A, a, dice);
    const auto gathered = events_of<event::AttackBatchesGathered>(events);
    ASSERT_EQ(gathered.size(), 1u);
    EXPECT_EQ(gathered[0].batches.at(0).profile.hit_modifier, -1);  // not a [CLOSE-QUARTERS] weapon
}

TEST(R10_06_CloseQuartersShooting, MonsterCloseQuartersWeaponAtAnEngagedUnitHasNoPenalty) {
    GameState gs = shoot_step_state();
    engage(gs);
    gs.units[kMarines].keywords.push_back("Monster");
    const auto selections = make_weapon_selections(
        gs, gs.units[kMarines], ShootingType::CloseQuarters,
        std::vector<ShotDeclaration>{shot(gs, kMarines, 0, kBoltPistol, 0, kGants)});
    EXPECT_EQ(selections.at(0).hit_modifier, 0);
}

TEST(R10_06_CloseQuartersShooting, MonsterBlastWeaponsCannotTargetAnEngagedUnit) {
    GameState gs = shoot_step_state();
    engage(gs);
    gs.units[kMarines].keywords.push_back("Monster");
    for (Model& m : gs.units[kMarines].models) {
        m.weapons[kBoltRifle].profiles[kSaturation].abilities.push_back(ability::Blast{1});
    }
    const auto reasons = rejection(gs, Side::A, marines_fire(gs, ShootingType::CloseQuarters, kBoltRifle, kSaturation));
    EXPECT_TRUE(starts_with(reasons.at(0), "10.06: 'Bolt Rifle - Saturation' is a [BLAST] weapon")) << reasons[0];
}

// ==== 09.07 Fall-back Move: not eligible to shoot ====

TEST(R09_07_FallBackMove, AUnitThatFellBackCannotShoot) {
    GameState gs = shoot_step_state();
    gs.units[kMarines].turn_flags.fell_back = true;
    EXPECT_TRUE(eligible_shooting_types(gs, gs.units[kMarines]).empty());
    EXPECT_EQ(legal_actions(gs, Side::A), (std::vector<GameAction>{action::EndShootStep{}}));
    const auto reasons = rejection(gs, Side::A, marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused));
    EXPECT_EQ(reasons.at(0), "09.07: unit 1 made a fall-back move this turn and is not eligible to shoot");
}

// ==== 04.02 Select Targets ====

TEST(R04_02_SelectTargets, TermagantsShootFrom18InchesButNotFrom18Point1) {
    GameState gs = shoot_step_state(Side::B);
    const Position marine = gs.units[kMarines].models[2].position;  // the centre Intercessor
    Model& gant = gs.units[kGants].models[0];
    const action::Shoot a{gs.units[kGants].id, ShootingType::Normal, {shot(gs, kGants, 0, kFleshborer, 0, kMarines)}};

    gant.position = Position{marine.x, marine.y + kMarineRadius + kGantRadius + in(18), 0.0f};
    expect_valid(gs, Side::B, a);

    gant.position = Position{marine.x, marine.y + kMarineRadius + kGantRadius + in(18.1f), 0.0f};
    const auto reasons = rejection(gs, Side::B, a);
    EXPECT_EQ(reasons.at(0), "04.02: unit 1 is not within range (18.00\") of model 6's 'Fleshborer'");
}

TEST(R04_02_SelectTargets, EachWeaponNeedsItsOwnTargetInRange) {
    // A Bolt pistol (12") can't reach a target 13" away that a Bolt Rifle (24") can.
    GameState gs = shoot_step_state();
    for (Model& m : gs.units[kGants].models) m.position.y += in(2);  // 13" away
    expect_valid(gs, Side::A, marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused));
    const auto reasons = rejection(gs, Side::A, marines_fire(gs, ShootingType::Normal, kBoltPistol, 0));
    EXPECT_EQ(reasons.size(), 5u);
    EXPECT_TRUE(starts_with(reasons.at(0), "04.02: unit 2 is not within range (12.00\")")) << reasons[0];
}

TEST(R04_02_SelectTargets, TheTargetMustBeAnEnemyUnit) {
    GameState gs = shoot_step_state();
    action::Shoot a = marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused);
    a.shots[0].target = gs.units[kMarines].id;
    const auto reasons = rejection(gs, Side::A, a);
    EXPECT_EQ(reasons.at(0), "04.02: the target of 'Bolt Rifle - Focused Fire' must be an enemy unit");
}

TEST(R04_02_SelectTargets, TheTargetMustBeUnengaged) {
    // A second Intercessor unit engages the Termagants; the first one can't shoot at them.
    GameState gs = shoot_step_state();
    const UnitId other = add_unit_copy(gs, kMarines);
    Unit& u = *gs.find_unit(other);
    u.models.resize(1);
    const Position g = gs.units[kGants].models[0].position;
    u.models[0].position = Position{g.x, g.y - kGantRadius - kMarineRadius - in(1), 0.0f};
    ASSERT_TRUE(is_engaged(gs, gs.units[kGants]));
    ASSERT_FALSE(is_engaged(gs, gs.units[kMarines]));
    const auto reasons = rejection(gs, Side::A, marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused));
    EXPECT_EQ(reasons.at(0), "04.02: unit 2 is engaged; targets must be unengaged");
}

// ==== 06.01 Visibility (through the engine) ====

namespace {

// One Termagant 10.6" north of the centre Intercessor; every other Termagant removed.
GameState lone_target() {
    GameState gs = shoot_step_state();
    gs.units[kGants].models.resize(1);
    const Position m = gs.units[kMarines].models[2].position;
    gs.units[kGants].models[0].position = Position{m.x, in(16), 0.0f};
    return gs;
}

Model big_model(const GameState& gs, std::uint32_t id, Position pos) {
    Model big = gs.units[kMarines].models[0];
    big.id = ModelId{id};
    big.base = Base{BaseShape::Round, 100.0f, 0.0f};
    big.position = pos;
    return big;
}

}  // namespace

TEST(R06_01_Visibility, AModelOfAnotherUnitBlocksTheTarget) {
    GameState gs = lone_target();
    const Position m = gs.units[kMarines].models[2].position;
    const UnitId blocker = add_unit_copy(gs, kMarines);  // a friendly unit in the way
    Unit& b = *gs.find_unit(blocker);
    b.models.resize(1);
    b.models[0] = big_model(gs, 500, Position{m.x, in(10.5f), 0.0f});
    ASSERT_FALSE(is_engaged(gs, gs.units[kGants]));

    const action::Shoot a{gs.units[kMarines].id, ShootingType::Normal, {shot(gs, kMarines, 2, kBoltRifle, kFocused, kGants)}};
    const auto reasons = rejection(gs, Side::A, a);
    EXPECT_EQ(reasons.at(0), "06.01: unit 2 is not visible to model 3");
}

TEST(R06_01_Visibility, ModelsInTheObservingUnitAreIgnored) {
    GameState gs = lone_target();
    const Position m = gs.units[kMarines].models[2].position;
    gs.units[kMarines].models.push_back(big_model(gs, 500, Position{m.x, in(10.5f), 0.0f}));
    EXPECT_TRUE(unit_visible_to_model(gs, gs.units[kMarines].models[2], gs.units[kGants]));
    const action::Shoot a{gs.units[kMarines].id, ShootingType::Normal, {shot(gs, kMarines, 2, kBoltRifle, kFocused, kGants)}};
    expect_valid(gs, Side::A, a);
}

// ==== 24.16 [HEAVY] ====

TEST(R24_16_Heavy, AppliesWhenNoModelMovedMoreThan3Inches) {
    GameState gs = after_marine_move(MoveType::Normal, 3.0f);
    EXPECT_FALSE(gs.units[kMarines].turn_flags.moved_over_3in);
    EXPECT_TRUE(shooting_context(gs, gs.units[kMarines]).heavy_conditions_met);
    ScriptedDice dice = all_misses(10);
    const auto events = apply_action(gs, Side::A, marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused), dice);
    EXPECT_EQ(events_of<event::AttackBatchesGathered>(events).at(0).batches.at(0).profile.hit_modifier, 1);
}

TEST(R24_16_Heavy, DoesNotApplyAfterAModelMovedMoreThan3Inches) {
    GameState gs = after_marine_move(MoveType::Normal, 3.5f);
    EXPECT_TRUE(gs.units[kMarines].turn_flags.moved_over_3in);
    EXPECT_FALSE(shooting_context(gs, gs.units[kMarines]).heavy_conditions_met);
    ScriptedDice dice = all_misses(10);
    const auto events = apply_action(gs, Side::A, marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused), dice);
    EXPECT_EQ(events_of<event::AttackBatchesGathered>(events).at(0).batches.at(0).profile.hit_modifier, 0);
}

TEST(R24_16_Heavy, DoesNotApplyWhileEngaged) {
    GameState gs = shoot_step_state();
    engage(gs);
    EXPECT_FALSE(shooting_context(gs, gs.units[kMarines]).heavy_conditions_met);
}

// ==== 24.30 [RAPID FIRE] ====

TEST(R24_30_RapidFire, ExtraAttackOnlyForModelsWithTheTargetWithinHalfRange) {
    // At the D-008 start positions only the centre Intercessor is within 12" of the Termagants.
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (gs.flow.stage != Stage::Shoot) apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    ScriptedDice dice = all_misses(6);
    const auto events = apply_action(gs, Side::A, marines_fire(gs, ShootingType::Normal, kBoltRifle, kFocused), dice);
    EXPECT_EQ(events_of<event::AttackBatchesGathered>(events).at(0).batches.at(0).attack_dice, 6);  // 5 + 1
}

// ==== D-026: weapons with abilities V1 does not implement are rejected when selected ====

TEST(D026_UnimplementedAbilities, ADevastatingWoundsRangedWeaponIsRejectedAndNotOffered) {
    // M61: [TWIN-LINKED] is implemented now (D-072; this test used it until then), so [DEVASTATING WOUNDS] (24.10),
    // still refused, stands in for an unimplemented ability on the Termagants' only ranged weapon.
    GameState gs = shoot_step_state(Side::B);
    for (Model& m : gs.units[kGants].models) {
        m.weapons[kFleshborer].profiles[0].abilities.push_back(ability::DevastatingWounds{});
    }
    EXPECT_TRUE(shot_options(gs, gs.units[kGants], ShootingType::Normal).empty());
    EXPECT_EQ(legal_actions(gs, Side::B), (std::vector<GameAction>{action::EndShootStep{}}));
    const action::Shoot a{gs.units[kGants].id, ShootingType::Normal, {shot(gs, kGants, 0, kFleshborer, 0, kMarines)}};
    const auto reasons = rejection(gs, Side::B, a);
    EXPECT_TRUE(starts_with(reasons.at(0), "24.10: 'Fleshborer' has [DEVASTATING WOUNDS]")) << reasons[0];
}
