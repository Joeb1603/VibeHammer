// M101 (D-103): [INDIRECT FIRE] (24.19) with Indirect Shooting (10.07), and [PRECISION] (24.28, with the app's 24.28.01
// and 24.37.01). The engine tests use the V1 scenario with stand-in abilities on the Bolt Rifle and the Fleshborer (no
// roster weapon has them), M88's barricade area for a target that can't be seen, and the Intercessors made a CHARACTER
// unit (each model its own allocation group, 05.03) for [PRECISION].

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "core/data/datasheet.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/casualties.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/terrain_shape.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/shooting.hpp"
#include "core/rules/terrain.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::add_unit_copy;
using wh40k::test::answer_casualties;
using wh40k::test::at;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::in;
using wh40k::test::model_ids;
using wh40k::test::rejection;
using wh40k::test::repo_root;
using wh40k::test::shoot_step_state;
using Json = nlohmann::json;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;
constexpr std::size_t kBoltRifle = 0;
constexpr std::size_t kBoltPistol = 1;
constexpr std::size_t kFocused = 0;
constexpr std::size_t kFleshborer = 0;

TerrainShape rect_in(float x0, float x1, float y0, float y1) {
    return polygon_shape({at(x0, y0), at(x1, y0), at(x1, y1), at(x0, y1)});
}

TerrainArea area_with(const std::string& name, TerrainShape shape, TerrainCategory category, TerrainShape feature) {
    TerrainFeature f;
    f.name = name;
    f.category = category;
    f.shape = std::move(feature);
    if (category == TerrainCategory::Dense) f.height_mm = in(5.0f);
    TerrainArea a;
    a.name = name;
    a.shape = std::move(shape);
    a.features = {f};
    return a;
}

// M88's barricade area between the units at y 10-13: an obscuring area (13.10), so neither unit sees the other.
std::vector<TerrainArea> barricades() {
    return {area_with("Barricades", rect_in(0.5f, 23.5f, 10, 13), TerrainCategory::Light, rect_in(1, 23, 11.4f, 11.6f))};
}

void row(Unit& u, float x_in, float y_in, float step_in) {
    for (std::size_t i = 0; i < u.models.size(); ++i) u.models[i].position = at(x_in + step_in * static_cast<float>(i), y_in);
}

// The V1 scenario with every Bolt Rifle (Focused Fire: A 1, BS 3+, [HEAVY], [RAPID FIRE 1], 24") given [INDIRECT FIRE]
// and these other abilities before the game starts, played to Side A's Shoot step (every unit remained stationary:
// recorded for the Intercessors, who have the weapon, D-103). Then the units stand in rows, the Intercessors along
// y = 6" and the Termagants along y = 17", with `terrain`.
GameState indirect_shooting(std::vector<TerrainArea> terrain, std::vector<WeaponAbility> more = {}) {
    GameState gs = wh40k::test::load_v1_scenario_closer();
    for (Model& m : gs.units[kMarines].models) {
        WeaponProfile& p = m.weapons[kBoltRifle].profiles[kFocused];
        p.abilities.push_back(ability::IndirectFire{});
        p.abilities.insert(p.abilities.end(), more.begin(), more.end());
    }
    start_game(gs);
    for (int guard = 0; guard < 50 && !(gs.flow.stage == Stage::Shoot && turn_side(gs) == Side::A); ++guard) {
        apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    }
    row(gs.units[kMarines], 10.0f, 6.0f, 1.5f);
    row(gs.units[kGants], 4.0f, 17.0f, 1.3f);
    gs.terrain = std::move(terrain);
    return gs;
}

// The first `n` Intercessors fire their Bolt Rifles (Focused Fire) at the Termagants with shooting type `type`.
action::Shoot rifles(const GameState& gs, ShootingType type, std::size_t n) {
    action::Shoot a{gs.units[kMarines].id, type, {}};
    for (std::size_t i = 0; i < n; ++i) {
        a.shots.push_back(ShotDeclaration{gs.units[kMarines].models[i].id, kBoltRifle, kFocused, gs.units[kGants].id});
    }
    return a;
}

// Side B's Shoot step on the V1 scenario with the Intercessors a CHARACTER unit (each model its own allocation group)
// and every Fleshborer (A 1, BS 4+, S 5, AP 0, D `damage`; it wounds T 5 on 4+) given these abilities.
GameState gants_at_characters(std::vector<WeaponAbility> abilities, DiceExpr damage = DiceExpr::fixed(1)) {
    GameState gs = shoot_step_state(Side::B);
    gs.units[kMarines].keywords.push_back("Character");
    for (Model& m : gs.units[kGants].models) {
        WeaponProfile& p = m.weapons[kFleshborer].profiles[0];
        p.abilities.insert(p.abilities.end(), abilities.begin(), abilities.end());
        p.damage = damage;
    }
    return gs;
}

action::Shoot fleshborers(const GameState& gs, std::size_t n) {
    action::Shoot a{gs.units[kGants].id, ShootingType::Normal, {}};
    for (std::size_t i = 0; i < n; ++i) {
        a.shots.push_back(ShotDeclaration{gs.units[kGants].models[i].id, kFleshborer, 0, gs.units[kMarines].id});
    }
    return a;
}

bool precision_pending(const GameState& gs) {
    return gs.pending &&
           gs.pending->kind == DecisionKind{decision::SelectPrecisionGroup{gs.units[kGants].id, gs.units[kMarines].id}};
}

GameState round_trip(const GameState& gs) { return Json(gs).get<GameState>(); }

}  // namespace

// ==== 24.19 [INDIRECT FIRE] and 10.07 Indirect Shooting ====

TEST(R24_19_IndirectFire, ParsesIndirectFire) {
    EXPECT_EQ(parse_weapon_ability("Indirect Fire"), WeaponAbility{ability::IndirectFire{}});
    EXPECT_EQ(parse_weapon_ability("[INDIRECT FIRE]"), WeaponAbility{ability::IndirectFire{}});
    EXPECT_EQ(Json(WeaponAbility{ability::IndirectFire{}}), Json({{"kind", "IndirectFire"}}));
    EXPECT_EQ(Json({{"kind", "Unknown"}, {"text", "Indirect Fire"}}).get<WeaponAbility>(),
              WeaponAbility{ability::IndirectFire{}});
    // The shooting type, by name (also its JSON name).
    EXPECT_EQ(shooting_type_name(ShootingType::Indirect), "Indirect");
    EXPECT_EQ(shooting_type_rule(ShootingType::Indirect), "10.07");
    EXPECT_EQ(shooting_type_from_name("Indirect"), ShootingType::Indirect);
    // Used up as the shooting type and the selection's facts: not an attack-time ability (D-006).
    WeaponProfile p = wh40k::test::load_v1_scenario().units[kMarines].models[0].weapons[kBoltRifle].profiles[kFocused];
    p.abilities.push_back(ability::IndirectFire{});
    EXPECT_TRUE(make_attack_profile(p, AttackContext{AttackType::Shooting, false}).attack_time_abilities.empty());
    // App 24.37.01: a [TORRENT] weapon cannot have it; such a profile is refused.
    p.abilities.push_back(ability::Torrent{});
    const auto problem = unimplemented_ability_problem(p);
    ASSERT_TRUE(problem.has_value());
    EXPECT_EQ(problem->rfind("24.37: ", 0), 0u) << *problem;
}

TEST(R10_07_IndirectShooting, EligibleForAnUnengagedUnitWithAnIndirectFireWeaponThatDidNotAdvance) {
    GameState gs = indirect_shooting({});
    const Unit& marines = gs.units[kMarines];
    EXPECT_TRUE(has_indirect_fire_weapon(marines));
    EXPECT_FALSE(has_indirect_fire_weapon(gs.units[kGants]));
    EXPECT_EQ(eligible_shooting_types(gs, marines), (std::vector<ShootingType>{ShootingType::Normal, ShootingType::Indirect}));
    EXPECT_FALSE(shooting_type_problem(gs, marines, ShootingType::Indirect).has_value());
    // A Shoot template for it is listed after the Normal one.
    std::vector<ShootingType> templates;
    for (const GameAction& a : legal_actions(gs, Side::A)) {
        if (const auto* s = std::get_if<action::Shoot>(&a)) templates.push_back(s->type);
    }
    EXPECT_EQ(templates, (std::vector<ShootingType>{ShootingType::Normal, ShootingType::Indirect}));
    // Without such a weapon.
    GameState plain = shoot_step_state();
    const auto no_weapon = shooting_type_problem(plain, plain.units[kMarines], ShootingType::Indirect);
    ASSERT_TRUE(no_weapon.has_value());
    EXPECT_EQ(*no_weapon, "10.07: unit " + std::to_string(plain.units[kMarines].id.v) + " has no [INDIRECT FIRE] weapons");
    EXPECT_EQ(eligible_shooting_types(plain, plain.units[kMarines]), std::vector<ShootingType>{ShootingType::Normal});
    // After an advance move.
    gs.units[kMarines].turn_flags.advanced = true;
    const auto advanced = shooting_type_problem(gs, gs.units[kMarines], ShootingType::Indirect);
    ASSERT_TRUE(advanced.has_value());
    EXPECT_EQ(advanced->rfind("10.07: ", 0), 0u);
    gs.units[kMarines].turn_flags.advanced = false;
    // Engaged.
    for (Model& m : gs.units[kGants].models) m.position.y = gs.units[kMarines].models[0].position.y + in(2.0f);
    const auto engaged = shooting_type_problem(gs, gs.units[kMarines], ShootingType::Indirect);
    ASSERT_TRUE(engaged.has_value());
    EXPECT_NE(engaged->find("is engaged"), std::string::npos) << *engaged;
}

TEST(R10_07_IndirectShooting, AnIndirectFireWeaponCanTargetAUnitItsModelCantSee) {
    GameState gs = indirect_shooting(barricades());
    const Unit& marines = gs.units[kMarines];
    ASSERT_FALSE(unit_visible_to_model(gs, marines.models[0], gs.units[kGants]));
    // Normal shooting: nothing can be targeted (13.10). Indirect shooting: the Bolt Rifles' [INDIRECT FIRE] profile
    // can, the Bolt pistols and the rifles' other profile can't.
    EXPECT_TRUE(shot_options(gs, marines, ShootingType::Normal).empty());
    const std::vector<ShotDeclaration> options = shot_options(gs, marines, ShootingType::Indirect);
    EXPECT_EQ(options.size(), marines.models.size());
    for (const ShotDeclaration& o : options) {
        EXPECT_EQ(o.weapon_index, kBoltRifle);
        EXPECT_EQ(o.profile_index, kFocused);
    }
    EXPECT_TRUE(validate_action(gs, Side::A, rifles(gs, ShootingType::Indirect, 5)).ok());
    // (The terrain reason cites two rules, "13.10 / 13.11: ...", so it is read from validate_action directly.)
    const ValidationResult normal = validate_action(gs, Side::A, rifles(gs, ShootingType::Normal, 1));
    ASSERT_FALSE(normal.ok());
    EXPECT_EQ(normal.reasons.at(0).rfind("13.10 / 13.11:", 0), 0u) << normal.reasons.at(0);
    const action::Shoot pistol{marines.id, ShootingType::Indirect,
                               {ShotDeclaration{marines.models[0].id, kBoltPistol, 0, gs.units[kGants].id}}};
    const ValidationResult unseen = validate_action(gs, Side::A, pistol);
    ASSERT_FALSE(unseen.ok());
    EXPECT_EQ(unseen.reasons.at(0).rfind("13.10 / 13.11:", 0), 0u) << unseen.reasons.at(0);
    // Range still applies (04.02): 16" further away the Termagants are beyond the rifles' 24".
    for (Model& m : gs.units[kGants].models) m.position.y += in(16.0f);
    EXPECT_TRUE(shot_options(gs, marines, ShootingType::Indirect).empty());
}

TEST(R10_07_IndirectShooting, TheTargetHasCoverAndUnmodifiedHitRollsOfOneToFiveFail) {
    GameState gs = indirect_shooting(barricades());
    ASSERT_TRUE(gs.units[kMarines].turn_flags.remained_stationary);
    const action::Shoot shoot = rifles(gs, ShootingType::Indirect, 2);
    const auto sels = make_weapon_selections(gs, gs.units[kMarines], ShootingType::Indirect, shoot.shots);
    ASSERT_EQ(sels.size(), 2u);
    for (const WeaponSelection& s : sels) {
        EXPECT_TRUE(s.benefit_of_cover);  // 10.07, though the Termagants are in no terrain area
        EXPECT_EQ(s.hit_fails_to, 5);     // remained stationary, but no friendly unit can see the target
    }
    // Two rifles within half range: 4 attack dice (24.30). BS 3+ worsened to 4+ by the cover (13.08), +1 for [HEAVY].
    // Hit rolls 5, 5, 6, 6: the 5s fail whatever the BS; the 6s are critical hits.
    ScriptedDice dice{5, 5, 6, 6, 1, 1};
    const auto events = apply_action(gs, Side::A, shoot, dice);
    EXPECT_EQ(dice.remaining(), 0u);
    const AttackBatch batch = events_of<event::AttackBatchesGathered>(events).at(0).batches.at(0);
    EXPECT_EQ(batch.attack_dice, 4);
    EXPECT_EQ(batch.profile.skill, 4);
    EXPECT_EQ(batch.profile.hit_modifier, 1);
    EXPECT_EQ(batch.profile.unmodified_hit_fails_to, 5);
    const HitRolls hits = events_of<event::HitRollsMade>(events).at(0).rolls;
    EXPECT_EQ(hits.hits(), 2);
    EXPECT_EQ(hits.critical_hits(), 2);
    EXPECT_EQ(Json(batch.profile).get<AttackProfile>(), batch.profile);
    EXPECT_EQ(Json(sels[0]).get<WeaponSelection>(), sels[0]);
}

TEST(R10_07_IndirectShooting, OneToThreeFailWhenTheUnitRemainedStationaryAndAFriendlyUnitSeesTheTarget) {
    GameState gs = indirect_shooting(barricades());
    // A second friendly unit of one model inside the barricade area: it sees out of it (13.10), so the Termagants
    // are visible to a friendly unit.
    const UnitId spotter = add_unit_copy(gs, kMarines);
    gs.find_unit(spotter)->models.resize(1);
    gs.find_unit(spotter)->models[0].position = at(20.0f, 10.2f);
    ASSERT_TRUE(unit_visible_to_side(gs, Side::A, gs.units[kGants]));
    ASSERT_FALSE(unit_visible_to_model(gs, gs.units[kMarines].models[0], gs.units[kGants]));
    const action::Shoot shoot = rifles(gs, ShootingType::Indirect, 2);
    const auto sels = make_weapon_selections(gs, gs.units[kMarines], ShootingType::Indirect, shoot.shots);
    EXPECT_EQ(sels.at(0).hit_fails_to, 3);
    // Hit rolls 3, 4, 5, 6 at BS 4+ with +1: the 3 fails whatever the modifier; the 4 and the 5 hit; the 6 is critical.
    ScriptedDice dice{3, 4, 5, 6, 1, 1, 1};
    const auto events = apply_action(gs, Side::A, shoot, dice);
    const HitRolls hits = events_of<event::HitRollsMade>(events).at(0).rolls;
    EXPECT_EQ(hits.rolls.at(0).outcome, HitOutcome::Fail);
    EXPECT_EQ(hits.rolls.at(1).outcome, HitOutcome::Hit);
    EXPECT_EQ(hits.rolls.at(2).outcome, HitOutcome::Hit);
    EXPECT_EQ(hits.rolls.at(3).outcome, HitOutcome::CriticalHit);
    // The unit did not remain stationary: 1-5 again, even with the spotter.
    GameState moved = indirect_shooting(barricades());
    const UnitId other = add_unit_copy(moved, kMarines);
    moved.find_unit(other)->models.resize(1);
    moved.find_unit(other)->models[0].position = at(20.0f, 10.2f);
    moved.units[kMarines].turn_flags.remained_stationary = false;
    EXPECT_EQ(make_weapon_selections(moved, moved.units[kMarines], ShootingType::Indirect,
                                     rifles(moved, ShootingType::Indirect, 1).shots)
                  .at(0)
                  .hit_fails_to,
              5);
}

TEST(R10_07_IndirectShooting, AppliesToItsAttacksEvenAtAVisibleTargetAndNotToTheOtherWeapons) {
    // No terrain: the Termagants are visible. In indirect shooting the [INDIRECT FIRE] rifles still give cover and
    // fail on 1-3 (the unit itself is a friendly unit that sees the target); a rifle's other profile (Saturation, no
    // [INDIRECT FIRE]) shoots as in normal shooting.
    GameState gs = indirect_shooting({});
    action::Shoot shoot = rifles(gs, ShootingType::Indirect, 1);
    shoot.shots.push_back(ShotDeclaration{gs.units[kMarines].models[1].id, kBoltRifle, 1, gs.units[kGants].id});
    ASSERT_TRUE(validate_action(gs, Side::A, shoot).ok());
    const auto sels = make_weapon_selections(gs, gs.units[kMarines], ShootingType::Indirect, shoot.shots);
    EXPECT_TRUE(sels.at(0).benefit_of_cover);
    EXPECT_EQ(sels.at(0).hit_fails_to, 3);
    EXPECT_FALSE(sels.at(1).benefit_of_cover);
    EXPECT_EQ(sels.at(1).hit_fails_to, 1);
    // In normal shooting the same rifle has neither (it needs to see its target there).
    const auto normal = make_weapon_selections(gs, gs.units[kMarines], ShootingType::Normal,
                                               rifles(gs, ShootingType::Normal, 1).shots);
    EXPECT_FALSE(normal.at(0).benefit_of_cover);
    EXPECT_EQ(normal.at(0).hit_fails_to, 1);
    // What it does here, for clients.
    const WeaponProfile& rifle = gs.units[kMarines].models[0].weapons[kBoltRifle].profiles[kFocused];
    const AttackContext context = shooting_context(gs, gs.units[kMarines], ShootingType::Indirect);
    EXPECT_TRUE(ability_effects(rifle, sels[0], context).back().active);
    EXPECT_FALSE(ability_effects(rifle, normal[0], context).back().active);
}

TEST(R10_07_IndirectShooting, IgnoresCoverAndPsychicWeaponsGiveNoCover) {
    for (const WeaponAbility& a : std::vector<WeaponAbility>{ability::IgnoresCover{}, ability::Psychic{}}) {
        GameState gs = indirect_shooting(barricades(), {a});
        const auto sels = make_weapon_selections(gs, gs.units[kMarines], ShootingType::Indirect,
                                                 rifles(gs, ShootingType::Indirect, 1).shots);
        EXPECT_FALSE(sels.at(0).benefit_of_cover);  // 24.18 / 24.29
        EXPECT_EQ(sels.at(0).hit_fails_to, 5);       // not a modifier: the failing rolls stay
    }
}

TEST(R10_07_IndirectShooting, ItsHitRollsCantBeReRolled) {
    // With 1 CP a hit roll is followed by Command Re-roll's window (15.02); not in indirect shooting.
    const auto after_hit_roll = [](ShootingType type) {
        GameState gs = indirect_shooting({});
        gs.command_points[0] = 1;
        ScriptedDice dice{4, 4};
        apply_action_paced(gs, Side::A, rifles(gs, type, 1), dice);
        EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::RollDice{RollKind::Hit, gs.units[kMarines].id}}));
        apply_action_paced(gs, Side::A, action::RollDice{RollKind::Hit, gs.units[kMarines].id}, dice);
        return gs;
    };
    const GameState normal = after_hit_roll(ShootingType::Normal);
    ASSERT_TRUE(window_pending(normal));
    EXPECT_EQ(normal.pending->side, Side::A);
    const GameState indirect = after_hit_roll(ShootingType::Indirect);
    EXPECT_FALSE(window_pending(indirect));
    EXPECT_EQ(indirect.pending, (PendingDecision{Side::A, decision::RollDice{RollKind::Wound, indirect.units[kMarines].id}}));
}

TEST(R10_07_IndirectShooting, RemainingStationaryIsRecordedOnlyForAUnitThatReadsIt) {
    // D-103: the flag is kept for a unit with an [INDIRECT FIRE] weapon, so every other state serializes as before.
    const GameState with = indirect_shooting({});
    EXPECT_TRUE(with.units[kMarines].turn_flags.remained_stationary);
    EXPECT_FALSE(with.units[kGants].turn_flags.remained_stationary);
    EXPECT_TRUE(Json(with.units[kMarines].turn_flags).contains("remained_stationary"));
    EXPECT_FALSE(Json(with.units[kGants].turn_flags).contains("remained_stationary"));
    EXPECT_EQ(round_trip(with), with);
    const GameState plain = shoot_step_state();
    EXPECT_FALSE(plain.units[kMarines].turn_flags.remained_stationary);
    // It is a turn flag: gone at the start of the next turn.
    GameState next = indirect_shooting({});
    wh40k::test::play_neutral_until(next, [](const GameState& g) { return turn_side(g) == Side::B; });
    EXPECT_FALSE(next.units[kMarines].turn_flags.remained_stationary);
}

// ==== 24.28 [PRECISION] ====

TEST(R24_28_Precision, ParsesPrecision) {
    EXPECT_EQ(parse_weapon_ability("Precision"), WeaponAbility{ability::Precision{}});
    EXPECT_EQ(parse_weapon_ability("[PRECISION]"), WeaponAbility{ability::Precision{}});
    EXPECT_EQ(Json(WeaponAbility{ability::Precision{}}), Json({{"kind", "Precision"}}));
    WeaponProfile p = wh40k::test::load_v1_scenario().units[kGants].models[0].weapons[kFleshborer].profiles[0];
    p.abilities.push_back(ability::Precision{});
    EXPECT_FALSE(unimplemented_ability_problem(p).has_value());
    EXPECT_TRUE(precision(make_attack_profile(p, AttackContext{AttackType::Shooting, false})));
    p.abilities.push_back(ability::Torrent{});  // app 24.37.01
    EXPECT_EQ(unimplemented_ability_problem(p)->rfind("24.37: ", 0), 0u);
}

TEST(R24_28_Precision, TheSelectedGroupIsTheCurrentOneUntilItIsDestroyed) {
    // Three CHARACTER groups of one W 2 model each, declared in the order 0, 1, 2; the group of model 3 is selected.
    Unit unit = wh40k::test::load_v1_scenario().units[kMarines];
    unit.keywords.push_back("Character");
    unit.models.resize(3);
    const std::vector<AllocationGroup> groups = create_allocation_groups(unit);
    ASSERT_EQ(groups.size(), 3u);
    const ModelId picked = unit.models[2].id;
    const std::vector<AllocationGroup> ordered = with_precision_group_first(groups, picked);
    EXPECT_EQ(ordered.at(0).models, std::vector<ModelId>{picked});
    EXPECT_EQ(ordered.at(1).models, groups[0].models);  // then the declared order
    EXPECT_EQ(ordered.at(2).models, groups[1].models);
    EXPECT_EQ(with_precision_group_first(groups, std::nullopt), groups);
    // Three failed saves of D 1: two destroy the selected model, the third goes to the first declared group.
    AttackProfile p;
    p.skill = 3;
    p.strength = 4;
    p.damage = DiceExpr::fixed(1);
    const ModelChooser first = [](const Unit&, std::span<const ModelId> options) { return options.front(); };
    const InflictDamageResult r = inflict_damage(unit, ordered, std::vector<int>{1, 1, 1}, p, first);
    ASSERT_EQ(r.steps.size(), 3u);
    EXPECT_EQ(r.steps[0].damage->model, picked);
    EXPECT_EQ(r.steps[1].damage->model, picked);
    EXPECT_TRUE(r.steps[1].damage->destroyed);
    EXPECT_EQ(r.steps[2].damage->model, groups[0].models[0]);
}

TEST(R24_28_Precision, TheAttackerSelectsAVisibleCharactersGroupBeforeTheOrderIsDeclared) {
    GameState gs = gants_at_characters({ability::Precision{}});
    ScriptedDice dice{4, 4, 4, 4};  // two hits, two wounds
    const auto events = apply_action(gs, Side::B, fleshborers(gs, 2), dice);
    EXPECT_TRUE(events_of<event::AllocationOrderDeclared>(events).empty());  // 24.28: at the start of that step
    ASSERT_TRUE(precision_pending(gs));
    EXPECT_EQ(gs.pending->side, Side::B);  // the attacking unit's player
    EXPECT_EQ(decision_rule(gs, *gs.pending), "24.28");
    EXPECT_EQ(round_trip(gs), gs);
    // One answer per CHARACTER model the attacking models can see, then none.
    const std::vector<GameAction> legal = legal_actions(gs, Side::B);
    ASSERT_EQ(legal.size(), 6u);
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(legal[i], GameAction{action::SelectPrecisionGroup{gs.units[kMarines].models[i].id}});
    }
    EXPECT_EQ(legal[5], GameAction{action::SelectPrecisionGroup{std::nullopt}});
    // A model that is not one of them is refused.
    const auto refused = rejection(gs, Side::B, action::SelectPrecisionGroup{gs.units[kGants].models[0].id});
    EXPECT_EQ(refused.at(0).rfind("24.28: ", 0), 0u) << refused.at(0);

    const ModelId picked = gs.units[kMarines].models[3].id;
    const auto selected = apply_action(gs, Side::B, action::SelectPrecisionGroup{picked});
    EXPECT_EQ(events_of<event::PrecisionGroupSelected>(selected).at(0),
              (event::PrecisionGroupSelected{gs.units[kGants].id, gs.units[kMarines].id, picked}));
    EXPECT_EQ(gs.flow.attack->current->precision, picked);
    EXPECT_EQ(round_trip(gs), gs);
    // The defender still declares the order of every group (05.03), here in unit order.
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::DeclareAllocationOrder>(gs.pending->kind));
    EXPECT_EQ(gs.pending->side, Side::A);
    ScriptedDice saves{1, 1};
    apply_action(gs, Side::A, action::DeclareAllocationOrder{gs.units[kMarines].id, {0, 1, 2, 3, 4}}, saves);
    // Both failed saves go to the selected CHARACTER, whatever the order: the casualty preview offers only it.
    const auto preview = preview_casualties(gs, {});
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->options, std::vector<ModelId>{picked});
    const auto resolved = events_of<event::SavesResolved>(answer_casualties(gs));
    ASSERT_EQ(resolved.size(), 1u);
    for (const SaveStep& s : resolved[0].result.steps) EXPECT_EQ(s.damage->model, picked);
    EXPECT_EQ(find_model(gs.units[kMarines], picked), nullptr);  // W 2: destroyed
    EXPECT_EQ(gs.units[kMarines].models.size(), 4u);
}

TEST(R24_28_Precision, SelectingNoneLeavesTheDeclaredOrder) {
    GameState gs = gants_at_characters({ability::Precision{}});
    ScriptedDice dice{4, 4};
    apply_action(gs, Side::B, fleshborers(gs, 1), dice);
    ASSERT_TRUE(precision_pending(gs));
    const auto selected = apply_action(gs, Side::B, action::SelectPrecisionGroup{std::nullopt});
    EXPECT_EQ(events_of<event::PrecisionGroupSelected>(selected).at(0).character, std::nullopt);
    EXPECT_TRUE(gs.flow.attack->current->precision_decided);
    EXPECT_FALSE(gs.flow.attack->current->precision.has_value());
    ScriptedDice saves{1};
    apply_action(gs, Side::A, action::DeclareAllocationOrder{gs.units[kMarines].id, {4, 3, 2, 1, 0}}, saves);
    const auto preview = preview_casualties(gs, {});
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->options, std::vector<ModelId>{gs.units[kMarines].models[4].id});  // the first declared group
}

TEST(R24_28_Precision, IsNotAskedWithoutTheAbilityOrWithNothingToSelect) {
    // Without [PRECISION]: straight to the allocation order.
    GameState plain = gants_at_characters({});
    ScriptedDice dice{4, 4};
    apply_action(plain, Side::B, fleshborers(plain, 1), dice);
    EXPECT_TRUE(plain.pending && std::holds_alternative<decision::DeclareAllocationOrder>(plain.pending->kind));
    // Against a unit with one allocation group (the Intercessors as they are: no CHARACTER): the save roll follows.
    GameState gs = shoot_step_state(Side::B);
    for (Model& m : gs.units[kGants].models) m.weapons[kFleshborer].profiles[0].abilities.push_back(ability::Precision{});
    ScriptedDice more{4, 4, 1};
    const auto events = apply_action(gs, Side::B, fleshborers(gs, 1), more);
    EXPECT_EQ(more.remaining(), 0u);
    EXPECT_TRUE(events_of<event::PrecisionGroupSelected>(events).empty());
    EXPECT_TRUE(gs.pending && std::holds_alternative<decision::OrderCasualties>(gs.pending->kind));
}

TEST(R24_28_Precision, OnlyCharactersTheAttackingModelsCanSeeAreOffered) {
    // One Termagant at (12, 17) fires at the CHARACTER Intercessors in a row along y = 6. A dense wall 2" wide just
    // north of the first Intercessor hides that model from it (13.11); the others are visible.
    GameState gs = gants_at_characters({ability::Precision{}});
    row(gs.units[kMarines], 10.0f, 6.0f, 1.5f);
    row(gs.units[kGants], 12.0f, 17.0f, 1.3f);
    gs.terrain = {area_with("Wall", rect_in(8.9f, 11.1f, 7.1f, 7.3f), TerrainCategory::Dense, rect_in(9.0f, 11.0f, 7.15f, 7.25f))};
    const Model& shooter = gs.units[kGants].models[0];
    const Unit& marines = gs.units[kMarines];
    ASSERT_FALSE(model_visible_to_model(gs, shooter, marines, marines.models[0]));
    ASSERT_TRUE(model_visible_to_model(gs, shooter, marines, marines.models[2]));
    ScriptedDice dice{4, 4};
    apply_action(gs, Side::B, fleshborers(gs, 1), dice);
    ASSERT_TRUE(precision_pending(gs));
    const std::vector<GameAction> legal = legal_actions(gs, Side::B);
    for (const GameAction& a : legal) {
        EXPECT_NE(a, GameAction{action::SelectPrecisionGroup{marines.models[0].id}});
    }
    EXPECT_NE(std::find(legal.begin(), legal.end(), GameAction{action::SelectPrecisionGroup{marines.models[2].id}}),
              legal.end());
    const auto refused = rejection(gs, Side::B, action::SelectPrecisionGroup{marines.models[0].id});
    EXPECT_EQ(refused.at(0).rfind("24.28: ", 0), 0u) << refused.at(0);
}

TEST(R24_28_Precision, DevastatingWoundsGoToTheSelectedCharacterFirst) {
    // App 24.28.01. D 1: a critical wound and a normal wound; the group of the fourth Intercessor is selected and its
    // save succeeds. 06.02 would offer every CHARACTER for the mortal wound; with [PRECISION] it goes to that model.
    GameState gs = gants_at_characters({ability::Precision{}, ability::DevastatingWounds{}});
    ScriptedDice dice{4, 4, 6, 4};
    apply_action(gs, Side::B, fleshborers(gs, 2), dice);
    ASSERT_TRUE(precision_pending(gs));
    const ModelId picked = gs.units[kMarines].models[3].id;
    apply_action(gs, Side::B, action::SelectPrecisionGroup{picked});
    ScriptedDice saves{6};  // saved
    apply_action(gs, Side::A, action::DeclareAllocationOrder{gs.units[kMarines].id, {0, 1, 2, 3, 4}}, saves);
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::OrderCasualties>(gs.pending->kind));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "06.02");
    const auto preview = preview_casualties(gs, {});
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->options, std::vector<ModelId>{picked});
    const auto inflicted = events_of<event::DevastatingWoundsInflicted>(answer_casualties(gs));
    ASSERT_EQ(inflicted.size(), 1u);
    EXPECT_EQ(inflicted[0].result.steps.at(0).damage.model, picked);
    // Without a selection (or once that model is destroyed) 06.02's own order applies.
    Unit unit = gs.units[kMarines];
    const ModelChooser first = [](const Unit&, std::span<const ModelId> options) { return options.front(); };
    const ModelId gone{9999};
    const DevastatingWoundsResult other = inflict_devastating_wounds(unit, 1, DiceExpr::fixed(1), first, false, {}, gone);
    EXPECT_NE(other.steps.at(0).damage.model, gone);
}

TEST(D103_Serialization, TheNewFieldsRoundTripAndOldFilesReadAsBefore) {
    BatchInProgress b{1, 2, {}, std::nullopt, std::nullopt};
    const Json plain(b);
    EXPECT_FALSE(plain.contains("precision_decided"));
    EXPECT_FALSE(plain.contains("precision"));
    b.precision_decided = true;
    b.precision = ModelId{7};
    EXPECT_EQ(Json(b).get<BatchInProgress>(), b);
    EXPECT_FALSE(Json(WeaponSelection{}).contains("hit_fails_to"));
    AttackProfile p;
    p.skill = 3;
    EXPECT_FALSE(Json(p).contains("unmodified_hit_fails_to"));
    EXPECT_FALSE(Json(UnitTurnFlags{}).contains("remained_stationary"));
    // The actions and the event.
    for (const GameAction& a : std::vector<GameAction>{action::SelectPrecisionGroup{ModelId{4}},
                                                       action::SelectPrecisionGroup{std::nullopt},
                                                       action::Shoot{UnitId{1}, ShootingType::Indirect, {}}}) {
        EXPECT_EQ(Json(a).get<GameAction>(), a);
    }
    for (const GameEvent& e : std::vector<GameEvent>{event::PrecisionGroupSelected{UnitId{1}, UnitId{2}, ModelId{4}},
                                                     event::PrecisionGroupSelected{UnitId{1}, UnitId{2}, std::nullopt}}) {
        EXPECT_EQ(Json(e).get<GameEvent>(), e);
    }
    const PendingDecision d{Side::B, decision::SelectPrecisionGroup{UnitId{2}, UnitId{1}}};
    EXPECT_EQ(Json(d).get<PendingDecision>(), d);
    for (const char* text : {"Indirect Fire", "Precision"}) {
        const Json old{{"kind", "Unknown"}, {"text", text}};
        EXPECT_FALSE(std::holds_alternative<ability::Unknown>(old.get<WeaponAbility>())) << text;
    }
}

// ==== The M101 replay: tests/replays/m101_indirect_precision.json (format 5) ====

namespace {

const std::filesystem::path kM101Replay = std::filesystem::path(WH40K_REPLAY_DIR) / "m101_indirect_precision.json";
constexpr std::uint64_t kM101Seed = 1;  // picked by D103_Replay.DISABLED_SearchM101Seed

// The V1 scenario with stand-in abilities, as no roster weapon has them: every Bolt Rifle (Focused Fire) has [INDIRECT
// FIRE], every Fleshborer has [PRECISION], and the Intercessors are a CHARACTER unit (each model its own allocation
// group, 05.03). M88's barricade area (obscuring, 13.10) lies at y 10-13 between the Intercessors (a row along y = 6")
// and the Termagants (a row along y = 14.5", just north of it).
GameState m101_replay_state() {
    GameState gs = wh40k::test::load_v1_scenario();
    gs.units[kMarines].keywords.push_back("Character");
    for (Model& m : gs.units[kMarines].models) {
        m.weapons[kBoltRifle].profiles[kFocused].abilities.push_back(ability::IndirectFire{});
    }
    for (Model& m : gs.units[kGants].models) m.weapons[kFleshborer].profiles[0].abilities.push_back(ability::Precision{});
    row(gs.units[kMarines], 10.0f, 6.0f, 1.5f);
    row(gs.units[kGants], 4.0f, 14.5f, 1.3f);
    gs.terrain = barricades();
    return gs;
}

struct M101Shown {
    int indirect_shoots = 0;        // 10.07: Shoots of the Indirect type
    int indirect_hits = 0;          // their hits (only unmodified 6s: the unit sees nothing and no friend does)
    bool only_sixes_hit = true;
    std::optional<ModelId> precision;  // 24.28: the CHARACTER whose group was selected
    int damage_on_precision = 0;       // failed saves resolved on that model
};

M101Shown m101_shown(const std::vector<GameEvent>& events) {
    M101Shown s;
    bool indirect = false;
    for (const GameEvent& e : events) {
        if (const auto* shoot = std::get_if<event::ShootingStarted>(&e)) {
            indirect = shoot->type == ShootingType::Indirect;
            s.indirect_shoots += indirect ? 1 : 0;
        } else if (const auto* h = std::get_if<event::HitRollsMade>(&e)) {
            if (!indirect) continue;
            for (const HitRoll& r : h->rolls.rolls) {
                if (r.outcome == HitOutcome::Fail) continue;
                ++s.indirect_hits;
                s.only_sixes_hit = s.only_sixes_hit && r.unmodified == 6;
            }
        } else if (const auto* p = std::get_if<event::PrecisionGroupSelected>(&e)) {
            if (p->character) s.precision = p->character;
        } else if (const auto* saves = std::get_if<event::SavesResolved>(&e)) {
            for (const SaveStep& step : saves->result.steps) {
                if (step.damage && s.precision && step.damage->model == *s.precision) ++s.damage_on_precision;
            }
        }
    }
    return s;
}

// The milestone's replay, played through a paced match. Round 1, Side A: the Intercessors remain stationary and fire
// their Bolt Rifles at the Termagants they can't see, using indirect shooting. Side B: the Termagants make a normal move
// 2" south into the barricade area (from inside it they see out, 13.10) and fire their Fleshborers; Side B selects the
// second Intercessor's group for the [PRECISION] attacks. Every roll is made, every window declined, the allocation
// order and the casualties in unit order. Stops at Side B's Charge step. `ok`: the seed shows both rules.
Replay m101_replay(std::uint64_t seed, bool& ok, std::string& problem) {
    ok = false;
    Replay r;
    r.format = kReplayFormatOwnTurnStratagems;
    r.description =
        "M101 (D-103): Indirect Shooting (10.07) with [INDIRECT FIRE] (24.19), and [PRECISION] (24.28), with stand-in "
        "abilities on the V1 weapons (no roster weapon has them). A barricade area (obscuring, 13.10) lies between the "
        "Intercessors (a CHARACTER unit here: each model its own allocation group) and the Termagants. Round 1, Side A: "
        "the Intercessors remain stationary and fire their [INDIRECT FIRE] Bolt Rifles at the Termagants, which they "
        "can't see, using indirect shooting: the target has cover, and with no friendly unit seeing it an unmodified hit "
        "roll of 1-5 fails. Side B: the Termagants move 2\" into the barricade area, from where they see out, and fire "
        "their [PRECISION] Fleshborers; at the start of the Allocation Order step Side B selects the second "
        "Intercessor's group, which takes the attacks first whatever order Side A declares. Every roll a step of its "
        "own, every window declined, casualties in unit order. Stops at Side B's Charge step.";
    GameState init = m101_replay_state();
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
    const ModelId second = init.units[kMarines].models[1].id;
    auto answer = [&](const GameState& gs) -> GameAction {
        if (window_pending(gs)) return action::DeclineStratagems{};
        if (roll_pending(gs)) {
            const auto& d = std::get<decision::RollDice>(gs.pending->kind);
            return action::RollDice{d.roll, d.unit};
        }
        if (std::holds_alternative<decision::SelectPrecisionGroup>(gs.pending->kind)) {
            // The second Intercessor's group, if it can still be selected; else none.
            const GameAction pick = action::SelectPrecisionGroup{second};
            if (validate_action(gs, gs.pending->side, pick).ok()) return pick;
            return action::SelectPrecisionGroup{std::nullopt};
        }
        return wh40k::test::neutral_action(gs);
    };
    auto play_until = [&](auto done) {
        for (int i = 0; i < 600 && !failed && match.state().pending && !done(match.state()); ++i) act(answer(match.state()));
    };
    const auto pending_is = [](PendingDecision d) {
        return [d](const GameState& gs) { return gs.pending == d; };
    };
    // Side A: indirect shooting.
    play_until(pending_is(PendingDecision{Side::A, decision::Shoot{}}));
    if (failed || match.state().pending != PendingDecision{Side::A, decision::Shoot{}}) return r;
    act(rifles(match.state(), ShootingType::Indirect, match.state().units[kMarines].models.size()));
    // Side B: the Termagants move into the barricade area and shoot.
    play_until(pending_is(PendingDecision{Side::B, decision::MoveUnits{}}));
    if (failed || match.state().pending != PendingDecision{Side::B, decision::MoveUnits{}}) return r;
    if (match.state().units[kGants].models.empty()) return r;
    act(action::StartMove{match.state().units[kGants].id, MoveType::Normal, std::nullopt});
    {
        std::vector<Model> models = match.state().units[kGants].models;
        for (const Model& m : models) {
            act(action::ProposeModelMove{m.id, {Position{m.position.x, m.position.y - in(2.0f), 0.0f}}});
        }
    }
    act(action::CommitUnitMove{});
    play_until(pending_is(PendingDecision{Side::B, decision::Shoot{}}));
    if (failed || match.state().pending != PendingDecision{Side::B, decision::Shoot{}}) return r;
    {
        const GameState& gs = match.state();
        action::Shoot s{gs.units[kGants].id, ShootingType::Normal, {}};
        for (const Model& m : gs.units[kGants].models) {
            s.shots.push_back(ShotDeclaration{m.id, kFleshborer, 0, gs.units[kMarines].id});
        }
        act(s);
    }
    play_until(pending_is(PendingDecision{Side::B, decision::Charge{}}));
    r.steps = match.steps();
    r.expect = replay_expectation(match.state());
    if (failed || match.state().pending != PendingDecision{Side::B, decision::Charge{}}) return r;
    const M101Shown shown = m101_shown(match.events());
    ok = shown.indirect_shoots == 1 && shown.indirect_hits > 0 && shown.only_sixes_hit && shown.precision == second &&
         shown.damage_on_precision > 0 && !match.state().units[kMarines].models.empty();
    return r;
}

}  // namespace

TEST(D103_Replay, TheReplayShowsIndirectShootingAndPrecision) {
    const Replay saved = load_replay(kM101Replay);
    EXPECT_EQ(saved.format, kReplayFormatOwnTurnStratagems);
    const ReplayResult run = run_replay(saved, repo_root());
    ASSERT_TRUE(run.ok()) << "step " << *run.failed_step << ": " << run.reasons.front();
    EXPECT_EQ(replay_expectation(run.final_state), saved.expect);
    const M101Shown shown = m101_shown(run.events);
    EXPECT_EQ(shown.indirect_shoots, 1);
    EXPECT_GT(shown.indirect_hits, 0);
    EXPECT_TRUE(shown.only_sixes_hit);
    ASSERT_TRUE(shown.precision.has_value());
    EXPECT_GT(shown.damage_on_precision, 0);
    // The steps hold the Indirect Shoot and the Precision selection.
    const auto step_count = [&saved](auto pred) { return std::count_if(saved.steps.begin(), saved.steps.end(), pred); };
    EXPECT_EQ(step_count([](const ReplayStep& s) {
                  const auto* shoot = std::get_if<action::Shoot>(&s.action);
                  return shoot != nullptr && shoot->type == ShootingType::Indirect;
              }),
              1);
    EXPECT_GE(step_count([](const ReplayStep& s) { return std::holds_alternative<action::SelectPrecisionGroup>(s.action); }), 1);
    // The file is what the builder plays now.
    bool ok = false;
    std::string problem;
    const Replay again = m101_replay(kM101Seed, ok, problem);
    EXPECT_TRUE(problem.empty()) << problem;
    EXPECT_TRUE(ok);
    EXPECT_EQ(Json(again).dump(2), Json(saved).dump(2));
}

TEST(D103_Replay, DISABLED_SearchM101Seed) {
    for (std::uint64_t seed = 1; seed <= 1000; ++seed) {
        bool ok = false;
        std::string problem;
        (void)m101_replay(seed, ok, problem);
        if (!problem.empty()) std::printf("seed %llu: %s\n", static_cast<unsigned long long>(seed), problem.c_str());
        if (ok) {
            std::printf("M101 seed %llu\n", static_cast<unsigned long long>(seed));
            return;
        }
    }
    ADD_FAILURE() << "no seed in 1..1000";
}

// Writes tests/replays/m101_indirect_precision.json. Run on purpose only:
//   wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D103_Replay.DISABLED_RegenerateM101Replay
TEST(D103_Replay, DISABLED_RegenerateM101Replay) {
    bool ok = false;
    std::string problem;
    const Replay r = m101_replay(kM101Seed, ok, problem);
    ASSERT_TRUE(problem.empty()) << problem;
    ASSERT_TRUE(ok);
    const ReplayResult run = run_replay(r, repo_root());
    ASSERT_TRUE(run.ok()) << "step " << *run.failed_step << ": " << run.reasons.front();
    save_replay(r, kM101Replay);
}
