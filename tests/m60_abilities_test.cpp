// M60 (D-071): [HAZARDOUS] (24.15, 06.03, 06.02), [SUSTAINED HITS X] (24.36, 05.01), [EXTRA ATTACKS] (24.11,
// 04.01) and the refusal of every weapon ability the engine does not implement. Most tests use the M59
// Havocs vs Hive Tyrant fixture (tests/data/scenarios), with stand-in weapons where the rosters have none.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "core/data/datasheet.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/casualties.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::add_unit_copy;
using wh40k::test::contains;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::expect_valid;
using wh40k::test::in;
using wh40k::test::is_rule_cited;
using wh40k::test::model_ids;
using wh40k::test::rejection;
using wh40k::test::repo_root;
using wh40k::test::starts_with;

namespace {

constexpr std::size_t kHavocs = 0;
constexpr std::size_t kTyrant = 1;
// Havoc Champion: Plasma gun (0), Astartes chainsword (1). Havocs w/ lascannon: Havoc lascannon (0), Close
// combat weapon (1). Hive Tyrant: Monstrous bonesword and lash whip (0), Monstrous scything talons (1).
constexpr std::size_t kPlasmaGun = 0;
constexpr std::size_t kChainsword = 1;
constexpr std::size_t kBonesword = 0;
constexpr std::size_t kTalons = 1;

GameState havocs_fixture() {
    return load_scenario(repo_root() / wh40k::test::kHavocsVsTyrantFixture, repo_root());
}

// The index of the profile called `name` on `weapon`.
std::size_t profile_named(const Weapon& weapon, const std::string& name) {
    for (std::size_t i = 0; i < weapon.profiles.size(); ++i) {
        if (weapon.profiles[i].name == name) return i;
    }
    ADD_FAILURE() << "no profile " << name;
    return 0;
}

std::size_t supercharge(const GameState& gs) {
    return profile_named(gs.units[kHavocs].models[0].weapons[kPlasmaGun], "Plasma gun - supercharge");
}
std::size_t standard(const GameState& gs) {
    return profile_named(gs.units[kHavocs].models[0].weapons[kPlasmaGun], "Plasma gun - standard");
}

// Starts the game and remains stationary with every unit of the side whose turn it is, so that side's Shoot
// decision is pending.
void to_shoot_step(GameState& gs) {
    start_game(gs);
    while (gs.pending && std::holds_alternative<decision::MoveUnits>(gs.pending->kind)) {
        for (const GameAction& a : legal_actions(gs, decider(gs))) {
            const auto* m = std::get_if<action::StartMove>(&a);
            if (m != nullptr && m->move_type == MoveType::RemainStationary) {
                apply_action(gs, decider(gs), a);
                break;
            }
        }
    }
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::Shoot>(gs.pending->kind));
}

// Then on to the first SelectUnitToFight decision: no shooting, no charges, no pile-ins.
void to_fight_step(GameState& gs) {
    to_shoot_step(gs);
    for (int guard = 0; guard < 20 && gs.pending; ++guard) {
        const DecisionKind& k = gs.pending->kind;
        if (std::holds_alternative<decision::SelectUnitToFight>(k)) return;
        if (std::holds_alternative<decision::Shoot>(k)) apply_action(gs, decider(gs), action::EndShootStep{});
        else if (std::holds_alternative<decision::Charge>(k)) apply_action(gs, decider(gs), action::EndChargeStep{});
        else if (std::holds_alternative<decision::PileIn>(k)) apply_action(gs, decider(gs), action::EndPileIns{});
        else apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    }
    ADD_FAILURE() << "did not reach the Fight step";
}

// Puts the Hive Tyrant's base in contact with Havoc model `index`, north of it.
void tyrant_in_contact(GameState& gs, std::size_t index, std::size_t tyrant_unit = kTyrant) {
    const Model& havoc = gs.units[kHavocs].models.at(index);
    Model& tyrant = gs.units[tyrant_unit].models[0];
    tyrant.position = Position{havoc.position.x,
                               havoc.position.y + base_radius(havoc.base) + base_radius(tyrant.base), 0.0f};
}

action::Shoot champion_plasma(const GameState& gs, std::size_t profile) {
    return action::Shoot{gs.units[kHavocs].id, ShootingType::Normal,
                         {ShotDeclaration{gs.units[kHavocs].models[0].id, kPlasmaGun, profile, gs.units[kTyrant].id}}};
}

template <typename E>
std::ptrdiff_t index_of(const std::vector<GameEvent>& events) {
    const auto it = std::find_if(events.begin(), events.end(), [](const GameEvent& e) { return std::holds_alternative<E>(e); });
    return it == events.end() ? -1 : it - events.begin();
}

// Keeps only the Havoc Champion, and gives Side A a second Havocs unit 3" further south so a self-wipe does not
// end the game (D-007).
void champion_alone_with_a_spare_unit(GameState& gs) {
    const UnitId spare = add_unit_copy(gs, kHavocs);
    for (Model& m : gs.find_unit(spare)->models) m.position.y -= in(3);
    gs.units[kHavocs].models.resize(1);
}

}  // namespace

// ==== 24.15 [HAZARDOUS] ====

TEST(R24_15_Hazardous, AHazardRollIsMadeAfterAllOfTheUnitsAttacks) {
    GameState gs = havocs_fixture();
    to_shoot_step(gs);
    // Supercharge within half range: 2 attack dice (24.30), both miss; then one hazard roll that passes.
    ScriptedDice dice{1, 1, 3};
    const auto events = apply_action(gs, Side::A, champion_plasma(gs, supercharge(gs)), dice);
    EXPECT_EQ(dice.remaining(), 0u);
    const auto hazards = events_of<event::HazardRollsMade>(events);
    ASSERT_EQ(hazards.size(), 1u);
    EXPECT_EQ(hazards[0].unit, gs.units[kHavocs].id);
    EXPECT_EQ(hazards[0].rolls.rolls, (std::vector<int>{3}));
    EXPECT_EQ(hazards[0].rolls.mortal_wounds, 0);
    EXPECT_GT(index_of<event::HazardRollsMade>(events), index_of<event::WoundRollsMade>(events));
    EXPECT_FALSE(gs.flow.attack.has_value());  // the attacks are resolved, then the unit's activation ends
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
}

TEST(R24_15_Hazardous, AProfileWithoutHazardousMakesNoHazardRoll) {
    GameState gs = havocs_fixture();
    to_shoot_step(gs);
    ScriptedDice dice{1, 1};
    const auto events = apply_action(gs, Side::A, champion_plasma(gs, standard(gs)), dice);
    EXPECT_EQ(dice.remaining(), 0u);
    EXPECT_TRUE(events_of<event::HazardRollsMade>(events).empty());
}

TEST(R24_15_Hazardous, AFailedRollIsAMortalWoundThatTheAttackingUnitsPlayerAllocates) {
    GameState gs = havocs_fixture();
    to_shoot_step(gs);
    ScriptedDice dice{1, 1, 2};  // both miss; the hazard roll fails (06.03: 1-2)
    const auto events = apply_action(gs, Side::A, champion_plasma(gs, supercharge(gs)), dice);
    const UnitId havocs = gs.units[kHavocs].id;
    ASSERT_EQ(events_of<event::HazardRollsMade>(events).at(0).rolls.mortal_wounds, 1);
    ASSERT_TRUE(gs.flow.attack.has_value());
    EXPECT_EQ(gs.flow.attack->pending_mortal_wounds, 1);
    // 06.02 Select Model: the attacking unit's player answers one OrderCasualties (D-013), even when forced (D-051).
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{havocs}}));
    EXPECT_EQ(active_side(gs), Side::A);
    const auto legal = legal_actions(gs, Side::A);
    ASSERT_EQ(legal.size(), 1u);
    EXPECT_TRUE(is_template(legal[0]));
    // D-051's preview: any of the five (none is a CHARACTER or wounded), 1 damage.
    const auto preview = preview_casualties(gs, {});
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->options, model_ids(gs.units[kHavocs]));
    EXPECT_EQ(preview->next_damage, 1);
    EXPECT_EQ(preview->damage_left, 1);
    // A priority with the last lascannon first: it takes the mortal wound.
    std::vector<ModelId> priority = model_ids(gs.units[kHavocs]);
    std::reverse(priority.begin(), priority.end());
    const ModelId last = priority.front();
    ASSERT_TRUE(rejection(gs, Side::A, action::OrderCasualties{havocs, {}}).size() > 0);
    const auto after = apply_action(gs, Side::A, action::OrderCasualties{havocs, priority});
    const auto inflicted = events_of<event::MortalWoundsInflicted>(after);
    ASSERT_EQ(inflicted.size(), 1u);
    EXPECT_EQ(inflicted[0].unit, havocs);
    ASSERT_EQ(inflicted[0].result.steps.size(), 1u);
    EXPECT_EQ(inflicted[0].result.steps[0].model, last);
    EXPECT_EQ(find_model(gs, last)->wounds_remaining, 2);
    EXPECT_FALSE(gs.flow.attack.has_value());
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
}

TEST(R24_15_Hazardous, OneRollPerHazardousWeaponSelected) {
    GameState gs = havocs_fixture();
    // Stand-in: a second Havoc carries the champion's plasma gun as well as its lascannon.
    gs.units[kHavocs].models[1].weapons.push_back(gs.units[kHavocs].models[0].weapons[kPlasmaGun]);
    to_shoot_step(gs);
    const UnitId tyrant = gs.units[kTyrant].id;
    const ModelId champion = gs.units[kHavocs].models[0].id;
    const ModelId gunner = gs.units[kHavocs].models[1].id;
    const std::size_t sc = supercharge(gs);
    // Two [HAZARDOUS] weapons (two hazard rolls), one standard profile (none).
    action::Shoot two{gs.units[kHavocs].id, ShootingType::Normal,
                      {ShotDeclaration{champion, kPlasmaGun, sc, tyrant}, ShotDeclaration{gunner, 2, sc, tyrant}}};
    EXPECT_EQ(hazardous_weapons_selected(gs, make_weapon_selections(gs, gs.units[kHavocs], ShootingType::Normal, two.shots)), 2);
    ScriptedDice dice{1, 1, 1, 1, 4, 5};  // four misses (one batch of identical attacks), then two hazard rolls
    const auto events = apply_action(gs, Side::A, two, dice);
    EXPECT_EQ(dice.remaining(), 0u);
    EXPECT_EQ(events_of<event::HazardRollsMade>(events).at(0).rolls.rolls, (std::vector<int>{4, 5}));
    action::Shoot mixed = two;
    mixed.shots[1].profile_index = standard(gs);
    EXPECT_EQ(hazardous_weapons_selected(gs, make_weapon_selections(gs, gs.units[kHavocs], ShootingType::Normal, mixed.shots)),
              1);
}

TEST(R24_15_Hazardous, AMonsterUnitSuffersThreeMortalWoundsPerFailedRoll) {
    GameState gs = havocs_fixture();
    gs.first_turn = Side::B;  // the Hive Tyrant shoots first
    // Stand-in: the Tyrant carries the Havoc Champion's plasma gun.
    gs.units[kTyrant].models[0].weapons.push_back(gs.units[kHavocs].models[0].weapons[kPlasmaGun]);
    to_shoot_step(gs);
    const UnitId tyrant = gs.units[kTyrant].id;
    const action::Shoot shot{tyrant, ShootingType::Normal,
                             {ShotDeclaration{gs.units[kTyrant].models[0].id, 2, supercharge(gs), gs.units[kHavocs].id}}};
    ScriptedDice dice{1, 1, 1};
    const auto events = apply_action(gs, Side::B, shot, dice);
    EXPECT_EQ(events_of<event::HazardRollsMade>(events).at(0).rolls.mortal_wounds, 3);  // 06.03: every model a MONSTER
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::B, decision::OrderCasualties{tyrant}}));
    const auto after = apply_action(gs, Side::B, action::OrderCasualties{tyrant, model_ids(gs.units[kTyrant])});
    EXPECT_EQ(events_of<event::MortalWoundsInflicted>(after).at(0).result.steps.size(), 3u);
    EXPECT_EQ(gs.units[kTyrant].models[0].wounds_remaining, 7);
}

TEST(R24_15_Hazardous, TheRollsAreMadeWhenTheTargetIsDestroyedBeforeEveryAttackIsResolved) {
    // D-012: a batch whose target is gone is lost; the unit's attacks are still resolved, then the hazard roll.
    GameState gs = havocs_fixture();
    const UnitId second = add_unit_copy(gs, kTyrant);  // a second Hive Tyrant, so Side B still has models
    gs.find_unit(second)->models[0].position.x -= in(6);
    gs.units[kHavocs].models[1].weapons.push_back(gs.units[kHavocs].models[0].weapons[kPlasmaGun]);
    gs.units[kTyrant].models[0].wounds_remaining = 1;
    to_shoot_step(gs);
    const UnitId tyrant = gs.units[kTyrant].id;
    const ModelId champion = gs.units[kHavocs].models[0].id;
    const ModelId gunner = gs.units[kHavocs].models[1].id;
    // The champion's supercharge and the gunner's standard profile: two batches; the attacker picks the standard one
    // first, which kills the wounded Tyrant, so the supercharge batch is lost.
    const action::Shoot shot{gs.units[kHavocs].id, ShootingType::Normal,
                             {ShotDeclaration{champion, kPlasmaGun, supercharge(gs), tyrant},
                              ShotDeclaration{gunner, 2, standard(gs), tyrant}}};
    apply_action(gs, Side::A, shot);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::SelectAttackBatch{gs.units[kHavocs].id}}));
    // Standard: S7 vs T10 wounds on 5+; AP-2 vs Sv2+ / InSv4+ (4+). Hits 4, 4; wounds 5, 6; saves 1, 1.
    ScriptedDice dice{4, 4, 5, 6, 1, 1, 3};
    const auto events = apply_action(gs, Side::A, action::SelectAttackBatch{1}, dice);
    std::vector<GameEvent> all = events;
    if (gs.pending && std::holds_alternative<decision::OrderCasualties>(gs.pending->kind)) {
        const auto more = apply_action(gs, Side::B, action::OrderCasualties{tyrant, model_ids(gs.units[kTyrant])}, dice);
        all.insert(all.end(), more.begin(), more.end());
    }
    EXPECT_TRUE(is_destroyed(*gs.find_unit(tyrant)));
    ASSERT_EQ(events_of<event::AttacksLost>(all).size(), 1u);
    ASSERT_EQ(events_of<event::HazardRollsMade>(all).size(), 1u);
    EXPECT_EQ(events_of<event::HazardRollsMade>(all)[0].rolls.rolls, (std::vector<int>{3}));
    EXPECT_GT(index_of<event::HazardRollsMade>(all), index_of<event::AttacksLost>(all));
    EXPECT_EQ(dice.remaining(), 0u);
}

TEST(R24_15_Hazardous, NoRollsOnceTheLastEnemyModelIsDestroyed) {
    // D-071 / D-007: the game ends once the attacks are resolved, before any hazard roll.
    GameState gs = havocs_fixture();
    gs.units[kTyrant].models[0].wounds_remaining = 1;
    to_shoot_step(gs);
    ScriptedDice dice{4, 1, 6, 1};  // one hit; wound 6; save 1: the Tyrant is destroyed
    std::vector<GameEvent> events = apply_action(gs, Side::A, champion_plasma(gs, supercharge(gs)), dice);
    if (gs.pending && std::holds_alternative<decision::OrderCasualties>(gs.pending->kind)) {
        const auto more = wh40k::test::answer_casualties(gs);
        events.insert(events.end(), more.begin(), more.end());
    }
    EXPECT_EQ(dice.remaining(), 0u);
    EXPECT_TRUE(events_of<event::HazardRollsMade>(events).empty());
    ASSERT_TRUE(gs.result.has_value());
    EXPECT_EQ(gs.result->winner, Side::A);
}

TEST(R24_15_Hazardous, AUnitDestroyedByItsOwnHazardsEndsItsShootingCleanly) {
    GameState gs = havocs_fixture();
    champion_alone_with_a_spare_unit(gs);
    gs.units[kHavocs].models[0].wounds_remaining = 1;
    to_shoot_step(gs);
    const UnitId havocs = gs.units[kHavocs].id;
    ScriptedDice dice{1, 1, 1};
    apply_action(gs, Side::A, champion_plasma(gs, supercharge(gs)), dice);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{havocs}}));
    const auto events = apply_action(gs, Side::A, action::OrderCasualties{havocs, model_ids(gs.units[kHavocs])});
    EXPECT_EQ(events_of<event::UnitDestroyed>(events).size(), 1u);
    EXPECT_TRUE(is_destroyed(gs.units[kHavocs]));
    EXPECT_FALSE(gs.flow.attack.has_value());
    EXPECT_FALSE(gs.flow.activation.has_value());
    EXPECT_FALSE(gs.result.has_value());
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));  // the spare unit may still shoot
    EXPECT_EQ(Json(gs).get<GameState>(), gs);
}

TEST(R24_15_Hazardous, FightingWithAHazardousWeaponMakesTheRollAfterTheMeleeAttacks) {
    GameState gs = havocs_fixture();
    champion_alone_with_a_spare_unit(gs);
    tyrant_in_contact(gs, 0);
    // Stand-in: the champion's chainsword is [HAZARDOUS].
    gs.units[kHavocs].models[0].weapons[kChainsword].profiles[0].abilities.push_back(ability::Hazardous{});
    to_fight_step(gs);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::SelectUnitToFight{}}));
    const UnitId havocs = gs.units[kHavocs].id;
    apply_action(gs, Side::A, action::SelectToFight{havocs, FightType::Normal});
    const action::Fight fight = wh40k::test::first_melee_options(gs, havocs);
    ASSERT_EQ(fight.declarations.size(), 1u);
    EXPECT_EQ(fight.declarations[0].weapon_index, kChainsword);
    ScriptedDice dice{1, 1, 1, 1, 1, 6};  // five misses (A5), then the hazard roll passes
    const auto events = apply_action(gs, Side::A, fight, dice);
    EXPECT_EQ(dice.remaining(), 0u);
    ASSERT_EQ(events_of<event::HazardRollsMade>(events).size(), 1u);
    EXPECT_GT(index_of<event::HazardRollsMade>(events), index_of<event::HitRollsMade>(events));
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::SelectUnitToFight{}}));  // 12.04: players alternate
}

TEST(R24_15_Hazardous, ASplitMeleeWeaponCountsOnce) {
    GameState gs = havocs_fixture();
    gs.units[kHavocs].models.resize(1);
    const UnitId second = add_unit_copy(gs, kTyrant);
    tyrant_in_contact(gs, 0);
    {
        // The second Tyrant touches the champion from the east.
        const Model& champ = gs.units[kHavocs].models[0];
        Model& t2 = gs.find_unit(second)->models[0];
        t2.position = Position{champ.position.x + base_radius(champ.base) + base_radius(t2.base), champ.position.y, 0.0f};
    }
    gs.units[kHavocs].models[0].weapons[kChainsword].profiles[0].abilities.push_back(ability::Hazardous{});
    to_fight_step(gs);
    const UnitId havocs = gs.units[kHavocs].id;
    apply_action(gs, Side::A, action::SelectToFight{havocs, FightType::Normal});
    const MeleeDeclaration split{gs.units[kHavocs].models[0].id, kChainsword, 0,
                                 {MeleeTarget{gs.units[kTyrant].id, 3}, MeleeTarget{second, 2}}};
    const action::Fight fight{havocs, {split}};
    expect_valid(gs, Side::A, fight);
    const auto selections = make_melee_selections(gs, fight.declarations);
    ASSERT_EQ(selections.size(), 2u);  // 04.02: one selection per target
    EXPECT_EQ(hazardous_weapons_selected(gs, selections), 1);
    ScriptedDice dice(std::vector<int>{1, 1, 1, 1, 1, 5});
    std::vector<GameEvent> events = apply_action(gs, Side::A, fight, dice);
    while (gs.pending && std::holds_alternative<decision::SelectAttackBatch>(gs.pending->kind)) {
        const auto more = apply_action(gs, Side::A, legal_actions(gs, Side::A).front(), dice);
        events.insert(events.end(), more.begin(), more.end());
    }
    ASSERT_EQ(events_of<event::HazardRollsMade>(events).size(), 1u);
    EXPECT_EQ(events_of<event::HazardRollsMade>(events)[0].rolls.rolls.size(), 1u);
    EXPECT_EQ(dice.remaining(), 0u);
}

TEST(R24_15_Hazardous, AFighterDestroyedByItsOwnHazardsHandsTheFightToTheNextUnit) {
    GameState gs = havocs_fixture();
    champion_alone_with_a_spare_unit(gs);
    tyrant_in_contact(gs, 0);
    gs.units[kHavocs].models[0].wounds_remaining = 1;
    gs.units[kHavocs].models[0].weapons[kChainsword].profiles[0].abilities.push_back(ability::Hazardous{});
    to_fight_step(gs);
    const UnitId havocs = gs.units[kHavocs].id;
    apply_action(gs, Side::A, action::SelectToFight{havocs, FightType::Normal});
    ScriptedDice dice{1, 1, 1, 1, 1, 1};
    apply_action(gs, Side::A, wh40k::test::first_melee_options(gs, havocs), dice);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{havocs}}));
    apply_action(gs, Side::A, action::OrderCasualties{havocs, model_ids(gs.units[kHavocs])});
    EXPECT_TRUE(is_destroyed(gs.units[kHavocs]));
    EXPECT_FALSE(gs.flow.fight.has_value());
    EXPECT_FALSE(gs.result.has_value());
    // The Hive Tyrant was engaged at the start of the Fight step, so it is still eligible to fight (12.04).
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::B, decision::SelectUnitToFight{}}));
    expect_valid(gs, Side::B, action::SelectToFight{gs.units[kTyrant].id, FightType::Overrun});
}

TEST(R24_15_Hazardous, TheAttackFlowsHazardFieldsRoundTripAndAreOmittedWhenZero) {
    GameState gs = havocs_fixture();
    to_shoot_step(gs);
    ScriptedDice dice{1, 1, 1};
    apply_action(gs, Side::A, champion_plasma(gs, supercharge(gs)), dice);
    ASSERT_TRUE(gs.flow.attack.has_value());
    EXPECT_EQ(Json(gs).get<GameState>(), gs);
    const Json pending = Json(*gs.flow.attack);
    EXPECT_EQ(pending.at("pending_mortal_wounds"), 1);
    EXPECT_FALSE(pending.contains("hazard_rolls"));
    AttackFlow f = *gs.flow.attack;
    f.hazard_rolls = 2;
    f.pending_mortal_wounds = 0;
    EXPECT_EQ(Json(f).get<AttackFlow>(), f);
    EXPECT_EQ(Json(f).at("hazard_rolls"), 2);
    EXPECT_FALSE(Json(f).contains("pending_mortal_wounds"));
}

// ==== 24.36 [SUSTAINED HITS X] ====

namespace {

AttackProfile sustained_profile(int x, int skill = 3) {
    AttackProfile p;
    p.skill = skill;
    p.strength = 4;
    p.damage = DiceExpr::fixed(1);
    if (x > 0) p.attack_time_abilities = {ability::SustainedHits{x}};
    return p;
}

}  // namespace

TEST(R24_36_SustainedHits, EachCriticalHitScoresXAdditionalHitsThatAreNotCritical) {
    ScriptedDice dice{6, 6, 3, 1, 2};
    const HitRolls r = make_hit_rolls(dice, sustained_profile(1), 5);
    EXPECT_EQ(r.rolled_hits(), 3);
    EXPECT_EQ(r.critical_hits(), 2);
    EXPECT_EQ(r.sustained_hits, 2);
    EXPECT_EQ(r.hits(), 5);
    ScriptedDice two{6};
    EXPECT_EQ(make_hit_rolls(two, sustained_profile(2), 1).hits(), 3);  // the 24.36 example: three hits
    ScriptedDice none{6, 6};
    EXPECT_EQ(make_hit_rolls(none, sustained_profile(0), 2).sustained_hits, 0);
}

TEST(R24_36_SustainedHits, OnlyAnUnmodifiedSixIsACriticalHit) {
    AttackProfile p = sustained_profile(1);
    p.hit_modifier = 1;  // a 5 becomes a 6, but is not critical (05.01)
    ScriptedDice dice{5, 6};
    const HitRolls r = make_hit_rolls(dice, p, 2);
    EXPECT_EQ(r.critical_hits(), 1);
    EXPECT_EQ(r.sustained_hits, 1);
    EXPECT_EQ(r.hits(), 3);
}

TEST(R24_36_SustainedHits, TorrentsAutomaticHitsAreNotCritical) {
    AttackProfile p = sustained_profile(2);
    p.skill.reset();
    p.torrent = true;
    ScriptedDice dice(std::vector<int>{});
    const HitRolls r = make_hit_rolls(dice, p, 3);
    EXPECT_EQ(r.hits(), 3);
    EXPECT_EQ(r.sustained_hits, 0);
}

TEST(R24_36_SustainedHits, DuplicatedInstancesUseTheHighestX) {
    AttackProfile p = sustained_profile(1);
    p.attack_time_abilities.push_back(ability::SustainedHits{2});
    EXPECT_EQ(sustained_hits(p), 2);  // 24.02: the controlling player selects one (D-071: the best)
    EXPECT_EQ(sustained_hits(sustained_profile(0)), 0);
}

TEST(R24_36_SustainedHits, AWoundRollIsMadeForEverySustainedHit) {
    GameState gs = wh40k::test::shoot_step_state();
    for (Model& m : gs.units[0].models) m.weapons[0].profiles[0].abilities.push_back(ability::SustainedHits{1});
    const UnitId gants = gs.units[1].id;
    // One Intercessor on Focused Fire within half range: 2 dice (24.30). A 6 (critical: +1 hit) and a 1.
    ScriptedDice dice{6, 1, 1, 1};
    const auto events = begin_attacks(gs, dice, gs.units[0].id, AttackContext{AttackType::Shooting, false},
                                      {wh40k::test::select(gs, 0, 0, 0, 0, gants)});
    EXPECT_EQ(dice.remaining(), 0u);
    const auto hits = events_of<event::HitRollsMade>(events).at(0).rolls;
    EXPECT_EQ(hits.sustained_hits, 1);
    EXPECT_EQ(hits.hits(), 2);
    EXPECT_EQ(events_of<event::WoundRollsMade>(events).at(0).rolls.rolls.size(), 2u);  // 05.02: one per hit
}

TEST(R24_36_SustainedHits, ItIsAnAttackTimeAbilitySoItSplitsIdenticalAttacks) {
    // D-006: Sustained Hits still acts at the hit roll.
    WeaponProfile plain;
    plain.name = "gun";
    plain.range_mm = in(24);
    plain.attacks = DiceExpr::fixed(2);
    plain.skill = 3;
    plain.strength = 4;
    plain.damage = DiceExpr::fixed(1);
    WeaponProfile sustained = plain;
    sustained.abilities = {ability::SustainedHits{1}};
    const AttackContext c{AttackType::Shooting, false};
    EXPECT_NE(make_attack_profile(plain, c), make_attack_profile(sustained, c));
    EXPECT_EQ(sustained_hits(make_attack_profile(sustained, c)), 1);
}

TEST(R24_36_SustainedHits, HitRollsRoundTripWithTheSustainedHitsOmittedWhenZero) {
    HitRolls r;
    r.rolls = {HitRoll{6, 6, HitOutcome::CriticalHit}};
    EXPECT_FALSE(Json(r).contains("sustained_hits"));
    EXPECT_EQ(Json(r).get<HitRolls>(), r);
    r.sustained_hits = 2;
    EXPECT_EQ(Json(r).at("sustained_hits"), 2);
    EXPECT_EQ(Json(r).get<HitRolls>(), r);
}

// ==== 24.11 [EXTRA ATTACKS] ====

namespace {

// The fixture with the Hive Tyrant in base contact with the middle Havoc, Side B's turn first, at the Tyrant's
// DeclareMeleeAttacks decision.
GameState tyrant_declaring(void (*edit)(GameState&) = nullptr) {
    GameState gs = havocs_fixture();
    gs.first_turn = Side::B;
    tyrant_in_contact(gs, 2);
    if (edit != nullptr) edit(gs);
    to_fight_step(gs);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::SelectUnitToFight{}}));
    apply_action(gs, Side::B, action::SelectToFight{gs.units[kTyrant].id, FightType::Normal});
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::DeclareMeleeAttacks{gs.units[kTyrant].id}}));
    return gs;
}

MeleeDeclaration tyrant_with(const GameState& gs, std::size_t weapon) {
    const Model& t = gs.units[kTyrant].models[0];
    return MeleeDeclaration{t.id, weapon, 0,
                            {MeleeTarget{gs.units[kHavocs].id, t.weapons[weapon].profiles[0].attacks.fixed_value()}}};
}

// Stand-in: the bonesword without [TWIN-LINKED] (until M61 implements it), so the Tyrant has another usable
// melee weapon.
void usable_bonesword(GameState& gs) { gs.units[kTyrant].models[0].weapons[kBonesword].profiles[0].abilities.clear(); }

}  // namespace

TEST(R24_11_ExtraAttacks, AModelMustSelectItsExtraAttacksWeaponAndOneOtherMeleeWeapon) {
    const GameState gs = tyrant_declaring(usable_bonesword);
    const Unit& tyrant = gs.units[kTyrant];
    const std::string who = "model " + std::to_string(tyrant.models[0].id.v);
    const action::Fight both{tyrant.id, {tyrant_with(gs, kBonesword), tyrant_with(gs, kTalons)}};
    expect_valid(gs, Side::B, both);
    // Without the talons.
    const auto no_talons = rejection(gs, Side::B, action::Fight{tyrant.id, {tyrant_with(gs, kBonesword)}});
    EXPECT_TRUE(contains(no_talons, "24.11: " + who +
                                        " must select all of its [EXTRA ATTACKS] weapons ('Monstrous scything talons' "
                                        "is not selected)"))
        << no_talons.front();
    // The talons alone, when it has another melee weapon it can use.
    const auto talons_alone = rejection(gs, Side::B, action::Fight{tyrant.id, {tyrant_with(gs, kTalons)}});
    EXPECT_TRUE(contains(talons_alone, "24.11: " + who +
                                           " must also select one of its melee weapons other than its [EXTRA ATTACKS] "
                                           "weapons"))
        << talons_alone.front();
}

TEST(R24_11_ExtraAttacks, BothWeaponsAttackInTheirOwnBatches) {
    GameState gs = tyrant_declaring(usable_bonesword);
    const Unit& tyrant = gs.units[kTyrant];
    const action::Fight both{tyrant.id, {tyrant_with(gs, kBonesword), tyrant_with(gs, kTalons)}};
    ScriptedDice dice(std::vector<int>(10, 1));  // every attack misses
    std::vector<GameEvent> events = apply_action(gs, Side::B, both, dice);
    const auto gathered = events_of<event::AttackBatchesGathered>(events);
    ASSERT_EQ(gathered.size(), 1u);
    ASSERT_EQ(gathered[0].batches.size(), 2u);
    EXPECT_EQ(gathered[0].batches[0].attack_dice + gathered[0].batches[1].attack_dice, 6 + 4);
    while (gs.pending && std::holds_alternative<decision::SelectAttackBatch>(gs.pending->kind)) {
        const auto more = apply_action(gs, Side::B, legal_actions(gs, Side::B).front(), dice);
        events.insert(events.end(), more.begin(), more.end());
    }
    EXPECT_EQ(events_of<event::HitRollsMade>(events).size(), 2u);
    EXPECT_EQ(dice.remaining(), 0u);
}

TEST(R24_11_ExtraAttacks, TwoOtherMeleeWeaponsOrOneWeaponTwiceAreRefused) {
    const GameState gs = tyrant_declaring([](GameState& g) {
        usable_bonesword(g);
        Weapon second = g.units[kTyrant].models[0].weapons[kBonesword];  // stand-in: a second ordinary weapon
        second.name = "Second bonesword";
        g.units[kTyrant].models[0].weapons.push_back(second);
    });
    const Unit& tyrant = gs.units[kTyrant];
    const std::string who = "model " + std::to_string(tyrant.models[0].id.v);
    const auto two_others = rejection(
        gs, Side::B, action::Fight{tyrant.id, {tyrant_with(gs, kBonesword), tyrant_with(gs, kTalons), tyrant_with(gs, 2)}});
    EXPECT_TRUE(contains(two_others, "24.11: " + who +
                                         " must select its [EXTRA ATTACKS] weapons and only one of its other melee weapons"))
        << two_others.front();
    expect_valid(gs, Side::B, action::Fight{tyrant.id, {tyrant_with(gs, 2), tyrant_with(gs, kTalons)}});
    const auto twice = rejection(
        gs, Side::B, action::Fight{tyrant.id, {tyrant_with(gs, kBonesword), tyrant_with(gs, kTalons), tyrant_with(gs, kTalons)}});
    EXPECT_TRUE(starts_with(twice.front(), "04.01: " + who + " selected weapon 1 more than once")) << twice.front();
}

TEST(R24_11_ExtraAttacks, AModelWithOnlyItsExtraAttacksWeaponSelectsItAlone) {
    const GameState gs = tyrant_declaring([](GameState& g) {
        auto& weapons = g.units[kTyrant].models[0].weapons;
        weapons.erase(weapons.begin() + static_cast<std::ptrdiff_t>(kBonesword));  // stand-in: talons only
    });
    const Unit& tyrant = gs.units[kTyrant];
    expect_valid(gs, Side::B, action::Fight{tyrant.id, {tyrant_with(gs, 0)}});
    const auto none = rejection(gs, Side::B, action::Fight{tyrant.id, {}});
    EXPECT_TRUE(starts_with(none.front(), "D-021: ")) << none.front();
}

TEST(R24_11_ExtraAttacks, WhileItsOtherWeaponIsRefusedTheTyrantSelectsItsTalonsAlone) {
    // D-071: the bonesword is [TWIN-LINKED], refused until M61 (D-026), so it counts as absent: the talons alone
    // are a legal selection; with the bonesword the declaration is refused for the bonesword.
    const GameState gs = tyrant_declaring();
    const Unit& tyrant = gs.units[kTyrant];
    for (const MeleeDeclaration& d : melee_options(gs, tyrant)) EXPECT_EQ(d.weapon_index, kTalons);
    expect_valid(gs, Side::B, action::Fight{tyrant.id, {tyrant_with(gs, kTalons)}});
    const auto with_bonesword =
        rejection(gs, Side::B, action::Fight{tyrant.id, {tyrant_with(gs, kBonesword), tyrant_with(gs, kTalons)}});
    EXPECT_TRUE(starts_with(with_bonesword.front(), "24.38: ")) << with_bonesword.front();
}

TEST(R24_11_ExtraAttacks, ItIsUsedUpAtSelectionSoItDoesNotSplitIdenticalAttacks) {
    // D-006 / D-071: [EXTRA ATTACKS] acts in the Select Weapons step only.
    const GameState gs = havocs_fixture();
    const WeaponProfile& talons = gs.units[kTyrant].models[0].weapons[kTalons].profiles[0];
    ASSERT_TRUE(talons.has<ability::ExtraAttacks>());
    WeaponProfile plain = talons;
    plain.abilities.clear();
    const AttackContext c{AttackType::Fighting, false};
    EXPECT_EQ(make_attack_profile(talons, c), make_attack_profile(plain, c));
    EXPECT_TRUE(is_extra_attacks_weapon(gs.units[kTyrant].models[0].weapons[kTalons]));
    EXPECT_FALSE(is_extra_attacks_weapon(gs.units[kTyrant].models[0].weapons[kBonesword]));
}

// ==== D-071: abilities the engine does not implement are refused ====

TEST(D071_UnknownAbilities, AProfileWithAnUnknownAbilityIsRefusedWithARuleCitedReason) {
    WeaponProfile p;
    p.name = "Chainfist";
    p.kind = WeaponKind::Melee;
    p.attacks = DiceExpr::fixed(3);
    p.skill = 4;
    p.strength = 8;
    p.damage = DiceExpr::fixed(2);
    p.abilities = {ability::Unknown{"Anti-VEHICLE 3+"}};
    const auto problem = unimplemented_ability_problem(p);
    ASSERT_TRUE(problem.has_value());
    EXPECT_EQ(*problem, "D-071: 'Chainfist' has [ANTI-VEHICLE 3+], which the engine does not implement yet; weapons "
                        "with it can't be selected");
    EXPECT_TRUE(is_rule_cited(*problem));
    // The implemented abilities are not refused.
    p.abilities = {ability::SustainedHits{1}, ability::ExtraAttacks{}, ability::Hazardous{}};
    EXPECT_FALSE(unimplemented_ability_problem(p).has_value());
}

TEST(D071_UnknownAbilities, AnUnknownAbilityIsNeverOfferedForShootingAndDeclaringItIsRefused) {
    GameState gs = wh40k::test::shoot_step_state();
    for (Model& m : gs.units[0].models) {
        for (WeaponProfile& p : m.weapons[0].profiles) p.abilities.push_back(ability::Unknown{"Lethal Hits"});
    }
    const Unit& marines = gs.units[0];
    const auto options = shot_options(gs, marines, ShootingType::Normal);
    EXPECT_FALSE(options.empty());  // the Bolt pistols
    for (const ShotDeclaration& d : options) EXPECT_NE(d.weapon_index, 0u);
    const action::Shoot rifle{marines.id, ShootingType::Normal,
                              {ShotDeclaration{marines.models[0].id, 0, 0, gs.units[1].id}}};
    const auto reasons = rejection(gs, Side::A, rifle);
    EXPECT_TRUE(contains(reasons, "D-071: 'Bolt Rifle - Focused Fire' has [LETHAL HITS], which the engine does not "
                                  "implement yet; weapons with it can't be selected"))
        << reasons.front();
    // With every ranged profile refused, no Shoot template is listed (D-016).
    for (Model& m : gs.units[0].models) m.weapons[1].profiles[0].abilities.push_back(ability::Unknown{"Precision"});
    for (const GameAction& a : legal_actions(gs, Side::A)) EXPECT_FALSE(std::holds_alternative<action::Shoot>(a));
}

TEST(D071_UnknownAbilities, AModelWhoseOnlyMeleeWeaponIsRefusedCannotMakeMeleeAttacks) {
    // D-026 / D-071: like an unsupported weapon, so D-021 doesn't require it to declare one.
    GameState gs = tyrant_declaring();
    Unit& tyrant = gs.units[kTyrant];
    tyrant.models[0].weapons[kTalons].profiles[0].abilities.push_back(ability::Unknown{"Lethal Hits"});
    EXPECT_TRUE(melee_options(gs, tyrant).empty());
    EXPECT_FALSE(can_make_melee_attacks(gs, tyrant));
    const std::vector<MeleeDeclaration> talons{tyrant_with(gs, kTalons)};
    const auto reasons = melee_problems(gs, tyrant, talons);
    ASSERT_FALSE(reasons.empty());
    EXPECT_TRUE(starts_with(reasons.front(), "D-071: 'Monstrous scything talons' has [LETHAL HITS]")) << reasons.front();
}

TEST(D071_UnknownAbilities, TheAttackSequenceRefusesToResolveOne) {
    // The second safety net (D-026): never resolved wrongly, even if validation were bypassed.
    AttackProfile p = sustained_profile(0);
    p.attack_time_abilities = {ability::Unknown{"Lethal Hits"}};
    ScriptedDice dice{4};
    EXPECT_THROW((void)make_hit_rolls(dice, p, 1), std::logic_error);
}

TEST(D071_UnknownAbilities, EveryRosterProfileWithAnUnknownAbilityIsRefused) {
    int refused = 0;
    for (const auto& entry : std::filesystem::directory_iterator(repo_root() / "data/units")) {
        const Datasheet ds = load_datasheet(entry.path());
        for (const Weapon& w : ds.weapons) {
            for (const WeaponProfile& p : w.profiles) {
                if (!p.has<ability::Unknown>()) continue;
                const auto problem = unimplemented_ability_problem(p);
                ASSERT_TRUE(problem.has_value()) << ds.name << ": " << p.name;
                EXPECT_TRUE(is_rule_cited(*problem)) << *problem;
                ++refused;
            }
        }
    }
    EXPECT_GT(refused, 5);  // Lethal Hits, Anti-X, Melta, Ignores Cover, Psychic, ... on the roster datasheets
}

TEST(D071_UnknownAbilities, NoWeaponEquippedInTheOldScenariosIsRefused) {
    // The V1, bot and Strike loadouts have no ability the engine refuses, so their play is unchanged (D-071).
    for (const char* file : {"data/scenarios/v1_skirmish.json", "data/scenarios/bot_termagants_first.json",
                             "data/scenarios/bot_20_termagants.json", "data/scenarios/strike_44x60.json"}) {
        const GameState gs = load_scenario(repo_root() / file, repo_root());
        for (const Unit& u : gs.units) {
            for (const Model& m : u.models) {
                for (const Weapon& w : m.weapons) {
                    for (const WeaponProfile& p : w.profiles) {
                        EXPECT_FALSE(unimplemented_ability_problem(p).has_value()) << file << ": " << p.name;
                        EXPECT_FALSE(p.has<ability::Hazardous>() || p.has<ability::SustainedHits>() ||
                                     is_extra_attacks_weapon(w))
                            << file << ": " << p.name;
                    }
                }
            }
        }
    }
}
