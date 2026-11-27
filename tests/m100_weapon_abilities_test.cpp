// M100 (D-102): [LETHAL HITS] (24.23), [PSYCHIC] (24.29), [CLEAVE X] (24.06), [ONE SHOT] (24.26) and the
// keyword-conditioned forms of 24.01. The engine tests use the V1 scenario with stand-in abilities on the Fleshborer,
// the Bolt Rifle and the Knives and Fists, the crater field of M90's cover tests and the monsters fixture (D-073).

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
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/terrain_shape.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::after_successful_charge;
using wh40k::test::answer_casualties;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::first_melee_options;
using wh40k::test::in;
using wh40k::test::model_ids;
using wh40k::test::rejection;
using wh40k::test::repo_root;
using wh40k::test::shoot_step_state;
using Json = nlohmann::json;
namespace mf = wh40k::test::monsters;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;
constexpr std::size_t kBoltRifle = 0;
constexpr std::size_t kKnives = 2;
constexpr std::size_t kFocused = 0;
constexpr std::size_t kFleshborer = 0;

AttackProfile profile(int strength, std::vector<WeaponAbility> abilities = {}) {
    AttackProfile p;
    p.skill = 3;
    p.strength = strength;
    p.damage = DiceExpr::fixed(1);
    p.attack_time_abilities = std::move(abilities);
    return p;
}

// Side B's Shoot step on the V1 scenario, with every Fleshborer (A 1, BS 4+, S 5, AP 0, D 1; the Intercessors are T 5,
// so it wounds on 4+) given these abilities.
GameState gants_shoot(std::vector<WeaponAbility> abilities) {
    GameState gs = shoot_step_state(Side::B);
    for (Model& m : gs.units[kGants].models) {
        WeaponProfile& p = m.weapons[kFleshborer].profiles[0];
        p.abilities.insert(p.abilities.end(), abilities.begin(), abilities.end());
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

Unit unit_with_keywords(std::vector<std::string> keywords) {
    Unit u;
    u.keywords = std::move(keywords);
    return u;
}

WeaponProfile roster_profile(const char* unit, const char* weapon, std::size_t p = 0) {
    return load_datasheet(repo_root() / (std::string("data/units/") + unit + ".json")).find_weapon(weapon)->profiles.at(p);
}

GameState round_trip(const GameState& gs) { return Json(gs).get<GameState>(); }

Position at(float x_in, float y_in) { return Position{in(x_in), in(y_in), 0.0f}; }

}  // namespace

// ==== 24.23 [LETHAL HITS] ====

TEST(R24_23_LethalHits, ParsesLethalHits) {
    EXPECT_EQ(parse_weapon_ability("Lethal Hits"), WeaponAbility{ability::LethalHits{}});
    EXPECT_EQ(parse_weapon_ability("[LETHAL HITS]"), WeaponAbility{ability::LethalHits{}});
    // A file saved before M100 holds it as unknown text and reads it as the ability now (D-071).
    EXPECT_EQ(Json({{"kind", "Unknown"}, {"text", "Lethal Hits"}}).get<WeaponAbility>(),
              WeaponAbility{ability::LethalHits{}});
    EXPECT_EQ(Json(WeaponAbility{ability::LethalHits{}}), Json({{"kind", "LethalHits"}}));
}

TEST(R24_23_LethalHits, ACriticalHitWoundsWithoutAWoundRoll) {
    const AttackProfile lethal = profile(4, {ability::LethalHits{}});
    EXPECT_TRUE(lethal_hits(lethal));
    EXPECT_FALSE(lethal_hits(profile(4)));
    // Hit rolls 6, 6, 4, 2: two critical hits, one hit, one miss (BS 3+).
    const std::vector<int> faces{6, 6, 4, 2};
    const HitRolls hits = judge_hit_rolls(lethal, 4, faces);
    EXPECT_EQ(hits.hits(), 3);
    EXPECT_EQ(automatic_wounds(lethal, hits), 2);
    EXPECT_EQ(automatic_wounds(profile(4), hits), 0);
    // One wound roll, for the hit that was not critical; it fails, and the two automatic wounds stand.
    ScriptedDice dice{2};
    const WoundRolls wounds = make_wound_rolls(dice, lethal, hits.hits(), 4, automatic_wounds(lethal, hits));
    EXPECT_EQ(dice.remaining(), 0u);
    EXPECT_EQ(wounds.rolls.size(), 1u);
    EXPECT_EQ(wounds.automatic, 2);
    EXPECT_EQ(wounds.wounds(), 2);
    EXPECT_EQ(wounds.critical_wounds(), 0);  // app 01.05.01: an automatic wound is not a critical wound
    EXPECT_EQ(Json(wounds).get<WoundRolls>(), wounds);
    EXPECT_FALSE(Json(judge_wound_rolls(lethal, 4, std::vector<int>{4})).contains("automatic"));  // omitted at 0
    // 15.09 snap shooting's 6s hit but are not critical hits (D-097), so none is lethal; [TORRENT]'s hits neither.
    AttackProfile snap = lethal;
    snap.snap_shooting = true;
    EXPECT_EQ(automatic_wounds(snap, judge_hit_rolls(snap, 2, std::vector<int>{6, 6})), 0);
    AttackProfile torrent = lethal;
    torrent.torrent = true;
    torrent.skill.reset();
    EXPECT_EQ(automatic_wounds(torrent, judge_hit_rolls(torrent, 3, {})), 0);
}

TEST(R24_23_LethalHits, TheEngineRollsToWoundOnlyForTheOtherHits) {
    GameState gs = gants_shoot({ability::LethalHits{}});
    // Three shots: hit rolls 6 (critical), 4 (a hit) and 2 (a miss); one wound roll, a 3 (fails against T 5); one save
    // roll for the automatic wound, a 1.
    ScriptedDice dice{6, 4, 2, 3, 1};
    const auto events = apply_action(gs, Side::B, fleshborers(gs, 3), dice);
    EXPECT_EQ(dice.remaining(), 0u);
    const WoundRolls wounds = events_of<event::WoundRollsMade>(events).at(0).rolls;
    EXPECT_EQ(wounds.rolls.size(), 1u);
    EXPECT_EQ(wounds.automatic, 1);
    EXPECT_EQ(wounds.wounds(), 1);
    EXPECT_EQ(events_of<event::SaveRollsMade>(events).at(0).rolls, std::vector<int>{1});
    EXPECT_EQ(gs.flow.attack->automatic_wounds, 0);  // used up once the wound rolls stand
    const auto resolved = events_of<event::SavesResolved>(answer_casualties(gs));
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_EQ(resolved[0].result.steps.at(0).damage->wounds_lost, 1);
}

TEST(R24_23_LethalHits, WithEveryHitCriticalNoWoundRollIsMade) {
    GameState gs = gants_shoot({ability::LethalHits{}});
    ScriptedDice dice{6, 6, 2, 1};  // two critical hits, then two save rolls: no wound dice at all
    const auto events = apply_action(gs, Side::B, fleshborers(gs, 2), dice);
    EXPECT_EQ(dice.remaining(), 0u);
    const auto made = events_of<event::WoundRollsMade>(events);
    ASSERT_EQ(made.size(), 1u);
    EXPECT_TRUE(made[0].rolls.rolls.empty());
    EXPECT_EQ(made[0].rolls.automatic, 2);
    EXPECT_EQ(events_of<event::SaveRollsMade>(events).at(0).rolls, (std::vector<int>{2, 1}));
}

TEST(R24_23_LethalHits, SustainedHitsExtraHitsStillRollToWound) {
    // A critical hit of a [LETHAL HITS] [SUSTAINED HITS 1] weapon: its own hit wounds automatically, and the additional
    // hit is a hit like any other (24.36), so it rolls.
    GameState gs = gants_shoot({ability::LethalHits{}, ability::SustainedHits{1}});
    ScriptedDice dice{6, 5, 1, 1};  // the critical hit; the extra hit's wound roll (5: wounds); two saves
    const auto events = apply_action(gs, Side::B, fleshborers(gs, 1), dice);
    EXPECT_EQ(dice.remaining(), 0u);
    const WoundRolls wounds = events_of<event::WoundRollsMade>(events).at(0).rolls;
    EXPECT_EQ(wounds.rolls.size(), 1u);
    EXPECT_EQ(wounds.automatic, 1);
    EXPECT_EQ(wounds.wounds(), 2);
}

TEST(R24_23_LethalHits, PacedRollsShowTheWoundDiceAndTheStateRoundTrips) {
    GameState gs = gants_shoot({ability::LethalHits{}});
    ScriptedDice dice{6, 4, 2};
    apply_action_paced(gs, Side::B, fleshborers(gs, 3), dice);
    // Roll the hit rolls and decline their window; then the Wound roll waits with one die, the automatic wound noted.
    for (int guard = 0; guard < 10 && gs.pending; ++guard) {
        if (const auto* r = std::get_if<decision::RollDice>(&gs.pending->kind)) {
            if (r->roll == RollKind::Wound) break;
            apply_action_paced(gs, gs.pending->side, action::RollDice{r->roll, r->unit}, dice);
        } else {
            apply_action_paced(gs, gs.pending->side, action::DeclineStratagems{}, dice);
        }
    }
    const DecisionKind wound_roll{decision::RollDice{RollKind::Wound, gs.units[kGants].id}};
    ASSERT_TRUE(gs.pending && gs.pending->kind == wound_roll);
    EXPECT_EQ(gs.flow.attack->automatic_wounds, 1);
    EXPECT_EQ(roll_dice_info(gs)->dice, 1);
    EXPECT_TRUE(Json(gs).at("flow").at("attack").contains("automatic_wounds"));
    EXPECT_EQ(round_trip(gs), gs);
    // An attack without the ability writes no such key (older saves load and save byte for byte).
    GameState plain = gants_shoot({});
    ScriptedDice same{6, 4, 2};
    apply_action_paced(plain, Side::B, fleshborers(plain, 3), same);
    apply_action_paced(plain, Side::B, action::RollDice{RollKind::Hit, plain.units[kGants].id}, same);
    EXPECT_FALSE(Json(plain).at("flow").at("attack").contains("automatic_wounds"));
}

TEST(R24_23_LethalHits, TwinLinkedReRollsKeepTheAutomaticWounds) {
    // [LETHAL HITS] with [TWIN-LINKED]: a critical hit and a hit; the hit's wound roll fails and is re-rolled (24.38).
    // The automatic wound is still there after the re-roll.
    GameState gs = gants_shoot({ability::LethalHits{}, ability::TwinLinked{}});
    ScriptedDice dice{6, 4, 2};
    apply_action(gs, Side::B, fleshborers(gs, 2), dice);
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::RerollWoundRolls>(gs.pending->kind));
    EXPECT_EQ(gs.flow.attack->wound_rerolls->rolls.automatic, 1);
    EXPECT_EQ(round_trip(gs), gs);
    ScriptedDice again{5, 1, 1};  // the re-roll wounds; two saves
    const auto events = apply_action(gs, Side::B, action::RerollWoundRolls{{0}}, again);
    EXPECT_EQ(again.remaining(), 0u);
    const auto rerolled = events_of<event::WoundRollsRerolled>(events);
    ASSERT_EQ(rerolled.size(), 1u);
    EXPECT_EQ(rerolled[0].rolls.automatic, 1);
    EXPECT_EQ(rerolled[0].rolls.wounds(), 2);
    EXPECT_EQ(events_of<event::SaveRollsMade>(events).at(0).rolls.size(), 2u);
}

TEST(R24_23_LethalHits, TheBatchResolvedAsOneFunctionAgreesWithTheEngine) {
    GameState gs = gants_shoot({ability::LethalHits{}});
    ScriptedDice dice{6, 4, 2, 4, 1, 1};  // hits 6, 4, 2; one wound roll (4: wounds); two saves fail
    apply_action(gs, Side::B, fleshborers(gs, 3), dice);
    answer_casualties(gs);
    EXPECT_EQ(dice.remaining(), 0u);

    GameState fresh = gants_shoot({ability::LethalHits{}});
    std::vector<WeaponSelection> selections;
    for (std::size_t i = 0; i < 3; ++i) {
        selections.push_back(select_weapon_target(fresh, fresh.units[kGants].models[i].id, kFleshborer, 0, fresh.units[kMarines].id));
    }
    const auto batches =
        gather_attack_batches(fresh, selections, shooting_context(fresh, fresh.units[kGants], ShootingType::Normal));
    ASSERT_EQ(batches.size(), 1u);
    Unit target = fresh.units[kMarines];
    ScriptedDice same{6, 4, 2, 4, 1, 1};
    const ModelChooser first = [](const Unit&, std::span<const ModelId> options) { return options.front(); };
    const BatchResult r = resolve_attack_batch(same, target, batches[0], {}, first);
    EXPECT_EQ(same.remaining(), 0u);
    EXPECT_EQ(r.wounds.automatic, 1);
    EXPECT_EQ(r.save_rolls.size(), 2u);
    EXPECT_EQ(target.models, gs.units[kMarines].models);
}

TEST(R24_23_LethalHits, AProfileWithDevastatingWoundsTooIsRefused) {
    // D-102: the choice between the automatic wound and a wound roll matters only with [DEVASTATING WOUNDS]; the engine
    // doesn't ask it yet, so such a profile is refused rather than resolved without the choice.
    WeaponProfile both = roster_profile("termagants", "Fleshborer");
    both.abilities = {ability::LethalHits{}, ability::DevastatingWounds{}};
    const auto problem = unimplemented_ability_problem(both);
    ASSERT_TRUE(problem.has_value());
    EXPECT_EQ(problem->rfind("24.23: ", 0), 0u) << *problem;
    both.abilities = {ability::LethalHits{}, ability::Anti{{"infantry"}, 4, false}};
    EXPECT_FALSE(unimplemented_ability_problem(both).has_value());  // a critical wound alone does nothing more
    GameState gs = gants_shoot({ability::LethalHits{}, ability::DevastatingWounds{}});
    EXPECT_TRUE(shot_options(gs, gs.units[kGants], ShootingType::Normal).empty());
    EXPECT_EQ(rejection(gs, Side::B, fleshborers(gs, 1)).at(0).rfind("24.23: ", 0), 0u);
}

// ==== 24.01: abilities followed by keywords ====

TEST(R24_01_ConditionedAbilities, ParseWithTheirKeywords) {
    EXPECT_EQ(parse_weapon_ability("Lethal Hits: VEHICLE"),
              (WeaponAbility{ability::LethalHits{ability::TargetCondition{{"vehicle"}, false}}}));
    EXPECT_EQ(parse_weapon_ability("[SUSTAINED HITS 1: INFANTRY/BEASTS]"),
              (WeaponAbility{ability::SustainedHits{1, ability::TargetCondition{{"infantry", "beasts"}, false}}}));
    EXPECT_EQ(parse_weapon_ability("Devastating Wounds: non-Monster/Vehicle"),
              (WeaponAbility{ability::DevastatingWounds{ability::TargetCondition{{"monster", "vehicle"}, true}}}));
    EXPECT_EQ(parse_weapon_ability("Sustained Hits 2: non-Monster/Vehicles"),
              (WeaponAbility{ability::SustainedHits{2, ability::TargetCondition{{"monster", "vehicles"}, true}}}));
    // A condition on an ability that acts before the hit roll, or a malformed one, is not known (D-102).
    for (const char* s : {"Rapid Fire 1: Infantry", "Blast: Vehicle", "Lethal Hits:", "Lethal Hits: /", "Twin-linked: Vehicle",
                          "Sustained Hits: Infantry"}) {
        EXPECT_TRUE(std::holds_alternative<ability::Unknown>(parse_weapon_ability(s))) << s;
    }
    // JSON: the condition is written only when there is one.
    const WeaponAbility conditioned = ability::LethalHits{ability::TargetCondition{{"monster", "vehicle"}, true}};
    EXPECT_EQ(Json(conditioned), Json({{"kind", "LethalHits"}, {"against", {"monster", "vehicle"}}, {"non", true}}));
    EXPECT_EQ(Json(conditioned).get<WeaponAbility>(), conditioned);
    const WeaponAbility sustained = ability::SustainedHits{1, ability::TargetCondition{{"infantry"}, false}};
    EXPECT_EQ(Json(sustained).get<WeaponAbility>(), sustained);
    EXPECT_EQ(Json(WeaponAbility{ability::DevastatingWounds{}}), Json({{"kind", "DevastatingWounds"}}));
    EXPECT_EQ(Json(WeaponAbility{ability::SustainedHits{1}}), Json({{"kind", "SustainedHits"}, {"x", 1}}));
}

TEST(R24_01_ConditionedAbilities, ApplyOnlyAgainstAUnitWithTheKeyword) {
    const ability::TargetCondition vehicle{{"vehicle"}, false};
    const ability::TargetCondition non_monster{{"monster", "vehicle"}, true};
    const Unit infantry = unit_with_keywords({"Infantry"});
    const Unit tank = unit_with_keywords({"Vehicle"});
    EXPECT_TRUE(condition_met(ability::TargetCondition{}, infantry));  // no keywords: always
    EXPECT_FALSE(condition_met(vehicle, infantry));
    EXPECT_TRUE(condition_met(vehicle, tank));
    EXPECT_TRUE(condition_met(non_monster, infantry));
    EXPECT_FALSE(condition_met(non_monster, tank));

    WeaponProfile weapon = roster_profile("termagants", "Fleshborer");
    weapon.abilities = {ability::LethalHits{vehicle}, ability::SustainedHits{1, non_monster}};
    const AttackContext shooting{AttackType::Shooting, false};
    const WeaponSelection sel;
    // Against INFANTRY: [SUSTAINED HITS 1] applies, [LETHAL HITS] doesn't. The kept ability has no condition left.
    const AttackProfile at_infantry = selection_attack_profile(weapon, sel, shooting, infantry);
    EXPECT_EQ(at_infantry.attack_time_abilities, (std::vector<WeaponAbility>{ability::SustainedHits{1}}));
    EXPECT_FALSE(lethal_hits(at_infantry));
    EXPECT_EQ(sustained_hits(at_infantry), 1);
    // Against a VEHICLE: the other way round.
    const AttackProfile at_tank = selection_attack_profile(weapon, sel, shooting, tank);
    EXPECT_EQ(at_tank.attack_time_abilities, (std::vector<WeaponAbility>{ability::LethalHits{}}));
    EXPECT_EQ(sustained_hits(at_tank), 0);
    // D-006: against a target that meets the condition the attacks are identical to an unconditioned weapon's.
    WeaponProfile plain = weapon;
    plain.abilities = {ability::LethalHits{}};
    EXPECT_EQ(selection_attack_profile(plain, sel, shooting, tank), at_tank);
    // What it does here, for clients (D-037).
    const auto effects = ability_effects(weapon, sel, shooting, &infantry);
    EXPECT_TRUE(effects.at(0).condition_unmet);
    EXPECT_FALSE(effects.at(0).active);
    EXPECT_FALSE(effects.at(1).condition_unmet);
    EXPECT_TRUE(effects.at(1).active);
}

TEST(R24_01_ConditionedAbilities, TheEngineGathersThemAgainstTheTargetsKeywords) {
    // [LETHAL HITS: INFANTRY] at the Intercessors (INFANTRY): the critical hit wounds automatically. The same shot with
    // [LETHAL HITS: VEHICLE] rolls to wound.
    GameState gs = gants_shoot({ability::LethalHits{ability::TargetCondition{{"infantry"}, false}}});
    ScriptedDice dice{6, 1};
    const auto events = apply_action(gs, Side::B, fleshborers(gs, 1), dice);
    EXPECT_EQ(events_of<event::WoundRollsMade>(events).at(0).rolls.automatic, 1);
    EXPECT_EQ(events_of<event::AttackBatchesGathered>(events).at(0).batches.at(0).profile.attack_time_abilities,
              (std::vector<WeaponAbility>{ability::LethalHits{}}));

    GameState other = gants_shoot({ability::LethalHits{ability::TargetCondition{{"vehicle"}, false}}});
    ScriptedDice same{6, 3};  // the critical hit, then its wound roll (3: fails)
    const auto none = apply_action(other, Side::B, fleshborers(other, 1), same);
    EXPECT_EQ(same.remaining(), 0u);
    EXPECT_EQ(events_of<event::WoundRollsMade>(none).at(0).rolls.automatic, 0);
    EXPECT_TRUE(events_of<event::AttackBatchesGathered>(none).at(0).batches.at(0).profile.attack_time_abilities.empty());
}

// ==== 24.29 [PSYCHIC] ====

TEST(R24_29_Psychic, ParsesPsychic) {
    EXPECT_EQ(parse_weapon_ability("Psychic"), WeaponAbility{ability::Psychic{}});
    EXPECT_EQ(parse_weapon_ability("[PSYCHIC]"), WeaponAbility{ability::Psychic{}});
    const WeaponProfile tome = roster_profile("legionaries", "Balefire tome");
    EXPECT_EQ(tome.abilities, (std::vector<WeaponAbility>{ability::Psychic{}}));
    EXPECT_FALSE(unimplemented_ability_problem(tome).has_value());
    EXPECT_TRUE(ignores_hit_penalties(tome));
    // A psychic attack keeps the ability in its profile, so it is not identical to the same attack without it (D-006).
    const AttackContext shooting{AttackType::Shooting, false};
    EXPECT_EQ(make_attack_profile(tome, shooting).attack_time_abilities, (std::vector<WeaponAbility>{ability::Psychic{}}));
}

TEST(R24_29_Psychic, IgnoresTheBenefitOfCover) {
    // M90's crater field: the Termagants are INFANTRY within a terrain area, so they have the benefit of cover (13.08)
    // against the second Intercessor's Bolt Rifle, and not against the first one's, which is [PSYCHIC].
    GameState gs = shoot_step_state();
    for (std::size_t i = 0; i < gs.units[kMarines].models.size(); ++i) {
        gs.units[kMarines].models[i].position = at(10.0f + 1.5f * static_cast<float>(i), 6.0f);
    }
    for (std::size_t i = 0; i < gs.units[kGants].models.size(); ++i) {
        gs.units[kGants].models[i].position = at(4.0f + 1.3f * static_cast<float>(i), 17.0f);
    }
    TerrainFeature crater;
    crater.name = "Crater";
    crater.category = TerrainCategory::Exposed;
    crater.shape = circle_shape(at(20, 17), in(1));
    TerrainArea craters;
    craters.name = "Craters";
    craters.shape = polygon_shape({at(0.5f, 15.5f), at(23.5f, 15.5f), at(23.5f, 18.5f), at(0.5f, 18.5f)});
    craters.features = {crater};
    gs.terrain = {craters};
    gs.units[kMarines].models[0].weapons[kBoltRifle].profiles[kFocused].abilities.push_back(ability::Psychic{});
    std::vector<ShotDeclaration> shots;
    for (std::size_t i = 0; i < 2; ++i) {
        shots.push_back(ShotDeclaration{gs.units[kMarines].models[i].id, kBoltRifle, kFocused, gs.units[kGants].id});
    }
    const auto sels = make_weapon_selections(gs, gs.units[kMarines], ShootingType::Normal, shots);
    ASSERT_EQ(sels.size(), 2u);
    EXPECT_FALSE(sels[0].benefit_of_cover);
    EXPECT_TRUE(sels[1].benefit_of_cover);
    const auto batches = gather_attack_batches(gs, sels, shooting_context(gs, gs.units[kMarines]));
    ASSERT_EQ(batches.size(), 2u);
    const int bs = *gs.units[kMarines].models[0].weapons[kBoltRifle].profiles[kFocused].skill;
    EXPECT_EQ(*batches[0].profile.skill, bs);
    EXPECT_EQ(*batches[1].profile.skill, bs + 1);
}

TEST(R24_29_Psychic, IgnoresTheMinusOneAtAnEngagedMonsterAndKeepsHeavy) {
    // The monsters fixture, engaged: the Hive Tyrant is engaged with the Legionaries, so the Havocs' shots at it are
    // at -1 to hit (17.03). The first Havoc's weapon gets [PSYCHIC] and [HEAVY]: no -1, and the +1 stays (they
    // remained stationary).
    GameState gs = mf::at_side_a_shooting();
    Unit& havocs = gs.units[mf::kHavocs];
    const UnitId tyrant = gs.units[mf::kTyrant].id;
    std::vector<ShotDeclaration> all = shot_options(gs, havocs, ShootingType::Normal);
    std::vector<ShotDeclaration> shots;
    for (const ShotDeclaration& o : all) {
        const bool have = std::any_of(shots.begin(), shots.end(), [&o](const ShotDeclaration& d) { return d.model == o.model; });
        if (o.target == tyrant && !have) shots.push_back(o);
    }
    ASSERT_GE(shots.size(), 2u);
    Model* first = find_model(havocs, shots[0].model);
    WeaponProfile& p = first->weapons[shots[0].weapon_index].profiles[shots[0].profile_index];
    p.abilities.push_back(ability::Psychic{});
    if (!p.has<ability::Heavy>()) p.abilities.push_back(ability::Heavy{});
    const auto sels = make_weapon_selections(gs, havocs, ShootingType::Normal, shots);
    EXPECT_EQ(sels[0].hit_modifier, 0);
    EXPECT_TRUE(sels[0].hit_modifier_rules.empty());
    EXPECT_EQ(sels[1].hit_modifier, -1);
    EXPECT_EQ(sels[1].hit_modifier_rules, std::vector<HitModifierRule>{HitModifierRule::EngagedMonsterTarget});
    const AttackContext context = shooting_context(gs, havocs, ShootingType::Normal);
    ASSERT_TRUE(context.heavy_conditions_met);
    const AttackProfile psychic = selection_attack_profile(p, sels[0], context, gs.units[mf::kTyrant]);
    EXPECT_EQ(psychic.hit_modifier, 1);  // [HEAVY]'s +1 is kept (app 02.02.02)
}

// ==== 24.06 [CLEAVE X] ====

TEST(R24_06_Cleave, ParsesCleaveX) {
    EXPECT_EQ(parse_weapon_ability("Cleave 1"), WeaponAbility{ability::Cleave{1}});
    EXPECT_EQ(parse_weapon_ability("[CLEAVE 2]"), WeaponAbility{ability::Cleave{2}});
    EXPECT_TRUE(std::holds_alternative<ability::Unknown>(parse_weapon_ability("Cleave")));  // always [CLEAVE X]
}

TEST(R24_06_Cleave, AddsXDicePerFiveModelsWhenTheWeaponHasOneTarget) {
    // The rule's example: [CLEAVE 1], A 3, one target of 16 models: three more dice, six in all.
    WeaponProfile cleave = roster_profile("intercessor_squad", "Knives and Fists");
    ASSERT_EQ(cleave.attacks, DiceExpr::fixed(3));
    cleave.abilities.push_back(ability::Cleave{1});
    const AttackContext fighting{AttackType::Fighting, false};
    WeaponSelection sel;
    sel.target_models_at_selection = 16;
    EXPECT_EQ(gather_attack_dice(cleave, sel, fighting), 6);
    sel.target_models_at_selection = 4;
    EXPECT_EQ(gather_attack_dice(cleave, sel, fighting), 3);
    cleave.abilities.back() = ability::Cleave{2};
    sel.target_models_at_selection = 10;
    EXPECT_EQ(gather_attack_dice(cleave, sel, fighting), 7);
    // Splitting the weapon's attacks between targets (04.02): only the declared attacks, no extra dice.
    sel.declared_attacks = 2;
    EXPECT_EQ(gather_attack_dice(cleave, sel, fighting), 2);
    // What it does, for clients.
    sel.declared_attacks.reset();
    EXPECT_EQ(ability_effects(cleave, sel, fighting).back().extra_attack_dice, 4);
    sel.declared_attacks = 2;
    EXPECT_FALSE(ability_effects(cleave, sel, fighting).back().active);
    // Used up when the dice are gathered: not one of the profile's attack-time abilities (D-006).
    EXPECT_TRUE(make_attack_profile(cleave, fighting).attack_time_abilities.empty());
}

TEST(R24_06_Cleave, TheChargingIntercessorsGetTheDiceAgainstTenTermagants) {
    GameState gs = after_successful_charge();
    for (Model& m : gs.units[kMarines].models) m.weapons[kKnives].profiles[0].abilities.push_back(ability::Cleave{1});
    while (!(gs.pending && std::holds_alternative<decision::SelectUnitToFight>(gs.pending->kind))) {
        apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    }
    apply_action(gs, Side::A, action::SelectToFight{gs.units[kMarines].id, FightType::Normal});
    const action::Fight fight = first_melee_options(gs, gs.units[kMarines].id);
    ScriptedDice misses(std::vector<int>(60, 1));
    const auto events = apply_action(gs, Side::A, fight, misses);
    const auto batches = events_of<event::AttackBatchesGathered>(events).at(0).batches;
    ASSERT_EQ(batches.size(), 1u);
    // Each model that fights: A 3, plus 2 for the ten Termagants.
    EXPECT_EQ(batches[0].attack_dice, static_cast<int>(fight.declarations.size()) * 5);
}

// ==== 24.26 [ONE SHOT] ====

TEST(R24_26_OneShot, ParsesOneShot) {
    EXPECT_EQ(parse_weapon_ability("One Shot"), WeaponAbility{ability::OneShot{}});
    EXPECT_EQ(parse_weapon_ability("[ONE SHOT]"), WeaponAbility{ability::OneShot{}});
}

TEST(R24_26_OneShot, TheWeaponCanBeSelectedOncePerBattle) {
    GameState gs = gants_shoot({ability::OneShot{}});
    const UnitId gants = gs.units[kGants].id;
    EXPECT_FALSE(Json(gs.units[kGants].models[0].weapons[kFleshborer]).contains("spent"));  // omitted when false
    // The first two Termagants fire: every shot misses.
    ScriptedDice dice{1, 1};
    apply_action(gs, Side::B, fleshborers(gs, 2), dice);
    EXPECT_TRUE(gs.units[kGants].models[0].weapons[kFleshborer].one_shot_spent);
    EXPECT_TRUE(gs.units[kGants].models[1].weapons[kFleshborer].one_shot_spent);
    EXPECT_FALSE(gs.units[kGants].models[2].weapons[kFleshborer].one_shot_spent);  // each model's own weapon
    EXPECT_TRUE(Json(gs.units[kGants].models[0].weapons[kFleshborer]).at("spent").get<bool>());
    EXPECT_EQ(round_trip(gs), gs);

    // Next battle round, Side B's Shoot step again: those two weapons are no shot option and are refused if named; the
    // other eight can still fire.
    wh40k::test::play_neutral_until(gs, [&](const GameState& g) {
        return g.flow.battle_round == 2 && g.pending == PendingDecision{Side::B, decision::Shoot{}};
    });
    ASSERT_EQ(gs.flow.battle_round, 2);
    const Unit& unit = *gs.find_unit(gants);
    ASSERT_EQ(unit.models.size(), 10u);
    const std::vector<ShotDeclaration> options = shot_options(gs, unit, ShootingType::Normal);
    for (const ShotDeclaration& o : options) {
        EXPECT_NE(o.model, unit.models[0].id);
        EXPECT_NE(o.model, unit.models[1].id);
    }
    EXPECT_FALSE(options.empty());
    const action::Shoot again{gants, ShootingType::Normal,
                              {ShotDeclaration{unit.models[0].id, kFleshborer, 0, gs.units[kMarines].id}}};
    const auto reasons = rejection(gs, Side::B, again);
    EXPECT_EQ(reasons.at(0).rfind("24.26: ", 0), 0u) << reasons.at(0);
    const action::Shoot third{gants, ShootingType::Normal,
                              {ShotDeclaration{unit.models[2].id, kFleshborer, 0, gs.units[kMarines].id}}};
    EXPECT_TRUE(validate_action(gs, Side::B, third).ok());
}

TEST(R24_26_OneShot, AProfileWithoutItOnTheSameWeaponCanStillBeSelected) {
    // D-102: the ability is the weapon's when one of its profiles has it; here only the Bolt Rifle's second profile.
    Weapon rifle = wh40k::test::load_v1_scenario().units[kMarines].models[0].weapons[kBoltRifle];
    ASSERT_GE(rifle.profiles.size(), 2u);
    rifle.profiles[1].abilities.push_back(ability::OneShot{});
    EXPECT_FALSE(one_shot_problem(rifle, rifle.profiles[1]).has_value());
    rifle.one_shot_spent = true;
    EXPECT_TRUE(one_shot_problem(rifle, rifle.profiles[1]).has_value());
    EXPECT_FALSE(one_shot_problem(rifle, rifle.profiles[0]).has_value());
}

TEST(R24_26_OneShot, ASpentMeleeWeaponLeavesTheModelUnableToFight) {
    // The Intercessors' only melee weapon is [ONE SHOT] and spent on every model but the first: only that model has a
    // melee option, and the others need not (and can't) declare (D-021).
    GameState gs = after_successful_charge();
    for (std::size_t i = 0; i < gs.units[kMarines].models.size(); ++i) {
        Weapon& w = gs.units[kMarines].models[i].weapons[kKnives];
        w.profiles[0].abilities.push_back(ability::OneShot{});
        w.one_shot_spent = i != 0;
    }
    while (!(gs.pending && std::holds_alternative<decision::SelectUnitToFight>(gs.pending->kind))) {
        apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    }
    apply_action(gs, Side::A, action::SelectToFight{gs.units[kMarines].id, FightType::Normal});
    const std::vector<MeleeDeclaration> options = melee_options(gs, gs.units[kMarines]);
    ASSERT_FALSE(options.empty());
    for (const MeleeDeclaration& d : options) EXPECT_EQ(d.model, gs.units[kMarines].models[0].id);
    action::Fight fight = first_melee_options(gs, gs.units[kMarines].id);
    EXPECT_EQ(fight.declarations.size(), 1u);
    EXPECT_TRUE(validate_action(gs, Side::A, fight).ok());
    action::Fight spent = fight;
    spent.declarations.push_back(MeleeDeclaration{gs.units[kMarines].models[1].id, kKnives, 0,
                                                  {MeleeTarget{gs.units[kGants].id, 3}}});
    EXPECT_EQ(rejection(gs, Side::A, spent).at(0).rfind("24.26: ", 0), 0u);
    // Fighting with it spends it.
    ScriptedDice misses(std::vector<int>(10, 1));
    apply_action(gs, Side::A, fight, misses);
    EXPECT_TRUE(gs.units[kMarines].models[0].weapons[kKnives].one_shot_spent);
}

// ==== the roster and old files ====

TEST(D102_Roster, NoRosterWeaponIsRefusedAnyMore) {
    for (const auto& entry : std::filesystem::directory_iterator(repo_root() / "data/units")) {
        const Datasheet ds = load_datasheet(entry.path());
        for (const Weapon& w : ds.weapons) {
            for (const WeaponProfile& p : w.profiles) {
                EXPECT_FALSE(unimplemented_ability_problem(p).has_value()) << ds.name << ": " << p.name;
            }
        }
    }
}

TEST(D102_Serialization, OldFilesReadTheNewAbilities) {
    for (const char* text : {"Lethal Hits", "Psychic", "Cleave 1", "One Shot", "Lethal Hits: non-Monster/Vehicle"}) {
        const Json old{{"kind", "Unknown"}, {"text", text}};
        EXPECT_EQ(old.get<WeaponAbility>(), parse_weapon_ability(text)) << text;
        EXPECT_FALSE(std::holds_alternative<ability::Unknown>(old.get<WeaponAbility>())) << text;
    }
    for (const WeaponAbility& a : std::vector<WeaponAbility>{ability::Psychic{}, ability::Cleave{2}, ability::OneShot{},
                                                             ability::LethalHits{}}) {
        EXPECT_EQ(Json(a).get<WeaponAbility>(), a);
    }
}

// ==== The M100 replay: tests/replays/m100_weapon_abilities.json (format 5) ====

namespace {

const std::filesystem::path kM100Replay = std::filesystem::path(WH40K_REPLAY_DIR) / "m100_weapon_abilities.json";
constexpr std::uint64_t kM100Seed = 1;  // picked by D102_Replay.DISABLED_SearchM100Seed

// The V1 scenario with M90's crater field (the Termagants are INFANTRY within a terrain area: 13.08) and stand-in
// abilities, as no roster weapon has them: every Bolt Rifle (Focused Fire) has [LETHAL HITS: INFANTRY], the first
// Intercessor's is [PSYCHIC] too, and every Fleshborer is a [ONE SHOT] weapon.
GameState m100_replay_state() {
    GameState gs = wh40k::test::load_v1_scenario();
    for (std::size_t i = 0; i < gs.units[kMarines].models.size(); ++i) {
        gs.units[kMarines].models[i].position = at(10.0f + 1.5f * static_cast<float>(i), 6.0f);
    }
    for (std::size_t i = 0; i < gs.units[kGants].models.size(); ++i) {
        gs.units[kGants].models[i].position = at(4.0f + 1.3f * static_cast<float>(i), 17.0f);
    }
    TerrainFeature crater;
    crater.name = "Crater";
    crater.category = TerrainCategory::Exposed;
    crater.shape = circle_shape(at(20, 17), in(1));
    TerrainArea craters;
    craters.name = "Craters";
    craters.shape = polygon_shape({at(0.5f, 15.5f), at(23.5f, 15.5f), at(23.5f, 18.5f), at(0.5f, 18.5f)});
    craters.features = {crater};
    gs.terrain = {craters};
    for (Model& m : gs.units[kMarines].models) {
        m.weapons[kBoltRifle].profiles[kFocused].abilities.push_back(
            ability::LethalHits{ability::TargetCondition{{"infantry"}, false}});
    }
    gs.units[kMarines].models[0].weapons[kBoltRifle].profiles[kFocused].abilities.push_back(ability::Psychic{});
    for (Model& m : gs.units[kGants].models) m.weapons[kFleshborer].profiles[0].abilities.push_back(ability::OneShot{});
    return gs;
}

struct M100Shown {
    int automatic_wounds = 0;  // 24.23: critical hits that wounded without a roll
    bool psychic_batch = false;  // 24.29: a batch at the weapon's own BS beside one with cover's worsened BS
    int spent = 0;               // 24.26: Fleshborers spent in the final state
    int gants_fired = 0;
};

M100Shown m100_shown(const std::vector<GameEvent>& events, const GameState& final_state) {
    M100Shown s;
    for (const GameEvent& e : events) {
        if (const auto* w = std::get_if<event::WoundRollsMade>(&e)) s.automatic_wounds += w->rolls.automatic;
        if (const auto* g = std::get_if<event::AttackBatchesGathered>(&e)) {
            bool psychic = false;
            bool covered = false;
            for (const AttackBatch& b : g->batches) {
                const bool is_psychic =
                    std::any_of(b.profile.attack_time_abilities.begin(), b.profile.attack_time_abilities.end(),
                                [](const WeaponAbility& a) { return std::holds_alternative<ability::Psychic>(a); });
                psychic = psychic || (is_psychic && b.profile.skill == 3);
                covered = covered || (!is_psychic && b.profile.skill == 4);
            }
            s.psychic_batch = s.psychic_batch || (psychic && covered);
        }
        if (const auto* shoot = std::get_if<event::ShootingStarted>(&e)) {
            if (shoot->unit == final_state.units[kGants].id) s.gants_fired += static_cast<int>(shoot->selections.size());
        }
    }
    for (const Model& m : final_state.units[kGants].models) s.spent += m.weapons[kFleshborer].one_shot_spent ? 1 : 0;
    return s;
}

// The milestone's replay, played through a paced match. Round 1: the Intercessors remain stationary and fire their
// Bolt Rifles (Focused Fire) at the Termagants; the Termagants remain stationary and every one of them left fires its
// Fleshborer at the Intercessors. Every roll is made, every window declined, every casualty order given in unit order.
// Stops at Side B's Charge step. `ok`: the seed shows the three abilities.
Replay m100_replay(std::uint64_t seed, bool& ok, std::string& problem) {
    ok = false;
    Replay r;
    r.format = kReplayFormatOwnTurnStratagems;
    r.description =
        "M100 (D-102): [LETHAL HITS], [PSYCHIC], [ONE SHOT] and a keyword-conditioned ability (24.01), with stand-in "
        "abilities on the V1 weapons (no roster weapon has them). The Termagants stand in a crater field (INFANTRY within a "
        "terrain area: the benefit of cover, 13.08). Every Bolt Rifle (Focused Fire) has [LETHAL HITS: INFANTRY], the "
        "first Intercessor's is [PSYCHIC] too, and every Fleshborer is a [ONE SHOT] weapon. Round 1, Side A: the "
        "Intercessors remain stationary and fire at the Termagants: the psychic rifle's batch keeps its BS 3+ while the "
        "others' is worsened to 4+ (24.29), and each critical hit wounds automatically, with no wound roll (24.23). Side "
        "B: the Termagants remain stationary and fire their Fleshborers, which are then spent (24.26). Every roll a step "
        "of its own, every window declined, casualties in unit order. Stops at Side B's Charge step.";
    GameState init = m100_replay_state();
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
    auto answer = [](const GameState& gs) -> GameAction {
        if (window_pending(gs)) return action::DeclineStratagems{};
        if (roll_pending(gs)) {
            const auto& d = std::get<decision::RollDice>(gs.pending->kind);
            return action::RollDice{d.roll, d.unit};
        }
        return wh40k::test::neutral_action(gs);
    };
    auto play_until = [&](auto done) {
        for (int i = 0; i < 600 && !failed && match.state().pending && !done(match.state()); ++i) act(answer(match.state()));
    };
    const auto at_shoot = [](Side side) {
        return [side](const GameState& gs) { return gs.pending == PendingDecision{side, decision::Shoot{}}; };
    };
    // Every model of unit `index` fires weapon `weapon`, profile `profile`, at the other unit.
    auto fire = [&](std::size_t index, std::size_t weapon, std::size_t profile_index) {
        const GameState& gs = match.state();
        action::Shoot s{gs.units[index].id, ShootingType::Normal, {}};
        for (const Model& m : gs.units[index].models) {
            s.shots.push_back(ShotDeclaration{m.id, weapon, profile_index, gs.units[1 - index].id});
        }
        act(s);
    };
    play_until(at_shoot(Side::A));
    if (failed || !at_shoot(Side::A)(match.state())) return r;
    fire(kMarines, kBoltRifle, kFocused);
    play_until(at_shoot(Side::B));
    if (failed || !at_shoot(Side::B)(match.state()) || match.state().units[kGants].models.empty()) return r;
    fire(kGants, kFleshborer, 0);
    play_until([](const GameState& gs) { return gs.pending == PendingDecision{Side::B, decision::Charge{}}; });
    r.steps = match.steps();
    r.expect = replay_expectation(match.state());
    if (failed || match.state().pending != PendingDecision{Side::B, decision::Charge{}}) return r;
    const M100Shown shown = m100_shown(match.events(), match.state());
    ok = shown.automatic_wounds > 0 && shown.psychic_batch && shown.gants_fired > 0 && shown.spent == shown.gants_fired &&
         !match.state().units[kMarines].models.empty();
    return r;
}

}  // namespace

TEST(D102_Replay, TheReplayShowsLethalHitsPsychicAndOneShot) {
    const Replay saved = load_replay(kM100Replay);
    EXPECT_EQ(saved.format, kReplayFormatOwnTurnStratagems);
    const ReplayResult run = run_replay(saved, repo_root());
    ASSERT_TRUE(run.ok()) << "step " << *run.failed_step << ": " << run.reasons.front();
    EXPECT_EQ(replay_expectation(run.final_state), saved.expect);
    const M100Shown shown = m100_shown(run.events, run.final_state);
    EXPECT_GT(shown.automatic_wounds, 0);
    EXPECT_TRUE(shown.psychic_batch);
    EXPECT_GT(shown.gants_fired, 0);
    EXPECT_EQ(shown.spent, shown.gants_fired);
    // A spent Fleshborer is no shot option any more (24.26).
    const Unit& gants = run.final_state.units[kGants];
    for (const Model& m : gants.models) {
        EXPECT_TRUE(one_shot_problem(m.weapons[kFleshborer], m.weapons[kFleshborer].profiles[0]).has_value());
    }
    // The file is what the builder plays now.
    bool ok = false;
    std::string problem;
    const Replay again = m100_replay(kM100Seed, ok, problem);
    EXPECT_TRUE(problem.empty()) << problem;
    EXPECT_TRUE(ok);
    EXPECT_EQ(Json(again).dump(2), Json(saved).dump(2));
}

TEST(D102_Replay, DISABLED_SearchM100Seed) {
    for (std::uint64_t seed = 1; seed <= 1000; ++seed) {
        bool ok = false;
        std::string problem;
        (void)m100_replay(seed, ok, problem);
        if (!problem.empty()) std::printf("seed %llu: %s\n", static_cast<unsigned long long>(seed), problem.c_str());
        if (ok) {
            std::printf("M100 seed %llu\n", static_cast<unsigned long long>(seed));
            return;
        }
    }
    ADD_FAILURE() << "no seed in 1..1000";
}

// Writes tests/replays/m100_weapon_abilities.json. Run on purpose only:
//   wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D102_Replay.DISABLED_RegenerateM100Replay
TEST(D102_Replay, DISABLED_RegenerateM100Replay) {
    bool ok = false;
    std::string problem;
    const Replay r = m100_replay(kM100Seed, ok, problem);
    ASSERT_TRUE(problem.empty()) << problem;
    ASSERT_TRUE(ok);
    const ReplayResult run = run_replay(r, repo_root());
    ASSERT_TRUE(run.ok()) << "step " << *run.failed_step << ": " << run.reasons.front();
    save_replay(r, kM100Replay);
}
