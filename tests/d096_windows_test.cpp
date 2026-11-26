// D-096 (M95): reaction windows (01.03.02 Rules Sequencing, app-only) and the first stratagems: spending CP (15.01),
// Command Re-roll (15.02, just after a roll) and Insane Bravery (15.04, just before a battle-shock roll).

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/distance.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::in;
using wh40k::test::rejection;
using wh40k::test::repo_root;

namespace {

PendingDecision window(Side side, WindowTiming timing, UnitId unit) {
    return PendingDecision{side, decision::UseStratagems{timing, unit}};
}

// Applies `a` for the deciding side, paced (up to the next decision, a roll or a window included).
std::vector<GameEvent> paced(GameState& gs, const GameAction& a, DiceSource& dice) {
    return apply_action_paced(gs, gs.pending->side, a, dice);
}

// Makes the pending roll, paced.
std::vector<GameEvent> roll(GameState& gs, DiceSource& dice) {
    EXPECT_TRUE(roll_pending(gs)) << (gs.pending ? describe(gs, *gs.pending) : std::string("no decision"));
    const auto& d = std::get<decision::RollDice>(gs.pending->kind);
    return paced(gs, action::RollDice{d.roll, d.unit}, dice);
}

std::vector<GameEvent> decline(GameState& gs) {
    ScriptedDice none{std::vector<int>{}};
    return paced(gs, action::DeclineStratagems{}, none);
}

// Side A's Charge step in V1, the Termagants `gap_in` inches from the Intercessors (round 1: each side has 1 CP).
GameState charge_step_state(float gap_in) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (!(gs.flow.stage == Stage::Charge && turn_side(gs) == Side::A)) {
        apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    }
    const float gap = mm_to_inches(*unit_distance(gs.units[0], gs.units[1]));
    for (auto& m : gs.units[1].models) m.position.y -= in(gap - gap_in);
    return gs;
}

// Side B's Shoot step in V1 (the Termagants 1" closer: round 1, Side A 2 CP, Side B 2 CP) and every fleshborer fired
// at the Intercessors, each changed by `change`. Fleshborer: BS4+, S5 against T4, AP0 against Sv3+, D1.
struct Volley {
    GameState gs;
    action::Shoot shoot;
};
template <typename Change>
Volley fleshborer_volley(Change change) {
    Volley v{wh40k::test::shoot_step_state(Side::B), {}};
    for (Model& m : v.gs.units[1].models) change(m.weapons[0].profiles[0]);
    v.shoot = action::Shoot{v.gs.units[1].id, ShootingType::Normal, {}};
    for (const Model& m : v.gs.units[1].models) {
        v.shoot.shots.push_back(ShotDeclaration{m.id, 0, 0, v.gs.units[0].id});
    }
    return v;
}
Volley fleshborer_volley() {
    return fleshborer_volley([](WeaponProfile&) {});
}

const DiceExpr kD3{1, 3, 0};

// The Intercessors (`models` of them) engaged with the Termagants in Side A's Move Units step.
GameState engaged_marines(std::size_t models) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    gs.units[0].models.resize(models);
    const Position m = gs.units[0].models[0].position;
    gs.units[1].models[0].position = Position{m.x - 16.0f - 14.25f - in(1), m.y, 0.0f};
    gs.pending = compute_decision(gs);
    return gs;
}

// V1 with two Termagant units below half-strength (4 and 3 models), played to Side B's Battle-shock step with every
// window before it declined (Side B has 2 CP there).
GameState side_b_battle_shock_step() {
    GameState gs = wh40k::test::load_v1_scenario();
    gs.units[0].models.resize(2);
    Unit second = gs.units[1];
    second.id = UnitId{50};
    for (std::size_t i = 0; i < second.models.size(); ++i) {
        second.models[i].id = ModelId{static_cast<std::uint32_t>(500 + i)};
        second.models[i].position.x += in(12);
    }
    second.models.resize(3);
    gs.units[1].models.resize(4);
    gs.units.push_back(second);
    start_game(gs);
    ScriptedDice none{std::vector<int>{}};
    while (!(gs.flow.stage == Stage::BattleShock && turn_side(gs) == Side::B)) {
        apply_action_paced(gs, decider(gs), wh40k::test::neutral_action(gs), none);
    }
    return gs;
}

template <typename E>
std::size_t count(const std::vector<GameEvent>& events) {
    return events_of<E>(events).size();
}

// The one DiceRerolled of `events`, as a value (a pointer into events_of's temporary would dangle).
event::DiceRerolled the_reroll(const std::vector<GameEvent>& events) {
    const auto rerolled = events_of<event::DiceRerolled>(events);
    EXPECT_EQ(rerolled.size(), 1u);
    return rerolled.empty() ? event::DiceRerolled{} : rerolled.front();
}

}  // namespace

// ==== 01.03.02 Rules Sequencing ====

TEST(R01_03_02_RulesSequencing, TheActivePlayersRulesComeFirstThenTheOpposingPlayers) {
    EXPECT_EQ(sequence_sides(Side::A, {true, true}), (std::vector<Side>{Side::A, Side::B}));
    EXPECT_EQ(sequence_sides(Side::B, {true, true}), (std::vector<Side>{Side::B, Side::A}));
    EXPECT_EQ(sequence_sides(Side::A, {false, true}), (std::vector<Side>{Side::B}));
    EXPECT_EQ(sequence_sides(Side::B, {false, true}), (std::vector<Side>{Side::B}));
    EXPECT_TRUE(sequence_sides(Side::A, {false, false}).empty());
}

// ==== 15.02 Command Re-roll ====

TEST(R15_02_CommandReroll, AChargeRollIsReRolledInFullBeforeItsResultIsUsed) {
    GameState gs = charge_step_state(9.9f);
    const UnitId marines = gs.units[0].id;
    const UnitId gants = gs.units[1].id;
    ASSERT_EQ(gs.command_points[0], 1);
    ScriptedDice dice{1, 1, 5, 6};
    paced(gs, action::DeclareCharge{marines}, dice);
    const auto rolled = roll(gs, dice);  // 2: no target within it
    ASSERT_EQ(count<event::ChargeRolled>(rolled), 1u);
    EXPECT_EQ(count<event::ChargeResolved>(rolled), 0u);  // the result waits for the window
    EXPECT_EQ(gs.pending, window(Side::A, WindowTiming::AfterRoll, marines));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "15.02");
    EXPECT_EQ(compute_decision(gs), gs.pending);
    const WindowInfo info = window_info(gs).value();
    EXPECT_EQ(info.roll, RollKind::Charge);
    EXPECT_EQ(info.side, Side::A);
    EXPECT_EQ(info.command_points, 1);
    EXPECT_TRUE(info.whole_roll);
    EXPECT_EQ(info.faces, (std::vector<int>{1, 1}));
    ASSERT_EQ(info.options.size(), 1u);
    EXPECT_EQ(info.options[0].stratagem, Stratagem::CommandReroll);
    EXPECT_EQ(info.options[0].cost, 1);
    EXPECT_EQ(legal_actions(gs, Side::A),
              (std::vector<GameAction>{action::DeclineStratagems{},
                                       action::UseStratagem{Stratagem::CommandReroll, marines, std::nullopt}}));
    EXPECT_TRUE(legal_actions(gs, Side::B).empty());
    // 15.02: a charge roll is re-rolled in full, so no die is picked; Insane Bravery has no place here.
    EXPECT_FALSE(rejection(gs, Side::A, action::UseStratagem{Stratagem::CommandReroll, marines, 0}).empty());
    EXPECT_FALSE(rejection(gs, Side::A, action::UseStratagem{Stratagem::InsaneBravery, marines, std::nullopt}).empty());
    EXPECT_FALSE(rejection(gs, Side::B, action::DeclineStratagems{}).empty());

    const auto used = paced(gs, action::UseStratagem{Stratagem::CommandReroll, marines, std::nullopt}, dice);
    EXPECT_EQ(dice.consumed(), 4u);
    const auto spent = events_of<event::StratagemUsed>(used);
    ASSERT_EQ(spent.size(), 1u);
    EXPECT_EQ(spent[0], (event::StratagemUsed{Side::A, Stratagem::CommandReroll, marines, 1, 0}));
    const auto rerolled = events_of<event::DiceRerolled>(used);
    ASSERT_EQ(rerolled.size(), 1u);
    EXPECT_EQ(rerolled[0].roll, RollKind::Charge);
    EXPECT_EQ(rerolled[0].die, std::nullopt);
    EXPECT_EQ(rerolled[0].before, (std::vector<int>{1, 1}));
    EXPECT_EQ(rerolled[0].after, (std::vector<int>{5, 6}));
    const auto* now = std::get_if<event::ChargeRolled>(&rerolled[0].result);
    ASSERT_NE(now, nullptr);
    EXPECT_EQ(now->roll.total(), 11);
    EXPECT_EQ(now->eligible_targets, (std::vector<UnitId>{gants}));
    EXPECT_EQ(gs.command_points[0], 0);
    EXPECT_EQ(gs.stratagems[0].used_this_phase, (std::vector<Stratagem>{Stratagem::CommandReroll}));
    EXPECT_EQ(gs.stratagems[0].targeted_this_phase, (std::vector<UnitId>{marines}));
    EXPECT_EQ(gs.units[0].phase_flags.charge_roll, 11);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::SelectChargeTargets{marines}}));
    EXPECT_FALSE(gs.flow.window.has_value());
}

TEST(R15_02_CommandReroll, ADeclinedChargeRollFailsAsWithoutTheWindow) {
    GameState gs = charge_step_state(9.9f);
    const UnitId marines = gs.units[0].id;
    ScriptedDice dice{1, 1};
    paced(gs, action::DeclareCharge{marines}, dice);
    roll(gs, dice);
    ASSERT_EQ(gs.pending, window(Side::A, WindowTiming::AfterRoll, marines));
    const auto declined = decline(gs);
    EXPECT_EQ(count<event::StratagemUsed>(declined), 0u);
    const auto resolved = events_of<event::ChargeResolved>(declined);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_FALSE(resolved[0].charge_move_made);
    EXPECT_EQ(gs.command_points[0], 1);
    EXPECT_TRUE(gs.stratagems[0].empty());
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::Charge{}}));
    // The same as the inline game, which declines every window (D-096 part 6).
    GameState inline_game = charge_step_state(9.9f);
    ScriptedDice same{1, 1};
    apply_action(inline_game, Side::A, action::DeclareCharge{marines}, same);
    EXPECT_EQ(inline_game, gs);
}

TEST(R15_02_CommandReroll, TheAdvanceRollIsReRolledAndStandsForThePhase) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);  // Side A's Move Units step, 1 CP
    const UnitId marines = gs.units[0].id;
    const ModelId first = gs.units[0].models[0].id;
    ScriptedDice dice{1, 6};
    paced(gs, action::StartMove{marines, MoveType::Advance, std::nullopt}, dice);
    roll(gs, dice);
    ASSERT_EQ(gs.pending, window(Side::A, WindowTiming::AfterRoll, marines));
    EXPECT_EQ(max_move_distance_mm(gs, first), std::nullopt);  // the roll waits in the window
    EXPECT_FALSE(gs.units[0].phase_flags.advance_roll.has_value());
    paced(gs, action::UseStratagem{Stratagem::CommandReroll, marines, 0}, dice);
    EXPECT_EQ(gs.units[0].phase_flags.advance_roll, 6);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::MoveModels{marines}}));
}

TEST(R15_02_CommandReroll, OneHitDieIsReRolledAndJudgedAsAFirstRoll) {
    Volley v = fleshborer_volley();
    GameState& gs = v.gs;
    const UnitId gants = gs.units[1].id;
    ScriptedDice dice{6, 6, 6, 1, 1, 1, 1, 1, 1, 1, 4};
    paced(gs, v.shoot, dice);
    roll(gs, dice);
    ASSERT_EQ(gs.pending, window(Side::B, WindowTiming::AfterRoll, gants));
    const WindowInfo info = window_info(gs).value();
    EXPECT_EQ(info.faces.size(), 10u);
    EXPECT_EQ(info.pickable, std::vector<bool>(10, true));
    EXPECT_FALSE(info.whole_roll);
    EXPECT_EQ(legal_actions(gs, Side::B).size(), 11u);  // the decline, then one per die
    EXPECT_FALSE(rejection(gs, Side::B, action::UseStratagem{Stratagem::CommandReroll, gants, 10}).empty());
    const auto used = paced(gs, action::UseStratagem{Stratagem::CommandReroll, gants, 3}, dice);
    const auto rerolled = events_of<event::DiceRerolled>(used);
    ASSERT_EQ(rerolled.size(), 1u);
    EXPECT_EQ(rerolled[0].die, std::size_t{3});
    EXPECT_EQ(rerolled[0].before[3], 1);
    EXPECT_EQ(rerolled[0].after[3], 4);
    const auto* hits = std::get_if<event::HitRollsMade>(&rerolled[0].result);
    ASSERT_NE(hits, nullptr);
    EXPECT_EQ(hits->rolls.rolls[3].outcome, HitOutcome::Hit);  // 4 against BS4+
    EXPECT_EQ(hits->rolls.hits(), 4);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Wound, gants}}));
    EXPECT_EQ(roll_dice_info(gs)->dice, 4);
}

TEST(R15_02_CommandReroll, ThePlayerCanUseItOncePerPhase) {
    Volley v = fleshborer_volley();
    GameState& gs = v.gs;
    const UnitId gants = gs.units[1].id;
    ScriptedDice dice{6, 6, 6, 1, 1, 1, 1, 1, 1, 1, 4, 6, 6, 6, 1};
    paced(gs, v.shoot, dice);
    roll(gs, dice);
    paced(gs, action::UseStratagem{Stratagem::CommandReroll, gants, 3}, dice);
    ASSERT_EQ(gs.command_points[1], 1);  // one CP left, but Command Re-roll is used this phase (15.01)
    roll(gs, dice);  // the wound rolls: no window for Side B now
    EXPECT_FALSE(window_pending(gs));
    EXPECT_FALSE(gs.flow.window.has_value());
}

TEST(R15_02_CommandReroll, TheDefenderReRollsItsOwnSaveRoll) {
    Volley v = fleshborer_volley();
    GameState& gs = v.gs;
    const UnitId marines = gs.units[0].id;
    ScriptedDice dice{6, 6, 6, 1, 1, 1, 1, 1, 1, 1, 6, 6, 6, 1, 1, 6, 5};
    paced(gs, v.shoot, dice);
    roll(gs, dice);  // hits: 3
    decline(gs);
    roll(gs, dice);  // wounds: 3
    decline(gs);
    roll(gs, dice);  // the defender's save rolls: 1, 1, 6
    // 05.03: the save rolls are the opposing player's, for its own unit, so the window is Side A's.
    ASSERT_EQ(gs.pending, window(Side::A, WindowTiming::AfterRoll, marines));
    EXPECT_EQ(window_info(gs)->faces, (std::vector<int>{1, 1, 6}));
    const auto used = paced(gs, action::UseStratagem{Stratagem::CommandReroll, marines, 0}, dice);
    EXPECT_EQ(events_of<event::StratagemUsed>(used).at(0).side, Side::A);
    const event::DiceRerolled rerolled = the_reroll(used);
    const auto* saves = std::get_if<event::SaveRollsMade>(&rerolled.result);
    ASSERT_NE(saves, nullptr);
    EXPECT_EQ(saves->rolls, (std::vector<int>{5, 1, 6}));
    EXPECT_EQ(gs.flow.attack->current->save_rolls, (std::vector<int>{5, 1, 6}));
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{marines}}));  // one save still failed
}

TEST(R15_02_CommandReroll, AWoundDieTwinLinkedReRolledCannotBePickedAgain) {
    Volley v = fleshborer_volley([](WeaponProfile& p) { p.abilities.push_back(ability::TwinLinked{}); });
    GameState& gs = v.gs;
    const UnitId gants = gs.units[1].id;
    ScriptedDice dice{6, 6, 6, 1, 1, 1, 1, 1, 1, 1, 6, 1, 1, 2, 2};
    paced(gs, v.shoot, dice);
    roll(gs, dice);  // hits: 3
    decline(gs);
    roll(gs, dice);  // wounds: 6, 1, 1
    ASSERT_EQ(gs.pending, (PendingDecision{Side::B, decision::RerollWoundRolls{gants}}));
    paced(gs, action::RerollWoundRolls{{1, 2}}, dice);  // 24.38: both failures again (2, 2)
    // Then 15.02's window, with dice 1 and 2 out of reach (01.05.02: a die is re-rolled only once).
    ASSERT_EQ(gs.pending, window(Side::B, WindowTiming::AfterRoll, gants));
    const WindowInfo info = window_info(gs).value();
    EXPECT_EQ(info.faces, (std::vector<int>{6, 2, 2}));
    EXPECT_EQ(info.pickable, (std::vector<bool>{true, false, false}));
    EXPECT_EQ(legal_actions(gs, Side::B),
              (std::vector<GameAction>{action::DeclineStratagems{},
                                       action::UseStratagem{Stratagem::CommandReroll, gants, 0}}));
    const auto refused = rejection(gs, Side::B, action::UseStratagem{Stratagem::CommandReroll, gants, 1});
    ASSERT_FALSE(refused.empty());
    EXPECT_NE(refused.front().find("D-011"), std::string::npos) << refused.front();  // 01.05.02 (app-only)
}

TEST(R15_02_CommandReroll, ADamageRollIsReRolledBeforeTheModelLosesTheWounds) {
    Volley v = fleshborer_volley([](WeaponProfile& p) { p.damage = kD3; });
    GameState& gs = v.gs;
    const UnitId marines = gs.units[0].id;
    const UnitId gants = gs.units[1].id;
    ScriptedDice dice{6, 6, 6, 1, 1, 1, 1, 1, 1, 1, 6, 6, 6, 1, 1, 1};
    paced(gs, v.shoot, dice);
    roll(gs, dice);
    decline(gs);
    roll(gs, dice);
    decline(gs);
    roll(gs, dice);  // saves 1, 1, 1: three attacks inflict damage
    decline(gs);
    paced(gs, action::OrderCasualties{marines, wh40k::test::model_ids(gs.units[0])}, dice);
    ASSERT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Damage, gants}}));
    ScriptedDice damage{1, 6};
    const auto first = roll(gs, damage);  // D3 = 1
    const auto rolled = events_of<event::DamageRolled>(first);
    ASSERT_EQ(rolled.size(), 1u);
    EXPECT_EQ(rolled[0].model, gs.units[0].models[0].id);
    EXPECT_EQ(rolled[0].roll.value, 1);
    ASSERT_EQ(gs.pending, window(Side::B, WindowTiming::AfterRoll, gants));
    EXPECT_EQ(gs.units[0].models[0].wounds_remaining, 2);  // nothing is lost while the roll waits
    const auto used = paced(gs, action::UseStratagem{Stratagem::CommandReroll, gants, 0}, damage);  // D3 = 3
    const event::DiceRerolled rerolled = the_reroll(used);
    const auto* now = std::get_if<event::DamageRolled>(&rerolled.result);
    ASSERT_NE(now, nullptr);
    EXPECT_EQ(now->roll.value, 3);
    EXPECT_EQ(gs.flow.attack->current->damage_rolls.size(), 1u);
    EXPECT_EQ(gs.flow.attack->current->damage_rolls[0].value, 3);
    // The next attack's damage roll, then the last: the batch resolves with a 3 for the first attack.
    ScriptedDice rest{2, 2};
    roll(gs, rest);
    const auto resolved = roll(gs, rest);
    ASSERT_EQ(count<event::SavesResolved>(resolved), 1u);
    EXPECT_EQ(events_of<event::SavesResolved>(resolved)[0].result.steps[0].damage->damage, 3);
    EXPECT_EQ(gs.units[0].models.size(), 3u);  // the first destroyed by 3; the second by 1 + 1
}

TEST(R15_02_CommandReroll, RandomAttacksReRollOneSelectionsRoll) {
    Volley v = fleshborer_volley([](WeaponProfile& p) { p.attacks = kD3; });
    GameState& gs = v.gs;
    const UnitId gants = gs.units[1].id;
    ScriptedDice dice{std::vector<int>{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 6}};
    const auto shot = paced(gs, v.shoot, dice);
    roll(gs, dice);  // each D3 a 1: 10 attack dice
    ASSERT_EQ(gs.pending, window(Side::B, WindowTiming::AfterRoll, gants));
    EXPECT_EQ(window_info(gs)->faces.size(), 10u);
    const auto used = paced(gs, action::UseStratagem{Stratagem::CommandReroll, gants, 2}, dice);
    const event::DiceRerolled rerolled = the_reroll(used);
    const auto* now = std::get_if<event::AttackDiceRolled>(&rerolled.result);
    ASSERT_NE(now, nullptr);
    EXPECT_EQ(now->rolls[2].roll.value, 3);
    EXPECT_EQ(now->attack_dice, 12);
    // On closing, the batch is gathered with the new total (04.03), then its hit rolls wait.
    const auto gathered = events_of<event::AttackBatchSelected>(used);
    ASSERT_EQ(gathered.size(), 1u);
    EXPECT_EQ(gathered[0].attack_dice, 12);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Hit, gants}}));
    EXPECT_EQ(roll_dice_info(gs)->dice, 12);
}

TEST(R15_02_CommandReroll, DesperateEscapeHazardsAreCountedAgain) {
    GameState gs = engaged_marines(2);  // not battle-shocked: 1 CP
    const UnitId marines = gs.units[0].id;
    ScriptedDice dice{1, 6, 5};
    paced(gs, action::StartMove{marines, MoveType::FallBack, FallBackMode::DesperateEscape}, dice);
    roll(gs, dice);  // 1, 6: one mortal wound
    ASSERT_EQ(gs.pending, window(Side::A, WindowTiming::AfterRoll, marines));
    EXPECT_EQ(gs.flow.move->pending_mortal_wounds, 0);  // counted when the window closes
    const auto used = paced(gs, action::UseStratagem{Stratagem::CommandReroll, marines, 0}, dice);
    const event::DiceRerolled rerolled = the_reroll(used);
    const auto* now = std::get_if<event::HazardRollsMade>(&rerolled.result);
    ASSERT_NE(now, nullptr);
    EXPECT_EQ(now->rolls.mortal_wounds, 0);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::MoveModels{marines}}));  // nothing to allocate
}

TEST(R15_02_CommandReroll, ABattleShockedUnitsRollsHaveNoWindow) {
    GameState gs = engaged_marines(2);
    gs.units[0].battle_shocked = true;  // 01.07: its player can't target it with stratagems
    const UnitId marines = gs.units[0].id;
    ScriptedDice dice{1, 6};
    paced(gs, action::StartMove{marines, MoveType::FallBack, FallBackMode::DesperateEscape}, dice);
    roll(gs, dice);
    EXPECT_FALSE(window_pending(gs));
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{marines}}));
}

TEST(R15_02_CommandReroll, NoWindowWithoutTheCp) {
    GameState gs = charge_step_state(9.9f);
    gs.command_points[0] = 0;
    const UnitId marines = gs.units[0].id;
    ScriptedDice dice{1, 1};
    paced(gs, action::DeclareCharge{marines}, dice);
    roll(gs, dice);
    EXPECT_FALSE(window_pending(gs));
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::Charge{}}));  // failed at once
}

TEST(R15_02_CommandReroll, NoWindowAfterARollMadeWithNoDice) {
    // [TORRENT]: the hits are automatic (24.37), no hit roll is made, so there is nothing to re-roll.
    Volley v = fleshborer_volley([](WeaponProfile& p) { p.abilities.push_back(ability::Torrent{}); });
    GameState& gs = v.gs;
    ScriptedDice dice{std::vector<int>{}};
    paced(gs, v.shoot, dice);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Wound, gs.units[1].id}}));
}

// ==== 15.01 Using stratagems ====

TEST(R15_01_UsingStratagems, OneStratagemPerUnitPerPhase) {
    GameState gs = charge_step_state(9.9f);
    const UnitId marines = gs.units[0].id;
    ScriptedDice dice{1, 1};
    paced(gs, action::DeclareCharge{marines}, dice);
    roll(gs, dice);
    ASSERT_TRUE(window_pending(gs));
    gs.stratagems[0].targeted_this_phase = {marines};  // as if another stratagem had targeted it this phase
    const auto refused = rejection(gs, Side::A, action::UseStratagem{Stratagem::CommandReroll, marines, std::nullopt});
    ASSERT_FALSE(refused.empty());
    EXPECT_NE(refused.front().find("15.01"), std::string::npos) << refused.front();
    EXPECT_EQ(legal_actions(gs, Side::A), (std::vector<GameAction>{action::DeclineStratagems{}}));
}

TEST(R15_01_UsingStratagems, ThePerPhaseRecordsEndWithThePhase) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);  // Side A's Move Units step, 1 CP
    const UnitId marines = gs.units[0].id;
    ScriptedDice dice{1, 6};
    paced(gs, action::StartMove{marines, MoveType::Advance, std::nullopt}, dice);
    roll(gs, dice);
    paced(gs, action::UseStratagem{Stratagem::CommandReroll, marines, 0}, dice);
    // Still the Movement phase (the unit's move waits): the records stand.
    ASSERT_EQ(gs.pending, (PendingDecision{Side::A, decision::MoveModels{marines}}));
    EXPECT_EQ(gs.stratagems[0].used_this_phase, (std::vector<Stratagem>{Stratagem::CommandReroll}));
    EXPECT_EQ(gs.stratagems[0].targeted_this_phase, (std::vector<UnitId>{marines}));
    // The move ends where it started, Side A's last, so on to the Shooting phase: the Movement phase's records are
    // gone (15.01: "this phase").
    ScriptedDice none{std::vector<int>{}};
    for (int guard = 0; guard < 20 && phase_of(gs.flow.stage) == Phase::Movement; ++guard) {
        paced(gs, wh40k::test::neutral_action(gs), none);
    }
    EXPECT_EQ(phase_of(gs.flow.stage), Phase::Shooting);
    EXPECT_TRUE(gs.stratagems[0].empty());
    EXPECT_EQ(gs.command_points[0], 0);  // the CP stay spent
    EXPECT_EQ(none.consumed(), 0u);
}

// ==== 15.04 Insane Bravery ====

TEST(R15_04_InsaneBravery, TheRollPassesAutomaticallyOncePerBattle) {
    GameState gs = side_b_battle_shock_step();
    const UnitId first = gs.units[1].id;
    ASSERT_EQ(gs.flow.battle_shock_tests, (std::vector<UnitId>{first, UnitId{50}}));
    ASSERT_EQ(gs.command_points[1], 2);
    // Just before the first unit's roll: Side B's window (15.04).
    ASSERT_EQ(gs.pending, window(Side::B, WindowTiming::BeforeRoll, first));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "15.04");
    EXPECT_EQ(legal_actions(gs, Side::B),
              (std::vector<GameAction>{action::DeclineStratagems{},
                                       action::UseStratagem{Stratagem::InsaneBravery, first, std::nullopt}}));
    ScriptedDice none{std::vector<int>{}};
    const auto used = paced(gs, action::UseStratagem{Stratagem::InsaneBravery, first, std::nullopt}, none);
    EXPECT_EQ(none.consumed(), 0u);  // 01.05.01: automatically successful, no dice
    EXPECT_EQ(events_of<event::StratagemUsed>(used).at(0),
              (event::StratagemUsed{Side::B, Stratagem::InsaneBravery, first, 1, 1}));
    EXPECT_EQ(count<event::BattleShockPassedAutomatically>(used), 1u);
    EXPECT_EQ(count<event::BattleShockRolled>(used), 0u);
    EXPECT_FALSE(gs.units[1].battle_shocked);
    EXPECT_EQ(gs.stratagems[1].used_this_battle, (std::vector<Stratagem>{Stratagem::InsaneBravery}));
    // Once per battle: the second unit's roll has no window and waits as usual.
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::BattleShock, UnitId{50}}}));
    // The per-phase records end with the Command phase; the once-per-battle record stays.
    ScriptedDice dice{6, 6};
    roll(gs, dice);
    for (int guard = 0; guard < 20 && phase_of(gs.flow.stage) == Phase::Command; ++guard) {
        paced(gs, wh40k::test::neutral_action(gs), none);
    }
    EXPECT_EQ(phase_of(gs.flow.stage), Phase::Movement);
    EXPECT_TRUE(gs.stratagems[1].used_this_phase.empty());
    EXPECT_TRUE(gs.stratagems[1].targeted_this_phase.empty());
    EXPECT_EQ(gs.stratagems[1].used_this_battle, (std::vector<Stratagem>{Stratagem::InsaneBravery}));
}

TEST(R15_04_InsaneBravery, DeclinedTheRollIsMadeAsUsual) {
    GameState gs = side_b_battle_shock_step();
    const UnitId first = gs.units[1].id;
    ASSERT_TRUE(window_pending(gs));
    decline(gs);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::BattleShock, first}}));
    ScriptedDice dice{6, 6};
    roll(gs, dice);
    // The next unit's window, just before its roll.
    EXPECT_EQ(gs.pending, window(Side::B, WindowTiming::BeforeRoll, UnitId{50}));
}

TEST(R15_04_InsaneBravery, NeverForABattleShockedUnit) {
    GameState gs = wh40k::test::load_v1_scenario();
    gs.units[1].models.resize(4);
    start_game(gs);
    gs.units[1].battle_shocked = true;  // FAQ: 01.07 bars stratagems on it
    ScriptedDice none{std::vector<int>{}};
    while (!(gs.flow.stage == Stage::BattleShock && turn_side(gs) == Side::B)) {
        apply_action_paced(gs, decider(gs), wh40k::test::neutral_action(gs), none);
    }
    EXPECT_FALSE(window_pending(gs));
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::BattleShock, gs.units[1].id}}));
}

TEST(R15_04_InsaneBravery, NotForTheBattleShockRollAfterADesperateEscape) {
    GameState gs = engaged_marines(2);
    const UnitId marines = gs.units[0].id;
    ScriptedDice dice{6, 6};
    paced(gs, action::StartMove{marines, MoveType::FallBack, FallBackMode::DesperateEscape}, dice);
    roll(gs, dice);
    decline(gs);
    const ModelId first = gs.units[0].models[0].id;
    const Position start = *move_start_position(gs, first);
    paced(gs, action::ProposeModelMove{first, {Position{start.x + in(3), start.y, 0.0f}}}, dice);
    paced(gs, action::CommitUnitMove{}, dice);
    // 09.07's battle-shock roll, in the Movement phase: 15.04's WHEN is the Battle-shock step, so no window before it.
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::RollDice{RollKind::BattleShock, marines}}));
}

// ==== The window in the state (D-096 part 1) and JSON ====

TEST(D096_Windows, AStateAtEachKindOfWindowRoundTripsAndTheNewFieldsAreOmittedOtherwise) {
    const auto round_trip = [](const GameState& gs) { return Json(gs).get<GameState>(); };
    std::vector<GameState> at_window;
    {
        GameState gs = charge_step_state(9.9f);
        ScriptedDice dice{1, 1};
        paced(gs, action::DeclareCharge{gs.units[0].id}, dice);
        roll(gs, dice);
        at_window.push_back(gs);  // AfterRoll (Charge)
    }
    at_window.push_back(side_b_battle_shock_step());  // BeforeRoll (BattleShock)
    for (const GameState& gs : at_window) {
        ASSERT_TRUE(window_pending(gs));
        ASSERT_TRUE(gs.flow.window.has_value());
        EXPECT_EQ(round_trip(gs), gs) << describe(gs, *gs.pending);
        EXPECT_TRUE(Json(gs).at("flow").contains("window"));
    }
    GameState used = at_window[0];
    ScriptedDice dice{5, 6};
    paced(used, action::UseStratagem{Stratagem::CommandReroll, used.units[0].id, std::nullopt}, dice);
    EXPECT_TRUE(Json(used).contains("stratagems"));
    EXPECT_EQ(round_trip(used), used);
    // No window, no stratagem used: none of the new fields is written.
    const std::string text = Json(wh40k::test::shoot_step_state()).dump();
    EXPECT_EQ(text.find("window"), std::string::npos);
    EXPECT_EQ(text.find("stratagems"), std::string::npos);
    EXPECT_EQ(text.find("damage_rolls"), std::string::npos);
}

TEST(D096_Windows, TheWindowIsSetExactlyWhileItsDecisionWaits) {
    GameState gs = charge_step_state(9.9f);
    const UnitId marines = gs.units[0].id;
    ScriptedDice dice{4, 6};
    paced(gs, action::DeclareCharge{marines}, dice);
    EXPECT_FALSE(gs.flow.window.has_value());  // at the RollDice
    roll(gs, dice);
    EXPECT_TRUE(gs.flow.window.has_value());
    decline(gs);
    EXPECT_FALSE(gs.flow.window.has_value());
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::SelectChargeTargets{marines}}));
}

// ==== Random play that uses stratagems (D-004 style): every answer valid, the invariants hold ====

TEST(D096_RandomPlay, GamesThatUseStratagemsStayValidAndReplay) {
    const std::vector<GameState> scenarios{wh40k::test::load_v1_scenario(), wh40k::test::load_rosters_scenario()};
    int windows = 0;
    int used = 0;
    for (std::size_t n = 0; n < scenarios.size(); ++n) {
        for (std::uint64_t seed = 1; seed <= 4; ++seed) {
            SCOPED_TRACE("scenario " + std::to_string(n) + " seed " + std::to_string(seed));
            GameState initial = scenarios[n];
            initial.dice = DiceState{seed, 0};
            Match match(initial, RollMode::Paced);
            wh40k::test::TestRng rng(seed * 13 + n);
            // Command Re-rolls per side and phase (15.01: at most one).
            std::map<std::string, int> per_phase;
            for (int i = 0; i < 600 && match.state().pending; ++i) {
                const GameState& gs = match.state();
                ASSERT_EQ(window_pending(gs), gs.flow.window.has_value());
                ASSERT_EQ(gs.pending, compute_decision(gs));
                ASSERT_GE(gs.command_points[0], 0);
                ASSERT_GE(gs.command_points[1], 0);
                if (window_pending(gs)) {
                    ++windows;
                    for (const GameAction& a : legal_actions(gs, gs.pending->side)) {
                        EXPECT_TRUE(validate_action(gs, gs.pending->side, a).ok()) << action_name(a);
                    }
                    // A die out of range is refused with a rule-cited reason.
                    const auto bad = rejection(gs, gs.pending->side,
                                               action::UseStratagem{Stratagem::CommandReroll, gs.flow.window->unit, 99});
                    ASSERT_FALSE(bad.empty());
                    EXPECT_TRUE(wh40k::test::is_rule_cited(bad.front())) << bad.front();
                }
                const std::size_t before = match.events().size();
                const GameAction a = wh40k::test::random_action(gs, rng);
                ASSERT_TRUE(match.submit(decider(gs), a).ok()) << action_name(a);
                for (std::size_t k = before; k < match.events().size(); ++k) {
                    if (const auto* s = std::get_if<event::StratagemUsed>(&match.events()[k])) {
                        ++used;
                        if (s->stratagem == Stratagem::CommandReroll) {
                            const std::string key = std::to_string(match.state().flow.battle_round) + "/" +
                                                    std::to_string(match.state().flow.turn_in_round) + "/" +
                                                    std::string(phase_name(phase_of(match.state().flow.stage))) + "/" +
                                                    (s->side == Side::A ? "A" : "B");
                            EXPECT_LE(++per_phase[key], 1) << key;
                        }
                    }
                }
            }
            // The game as a format-3 replay plays the same.
            const Replay replay = match.to_replay("random play with stratagems");
            EXPECT_EQ(replay.format, kReplayFormatWindowsRecorded);
            const ReplayResult again = run_replay(Json(replay).get<Replay>(), repo_root());
            ASSERT_TRUE(again.ok()) << "step " << *again.failed_step << ": " << again.reasons.front();
            EXPECT_EQ(again.final_state, match.state());
            EXPECT_EQ(again.events, match.events());
        }
    }
    EXPECT_GT(windows, 50);
    EXPECT_GT(used, 10);
}
