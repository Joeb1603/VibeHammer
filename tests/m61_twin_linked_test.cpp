// M61 (D-072): [TWIN-LINKED] (24.38) with the attacker's re-roll decision (D-011 Re-rolls). The V1 Bolt Rifles get the
// ability as a stand-in (no V1 weapon has it); the Hive Tyrant's real bonesword is used in the Fight phase.

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::answer_casualties;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::expect_valid;
using wh40k::test::is_rule_cited;
using wh40k::test::model_ids;
using wh40k::test::rejection;
using wh40k::test::repo_root;
using wh40k::test::select;
using wh40k::test::shoot_step_state;
using wh40k::test::starts_with;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;
constexpr std::size_t kBoltRifle = 0;
constexpr std::size_t kSaturation = 1;  // S5 against the Termagants' T3: 3+ to wound
constexpr std::size_t kFleshborer = 0;
const AttackContext kShooting{AttackType::Shooting, true};

// Side A's Shoot step with every Bolt Rifle profile [TWIN-LINKED] (and [SUSTAINED HITS x] when x > 0), or plain.
GameState rifles(bool twin_linked, int sustained = 0) {
    GameState gs = shoot_step_state();
    for (Model& m : gs.units[kMarines].models) {
        for (WeaponProfile& p : m.weapons[kBoltRifle].profiles) {
            if (twin_linked) p.abilities.push_back(ability::TwinLinked{});
            if (sustained > 0) p.abilities.push_back(ability::SustainedHits{sustained});
        }
    }
    return gs;
}

// One Intercessor fires its Bolt Rifle on Saturation at the Termagants: 3 attack dice (A2, Rapid Fire 1 within half
// range), BS3+, then 3+ to wound.
std::vector<GameEvent> fire_one(GameState& gs, DiceSource& dice) {
    return begin_attacks(gs, dice, gs.units[kMarines].id, kShooting,
                         {select(gs, kMarines, 0, kBoltRifle, kSaturation, gs.units[kGants].id)});
}

PendingDecision reroll_decision(const GameState& gs) {
    return PendingDecision{Side::A, decision::RerollWoundRolls{gs.units[kMarines].id}};
}

std::vector<WoundOutcome> outcomes(const WoundRolls& r) {
    std::vector<WoundOutcome> out;
    for (const WoundRoll& w : r.rolls) out.push_back(w.outcome);
    return out;
}

AttackProfile profile_with_modifier(int wound_modifier) {
    AttackProfile p;
    p.skill = 3;
    p.strength = 4;
    p.damage = DiceExpr::fixed(1);
    p.wound_modifier = wound_modifier;
    p.attack_time_abilities = {ability::TwinLinked{}};
    return p;
}

// ---- the Havocs vs Hive Tyrant fixture (M59) ----

constexpr std::size_t kHavocs = 0;
constexpr std::size_t kTyrant = 1;
constexpr std::size_t kBonesword = 0;
constexpr std::size_t kTalons = 1;

GameState havocs_fixture() { return load_scenario(repo_root() / wh40k::test::kHavocsVsTyrantFixture, repo_root()); }

MeleeDeclaration tyrant_with(const GameState& gs, std::size_t weapon) {
    const Model& t = gs.units[kTyrant].models[0];
    return MeleeDeclaration{t.id, weapon, 0,
                            {MeleeTarget{gs.units[kHavocs].id, t.weapons[weapon].profiles[0].attacks.fixed_value()}}};
}

// The fixture with the Tyrant in base contact with the middle Havoc and Side A (the Havocs) taking the first turn,
// played to the Fight step of Side A's turn.
GameState engaged_in_side_a_turn() {
    GameState gs = havocs_fixture();
    const Model& havoc = gs.units[kHavocs].models[2];
    Model& tyrant = gs.units[kTyrant].models[0];
    tyrant.position = Position{havoc.position.x, havoc.position.y + base_radius(havoc.base) + base_radius(tyrant.base), 0};
    start_game(gs);
    for (int guard = 0; guard < 40 && gs.pending; ++guard) {
        const DecisionKind& k = gs.pending->kind;
        if (std::holds_alternative<decision::SelectUnitToFight>(k)) break;
        if (std::holds_alternative<decision::MoveUnits>(k)) {
            for (const GameAction& a : legal_actions(gs, decider(gs))) {
                const auto* m = std::get_if<action::StartMove>(&a);
                if (m != nullptr && m->move_type == MoveType::RemainStationary) {
                    apply_action(gs, decider(gs), a);
                    break;
                }
            }
        } else if (std::holds_alternative<decision::Shoot>(k)) {
            apply_action(gs, decider(gs), action::EndShootStep{});
        } else if (std::holds_alternative<decision::Charge>(k)) {
            apply_action(gs, decider(gs), action::EndChargeStep{});
        } else if (std::holds_alternative<decision::PileIn>(k)) {
            apply_action(gs, decider(gs), action::EndPileIns{});
        } else {
            apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
        }
    }
    EXPECT_TRUE(gs.pending && std::holds_alternative<decision::SelectUnitToFight>(gs.pending->kind));
    EXPECT_EQ(turn_side(gs), Side::A);
    return gs;
}

}  // namespace

// ==== 24.38: when the decision is asked ====

TEST(R24_38_TwinLinked, TheAttackerIsAskedWhenAWoundRollFails) {
    GameState gs = rifles(true);
    ScriptedDice dice{3, 3, 3,   // three hits (BS3+)
                      2, 4, 5};  // wound rolls (3+): the first fails
    const auto events = fire_one(gs, dice);
    EXPECT_EQ(dice.remaining(), 0u);
    ASSERT_EQ(events_of<event::WoundRollsMade>(events).size(), 1u);
    EXPECT_TRUE(events_of<event::SaveRollsMade>(events).empty());  // the batch waits before 05.03
    ASSERT_TRUE(gs.pending.has_value());
    EXPECT_EQ(*gs.pending, reroll_decision(gs));
    EXPECT_EQ(active_side(gs), Side::A);  // 01.03: the attacking unit's player
    EXPECT_TRUE(legal_actions(gs, Side::B).empty());
    // D-072: every failed wound roll first, then none.
    EXPECT_EQ(legal_actions(gs, Side::A),
              (std::vector<GameAction>{action::RerollWoundRolls{{0}}, action::RerollWoundRolls{}}));
    for (const GameAction& a : legal_actions(gs, Side::A)) EXPECT_FALSE(is_template(a));
    EXPECT_EQ(decision_rule(*gs.pending), "24.38");
    const std::string text = describe(gs, *gs.pending);
    EXPECT_EQ(text.substr(text.size() - 7), "(24.38)") << text;
    ASSERT_TRUE(gs.flow.attack && gs.flow.attack->wound_rerolls);
    EXPECT_EQ(gs.flow.attack->wound_rerolls->batch, 0u);
    EXPECT_EQ(outcomes(gs.flow.attack->wound_rerolls->rolls),
              (std::vector<WoundOutcome>{WoundOutcome::Fail, WoundOutcome::Wound, WoundOutcome::Wound}));
}

TEST(R24_38_TwinLinked, NoDecisionWhenEveryWoundRollSucceeds) {
    GameState gs = rifles(true);
    ScriptedDice dice{3, 3, 3,  // three hits
                      3, 4, 6,  // three wounds
                      6, 6, 6};  // the saves follow at once (6+ after AP-1: all saved)
    const auto events = fire_one(gs, dice);
    EXPECT_EQ(dice.remaining(), 0u);
    EXPECT_EQ(events_of<event::SaveRollsMade>(events).at(0).rolls, (std::vector<int>{6, 6, 6}));
    EXPECT_TRUE(events_of<event::WoundRollsRerolled>(events).empty());
    EXPECT_FALSE(gs.flow.attack.has_value());  // resolved: back to the Shoot decision
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
}

TEST(R24_38_TwinLinked, NoDecisionForABatchWithNoHits) {
    GameState gs = rifles(true);
    ScriptedDice dice{1, 2, 1};  // every hit roll fails: no wound rolls
    fire_one(gs, dice);
    EXPECT_EQ(dice.remaining(), 0u);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
}

// ==== the answer ====

TEST(R24_38_TwinLinked, ReRollAllFailed) {
    GameState gs = rifles(true);
    ScriptedDice dice{3, 3, 3,  // hits
                      1, 2, 5,  // wound rolls: the first two fail
                      6, 3,     // their re-rolls: a critical wound and a wound
                      1, 1, 1};  // three save rolls (all fail)
    fire_one(gs, dice);
    ASSERT_EQ(*gs.pending, reroll_decision(gs));
    const GameAction all_failed = legal_actions(gs, Side::A).front();
    EXPECT_EQ(all_failed, GameAction{(action::RerollWoundRolls{{0, 1}})});
    const auto events = apply_action(gs, Side::A, all_failed, dice);
    const auto rerolled = events_of<event::WoundRollsRerolled>(events);
    ASSERT_EQ(rerolled.size(), 1u);
    EXPECT_EQ(rerolled[0].batch, 0u);
    ASSERT_EQ(rerolled[0].rerolls.size(), 2u);
    EXPECT_EQ(rerolled[0].rerolls[0], (WoundReroll{0, WoundRoll{1, 1, WoundOutcome::Fail},
                                                   WoundRoll{6, 6, WoundOutcome::CriticalWound}}));
    EXPECT_EQ(rerolled[0].rerolls[1], (WoundReroll{1, WoundRoll{2, 2, WoundOutcome::Fail},
                                                   WoundRoll{3, 3, WoundOutcome::Wound}}));
    EXPECT_EQ(rerolled[0].rolls.wounds(), 3);
    EXPECT_EQ(rerolled[0].rolls.critical_wounds(), 1);  // 05.02: the re-rolled unmodified 6
    // Then 05.03 with three wounds; the re-roll event comes before the save rolls.
    EXPECT_EQ(events_of<event::SaveRollsMade>(events).at(0).rolls, (std::vector<int>{1, 1, 1}));
    EXPECT_FALSE(gs.flow.attack && gs.flow.attack->wound_rerolls);
    answer_casualties(gs);
    EXPECT_EQ(dice.remaining(), 0u);
    EXPECT_EQ(gs.units[kGants].models.size(), 7u);  // three Termagants slain
}

TEST(R24_38_TwinLinked, ReRollASubsetOfTheFailedRolls) {
    GameState gs = rifles(true);
    ScriptedDice dice{3, 3, 3, 1, 2, 5,
                      4,      // the re-roll of die 1 only
                      6, 6};  // two save rolls
    fire_one(gs, dice);
    const auto events = apply_action(gs, Side::A, action::RerollWoundRolls{{1}}, dice);
    const auto rerolled = events_of<event::WoundRollsRerolled>(events);
    ASSERT_EQ(rerolled.at(0).rerolls.size(), 1u);
    EXPECT_EQ(rerolled[0].rerolls[0].index, 1u);
    EXPECT_EQ(outcomes(rerolled[0].rolls),
              (std::vector<WoundOutcome>{WoundOutcome::Fail, WoundOutcome::Wound, WoundOutcome::Wound}));
    EXPECT_EQ(events_of<event::SaveRollsMade>(events).at(0).rolls.size(), 2u);
    EXPECT_EQ(dice.remaining(), 0u);
}

TEST(R24_38_TwinLinked, ASuccessfulWoundRollCanBeReRolledToo) {
    // 24.38: "you can re-roll the wound roll", of any attack made with the weapon: a success too (D-072).
    GameState gs = rifles(true);
    ScriptedDice dice{3, 3, 3, 1, 2, 5,
                      1};  // the success (die 2) re-rolled into a 1
    fire_one(gs, dice);
    const auto events = apply_action(gs, Side::A, action::RerollWoundRolls{{2}}, dice);
    const auto rerolled = events_of<event::WoundRollsRerolled>(events);
    ASSERT_EQ(rerolled.at(0).rerolls.size(), 1u);
    EXPECT_EQ(rerolled[0].rerolls[0].before.outcome, WoundOutcome::Wound);
    EXPECT_EQ(rerolled[0].rerolls[0].after.outcome, WoundOutcome::Fail);
    EXPECT_EQ(rerolled[0].rolls.wounds(), 0);
    EXPECT_TRUE(events_of<event::SaveRollsMade>(events).empty());  // nothing wounded: the batch is done
    EXPECT_FALSE(gs.flow.attack.has_value());
    EXPECT_EQ(dice.remaining(), 0u);
}

TEST(R24_38_TwinLinked, KeepReRollsNothing) {
    GameState gs = rifles(true);
    ScriptedDice dice{3, 3, 3, 1, 2, 5,
                      6};  // the one save roll: no dice are rolled for "Keep"
    fire_one(gs, dice);
    const WoundRolls before = gs.flow.attack->wound_rerolls->rolls;
    const auto events = apply_action(gs, Side::A, action::RerollWoundRolls{}, dice);
    const auto rerolled = events_of<event::WoundRollsRerolled>(events);
    ASSERT_EQ(rerolled.size(), 1u);  // D-072: the choice is in the log
    EXPECT_TRUE(rerolled[0].rerolls.empty());
    EXPECT_EQ(rerolled[0].rolls, before);
    EXPECT_EQ(events_of<event::SaveRollsMade>(events).at(0).rolls, (std::vector<int>{6}));
    EXPECT_EQ(dice.remaining(), 0u);
}

TEST(R24_38_TwinLinked, TheOrderOfTheIndicesDoesNotMatter) {
    // The re-rolls are made in ascending index order whatever order the indices are listed in.
    for (const std::vector<std::size_t>& order : {std::vector<std::size_t>{0, 1}, std::vector<std::size_t>{1, 0}}) {
        GameState gs = rifles(true);
        ScriptedDice dice{3, 3, 3, 1, 2, 5, 4, 1, 6, 6};
        fire_one(gs, dice);
        const auto events = apply_action(gs, Side::A, action::RerollWoundRolls{order}, dice);
        const auto r = events_of<event::WoundRollsRerolled>(events).at(0);
        EXPECT_EQ(r.rerolls.at(0).index, 0u);
        EXPECT_EQ(r.rerolls.at(0).after.unmodified, 4);
        EXPECT_EQ(r.rerolls.at(1).index, 1u);
        EXPECT_EQ(r.rerolls.at(1).after.unmodified, 1);
    }
}

// ==== D-011: re-roll the raw die once, then apply modifiers ====

TEST(R24_38_TwinLinked, ModifiersApplyAfterTheReRoll) {
    // S4 against T4: 4+ to wound.
    WoundRolls w;
    w.required = 4;
    w.rolls = {wound_roll_result(1, -1, 4), wound_roll_result(2, -1, 4), wound_roll_result(3, -1, 4)};
    ScriptedDice dice{4, 6, 1};
    const auto rerolls = reroll_wound_rolls(dice, profile_with_modifier(-1), w, std::vector<std::size_t>{0, 1, 2});
    ASSERT_EQ(rerolls.size(), 3u);
    EXPECT_EQ(w.rolls[0], (WoundRoll{4, 3, WoundOutcome::Fail}));           // 4 - 1 = 3 < 4+
    EXPECT_EQ(w.rolls[1], (WoundRoll{6, 5, WoundOutcome::CriticalWound}));  // an unmodified 6 is critical
    EXPECT_EQ(w.rolls[2], (WoundRoll{1, 1, WoundOutcome::Fail}));           // floor 1; an unmodified 1 fails
    // +2 is capped to +1 (D-011): a re-rolled 3 becomes 4 and wounds; a re-rolled 1 still fails.
    WoundRolls up;
    up.required = 4;
    up.rolls = {wound_roll_result(2, 1, 4), wound_roll_result(2, 1, 4)};
    ScriptedDice more{3, 1};
    (void)reroll_wound_rolls(more, profile_with_modifier(2), up, std::vector<std::size_t>{0, 1});
    EXPECT_EQ(up.rolls[0], (WoundRoll{3, 4, WoundOutcome::Wound}));
    EXPECT_EQ(up.rolls[1], (WoundRoll{1, 2, WoundOutcome::Fail}));
}

TEST(R24_38_TwinLinked, ADieCanBeReRolledOnlyOnce) {
    WoundRolls w;
    w.required = 3;
    w.rolls = {wound_roll_result(2, 0, 3), wound_roll_result(1, 0, 3)};
    ScriptedDice dice{4, 4};
    EXPECT_THROW((void)reroll_wound_rolls(dice, profile_with_modifier(0), w, std::vector<std::size_t>{1, 1}),
                 std::logic_error);  // a programmer error: validation refuses it first
    EXPECT_THROW((void)reroll_wound_rolls(dice, profile_with_modifier(0), w, std::vector<std::size_t>{2}),
                 std::logic_error);
    EXPECT_EQ(dice.consumed(), 0u);
    EXPECT_EQ(failed_wound_rolls(w), (std::vector<std::size_t>{0, 1}));
}

TEST(R24_38_TwinLinked, ACriticalWoundFromAReRolledSix) {
    GameState gs = rifles(true);
    ScriptedDice dice{3, 3, 3, 1, 3, 3, 6, 6, 6, 6};
    fire_one(gs, dice);
    const auto events = apply_action(gs, Side::A, legal_actions(gs, Side::A).front(), dice);
    const auto r = events_of<event::WoundRollsRerolled>(events).at(0);
    EXPECT_EQ(r.rerolls.at(0).after.outcome, WoundOutcome::CriticalWound);
    EXPECT_EQ(r.rolls.critical_wounds(), 1);
}

// ==== [SUSTAINED HITS] with [TWIN-LINKED] ====

TEST(R24_38_TwinLinked, TheWoundRollsOfSustainedHitsCanBeReRolled) {
    // D-072: 24.36's extra hits are hits of the same Twin-linked attack, so all four wound rolls can be re-rolled.
    GameState gs = rifles(true, 1);
    ScriptedDice dice{6, 3, 3,     // a critical hit (+1 sustained hit) and two hits: 4 hits
                      1, 1, 1, 1,  // four wound rolls, all failed
                      3, 3, 3, 2,  // the re-rolls of all four (the last fails again)
                      1, 1, 1};    // three save rolls
    const auto first = fire_one(gs, dice);
    EXPECT_EQ(events_of<event::HitRollsMade>(first).at(0).rolls.sustained_hits, 1);
    EXPECT_EQ(events_of<event::WoundRollsMade>(first).at(0).rolls.rolls.size(), 4u);
    ASSERT_EQ(*gs.pending, reroll_decision(gs));
    EXPECT_EQ(legal_actions(gs, Side::A).front(), GameAction{(action::RerollWoundRolls{{0, 1, 2, 3}})});
    const auto events = apply_action(gs, Side::A, legal_actions(gs, Side::A).front(), dice);
    EXPECT_EQ(events_of<event::WoundRollsRerolled>(events).at(0).rerolls.size(), 4u);
    EXPECT_EQ(events_of<event::WoundRollsRerolled>(events).at(0).rolls.wounds(), 3);
    answer_casualties(gs);
    EXPECT_EQ(dice.remaining(), 0u);
}

// ==== the dice stream of everything else is unchanged ====

TEST(R24_38_TwinLinked, WithoutTheAbilityTheDiceStreamIsUnchanged) {
    // A failed wound roll without [TWIN-LINKED]: no decision, the next dice are the save rolls.
    {
        GameState gs = rifles(false);
        ScriptedDice dice{3, 3, 3, 2, 4, 5, 6, 6};
        const auto events = fire_one(gs, dice);
        EXPECT_EQ(dice.remaining(), 0u);
        EXPECT_EQ(events_of<event::SaveRollsMade>(events).at(0).rolls, (std::vector<int>{6, 6}));
        EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
    }
    // [TWIN-LINKED] with no failed wound roll gives the same events, dice and result as without it.
    const std::vector<int> faces{3, 3, 3, 3, 4, 5, 1, 2, 6};
    GameState plain = rifles(false);
    GameState twin = rifles(true);
    ScriptedDice d1(faces);
    ScriptedDice d2(faces);
    std::vector<GameEvent> e1 = fire_one(plain, d1);
    std::vector<GameEvent> e2 = fire_one(twin, d2);
    if (plain.pending && std::holds_alternative<decision::OrderCasualties>(plain.pending->kind)) {
        const auto more1 = answer_casualties(plain);
        const auto more2 = answer_casualties(twin);
        e1.insert(e1.end(), more1.begin(), more1.end());
        e2.insert(e2.end(), more2.begin(), more2.end());
    }
    EXPECT_EQ(d1.consumed(), d2.consumed());
    const auto strip = [](std::vector<GameEvent> events) {
        std::erase_if(events, [](const GameEvent& e) { return std::holds_alternative<event::AttackBatchesGathered>(e); });
        return events;
    };
    EXPECT_EQ(strip(e1), strip(e2));  // only the gathered batch's profile differs (its abilities)
    EXPECT_EQ(plain.units[kGants].models, twin.units[kGants].models);
    EXPECT_EQ(plain.pending, twin.pending);
}

// ==== the Fight phase: the attacking player is not the player whose turn it is ====

TEST(R24_38_TwinLinked, InTheFightPhaseThePlayerWhoseTurnItIsNotAnswers) {
    GameState gs = engaged_in_side_a_turn();
    for (int guard = 0; guard < 6 && gs.pending; ++guard) {
        if (!std::holds_alternative<decision::SelectUnitToFight>(gs.pending->kind)) break;
        if (gs.pending->side == Side::B) break;
        // Side A's Havocs fight first if it is their pick: every attack misses.
        apply_action(gs, Side::A, action::SelectToFight{gs.units[kHavocs].id, FightType::Normal});
        ScriptedDice misses(std::vector<int>(64, 1));
        apply_action(gs, Side::A, wh40k::test::first_melee_options(gs, gs.units[kHavocs].id), misses);
        while (gs.flow.attack && gs.pending) apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front(), misses);
    }
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::B, decision::SelectUnitToFight{}}));
    apply_action(gs, Side::B, action::SelectToFight{gs.units[kTyrant].id, FightType::Normal});
    // 24.11 (D-071): the bonesword ([TWIN-LINKED], usable since M61) and the talons.
    apply_action(gs, Side::B, action::Fight{gs.units[kTyrant].id, {tyrant_with(gs, kBonesword), tyrant_with(gs, kTalons)}});
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::B, decision::SelectAttackBatch{gs.units[kTyrant].id}}));
    ScriptedDice dice{2, 2, 2, 2, 2, 2,  // the bonesword's six hits (WS2+)
                      1, 3, 3, 3, 3, 3};  // wound rolls (S9 against T5: 3+): the first fails
    apply_action(gs, Side::B, action::SelectAttackBatch{0}, dice);
    EXPECT_EQ(dice.remaining(), 0u);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::B, decision::RerollWoundRolls{gs.units[kTyrant].id}}));
    EXPECT_EQ(turn_side(gs), Side::A);
    EXPECT_EQ(active_side(gs), Side::B);  // 01.03: the unit fighting
    EXPECT_TRUE(legal_actions(gs, Side::A).empty());
    EXPECT_FALSE(validate_action(gs, Side::A, action::RerollWoundRolls{{0}}).ok());
    ScriptedDice rest{4,                  // the re-roll: a wound
                      1, 1, 1, 1, 1, 1};  // six save rolls
    const auto after = apply_action(gs, Side::B, action::RerollWoundRolls{{0}}, rest);
    EXPECT_EQ(events_of<event::WoundRollsRerolled>(after).at(0).rolls.wounds(), 6);
    EXPECT_EQ(rest.remaining(), 0u);
    ASSERT_TRUE(gs.pending.has_value());
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{gs.units[kHavocs].id}}));
}

// ==== validation ====

TEST(R24_38_TwinLinked, ValidationRefusesUnknownAndRepeatedDice) {
    GameState gs = rifles(true);
    ScriptedDice dice{3, 3, 3, 2, 4, 5};
    fire_one(gs, dice);
    ASSERT_EQ(*gs.pending, reroll_decision(gs));
    const auto out_of_range = rejection(gs, Side::A, action::RerollWoundRolls{{3}});
    EXPECT_EQ(out_of_range.at(0), "24.38: there is no wound roll 3; attack batch 0 made 3 wound rolls (0 to 2)");
    const auto twice = rejection(gs, Side::A, action::RerollWoundRolls{{0, 0}});
    EXPECT_EQ(twice.at(0), "D-011: wound roll 0 is listed twice; a dice can't be re-rolled more than once");
    const auto other_side = rejection(gs, Side::B, action::RerollWoundRolls{{0}});
    EXPECT_TRUE(starts_with(other_side.at(0), "24.38: waiting for Side A: choose the wound rolls to re-roll"))
        << other_side[0];
    for (const auto& reasons : {out_of_range, twice, other_side}) {
        for (const std::string& r : reasons) EXPECT_TRUE(is_rule_cited(r)) << r;
    }
    // Any subset of distinct dice is valid, in any order, successes included.
    for (const std::vector<std::size_t>& ok : {std::vector<std::size_t>{}, std::vector<std::size_t>{1},
                                               std::vector<std::size_t>{2, 0}, std::vector<std::size_t>{0, 1, 2}}) {
        expect_valid(gs, Side::A, action::RerollWoundRolls{ok});
    }
    // The action answers only this decision.
    GameState shoot = rifles(true);
    const auto not_now = rejection(shoot, Side::A, action::RerollWoundRolls{});
    EXPECT_TRUE(starts_with(not_now.at(0), "24.38: RerollWoundRolls is not possible now")) << not_now[0];
}

// ==== the weapons are offered now (D-026 amended) ====

TEST(R24_38_TwinLinked, TwinLinkedWeaponsAreOfferedAndSelectable) {
    // The Termagants' fleshborer with [TWIN-LINKED] (the spinefist's ability) is offered for shooting.
    GameState gs = shoot_step_state(Side::B);
    for (Model& m : gs.units[kGants].models) m.weapons[kFleshborer].profiles[0].abilities.push_back(ability::TwinLinked{});
    EXPECT_FALSE(unimplemented_ability_problem(gs.units[kGants].models[0].weapons[kFleshborer].profiles[0]).has_value());
    EXPECT_FALSE(shot_options(gs, gs.units[kGants], ShootingType::Normal).empty());
    // The Hive Tyrant's real bonesword is a melee option once it is engaged.
    GameState close = havocs_fixture();
    const Model& havoc = close.units[kHavocs].models[2];
    Model& tyrant = close.units[kTyrant].models[0];
    tyrant.position = Position{havoc.position.x, havoc.position.y + base_radius(havoc.base) + base_radius(tyrant.base), 0};
    bool bonesword = false;
    for (const MeleeDeclaration& d : melee_options(close, close.units[kTyrant])) bonesword |= d.weapon_index == kBonesword;
    EXPECT_TRUE(bonesword);
}

TEST(D072_TwinLinked, NoWeaponEquippedInTheOldScenariosIsTwinLinked) {
    // V1, both bot scenarios and Strike: their play can't meet the new decision, so it is unchanged (D-072).
    for (const char* file : {"data/scenarios/v1_skirmish.json", "data/scenarios/bot_termagants_first.json",
                             "data/scenarios/bot_20_termagants.json", "data/scenarios/strike_44x60.json"}) {
        const GameState gs = load_scenario(repo_root() / file, repo_root());
        for (const Unit& u : gs.units) {
            for (const Model& m : u.models) {
                for (const Weapon& w : m.weapons) {
                    for (const WeaponProfile& p : w.profiles) EXPECT_FALSE(p.has<ability::TwinLinked>()) << file << ": " << p.name;
                }
            }
        }
    }
}

// ==== D-072: JSON ====

TEST(D072_TwinLinked, ThePendingReRollRoundTripsAndIsOmittedWhenAbsent) {
    GameState gs = rifles(true);
    {
        const Json before = Json(gs);
        EXPECT_FALSE(before.dump().find("wound_rerolls") != std::string::npos);
    }
    ScriptedDice dice{3, 3, 3, 1, 4, 6};
    fire_one(gs, dice);
    ASSERT_TRUE(gs.flow.attack && gs.flow.attack->wound_rerolls);
    const Json j = Json(gs);
    ASSERT_TRUE(j.at("flow").at("attack").contains("wound_rerolls"));
    EXPECT_EQ(j.at("flow").at("attack").at("wound_rerolls").at("batch"), 0);
    EXPECT_EQ(j.at("pending").at("kind"), "RerollWoundRolls");
    EXPECT_EQ(j.at("pending").at("attacker"), gs.units[kMarines].id.v);
    EXPECT_EQ(j.get<GameState>(), gs);
    EXPECT_EQ(Json(*gs.flow.attack).get<AttackFlow>(), *gs.flow.attack);
    // Answered: the field is gone again.
    ScriptedDice rest{5, 6, 6, 6};
    apply_action(gs, Side::A, legal_actions(gs, Side::A).front(), rest);
    EXPECT_EQ(Json(gs).dump().find("wound_rerolls"), std::string::npos);
}

TEST(D072_TwinLinked, AnAttackFlowWrittenBeforeM61LoadsTheSame) {
    // An attack flow without the field (every file before M61), and one with it null, load with nothing pending.
    GameState gs = rifles(false);
    ScriptedDice dice{3, 3, 3, 3, 4, 5, 1, 1, 1};
    fire_one(gs, dice);
    ASSERT_TRUE(gs.flow.attack.has_value());
    Json j = Json(*gs.flow.attack);
    EXPECT_FALSE(j.contains("wound_rerolls"));
    EXPECT_FALSE(j.get<AttackFlow>().wound_rerolls.has_value());
    EXPECT_EQ(j.get<AttackFlow>(), *gs.flow.attack);
    j["wound_rerolls"] = nullptr;
    EXPECT_EQ(j.get<AttackFlow>(), *gs.flow.attack);
}

TEST(D072_TwinLinked, TheActionDecisionAndEventHaveTheirJsonNames) {
    Json a;
    action::to_json(a, GameAction{action::RerollWoundRolls{{2, 0}}});
    EXPECT_EQ(a, Json::parse(R"({"kind":"RerollWoundRolls","indices":[2,0]})"));
    Json keep;
    action::to_json(keep, GameAction{action::RerollWoundRolls{}});
    EXPECT_EQ(keep, Json::parse(R"({"kind":"RerollWoundRolls","indices":[]})"));
    GameAction back;
    action::from_json(keep, back);
    EXPECT_EQ(back, GameAction{action::RerollWoundRolls{}});
    EXPECT_EQ(action_rule(GameAction{action::RerollWoundRolls{}}), "24.38");
    const Json d = Json(PendingDecision{Side::B, decision::RerollWoundRolls{UnitId{7}}});
    EXPECT_EQ(d, Json::parse(R"({"side":"B","kind":"RerollWoundRolls","attacker":7})"));
    WoundRolls rolls;
    rolls.required = 3;
    rolls.rolls = {WoundRoll{5, 5, WoundOutcome::Wound}};
    const GameEvent e = event::WoundRollsRerolled{0, {WoundReroll{0, WoundRoll{1, 1, WoundOutcome::Fail}, rolls.rolls[0]}}, rolls};
    Json je;
    event::to_json(je, e);
    EXPECT_EQ(je.at("kind"), "WoundRollsRerolled");
    EXPECT_EQ(je.at("rule"), "24.38");
    EXPECT_EQ(je.at("rerolls").at(0).at("index"), 0);
    EXPECT_EQ(je.at("rerolls").at(0).at("before").at("unmodified"), 1);
    GameEvent eb;
    event::from_json(je, eb);
    EXPECT_EQ(eb, e);
}

// ==== D-072: the pure 05 function asks the same question ====

TEST(D072_TwinLinked, ResolveAttackBatchReRollsAsTheEngineDoes) {
    const std::vector<int> faces{3, 3, 3, 1, 2, 5, 6, 3, 1, 4, 1};
    GameState gs = rifles(true);
    ScriptedDice engine_dice(faces);
    std::vector<GameEvent> events = fire_one(gs, engine_dice);
    const auto rest = apply_action(gs, Side::A, legal_actions(gs, Side::A).front(), engine_dice);
    events.insert(events.end(), rest.begin(), rest.end());
    const auto more = answer_casualties(gs);
    events.insert(events.end(), more.begin(), more.end());

    GameState fresh = rifles(true);
    const std::vector<AttackBatch> batches = gather_attack_batches(
        fresh, std::vector<WeaponSelection>{select(fresh, kMarines, 0, kBoltRifle, kSaturation, fresh.units[kGants].id)},
        kShooting);
    ScriptedDice pure_dice(faces);
    int asked = 0;
    const WoundRerollChooser all_failed = [&asked](const WoundRolls& w) {
        ++asked;
        return failed_wound_rolls(w);
    };
    const ModelChooser first = [](const Unit&, std::span<const ModelId> options) { return options.front(); };
    const AllocationOrderChooser order = [](const Unit&, std::span<const AllocationGroup>,
                                            std::span<const std::vector<std::size_t>> legal) { return legal.front(); };
    Unit target = fresh.units[kGants];
    const BatchResult r = resolve_attack_batch(pure_dice, target, batches.at(0), order, first, all_failed);
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(pure_dice.consumed(), engine_dice.consumed());
    EXPECT_EQ(r.wounds, events_of<event::WoundRollsRerolled>(events).at(0).rolls);
    EXPECT_EQ(r.rerolls, events_of<event::WoundRollsRerolled>(events).at(0).rerolls);
    EXPECT_EQ(r.save_rolls, events_of<event::SaveRollsMade>(events).at(0).rolls);
    EXPECT_EQ(target.models.size(), gs.units[kGants].models.size());

    // Without a chooser, a Twin-linked batch with a failed wound roll is a programmer error; with none failed it is
    // not asked.
    Unit again = fresh.units[kGants];
    ScriptedDice failing(faces);
    EXPECT_THROW((void)resolve_attack_batch(failing, again, batches.at(0), order, first), std::logic_error);
    ScriptedDice all_wound{3, 3, 3, 3, 3, 3, 6, 6, 6};
    Unit third = fresh.units[kGants];
    (void)resolve_attack_batch(all_wound, third, batches.at(0), order, first, all_failed);
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(all_wound.remaining(), 0u);
}

// ==== M61: the casualty decision cites the rule it answers (the M60 carry-over) ====

TEST(D072_DecisionRule, TheCasualtyDecisionCites0602ForMortalWoundsAnd0504ForSaves) {
    // No attack in progress (Desperate Escape, 09.07): 06.02. An attack with pending mortal wounds ([HAZARDOUS],
    // 24.15): 06.02. An attack's failed saves: 05.04. The one-argument form keeps 05.04.
    GameState gs = rifles(false);
    const PendingDecision casualties{Side::B, decision::OrderCasualties{gs.units[kGants].id}};
    EXPECT_EQ(decision_rule(casualties), "05.04");
    EXPECT_EQ(decision_rule(gs, casualties), "06.02");
    ScriptedDice dice{3, 3, 3, 3, 4, 5, 1, 1, 1};
    fire_one(gs, dice);
    ASSERT_EQ(*gs.pending, casualties);
    EXPECT_EQ(decision_rule(gs, *gs.pending), "05.04");
    EXPECT_EQ(describe(gs, *gs.pending), describe(*gs.pending));
    gs.flow.attack->pending_mortal_wounds = 1;  // as after a failed hazard roll (only the rule lookup is tested here)
    EXPECT_EQ(decision_rule(gs, *gs.pending), "06.02");
    const std::string text = describe(gs, *gs.pending);
    EXPECT_EQ(text.substr(text.size() - 7), "(06.02)") << text;
    // Other decisions keep their rule.
    EXPECT_EQ(decision_rule(gs, PendingDecision{Side::A, decision::Shoot{}}), "10.02");
    EXPECT_EQ(decision_rule(gs, reroll_decision(gs)), "24.38");
}

TEST(D072_DecisionRule, AHazardMortalWoundDecisionIsDescribedAs0602) {
    // The Havocs' champion fires its plasma gun on supercharge; the hazard roll fails (24.15): Side A allocates the
    // mortal wound (06.02), and validation's "waiting for" text and the casualty messages say 06.02.
    GameState gs = havocs_fixture();
    start_game(gs);
    apply_action(gs, Side::A, action::StartMove{gs.units[kHavocs].id, MoveType::RemainStationary, std::nullopt});
    const Weapon& plasma = gs.units[kHavocs].models[0].weapons[0];
    const std::size_t supercharge = plasma.profiles[1].has<ability::Hazardous>() ? 1 : 0;
    ScriptedDice dice{1, 1, 2};  // both shots miss; the hazard roll fails
    apply_action(gs, Side::A,
                 action::Shoot{gs.units[kHavocs].id, ShootingType::Normal,
                               {ShotDeclaration{gs.units[kHavocs].models[0].id, 0, supercharge, gs.units[kTyrant].id}}},
                 dice);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{gs.units[kHavocs].id}}));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "06.02");
    const auto wrong_side = rejection(gs, Side::B, action::OrderCasualties{gs.units[kHavocs].id, {}});
    const std::string unit = "unit " + std::to_string(gs.units[kHavocs].id.v);
    EXPECT_TRUE(starts_with(wrong_side.at(0), "06.02: waiting for Side A: order the casualties for " + unit + " (06.02)"))
        << wrong_side[0];
    const auto empty = rejection(gs, Side::A, action::OrderCasualties{gs.units[kHavocs].id, {}});
    EXPECT_TRUE(starts_with(empty.at(0), "06.02: the casualty order is empty")) << empty[0];
}
