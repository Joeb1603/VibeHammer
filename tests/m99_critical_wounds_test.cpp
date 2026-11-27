// M99 (D-101): the weapon abilities that turn on critical wounds and on the facts of the Select Targets step:
// [ANTI-X Y+] (24.03), [DEVASTATING WOUNDS] (24.10, with 06.02), [MELTA X] (24.25) and [LANCE] (24.21). The engine
// tests use the V1 scenario with stand-in abilities on the Fleshborer, the Bolt Rifle and the Knives and Fists; the
// roster weapons with these abilities are checked at the end.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "core/data/datasheet.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/casualties.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/distance.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/mortal_wounds.hpp"
#include "core/rules/shooting.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "support/army_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::after_successful_charge;
using wh40k::test::answer_casualties;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::first_melee_options;
using wh40k::test::in;
using wh40k::test::model_ids;
using wh40k::test::repo_root;
using wh40k::test::shoot_step_state;
using Json = nlohmann::json;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;
constexpr std::size_t kBoltRifle = 0;
constexpr std::size_t kKnives = 2;
constexpr std::size_t kFocused = 0;
constexpr std::size_t kFleshborer = 0;
constexpr std::size_t kClaws = 1;

// A 05.01-05.02 profile: BS 3+, S 4, AP 0, D `damage`.
AttackProfile profile(int strength, DiceExpr damage) {
    AttackProfile p;
    p.skill = 3;
    p.strength = strength;
    p.damage = damage;
    return p;
}

// Side B's Shoot step on the V1 scenario (the Termagants about 11" from the Intercessors), with every Fleshborer
// (A 1, BS 4+, S 5, AP 0, 18") given these abilities and this D. S 5 against the Intercessors' T 5 wounds on 4+.
GameState gants_shoot(std::vector<WeaponAbility> abilities, DiceExpr damage = DiceExpr::fixed(1)) {
    GameState gs = shoot_step_state(Side::B);
    for (Model& m : gs.units[kGants].models) {
        WeaponProfile& p = m.weapons[kFleshborer].profiles[0];
        p.abilities.insert(p.abilities.end(), abilities.begin(), abilities.end());
        p.damage = damage;
    }
    return gs;
}

// The first `n` Termagants fire their Fleshborers at the Intercessors.
action::Shoot fleshborers(const GameState& gs, std::size_t n) {
    action::Shoot a{gs.units[kGants].id, ShootingType::Normal, {}};
    for (std::size_t i = 0; i < n; ++i) {
        a.shots.push_back(ShotDeclaration{gs.units[kGants].models[i].id, kFleshborer, 0, gs.units[kMarines].id});
    }
    return a;
}

bool casualties_pending(const GameState& gs, std::size_t unit) {
    return gs.pending && gs.pending->kind == DecisionKind{decision::OrderCasualties{gs.units[unit].id}};
}

GameState round_trip(const GameState& gs) { return Json(gs).get<GameState>(); }

WeaponProfile roster_profile(const char* unit, const char* weapon, std::size_t p = 0) {
    return load_datasheet(repo_root() / (std::string("data/units/") + unit + ".json")).find_weapon(weapon)->profiles.at(p);
}

Unit unit_with_keywords(std::vector<std::string> keywords) {
    Unit u;
    u.keywords = std::move(keywords);
    return u;
}

}  // namespace

// ==== 24.03 [ANTI-X Y+] ====

TEST(R24_03_Anti, ParsesAntiXY) {
    EXPECT_EQ(parse_weapon_ability("Anti-INFANTRY 4+"), (WeaponAbility{ability::Anti{{"infantry"}, 4, false}}));
    EXPECT_EQ(parse_weapon_ability("[ANTI-VEHICLE 3+]"), (WeaponAbility{ability::Anti{{"vehicle"}, 3, false}}));
    EXPECT_EQ(parse_weapon_ability("Anti-Fly 2+"), (WeaponAbility{ability::Anti{{"fly"}, 2, false}}));
    EXPECT_EQ(parse_weapon_ability("Anti‑Psyker 2+"), (WeaponAbility{ability::Anti{{"psyker"}, 2, false}}));  // U+2011
    EXPECT_EQ(parse_weapon_ability("Anti Vehicle 4+"), (WeaponAbility{ability::Anti{{"vehicle"}, 4, false}}));
    // 24.01 / app 02.05.01: a series of keywords separated by slashes; the FAQ's "ANTI-non-X".
    EXPECT_EQ(parse_weapon_ability("Anti-Monster/Vehicle 4+"),
              (WeaponAbility{ability::Anti{{"monster", "vehicle"}, 4, false}}));
    EXPECT_EQ(parse_weapon_ability("Anti-non-Monster/Vehicle 5+"),
              (WeaponAbility{ability::Anti{{"monster", "vehicle"}, 5, true}}));
    EXPECT_EQ(parse_weapon_ability("Anti-Adeptus Astartes 4+"),
              (WeaponAbility{ability::Anti{{"adeptus astartes"}, 4, false}}));
    // One BSData entry has no "+".
    EXPECT_EQ(parse_weapon_ability("Anti-FLY 2"), (WeaponAbility{ability::Anti{{"fly"}, 2, false}}));
    // Not the form [ANTI-X Y+]: unknown, so refused (D-071). An unmodified 1 always fails (05.02): no "1+".
    for (const char* s : {"Anti-Infantry", "Anti-Infantry 1+", "Anti-Infantry 7+", "Anti- 4+", "Anti-Infantry/ 4+", "Anti 4+"}) {
        EXPECT_TRUE(std::holds_alternative<ability::Unknown>(parse_weapon_ability(s))) << s;
    }
}

TEST(R24_03_Anti, AppliesToATargetUnitWithTheKeyword) {
    const Unit infantry = unit_with_keywords({"Infantry", "Battleline"});
    const Unit monster = unit_with_keywords({"Monster", "Character"});
    const Unit vehicles = unit_with_keywords({"Vehicles"});  // singular and plural are alike (D-101)
    EXPECT_TRUE(anti_applies(ability::Anti{{"infantry"}, 4, false}, infantry));  // keywords are case-insensitive
    EXPECT_FALSE(anti_applies(ability::Anti{{"infantry"}, 4, false}, monster));
    EXPECT_TRUE(anti_applies(ability::Anti{{"monster", "vehicle"}, 4, false}, monster));
    EXPECT_TRUE(anti_applies(ability::Anti{{"monster", "vehicle"}, 4, false}, vehicles));
    EXPECT_FALSE(anti_applies(ability::Anti{{"monster", "vehicle"}, 4, false}, infantry));
    // "non-X": a unit that does not have the keyword (FAQ; app 02.05.01).
    EXPECT_TRUE(anti_applies(ability::Anti{{"monster", "vehicle"}, 4, true}, infantry));
    EXPECT_FALSE(anti_applies(ability::Anti{{"monster", "vehicle"}, 4, true}, monster));
    // Faction keywords are keywords too.
    Unit tyranid;
    tyranid.faction_keywords = {"Tyranids"};
    EXPECT_TRUE(anti_applies(ability::Anti{{"tyranids"}, 4, false}, tyranid));
}

TEST(R24_03_Anti, AnUnmodifiedWoundRollOfYOrMoreIsACriticalWound) {
    // S 2 against T 4 needs 6+ (05.02). With a critical wound on 4+, the 4 and the 5 are critical wounds too: they
    // wound whatever the required result. An unmodified 1 still fails, and a 3 fails as before.
    AttackProfile p = profile(2, DiceExpr::fixed(1));
    p.critical_wound_on = 4;
    const std::vector<int> faces{1, 2, 3, 4, 5, 6};
    const WoundRolls rolls = judge_wound_rolls(p, 4, faces);
    EXPECT_EQ(rolls.required, 6);
    const std::vector<WoundOutcome> expected{WoundOutcome::Fail,          WoundOutcome::Fail,
                                             WoundOutcome::Fail,          WoundOutcome::CriticalWound,
                                             WoundOutcome::CriticalWound, WoundOutcome::CriticalWound};
    for (std::size_t i = 0; i < faces.size(); ++i) EXPECT_EQ(rolls.rolls[i].outcome, expected[i]) << faces[i];
    EXPECT_EQ(rolls.wounds(), 3);
    EXPECT_EQ(rolls.critical_wounds(), 3);
    // The threshold is on the unmodified roll: a -1 to wound changes nothing for the 4.
    p.wound_modifier = -1;
    EXPECT_EQ(judge_wound_rolls(p, 4, faces).critical_wounds(), 3);
    // Without [ANTI] only the 6 is (05.02).
    EXPECT_EQ(judge_wound_rolls(profile(2, DiceExpr::fixed(1)), 4, faces).critical_wounds(), 1);
    // A re-rolled wound roll is judged with the same threshold (24.38).
    p.attack_time_abilities = {ability::TwinLinked{}};
    WoundRolls one = judge_wound_rolls(p, 4, std::vector<int>{2});
    ScriptedDice again{4};
    const std::vector<std::size_t> failed{0};
    EXPECT_EQ(reroll_wound_rolls(again, p, one, failed).at(0).after.outcome, WoundOutcome::CriticalWound);
}

TEST(R24_03_Anti, TheThresholdIsTheProfilesAgainstTheTargetsKeywords) {
    WeaponProfile weapon;
    weapon.abilities = {ability::Anti{{"infantry"}, 4, false}};
    const Unit infantry = unit_with_keywords({"Infantry"});
    const Unit monster = unit_with_keywords({"Monster"});
    EXPECT_EQ(anti_critical_wound_on(weapon, infantry), 4);
    EXPECT_EQ(anti_critical_wound_on(weapon, monster), 6);
    // 24.02 Duplicated Abilities: the controlling player selects one instance; the lowest Y that applies is used (D-101).
    weapon.abilities = {ability::Anti{{"infantry"}, 4, false}, ability::Anti{{"monster"}, 2, false},
                        ability::Anti{{"infantry"}, 3, false}};
    EXPECT_EQ(anti_critical_wound_on(weapon, infantry), 3);
    EXPECT_EQ(anti_critical_wound_on(weapon, monster), 2);
    EXPECT_EQ(anti_critical_wound_on(weapon, unit_with_keywords({"Vehicle"})), 6);
}

TEST(R24_03_Anti, ShotsAtAUnitWithTheKeywordWoundOnTheThreshold) {
    // The Intercessors are INFANTRY. A Fleshborer with S 2 needs 6+ against T 5; with [ANTI-INFANTRY 4+] a 4 is a
    // critical wound.
    GameState gs = gants_shoot({ability::Anti{{"infantry"}, 4, false}});
    for (Model& m : gs.units[kGants].models) m.weapons[kFleshborer].profiles[0].strength = 2;
    ASSERT_TRUE(has_keyword(gs.units[kMarines], "Infantry"));
    ScriptedDice dice{4, 4, 4, 3, 4, 5, 1, 1};  // three hits; wound rolls 3 (fails), 4 and 5 (critical); two failed saves
    const auto events = apply_action(gs, Side::B, fleshborers(gs, 3), dice);
    const auto gathered = events_of<event::AttackBatchesGathered>(events);
    ASSERT_EQ(gathered.size(), 1u);
    EXPECT_EQ(gathered[0].batches.at(0).profile.critical_wound_on, 4);
    EXPECT_TRUE(gathered[0].batches[0].profile.attack_time_abilities.empty());  // used up into the threshold (D-006)
    const WoundRolls wounds = events_of<event::WoundRollsMade>(events).at(0).rolls;
    EXPECT_EQ(wounds.required, 6);
    EXPECT_EQ(wounds.wounds(), 2);
    EXPECT_EQ(wounds.critical_wounds(), 2);
    // Without [DEVASTATING WOUNDS] a critical wound is saved like any other (05.03).
    EXPECT_EQ(events_of<event::SaveRollsMade>(events).at(0).rolls, (std::vector<int>{1, 1}));
    EXPECT_TRUE(casualties_pending(gs, kMarines));

    // Against a unit without the keyword the threshold is 6, and the same rolls wound nothing.
    GameState other = gants_shoot({ability::Anti{{"vehicle"}, 4, false}});
    for (Model& m : other.units[kGants].models) m.weapons[kFleshborer].profiles[0].strength = 2;
    ScriptedDice same{4, 4, 4, 3, 4, 5};
    const auto none = apply_action(other, Side::B, fleshborers(other, 3), same);
    EXPECT_EQ(events_of<event::AttackBatchesGathered>(none).at(0).batches.at(0).profile.critical_wound_on, 6);
    EXPECT_EQ(events_of<event::WoundRollsMade>(none).at(0).rolls.wounds(), 0);
}

// ==== 24.25 [MELTA X] ====

TEST(R24_25_Melta, ParsesMeltaX) {
    EXPECT_EQ(parse_weapon_ability("Melta 2"), WeaponAbility{ability::Melta{2}});
    EXPECT_EQ(parse_weapon_ability("[MELTA 4]"), WeaponAbility{ability::Melta{4}});
    EXPECT_TRUE(std::holds_alternative<ability::Unknown>(parse_weapon_ability("Melta")));     // always [MELTA X]
    EXPECT_TRUE(std::holds_alternative<ability::Unknown>(parse_weapon_ability("Melta D3")));  // a random X: not known
}

TEST(R24_25_Melta, AddsXToTheDamageWithinHalfRange) {
    // The rule's example: [MELTA 2] with D D6 is D6+2 within half range.
    WeaponProfile melta = roster_profile("havocs", "Meltagun");
    ASSERT_EQ(melta.abilities, (std::vector<WeaponAbility>{ability::Melta{2}}));
    ASSERT_EQ(melta.damage, (DiceExpr{1, 6, 0}));
    const AttackContext shooting{AttackType::Shooting, false};
    const Unit target = unit_with_keywords({"Infantry"});
    WeaponSelection sel;
    sel.target_within_half_range = true;
    const AttackProfile within = selection_attack_profile(melta, sel, shooting, target);
    EXPECT_EQ(within.damage, (DiceExpr{1, 6, 2}));
    EXPECT_TRUE(within.attack_time_abilities.empty());  // used up into the D (D-006)
    sel.target_within_half_range = false;
    const AttackProfile beyond = selection_attack_profile(melta, sel, shooting, target);
    EXPECT_EQ(beyond.damage, (DiceExpr{1, 6, 0}));
    EXPECT_NE(within, beyond);  // not identical attacks (D-006): separate batches
    // A fixed D.
    melta.damage = DiceExpr::fixed(3);
    sel.target_within_half_range = true;
    EXPECT_EQ(selection_attack_profile(melta, sel, shooting, target).damage, DiceExpr::fixed(5));
    // What it does, for clients (D-037).
    const auto effects = ability_effects(melta, sel, shooting);
    ASSERT_EQ(effects.size(), 1u);
    EXPECT_TRUE(effects[0].active);
    EXPECT_EQ(effects[0].damage_modifier, 2);
    sel.target_within_half_range = false;
    EXPECT_FALSE(ability_effects(melta, sel, shooting)[0].active);
}

TEST(R24_25_Melta, ModelsWithinAndBeyondHalfRangeAreSeparateBatches) {
    // Side A's Shoot step: the Bolt Rifles (24", D 2) get [MELTA 2]. Two Intercessors step back 2", beyond 12": their
    // shots keep D 2, the others' have D 4.
    GameState gs = shoot_step_state(Side::A);
    for (Model& m : gs.units[kMarines].models) m.weapons[kBoltRifle].profiles[kFocused].abilities.push_back(ability::Melta{2});
    gs.units[kMarines].models[0].position.y -= in(1.5f);
    gs.units[kMarines].models[1].position.y -= in(1.5f);
    action::Shoot a{gs.units[kMarines].id, ShootingType::Normal, {}};
    for (const Model& m : gs.units[kMarines].models) {
        a.shots.push_back(ShotDeclaration{m.id, kBoltRifle, kFocused, gs.units[kGants].id});
    }
    ScriptedDice dice(std::vector<int>(20, 1));  // every hit roll fails
    const auto events = apply_action(gs, Side::A, a, dice);
    const auto batches = events_of<event::AttackBatchesGathered>(events).at(0).batches;
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[0].profile.damage, DiceExpr::fixed(2));
    EXPECT_EQ(batches[0].selections.size(), 2u);
    EXPECT_EQ(batches[0].attack_dice, 2);  // no [RAPID FIRE] dice either (24.30)
    EXPECT_EQ(batches[1].profile.damage, DiceExpr::fixed(4));
    EXPECT_EQ(batches[1].selections.size(), 3u);
    EXPECT_EQ(batches[1].attack_dice, 6);
}

TEST(R24_25_Melta, TheDamageInflictedIsTheModifiedDamage) {
    // One Termagant's Fleshborer with [MELTA 2] and D 1, at about 11": beyond half its 18", so D 1. Then at 8": D 3,
    // which destroys an Intercessor (W 2) outright.
    for (const bool close : {false, true}) {
        GameState gs = gants_shoot({ability::Melta{2}});
        if (close) {
            for (Model& m : gs.units[kGants].models) m.position.y -= in(3);
        }
        ScriptedDice dice{4, 4, 1};  // hit, wound (4+), failed save
        apply_action(gs, Side::B, fleshborers(gs, 1), dice);
        ASSERT_TRUE(casualties_pending(gs, kMarines));
        const auto resolved = events_of<event::SavesResolved>(answer_casualties(gs));
        ASSERT_EQ(resolved.size(), 1u);
        const DamageResult d = *resolved[0].result.steps.at(0).damage;
        EXPECT_EQ(d.damage, close ? 3 : 1);
        EXPECT_EQ(d.destroyed, close);
    }
}

// ==== 24.21 [LANCE] ====

TEST(R24_21_Lance, ParsesLance) {
    EXPECT_EQ(parse_weapon_ability("Lance"), WeaponAbility{ability::Lance{}});
    EXPECT_EQ(parse_weapon_ability("[LANCE]"), WeaponAbility{ability::Lance{}});
}

TEST(R24_21_Lance, AddsOneToTheWoundRollAfterAChargeMove) {
    WeaponProfile lance = roster_profile("havocs", "Astartes chainsword");
    lance.abilities.push_back(ability::Lance{});
    AttackContext fighting{AttackType::Fighting, false};
    EXPECT_EQ(make_attack_profile(lance, fighting).wound_modifier, 0);
    fighting.charged = true;
    const AttackProfile charged = make_attack_profile(lance, fighting);
    EXPECT_EQ(charged.wound_modifier, 1);
    EXPECT_FALSE(std::any_of(charged.attack_time_abilities.begin(), charged.attack_time_abilities.end(),
                             [](const WeaponAbility& a) { return std::holds_alternative<ability::Lance>(a); }));
    // The context of a fight reads the unit's charge move (11.04).
    Unit unit;
    EXPECT_FALSE(fighting_context(unit).charged);
    unit.turn_flags.made_charge_move = true;
    EXPECT_TRUE(fighting_context(unit).charged);
    EXPECT_EQ(fighting_context(unit).type, AttackType::Fighting);
    // D-011: +1 is what the wound roll gets: S 4 against T 4 needs 4+, and a 3 wounds.
    AttackProfile p = profile(4, DiceExpr::fixed(1));
    p.wound_modifier = 1;
    EXPECT_EQ(judge_wound_rolls(p, 4, std::vector<int>{3}).wounds(), 1);
    EXPECT_EQ(judge_wound_rolls(p, 4, std::vector<int>{1}).wounds(), 0);  // an unmodified 1 always fails
    // What it does, for clients.
    EXPECT_EQ(ability_effects(lance, WeaponSelection{}, fighting).back().wound_modifier, 1);
    EXPECT_FALSE(ability_effects(lance, WeaponSelection{}, AttackContext{AttackType::Fighting, false}).back().active);
}

TEST(R24_21_Lance, TheChargingUnitsAttacksGetItAndTheChargedUnitsDoNot) {
    // The Intercessors charged this turn. Their Knives and Fists and the Termagants' claws both get [LANCE].
    GameState gs = after_successful_charge();
    for (Unit& u : gs.units) {
        for (Model& m : u.models) m.weapons[u.side == Side::A ? kKnives : kClaws].profiles[0].abilities.push_back(ability::Lance{});
    }
    ASSERT_TRUE(gs.units[kMarines].turn_flags.made_charge_move);
    while (!(gs.pending && std::holds_alternative<decision::SelectUnitToFight>(gs.pending->kind))) {
        apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    }
    ASSERT_EQ(decider(gs), Side::A);  // the charging unit fights first (12.04)
    apply_action(gs, Side::A, action::SelectToFight{gs.units[kMarines].id, FightType::Normal});
    ScriptedDice misses(std::vector<int>(40, 1));
    const auto marines = apply_action(gs, Side::A, first_melee_options(gs, gs.units[kMarines].id), misses);
    const auto a_batches = events_of<event::AttackBatchesGathered>(marines).at(0).batches;
    ASSERT_FALSE(a_batches.empty());
    for (const AttackBatch& b : a_batches) EXPECT_EQ(b.profile.wound_modifier, 1);

    while (gs.pending && !std::holds_alternative<decision::SelectUnitToFight>(gs.pending->kind) &&
           !std::holds_alternative<decision::DeclareMeleeAttacks>(gs.pending->kind)) {
        apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    }
    ASSERT_TRUE(gs.pending);
    if (std::holds_alternative<decision::SelectUnitToFight>(gs.pending->kind)) {
        ASSERT_EQ(decider(gs), Side::B);
        apply_action(gs, Side::B, action::SelectToFight{gs.units[kGants].id, FightType::Normal});
    }
    ScriptedDice more(std::vector<int>(40, 1));
    const auto gants = apply_action(gs, Side::B, first_melee_options(gs, gs.units[kGants].id), more);
    const auto b_batches = events_of<event::AttackBatchesGathered>(gants).at(0).batches;
    ASSERT_FALSE(b_batches.empty());
    for (const AttackBatch& b : b_batches) EXPECT_EQ(b.profile.wound_modifier, 0);  // they made no charge move
}

// ==== 24.10 [DEVASTATING WOUNDS] ====

TEST(R24_10_DevastatingWounds, CriticalWoundsMakeNoSaveRoll) {
    AttackProfile p = profile(4, DiceExpr::fixed(3));
    const WoundRolls rolls = judge_wound_rolls(p, 4, std::vector<int>{6, 4, 2, 6, 5});
    EXPECT_EQ(split_wounds(p, rolls), (WoundSplit{4, 0}));  // without the ability every wound is saved
    p.attack_time_abilities = {ability::DevastatingWounds{}};
    EXPECT_TRUE(devastating_wounds(p));
    EXPECT_EQ(split_wounds(p, rolls), (WoundSplit{2, 2}));
    // With [ANTI] the critical wounds on its threshold count (24.03).
    p.critical_wound_on = 4;
    EXPECT_EQ(split_wounds(p, judge_wound_rolls(p, 4, std::vector<int>{6, 4, 2, 6, 5})), (WoundSplit{0, 4}));
}

TEST(R24_10_DevastatingWounds, EachCriticalWoundDamagesOneModelAndTheRestIsLost) {
    // The rule's example: D 3 against Intercessors (W 2): the first 2 mortal wounds destroy one model, the third is
    // lost.
    Unit marines = wh40k::test::load_v1_scenario().units[kMarines];
    const ModelChooser first = [](const Unit&, std::span<const ModelId> options) { return options.front(); };
    const ModelId front = marines.models[0].id;
    DevastatingWoundsResult r = inflict_devastating_wounds(marines, 1, DiceExpr::fixed(3), first);
    ASSERT_EQ(r.steps.size(), 1u);
    EXPECT_EQ(r.steps[0].damage, (DamageResult{front, 3, 2, true}));
    EXPECT_EQ(r.models_destroyed, 1);
    EXPECT_EQ(marines.models.size(), 4u);
    for (const Model& m : marines.models) EXPECT_EQ(m.wounds_remaining, 2);  // nothing spilled over

    // 06.02 Select Model: a model that has lost wounds must be selected, so it takes the next critical wound's
    // mortal wounds; then the choice is open again.
    marines.models[2].wounds_remaining = 1;
    const ModelId wounded = marines.models[2].id;
    std::vector<std::vector<ModelId>> offered;
    const ModelChooser last = [&offered](const Unit&, std::span<const ModelId> options) {
        offered.emplace_back(options.begin(), options.end());
        return options.back();
    };
    r = inflict_devastating_wounds(marines, 2, DiceExpr::fixed(1), last, true);
    ASSERT_EQ(r.steps.size(), 2u);
    EXPECT_EQ(r.steps[0].damage, (DamageResult{wounded, 1, 1, true}));
    EXPECT_EQ(offered.at(0), std::vector<ModelId>{wounded});
    EXPECT_EQ(offered.at(1).size(), 3u);
    EXPECT_EQ(r.steps[1].damage.wounds_lost, 1);
    EXPECT_FALSE(r.steps[1].damage.destroyed);

    // The unit is destroyed: the critical wounds left are lost.
    Unit one = wh40k::test::load_v1_scenario().units[kMarines];
    one.models.resize(1);
    r = inflict_devastating_wounds(one, 3, DiceExpr::fixed(2), first);
    EXPECT_EQ(r.steps.size(), 1u);
    EXPECT_EQ(r.critical_wounds_lost, 2);
    EXPECT_TRUE(one.models.empty());

    // A random D is rolled for each critical wound, once its model is selected (D-069); without a roller it is a
    // programmer error.
    Unit again = wh40k::test::load_v1_scenario().units[kMarines];
    int rolls = 0;
    const DamageRoller roller = [&rolls](const DiceExpr&) {
        ++rolls;
        return CharacteristicRoll{{1}, 1};
    };
    r = inflict_devastating_wounds(again, 2, DiceExpr{1, 3, 0}, first, false, roller);
    EXPECT_EQ(rolls, 2);
    ASSERT_EQ(r.steps.size(), 2u);
    EXPECT_EQ(r.steps[0].damage_roll, (CharacteristicRoll{{1}, 1}));
    EXPECT_EQ(r.steps[1].damage.model, r.steps[0].damage.model);  // the wounded model again
    EXPECT_TRUE(r.steps[1].damage.destroyed);
    EXPECT_THROW((void)inflict_devastating_wounds(again, 1, DiceExpr{1, 3, 0}, first), std::logic_error);
}

TEST(R24_10_DevastatingWounds, ACharacterIsSelectedLast) {
    // 06.02: non-CHARACTER models first. Model-level CHARACTER comes with attached units (19); here the whole unit is.
    Unit lone = wh40k::test::load_v1_scenario().units[kMarines];
    lone.keywords.push_back("Character");
    lone.models[3].wounds_remaining = 1;
    EXPECT_EQ(mortal_wound_candidates(lone), std::vector<ModelId>{lone.models[3].id});  // a wounded CHARACTER first
    const ModelChooser first = [](const Unit&, std::span<const ModelId> options) { return options.front(); };
    const ModelId wounded = lone.models[3].id;
    const DevastatingWoundsResult r = inflict_devastating_wounds(lone, 1, DiceExpr::fixed(3), first);
    EXPECT_EQ(r.steps.at(0).damage, (DamageResult{wounded, 3, 1, true}));
}

TEST(R24_10_DevastatingWounds, ACriticalWoundInflictsTheWeaponsDamageAsMortalWoundsWithNoSave) {
    GameState gs = gants_shoot({ability::DevastatingWounds{}}, DiceExpr::fixed(3));
    ScriptedDice dice{4, 6};  // a hit, then a critical wound: no save roll follows
    const auto events = apply_action(gs, Side::B, fleshborers(gs, 1), dice);
    EXPECT_EQ(dice.remaining(), 0u);
    EXPECT_TRUE(events_of<event::SaveRollsMade>(events).empty());
    EXPECT_TRUE(events_of<event::AllocationOrderDeclared>(events).empty());
    // 06.02: the target's controlling player selects the model.
    ASSERT_TRUE(casualties_pending(gs, kMarines));
    EXPECT_EQ(gs.pending->side, Side::A);
    EXPECT_EQ(decision_rule(gs, *gs.pending), "06.02");
    ASSERT_TRUE(gs.flow.attack && gs.flow.attack->current);
    EXPECT_EQ(gs.flow.attack->current->devastating, 1);
    EXPECT_EQ(gs.flow.attack->current->wounds, 0);
    EXPECT_TRUE(gs.flow.attack->current->mortal);
    EXPECT_EQ(round_trip(gs), gs);

    // The second Intercessor is picked: it loses 2 wounds and is destroyed; the third mortal wound is lost.
    std::vector<ModelId> priority = model_ids(gs.units[kMarines]);
    std::swap(priority[0], priority[1]);
    const ModelId picked = priority[0];
    const auto resolved = apply_action(gs, Side::A, action::OrderCasualties{gs.units[kMarines].id, priority});
    const auto inflicted = events_of<event::DevastatingWoundsInflicted>(resolved);
    ASSERT_EQ(inflicted.size(), 1u);
    EXPECT_EQ(inflicted[0].attacker, gs.units[kGants].id);
    EXPECT_EQ(inflicted[0].target, gs.units[kMarines].id);
    ASSERT_EQ(inflicted[0].result.steps.size(), 1u);
    EXPECT_EQ(inflicted[0].result.steps[0].damage, (DamageResult{picked, 3, 2, true}));
    EXPECT_EQ(event_rule(GameEvent{inflicted[0]}), "24.10");
    EXPECT_EQ(events_of<event::ModelDestroyed>(resolved).size(), 1u);
    EXPECT_EQ(gs.units[kMarines].models.size(), 4u);
    for (const Model& m : gs.units[kMarines].models) EXPECT_EQ(m.wounds_remaining, 2);
    EXPECT_FALSE(gs.flow.attack.has_value());  // the unit's attacks are resolved
    EXPECT_TRUE(std::holds_alternative<decision::Shoot>(gs.pending->kind));
}

TEST(R24_10_DevastatingWounds, TheNormalDamageIsResolvedBeforeTheMortalWounds) {
    // Three shots: wound rolls 6 (critical), 4 (a wound) and 2 (fails). One save roll, for the normal wound; it fails.
    GameState gs = gants_shoot({ability::DevastatingWounds{}}, DiceExpr::fixed(1));
    ScriptedDice dice{4, 4, 4, 6, 4, 2, 1};
    const auto events = apply_action(gs, Side::B, fleshborers(gs, 3), dice);
    EXPECT_EQ(dice.remaining(), 0u);
    EXPECT_EQ(events_of<event::SaveRollsMade>(events).at(0).rolls, std::vector<int>{1});  // one save roll, not two
    ASSERT_TRUE(casualties_pending(gs, kMarines));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "05.04");
    EXPECT_FALSE(gs.flow.attack->current->mortal);
    // 05.04: the last Intercessor takes the normal damage.
    std::vector<ModelId> priority = model_ids(gs.units[kMarines]);
    std::reverse(priority.begin(), priority.end());
    const ModelId wounded = priority[0];
    const auto normal = apply_action(gs, Side::A, action::OrderCasualties{gs.units[kMarines].id, priority});
    EXPECT_EQ(events_of<event::SavesResolved>(normal).size(), 1u);
    EXPECT_TRUE(events_of<event::DevastatingWoundsInflicted>(normal).empty());
    // 06.02 next: the same player is asked again; the model that lost a wound must be selected.
    ASSERT_TRUE(casualties_pending(gs, kMarines));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "06.02");
    EXPECT_TRUE(gs.flow.attack->current->mortal);
    EXPECT_EQ(round_trip(gs), gs);
    const std::optional<CasualtyPreview> preview = preview_casualties(gs, {});
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->options, std::vector<ModelId>{wounded});
    EXPECT_EQ(preview->next_damage, 1);
    EXPECT_EQ(preview->damage_left, 1);
    const auto mortal = answer_casualties(gs);  // unit order: the wounded model is still the one selected
    const auto inflicted = events_of<event::DevastatingWoundsInflicted>(mortal);
    ASSERT_EQ(inflicted.size(), 1u);
    EXPECT_EQ(inflicted[0].result.steps.at(0).damage, (DamageResult{wounded, 1, 1, true}));
    EXPECT_EQ(gs.units[kMarines].models.size(), 4u);
}

TEST(R24_10_DevastatingWounds, TheMortalWoundsAreLostWhenTheNormalDamageDestroysTheUnit) {
    // One Intercessor left, on its last wound, and a second Intercessor unit so the game goes on (D-007).
    GameState gs = gants_shoot({ability::DevastatingWounds{}}, DiceExpr::fixed(1));
    const UnitId spare = wh40k::test::add_unit_copy(gs, kMarines);
    for (Model& m : gs.find_unit(spare)->models) m.position.y -= in(3);
    gs.units[kMarines].models.resize(1);
    gs.units[kMarines].models[0].wounds_remaining = 1;
    ScriptedDice dice{4, 4, 6, 4, 1};  // two hits; a critical wound and a wound; the save fails
    apply_action(gs, Side::B, fleshborers(gs, 2), dice);
    ASSERT_TRUE(casualties_pending(gs, kMarines));
    const auto events = answer_casualties(gs);
    EXPECT_EQ(events_of<event::UnitDestroyed>(events).size(), 1u);
    const auto inflicted = events_of<event::DevastatingWoundsInflicted>(events);
    ASSERT_EQ(inflicted.size(), 1u);
    EXPECT_TRUE(inflicted[0].result.steps.empty());
    EXPECT_EQ(inflicted[0].result.critical_wounds_lost, 1);
    EXPECT_FALSE(gs.flow.attack.has_value());
}

TEST(R24_10_DevastatingWounds, ARandomDamageIsRolledPerCriticalWoundAfterItsModelIsSelected) {
    // D D3: two critical wounds. The casualty order comes first, then one Damage roll for each critical wound (D-096),
    // made inline here from the same dice.
    GameState gs = gants_shoot({ability::DevastatingWounds{}}, DiceExpr{1, 3, 0});
    ScriptedDice dice{4, 4, 6, 6};
    apply_action(gs, Side::B, fleshborers(gs, 2), dice);
    ASSERT_TRUE(casualties_pending(gs, kMarines));
    const std::optional<CasualtyPreview> preview = preview_casualties(gs, {});
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->random_damage, (DiceExpr{1, 3, 0}));
    EXPECT_EQ(preview->next_damage, 3);  // its maximum (damage_stand_in)
    const std::vector<ModelId> priority = model_ids(gs.units[kMarines]);
    ScriptedDice damage{2, 6};  // a D3 of 1 (wounds the first Intercessor), then of 3 on the same model
    const auto events =
        apply_action(gs, Side::A, action::OrderCasualties{gs.units[kMarines].id, priority}, damage);
    EXPECT_EQ(damage.remaining(), 0u);
    const auto rolled = events_of<event::DamageRolled>(events);
    ASSERT_EQ(rolled.size(), 2u);
    EXPECT_EQ(rolled[0].model, priority[0]);
    EXPECT_EQ(rolled[0].roll, (CharacteristicRoll{{2}, 1}));
    EXPECT_EQ(rolled[1].model, priority[0]);  // 06.02: the wounded model
    EXPECT_EQ(rolled[1].roll, (CharacteristicRoll{{6}, 3}));
    const auto inflicted = events_of<event::DevastatingWoundsInflicted>(events);
    ASSERT_EQ(inflicted.size(), 1u);
    ASSERT_EQ(inflicted[0].result.steps.size(), 2u);
    EXPECT_EQ(inflicted[0].result.steps[0].damage, (DamageResult{priority[0], 1, 1, false}));
    EXPECT_EQ(inflicted[0].result.steps[0].damage_roll, (CharacteristicRoll{{2}, 1}));
    EXPECT_EQ(inflicted[0].result.steps[1].damage, (DamageResult{priority[0], 3, 1, true}));
    EXPECT_EQ(gs.units[kMarines].models.size(), 4u);
}

TEST(R24_10_DevastatingWounds, PacedRollsStopAtEachDamageRollAndGiveTheInlineResult) {
    const auto shoot = [](GameState& gs, DiceSource& dice, bool paced) {
        const action::Shoot a = fleshborers(gs, 2);
        if (!paced) {
            apply_action(gs, Side::B, a, dice);
            apply_action(gs, Side::A, action::OrderCasualties{gs.units[kMarines].id, model_ids(gs.units[kMarines])}, dice);
            return;
        }
        apply_action_paced(gs, Side::B, a, dice);
        int damage_rolls = 0;
        for (int guard = 0; guard < 50 && gs.pending; ++guard) {
            if (const auto* r = std::get_if<decision::RollDice>(&gs.pending->kind)) {
                if (r->roll == RollKind::Damage) {
                    ++damage_rolls;
                    EXPECT_EQ(gs.pending->side, Side::B);  // the attacking unit's player
                    const RollDiceInfo info = *roll_dice_info(gs);
                    EXPECT_EQ(info.expression, "D3");
                    EXPECT_EQ(info.target, gs.units[kMarines].id);
                    EXPECT_TRUE(info.model.has_value());
                    EXPECT_EQ(round_trip(gs), gs);
                }
                apply_action_paced(gs, gs.pending->side, action::RollDice{r->roll, r->unit}, dice);
            } else if (std::holds_alternative<decision::UseStratagems>(gs.pending->kind)) {
                apply_action_paced(gs, gs.pending->side, action::DeclineStratagems{}, dice);
            } else if (std::holds_alternative<decision::OrderCasualties>(gs.pending->kind)) {
                apply_action_paced(gs, Side::A,
                                   action::OrderCasualties{gs.units[kMarines].id, model_ids(gs.units[kMarines])}, dice);
            } else {
                break;
            }
        }
        EXPECT_EQ(damage_rolls, 2);
    };
    GameState inline_gs = gants_shoot({ability::DevastatingWounds{}}, DiceExpr{1, 3, 0});
    GameState paced_gs = inline_gs;
    ScriptedDice a{4, 4, 6, 6, 2, 6};
    ScriptedDice b{4, 4, 6, 6, 2, 6};
    shoot(inline_gs, a, false);
    shoot(paced_gs, b, true);
    EXPECT_EQ(a.remaining(), 0u);
    EXPECT_EQ(b.remaining(), 0u);
    EXPECT_EQ(paced_gs, inline_gs);
}

TEST(R24_10_DevastatingWounds, AntiMakesTheCriticalWoundsThatInflictThem) {
    // The Terminators' Combi-weapon: [ANTI-INFANTRY 4+] and [DEVASTATING WOUNDS]. Here on the Fleshborer, D 2: a wound
    // roll of 4 against the Intercessors (INFANTRY) is a critical wound, so 2 mortal wounds with no save.
    GameState gs = gants_shoot({ability::Anti{{"infantry"}, 4, false}, ability::DevastatingWounds{}}, DiceExpr::fixed(2));
    ScriptedDice dice{4, 4};
    const auto events = apply_action(gs, Side::B, fleshborers(gs, 1), dice);
    EXPECT_TRUE(events_of<event::SaveRollsMade>(events).empty());
    ASSERT_TRUE(casualties_pending(gs, kMarines));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "06.02");
    const auto inflicted = events_of<event::DevastatingWoundsInflicted>(answer_casualties(gs));
    ASSERT_EQ(inflicted.size(), 1u);
    EXPECT_EQ(inflicted[0].result.steps.at(0).damage.wounds_lost, 2);
    EXPECT_TRUE(inflicted[0].result.steps[0].damage.destroyed);
}

TEST(R24_10_DevastatingWounds, TheBatchResolvedAsOneFunctionAgreesWithTheEngine) {
    // resolve_attack_batch (the bots' sampling and the pure tests) makes the same rolls in the same order.
    GameState gs = gants_shoot({ability::DevastatingWounds{}}, DiceExpr{1, 3, 0});
    ScriptedDice dice{4, 4, 4, 6, 4, 2, 1, 5, 4};  // hits; wounds 6, 4, 2; one save (1); the save's D3 (5 -> 3); the crit's (4 -> 2)
    apply_action(gs, Side::B, fleshborers(gs, 3), dice);
    ASSERT_TRUE(casualties_pending(gs, kMarines));
    apply_action(gs, Side::A, action::OrderCasualties{gs.units[kMarines].id, model_ids(gs.units[kMarines])}, dice);
    ASSERT_TRUE(casualties_pending(gs, kMarines));
    apply_action(gs, Side::A, action::OrderCasualties{gs.units[kMarines].id, model_ids(gs.units[kMarines])}, dice);
    EXPECT_EQ(dice.remaining(), 0u);

    GameState fresh = gants_shoot({ability::DevastatingWounds{}}, DiceExpr{1, 3, 0});
    const Unit& gants = fresh.units[kGants];
    std::vector<WeaponSelection> selections;
    for (std::size_t i = 0; i < 3; ++i) {
        selections.push_back(select_weapon_target(fresh, gants.models[i].id, kFleshborer, 0, fresh.units[kMarines].id));
    }
    const auto batches = gather_attack_batches(fresh, selections, shooting_context(fresh, gants, ShootingType::Normal));
    ASSERT_EQ(batches.size(), 1u);
    Unit target = fresh.units[kMarines];
    ScriptedDice same{4, 4, 4, 6, 4, 2, 1, 5, 4};
    const ModelChooser first = [](const Unit&, std::span<const ModelId> options) { return options.front(); };
    const BatchResult r = resolve_attack_batch(same, target, batches[0], {}, first);
    EXPECT_EQ(same.remaining(), 0u);
    EXPECT_EQ(r.save_rolls, std::vector<int>{1});
    EXPECT_EQ(r.devastating.steps.size(), 1u);
    EXPECT_EQ(target.models, gs.units[kMarines].models);
}

// ==== the roster weapons ====

TEST(D101_Roster, TheWeaponsWithTheseAbilitiesCanBeSelected) {
    const std::vector<std::pair<const char*, const char*>> weapons{
        {"chaos_lord", "Daemon hammer"},         {"chaos_terminator_squad", "Combi-weapon"},
        {"chaos_terminator_squad", "Chainfist"}, {"chaos_terminator_squad", "Reaper autocannon"},
        {"havocs", "Meltagun"},                  {"legionaries", "Meltagun"},
        {"termagants", "Strangleweb"},           {"intercessor_squad", "Thunder Hammer"}};
    for (const auto& [unit, weapon] : weapons) {
        const WeaponProfile p = roster_profile(unit, weapon);
        EXPECT_FALSE(unimplemented_ability_problem(p).has_value()) << unit << " " << weapon;
        EXPECT_FALSE(p.has<ability::Unknown>()) << unit << " " << weapon;
    }
    EXPECT_EQ(roster_profile("chaos_terminator_squad", "Combi-weapon").abilities,
              (std::vector<WeaponAbility>{ability::Anti{{"infantry"}, 4, false}, ability::DevastatingWounds{},
                                          ability::RapidFire{1}}));
    EXPECT_EQ(roster_profile("chaos_terminator_squad", "Chainfist").abilities,
              (std::vector<WeaponAbility>{ability::Anti{{"vehicle"}, 3, false}}));
    // M100 (D-102): [PSYCHIC] (24.29) too, so the Balefire tome is no longer refused.
    EXPECT_FALSE(unimplemented_ability_problem(roster_profile("legionaries", "Balefire tome")).has_value());
}

TEST(D101_Serialization, TheNewFieldsRoundTripAndOldFilesReadAsBefore) {
    AttackContext context{AttackType::Fighting, false};
    context.charged = true;
    EXPECT_EQ(Json(context).get<AttackContext>(), context);
    EXPECT_FALSE(Json(AttackContext{}).contains("charged"));  // omitted when false: older files are byte for byte
    AttackProfile p = profile(4, DiceExpr{1, 6, 2});
    p.critical_wound_on = 3;
    p.wound_modifier = 1;
    p.attack_time_abilities = {ability::DevastatingWounds{}};
    EXPECT_EQ(Json(p).get<AttackProfile>(), p);
    EXPECT_FALSE(Json(profile(4, DiceExpr::fixed(1))).contains("critical_wound_on"));
    BatchInProgress b{2, 0, {}, std::nullopt, std::nullopt};
    b.devastating = 3;
    b.mortal = true;
    b.priority = std::vector<ModelId>{ModelId{4}, ModelId{2}};
    b.damage_rolls = {CharacteristicRoll{{5}, 3}};
    EXPECT_EQ(Json(b).get<BatchInProgress>(), b);
    const Json plain(BatchInProgress{1, 2, {}, std::nullopt, std::nullopt});
    EXPECT_FALSE(plain.contains("devastating"));
    EXPECT_FALSE(plain.contains("mortal"));
    // A file saved before M99 holds these abilities as unknown text; reading it gives the ability (D-071).
    for (const char* text : {"Anti-INFANTRY 4+", "Melta 2", "Lance"}) {
        const Json old{{"kind", "Unknown"}, {"text", text}};
        EXPECT_EQ(old.get<WeaponAbility>(), parse_weapon_ability(text)) << text;
        EXPECT_FALSE(std::holds_alternative<ability::Unknown>(old.get<WeaponAbility>())) << text;
    }
}

// ==== The M99 replay: tests/replays/m99_critical_wounds.json (format 5) ====

namespace {

const std::filesystem::path kM99Replay = std::filesystem::path(WH40K_REPLAY_DIR) / "m99_critical_wounds.json";
constexpr std::uint64_t kM99Seed = 7;  // picked by D101_Replay.DISABLED_SearchM99Seed

// Terminators with Combi-weapons ([ANTI-INFANTRY 4+], [DEVASTATING WOUNDS]) and a Reaper autocannon ([DEVASTATING
// WOUNDS]); Havocs whose champion has a Meltagun ([MELTA 2]).
ArmyList m99_replay_csm() {
    ArmyList l{"Chaos Space Marines (M99 replay)", "chaos_space_marines", {}};
    // D-079: an army needs a CHARACTER unit. The Lord (Daemon hammer, [DEVASTATING WOUNDS]) takes no part here.
    l.units.push_back({"data/units/chaos_lord.json", {wh40k::test::group("Chaos Lord", 1, {"Daemon hammer", "Plasma pistol"})}});
    l.units.push_back({"data/units/chaos_terminator_squad.json",
                       {wh40k::test::group("Terminator Champion", 1, {"Accursed weapon", "Combi-weapon"}),
                        wh40k::test::group("Accursed weapon and combi-weapon", 3, {"Accursed weapon", "Combi-weapon"}),
                        wh40k::test::group("Heavy weapon", 1, {"Reaper autocannon", "Accursed weapon"})}});
    l.units.push_back({"data/units/havocs.json",
                       {wh40k::test::group("Havoc Champion", 1, {"Astartes chainsword", "Meltagun"}),
                        wh40k::test::group("Havoc w/ heavy bolter", 4, {"Havoc heavy bolter", "Close combat weapon"})}});
    return l;
}

ArmyList m99_replay_tyranids() {
    ArmyList l{"Tyranids (M99 replay)", "tyranids", {}};
    l.units.push_back({"data/units/termagants.json",
                       {wh40k::test::group("Termagants", 10, {"Chitinous claws and teeth", "Fleshborer"})}});
    l.units.push_back({"data/units/hive_tyrant.json",
                       {wh40k::test::group("Hive Tyrant", 1, {"Monstrous bonesword and lash whip", "Monstrous scything talons"})}});
    return l;
}

Unit& unit_named(GameState& gs, const std::string& prefix) {
    for (Unit& u : gs.units) {
        if (u.name.rfind(prefix, 0) == 0) return u;
    }
    throw std::logic_error("no unit " + prefix);
}

// Moves `unit` so that it stands `gap_in` north of `of` (edge to edge), centred on `x_mm`.
void stand_north_of(GameState& gs, Unit& unit, const Unit& of, float x_mm, float gap_in) {
    float cx = 0.0f;
    for (const Model& m : unit.models) cx += m.position.x;
    cx /= static_cast<float>(unit.models.size());
    for (Model& m : unit.models) m.position.x += x_mm - cx;
    for (int i = 0; i < 4; ++i) {
        const float gap = *unit_distance(unit, of);
        for (Model& m : unit.models) m.position.y -= gap - in(gap_in);
    }
    (void)gs;
}

// The custom battle of the two lists with the Tyranids brought into range: the Termagants 9" north of the Terminators
// (within half the Combi-weapons' 24"), the Hive Tyrant 5" north of the Havoc Champion (within half the Meltagun's 12").
GameState m99_replay_battle() {
    GameState gs = wh40k::test::custom_battle(m99_replay_csm(), m99_replay_tyranids());
    Unit& terminators = unit_named(gs, "Chaos Terminator");
    Unit& havocs = unit_named(gs, "Havocs");
    float tx = 0.0f;
    for (const Model& m : terminators.models) tx += m.position.x;
    tx /= static_cast<float>(terminators.models.size());
    const Model* champion = nullptr;
    for (const Model& m : havocs.models) {
        for (const Weapon& w : m.weapons) {
            if (w.name == "Meltagun") champion = &m;
        }
    }
    if (champion == nullptr) throw std::logic_error("no Meltagun");
    stand_north_of(gs, unit_named(gs, "Termagants"), terminators, tx, 9.0f);
    stand_north_of(gs, unit_named(gs, "Hive Tyrant"), havocs, champion->position.x, 5.0f);
    return gs;
}

struct M99Shown {
    int devastating_steps = 0;      // critical wounds whose mortal wounds reached a model (24.10)
    int anti_critical_wounds = 0;   // critical wounds on an unmodified 4 or 5 (24.03)
    int melta_damage_rolls = 0;     // damage rolls of D6+2 (24.25)
};

M99Shown m99_shown(const std::vector<GameEvent>& events) {
    M99Shown s;
    for (const GameEvent& e : events) {
        if (const auto* d = std::get_if<event::DevastatingWoundsInflicted>(&e)) {
            s.devastating_steps += static_cast<int>(d->result.steps.size());
        } else if (const auto* w = std::get_if<event::WoundRollsMade>(&e)) {
            for (const WoundRoll& r : w->rolls.rolls) {
                if (r.outcome == WoundOutcome::CriticalWound && r.unmodified < 6) ++s.anti_critical_wounds;
            }
        } else if (const auto* r = std::get_if<event::DamageRolled>(&e)) {
            if (r->damage == DiceExpr{1, 6, 2}) ++s.melta_damage_rolls;
        }
    }
    return s;
}

// The milestone's replay, played through a paced match: Side A remains stationary, then the Terminators fire every
// ranged weapon at the Termagants and the Havocs at the Hive Tyrant; every roll is made, every window declined and
// every casualty order given in unit order. Stops at Side A's Charge step. `ok`: the seed shows all three abilities.
Replay m99_replay(std::uint64_t seed, bool& ok, std::string& problem) {
    ok = false;
    Replay r;
    r.format = kReplayFormatOwnTurnStratagems;
    r.description =
        "M99 (D-101): the weapon abilities that turn on critical wounds and on the Select Targets step, with the roster "
        "weapons. A custom battle: Chaos Terminators (four Combi-weapons, [ANTI-INFANTRY 4+] and [DEVASTATING WOUNDS], and "
        "a Reaper autocannon, [DEVASTATING WOUNDS]), Havocs (a champion's Meltagun, [MELTA 2], and four heavy bolters) "
        "and a Chaos Lord, who takes no part, against Termagants 9\" north of the Terminators and a Hive Tyrant 5\" north of the Havoc Champion. Round 1, Side "
        "A remains stationary; the Terminators fire at the Termagants (INFANTRY): wound rolls of 4+ are critical wounds "
        "(24.03), which make no save roll and inflict the weapon's D as mortal wounds after the batch's normal damage "
        "(24.10, 06.02: Side B's casualty order for each); the Havocs fire at the Hive Tyrant, the Meltagun within half "
        "range, so its damage roll is D6+2 (24.25). Every roll a step of its own, every window declined, casualties in "
        "unit order. Stops at Side A's Charge step.";
    GameState init = m99_replay_battle();
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
    const auto at_shoot = [](const GameState& gs) { return gs.pending == PendingDecision{Side::A, decision::Shoot{}}; };
    // Every model of `shooter` fires each of its ranged weapons (its first profile) at `target`.
    auto fire = [&](const std::string& shooter, const std::string& target) {
        GameState gs = match.state();
        const Unit& unit = unit_named(gs, shooter);
        const UnitId at = unit_named(gs, target).id;
        action::Shoot s{unit.id, ShootingType::Normal, {}};
        for (const ShotDeclaration& o : shot_options(gs, unit, ShootingType::Normal)) {
            const bool have = std::any_of(s.shots.begin(), s.shots.end(), [&o](const ShotDeclaration& d) {
                return d.model == o.model && d.weapon_index == o.weapon_index;
            });
            if (o.target == at && !have) s.shots.push_back(o);
        }
        act(s);
    };
    play_until(at_shoot);
    if (failed || !at_shoot(match.state())) return r;
    fire("Chaos Terminator", "Termagants");
    play_until(at_shoot);
    if (failed || !at_shoot(match.state())) return r;
    fire("Havocs", "Hive Tyrant");
    play_until([](const GameState& gs) { return gs.pending == PendingDecision{Side::A, decision::Charge{}}; });
    r.steps = match.steps();
    r.expect = replay_expectation(match.state());
    if (failed || match.state().pending != PendingDecision{Side::A, decision::Charge{}}) return r;
    const M99Shown shown = m99_shown(match.events());
    ok = shown.devastating_steps > 0 && shown.anti_critical_wounds > 0 && shown.melta_damage_rolls > 0;
    return r;
}

}  // namespace

TEST(D101_Replay, TheReplayShowsAntiDevastatingWoundsAndMelta) {
    const Replay saved = load_replay(kM99Replay);
    EXPECT_EQ(saved.format, kReplayFormatOwnTurnStratagems);
    const ReplayResult run = run_replay(saved, repo_root());
    ASSERT_TRUE(run.ok()) << "step " << *run.failed_step << ": " << run.reasons.front();
    EXPECT_EQ(replay_expectation(run.final_state), saved.expect);
    const M99Shown shown = m99_shown(run.events);
    EXPECT_GT(shown.devastating_steps, 0);
    EXPECT_GT(shown.anti_critical_wounds, 0);
    EXPECT_GT(shown.melta_damage_rolls, 0);
    // The Combi-weapons' batch against the INFANTRY Termagants has the threshold; the Meltagun's has D6+2.
    bool anti_batch = false;
    bool melta_batch = false;
    for (const GameEvent& e : run.events) {
        if (const auto* g = std::get_if<event::AttackBatchesGathered>(&e)) {
            for (const AttackBatch& b : g->batches) {
                anti_batch = anti_batch || (b.profile.critical_wound_on == 4 && devastating_wounds(b.profile));
                melta_batch = melta_batch || b.profile.damage == DiceExpr{1, 6, 2};
            }
        }
    }
    EXPECT_TRUE(anti_batch);
    EXPECT_TRUE(melta_batch);
    // The starting positions the builder set: no two units overlap, the Termagants are within half the Combi-weapons'
    // range of the Terminators and the Hive Tyrant within half the Meltagun's of the Havocs.
    GameState start = replay_initial_state(saved, repo_root());
    for (const Unit& a : start.units) {
        for (const Unit& b : start.units) {
            if (a.id.v < b.id.v) EXPECT_GT(*unit_distance(a, b), in(1.0f)) << a.name << " / " << b.name;
        }
    }
    EXPECT_NEAR(*unit_distance(unit_named(start, "Termagants"), unit_named(start, "Chaos Terminator")), in(9.0f), in(0.1f));
    EXPECT_NEAR(*unit_distance(unit_named(start, "Hive Tyrant"), unit_named(start, "Havocs")), in(5.0f), in(0.1f));
    // The file is what the builder plays now.
    bool ok = false;
    std::string problem;
    const Replay again = m99_replay(kM99Seed, ok, problem);
    EXPECT_TRUE(problem.empty()) << problem;
    EXPECT_TRUE(ok);
    EXPECT_EQ(Json(again).dump(2), Json(saved).dump(2));
}

TEST(D101_Replay, DISABLED_SearchM99Seed) {
    for (std::uint64_t seed = 1; seed <= 1000; ++seed) {
        bool ok = false;
        std::string problem;
        (void)m99_replay(seed, ok, problem);
        if (!problem.empty()) std::printf("seed %llu: %s\n", static_cast<unsigned long long>(seed), problem.c_str());
        if (ok) {
            std::printf("M99 seed %llu\n", static_cast<unsigned long long>(seed));
            return;
        }
    }
    ADD_FAILURE() << "no seed in 1..1000";
}

// Writes tests/replays/m99_critical_wounds.json. Run on purpose only:
//   wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D101_Replay.DISABLED_RegenerateM99Replay
TEST(D101_Replay, DISABLED_RegenerateM99Replay) {
    bool ok = false;
    std::string problem;
    const Replay r = m99_replay(kM99Seed, ok, problem);
    ASSERT_TRUE(problem.empty()) << problem;
    ASSERT_TRUE(ok);
    const ReplayResult run = run_replay(r, repo_root());
    ASSERT_TRUE(run.ok()) << "step " << *run.failed_step << ": " << run.reasons.front();
    save_replay(r, kM99Replay);
}
