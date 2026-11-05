// M11: the Command phase (08.02 Gain Core CP, 08.03 Battle-shock), starting strength and half-strength
// (Rules Appendix), battle-shock effects (01.07), Regaining Coherency in the End of Turn step (03.03) and
// the end of the game (D-007). Battle-shock rolls use ScriptedDice.

#include <gtest/gtest.h>

#include <vector>

#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/rules/leadership.hpp"
#include "core/rules/unit_strength.hpp"
#include "core/state/queries.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::events_of;
using wh40k::test::decider;
using wh40k::test::in;
using wh40k::test::load_v1_scenario;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

// Passes every decision (legal_actions().front()) until `side`'s next Movement phase has started. The
// action that enters `side`'s Command phase uses `dice` (so its battle-shock rolls are scripted); every
// other action uses the seeded dice. Returns the events of that action.
std::vector<GameEvent> pass_to_command_phase_of(GameState& gs, Side side, DiceSource& dice) {
    for (int guard = 0; guard < 200; ++guard) {
        const Side who = decider(gs);
        const GameAction a = legal_actions(gs, who).front();
        const bool enters_command = std::holds_alternative<action::EndChargeStep>(a) && turn_side(gs) != side;
        if (enters_command) {
            auto events = apply_action(gs, who, a, dice);
            EXPECT_EQ(turn_side(gs), side);
            return events;
        }
        apply_action(gs, who, a);
    }
    ADD_FAILURE() << "did not reach the Command phase";
    return {};
}

// Keeps the first `n` models of units[index]; the starting strength stays as loaded.
void reduce_to(GameState& gs, std::size_t index, std::size_t n) { gs.units[index].models.resize(n); }

GameState start_with(GameState gs, DiceSource& dice) {
    start_game(gs, dice);
    return gs;
}

}  // namespace

// ==== Rules Appendix: Starting Strength and Half-strength ====

TEST(Appendix_StartingStrength, TenTermagantsAreAtHalfStrengthWithFiveAndBelowWithFour) {
    GameState gs = load_v1_scenario();
    Unit& gants = gs.units[kGants];
    ASSERT_EQ(gants.starting_strength, 10);
    EXPECT_FALSE(below_starting_strength(gants));
    reduce_to(gs, kGants, 6);
    EXPECT_TRUE(below_starting_strength(gants));
    EXPECT_FALSE(at_or_below_half_strength(gants));
    reduce_to(gs, kGants, 5);
    EXPECT_TRUE(at_half_strength(gants));
    EXPECT_FALSE(below_half_strength(gants));
    reduce_to(gs, kGants, 4);
    EXPECT_FALSE(at_half_strength(gants));
    EXPECT_TRUE(below_half_strength(gants));
}

TEST(Appendix_StartingStrength, FiveIntercessorsCanNeverBeAtHalfStrength) {
    GameState gs = load_v1_scenario();
    Unit& marines = gs.units[kMarines];
    ASSERT_EQ(marines.starting_strength, 5);
    reduce_to(gs, kMarines, 3);
    EXPECT_FALSE(at_or_below_half_strength(marines));
    reduce_to(gs, kMarines, 2);
    EXPECT_FALSE(at_half_strength(marines));
    EXPECT_TRUE(below_half_strength(marines));
}

TEST(Appendix_StartingStrength, AOneModelUnitUsesItsWounds) {
    GameState gs = load_v1_scenario();
    Unit& one = gs.units[kMarines];
    one.models.resize(1);
    one.starting_strength = 1;
    Model& m = one.models[0];
    ASSERT_EQ(m.characteristics.wounds, 2);
    EXPECT_FALSE(below_starting_strength(one));
    m.wounds_remaining = 1;
    EXPECT_TRUE(below_starting_strength(one));
    EXPECT_TRUE(at_half_strength(one));
    m.characteristics.wounds = 3;  // an odd W can never be at half-strength
    EXPECT_FALSE(at_half_strength(one));
    EXPECT_TRUE(below_half_strength(one));
}

// ==== 08.02 Gain Core CP ====

TEST(R08_02_GainCoreCP, BothPlayersGainOneCPInEveryCommandPhase) {
    GameState gs = load_v1_scenario();
    const auto events = start_game(gs);
    EXPECT_EQ(gs.command_points, (std::array<int, 2>{1, 1}));
    EXPECT_EQ(events_of<event::CommandPointGained>(events),
              (std::vector<event::CommandPointGained>{{Side::A, 1}, {Side::B, 1}}));
    while (gs.pending) apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    EXPECT_EQ(gs.command_points, (std::array<int, 2>{10, 10}));  // 5 battle rounds x 2 Command phases
}

// ==== 08.03 Battle-shock (acceptance) ====

TEST(R08_03_BattleShock, TermagantsAtHalfStrengthTestButSixDoNot) {
    GameState five = load_v1_scenario();
    reduce_to(five, kGants, 5);
    start_game(five);
    ScriptedDice fail{1, 1};
    const auto events = pass_to_command_phase_of(five, Side::B, fail);
    const auto rolled = events_of<event::BattleShockRolled>(events);
    ASSERT_EQ(rolled.size(), 1u);
    EXPECT_EQ(rolled[0].unit, five.units[kGants].id);
    EXPECT_TRUE(rolled[0].roll.becomes_battle_shocked);  // 2 vs Ld 8+
    EXPECT_TRUE(five.units[kGants].battle_shocked);
    EXPECT_EQ(fail.remaining(), 0u);

    GameState six = load_v1_scenario();
    reduce_to(six, kGants, 6);
    start_game(six);
    ScriptedDice unused{1, 1};
    EXPECT_TRUE(events_of<event::BattleShockRolled>(pass_to_command_phase_of(six, Side::B, unused)).empty());
    EXPECT_EQ(unused.remaining(), 2u);
    EXPECT_FALSE(six.units[kGants].battle_shocked);
}

TEST(R08_03_BattleShock, IntercessorsTestWithTwoLeftButNotWithThree) {
    GameState three = load_v1_scenario();
    reduce_to(three, kMarines, 3);
    ScriptedDice unused{1, 1};
    const auto none = start_with(three, unused);
    EXPECT_EQ(unused.remaining(), 2u);
    EXPECT_FALSE(none.units[kMarines].battle_shocked);

    GameState two = load_v1_scenario();
    reduce_to(two, kMarines, 2);
    ScriptedDice fail{2, 3};
    const auto shocked = start_with(two, fail);
    EXPECT_EQ(fail.remaining(), 0u);
    EXPECT_TRUE(shocked.units[kMarines].battle_shocked);  // 5 vs Ld 6+
}

TEST(R08_03_BattleShock, OnlyTheActivePlayersUnitsTest) {
    GameState gs = load_v1_scenario();
    reduce_to(gs, kGants, 4);  // below half, but it is Side A's Command phase
    ScriptedDice unused{1, 1};
    start_game(gs, unused);
    EXPECT_EQ(unused.remaining(), 2u);
    EXPECT_FALSE(gs.units[kGants].battle_shocked);
}

TEST(R08_03_BattleShock, ABattleShockedUnitTestsEachCommandPhaseUntilItPasses) {
    GameState gs = load_v1_scenario();
    gs.units[kGants].battle_shocked = true;  // at full strength: it tests only because it is battle-shocked
    start_game(gs);
    ScriptedDice fail{3, 4};
    const auto first = pass_to_command_phase_of(gs, Side::B, fail);
    EXPECT_EQ(events_of<event::BattleShockRolled>(first).size(), 1u);
    EXPECT_TRUE(events_of<event::BattleShockEnded>(first).empty());
    EXPECT_TRUE(gs.units[kGants].battle_shocked);  // 7 vs Ld 8+: still battle-shocked

    ScriptedDice pass{4, 4};
    const auto second = pass_to_command_phase_of(gs, Side::B, pass);
    EXPECT_EQ(events_of<event::BattleShockRolled>(second).size(), 1u);
    EXPECT_EQ(events_of<event::BattleShockEnded>(second).size(), 1u);
    EXPECT_FALSE(gs.units[kGants].battle_shocked);  // 8: passed, so it is no longer battle-shocked

    ScriptedDice unused{1, 1};
    EXPECT_TRUE(events_of<event::BattleShockRolled>(pass_to_command_phase_of(gs, Side::B, unused)).empty());
}

// ==== 01.07 Battle-shock effects ====

TEST(R01_07_BattleShockRolls, ABattleShockedUnitsModelsHaveNoObjectiveControl) {
    GameState gs = load_v1_scenario();
    Unit& gants = gs.units[kGants];
    EXPECT_EQ(objective_control(gants, gants.models[0]), 2);
    gants.battle_shocked = true;
    EXPECT_FALSE(objective_control(gants, gants.models[0]).has_value());  // OC '-'
}

// ==== 03.03 Regaining Coherency (acceptance) ====

namespace {

// Side A's Charge step at the start of the game.
GameState charge_step() {
    GameState gs = load_v1_scenario();
    start_game(gs);
    while (gs.flow.stage != Stage::Charge) apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    return gs;
}

}  // namespace

TEST(R03_03_RegainingCoherency, ModelsAreRemovedOneAtATimeUntilTheUnitIsCoherent) {
    GameState gs = charge_step();
    // The two westernmost Intercessors wander 5" west and 6" north: more than 9" from the eastern ones.
    for (std::size_t i = 0; i < 2; ++i) {
        gs.units[kMarines].models[i].position.x -= in(5);
        gs.units[kMarines].models[i].position.y += in(6);
    }
    ASSERT_FALSE(unit_coherency(gs.units[kMarines]).ok());
    apply_action(gs, Side::A, action::EndChargeStep{});

    // End of Turn step: Side A removes models from its unit.
    ASSERT_EQ(gs.flow.stage, Stage::EndOfTurn);
    const UnitId marines = gs.units[kMarines].id;
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::RestoreCoherency{marines}}));
    EXPECT_EQ(legal_actions(gs, Side::A).size(), 5u);
    const auto first = apply_action(gs, Side::A, action::RemoveModelForCoherency{marines, ModelId{1}});
    EXPECT_EQ(events_of<event::ModelRemovedForCoherency>(first).size(), 1u);
    EXPECT_TRUE(events_of<event::ModelDestroyed>(first).empty());  // no rules triggered by destruction
    // Model 2 is still too far from the others.
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::RestoreCoherency{marines}}));
    apply_action(gs, Side::A, action::RemoveModelForCoherency{marines, ModelId{2}});
    EXPECT_TRUE(unit_coherency(gs.units[kMarines]).ok());
    EXPECT_EQ(gs.units[kMarines].models.size(), 3u);
    EXPECT_EQ(turn_side(gs), Side::B);  // the turn ended
}

TEST(R03_03_RegainingCoherency, TheOpponentsIncoherentUnitsAreFixedTooByTheirController) {
    GameState gs = charge_step();
    gs.units[kGants].models[0].position.y -= in(5);  // a Termagant strays 5" south of its unit
    ASSERT_FALSE(unit_coherency(gs.units[kGants]).ok());
    apply_action(gs, Side::A, action::EndChargeStep{});
    const UnitId gants = gs.units[kGants].id;
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::RestoreCoherency{gants}}));
    EXPECT_EQ(validate_action(gs, Side::B, action::RemoveModelForCoherency{gs.units[kMarines].id, ModelId{1}})
                  .reasons.at(0),
              "03.03: models are being removed from unit 2, not unit 1");
    apply_action(gs, Side::B, action::RemoveModelForCoherency{gants, gs.units[kGants].models[0].id});
    EXPECT_TRUE(unit_coherency(gs.units[kGants]).ok());
}

// ==== D-007: the end of the game (acceptance) ====

TEST(D007_GameEnd, ADrawAfterBattleRoundFiveIsAGameOverEvent) {
    GameState gs = load_v1_scenario();
    start_game(gs);
    std::vector<GameEvent> last;
    while (gs.pending) last = apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    const auto over = events_of<event::GameOver>(last);
    ASSERT_EQ(over.size(), 1u);
    EXPECT_FALSE(over[0].winner.has_value());
    EXPECT_EQ(gs.flow.battle_round, 5);
    EXPECT_EQ(gs.flow.stage, Stage::GameOver);
}

TEST(D007_GameEnd, WipingOutASideIsAnImmediateWinEvenInTheFightPhase) {
    // The Intercessors charge and destroy every Termagant in the Fight phase: the game ends at once.
    GameState gs = wh40k::test::after_successful_charge();
    apply_action(gs, Side::A, action::EndChargeStep{});
    while (gs.flow.stage == Stage::PileIn) apply_action(gs, decider(gs), action::EndPileIns{});
    // Every Termagant in the unit: 15 hits, 15 wounds, 15 failed saves.
    gs.units[kGants].models.resize(5);
    apply_action(gs, Side::A, action::SelectToFight{gs.units[kMarines].id, FightType::Normal});
    std::vector<int> dice(15, 6);
    dice.insert(dice.end(), 15, 6);
    dice.insert(dice.end(), 15, 1);
    ScriptedDice scripted(dice);
    std::vector<GameEvent> events =
        apply_action(gs, Side::A, wh40k::test::first_melee_options(gs, gs.units[kMarines].id), scripted);
    if (gs.pending && std::holds_alternative<decision::OrderCasualties>(gs.pending->kind)) {
        events = apply_action(gs, Side::B, action::OrderCasualties{gs.units[kGants].id,
                                                                   wh40k::test::model_ids(gs.units[kGants])});
    }
    const auto over = events_of<event::GameOver>(events);
    ASSERT_EQ(over.size(), 1u);
    EXPECT_EQ(over[0].winner, Side::A);
    EXPECT_EQ(gs.flow.stage, Stage::GameOver);
    EXPECT_FALSE(gs.pending.has_value());
}
