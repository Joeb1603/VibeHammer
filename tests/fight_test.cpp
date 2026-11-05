// M10: the Fight phase through the engine. 12.02/12.03 pile-in moves, the 12.04 Fight step's alternation
// (D-020) and passing (Appendix: Eligible to Fight, but Unable to Fight), 12.05/12.06 fight types, melee
// declarations (04.01/04.02, D-021), and 12.07/12.08 consolidation with forced fights. ScriptedDice
// throughout.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/charge.hpp"
#include "core/rules/fight.hpp"
#include "core/state/queries.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::events_of;
using wh40k::test::contains;
using wh40k::test::rejection;
using wh40k::test::expect_valid;
using wh40k::test::add_unit_copy;
using wh40k::test::after_successful_charge;
using wh40k::test::decider;
using wh40k::test::first_melee_options;
using wh40k::test::in;
using wh40k::test::is_rule_cited;
using wh40k::test::model_ids;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;
constexpr std::size_t kBoltRifle = 0;
constexpr std::size_t kKnives = 2;
constexpr float kMarineRadius = 16.0f;
constexpr float kGantRadius = 14.25f;

ScriptedDice rolls(int n, int face) { return ScriptedDice(std::vector<int>(static_cast<std::size_t>(n), face)); }

std::vector<GameEvent> act(GameState& gs, const GameAction& a) { return apply_action(gs, decider(gs), a); }
std::vector<GameEvent> act(GameState& gs, const GameAction& a, DiceSource& dice) {
    return apply_action(gs, decider(gs), a, dice);
}

UnitId id_of(const GameState& gs, std::size_t index) { return gs.units[index].id; }

// The Termagants' front-rank model furthest west (lowest y is the front: they face south).
Position front_west_gant(const GameState& gs) {
    Position best = gs.units[kGants].models[0].position;
    for (const Model& m : gs.units[kGants].models) {
        if (m.position.y < best.y - 0.01f || (std::abs(m.position.y - best.y) <= 0.01f && m.position.x < best.x)) {
            best = m.position;
        }
    }
    return best;
}

// A one-model copy of units[index] at `pos`, with fresh turn and phase flags (it did not charge).
UnitId add_one_model_unit(GameState& gs, std::size_t index, Position pos) {
    const UnitId id = add_unit_copy(gs, index);
    Unit& u = *gs.find_unit(id);
    u.models.resize(1);
    u.models[0].position = pos;
    u.turn_flags = UnitTurnFlags{};
    u.phase_flags = UnitPhaseFlags{};
    return id;
}

// From the Charge step: end it and make no pile-in moves, reaching the Fight step.
void to_fight_step(GameState& gs) {
    act(gs, action::EndChargeStep{});
    while (gs.flow.stage == Stage::PileIn) act(gs, action::EndPileIns{});
    ASSERT_EQ(gs.flow.stage, Stage::Fight);
}

// Answers casualty orders with the unit's own model order (D-013).
void order_casualties(GameState& gs) {
    while (gs.pending && std::holds_alternative<decision::OrderCasualties>(gs.pending->kind)) {
        const UnitId target = std::get<decision::OrderCasualties>(gs.pending->kind).target;
        act(gs, action::OrderCasualties{target, model_ids(*gs.find_unit(target))});
    }
}

// The Intercessors fight (Normal) and destroy every Termagant: 15 hits, 15 wounds, 15 failed saves.
std::vector<GameEvent> marines_destroy_the_termagants(GameState& gs) {
    act(gs, action::SelectToFight{id_of(gs, kMarines), FightType::Normal});
    std::vector<int> dice(15, 6);
    dice.insert(dice.end(), 15, 6);
    dice.insert(dice.end(), 15, 1);
    ScriptedDice scripted(dice);
    auto events = act(gs, first_melee_options(gs, id_of(gs, kMarines)), scripted);
    order_casualties(gs);
    EXPECT_TRUE(is_destroyed(gs.units[kGants]));
    return events;
}

// After the charge: unit 3, one Intercessor 1" west of the Termagants' front-west model (engaged with
// them, did not charge), and unit 4, one Termagant `gap_in` west of unit 3 (unengaged).
struct OverrunSetup {
    GameState gs;
    UnitId third;
    UnitId fourth;
};

OverrunSetup overrun_setup(float gap_in) {
    OverrunSetup s{after_successful_charge(), {}, {}};
    const Position g = front_west_gant(s.gs);
    const Position third{g.x - kGantRadius - kMarineRadius - in(1), g.y, 0.0f};
    s.third = add_one_model_unit(s.gs, kMarines, third);
    s.fourth = add_one_model_unit(s.gs, kGants, Position{third.x - kMarineRadius - kGantRadius - in(gap_in), g.y, 0});
    return s;
}

// Both units eligible to fight but 12" apart (a test-only state: made_charge_move set by hand), at Side
// A's Charge step.
GameState far_apart_fighters(bool marines_fight_first) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (gs.flow.stage != Stage::Charge) act(gs, legal_actions(gs, decider(gs)).front());
    gs.units[kMarines].turn_flags.made_charge_move = true;
    gs.units[kMarines].turn_flags.fights_first = marines_fight_first;
    gs.units[kGants].turn_flags.made_charge_move = true;
    return gs;
}

const FightSequence& sequence(const GameState& gs) { return gs.flow.fight_sequence.value(); }

}  // namespace

// ==== 12.04: the sequence after a successful charge (acceptance) ====

TEST(R12_04_Fight, AfterAChargeTheIntercessorsFightFirstThenTheSequenceAlternates) {
    GameState gs = after_successful_charge();
    const UnitId marines = id_of(gs, kMarines);
    const UnitId gants = id_of(gs, kGants);
    act(gs, action::EndChargeStep{});

    // 12.02 Pile In: the player whose turn it is resolves their moves first, then their opponent.
    ASSERT_EQ(gs.flow.stage, Stage::PileIn);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::PileIn{}}));
    EXPECT_EQ(legal_actions(gs, Side::A),
              (std::vector<GameAction>{action::EndPileIns{}, action::StartPileIn{marines, {gants}}}));
    act(gs, action::EndPileIns{});
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::PileIn{}}));
    act(gs, action::EndPileIns{});

    // 12.04 Resolve Fights First Combats, starting with the player whose turn it is.
    ASSERT_EQ(gs.flow.stage, Stage::Fight);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::SelectUnitToFight{}}));
    EXPECT_EQ(sequence(gs).sub_step, FightSubStep::FightsFirst);
    // Engaged at the start of the step: Normal only. Within 5" of the enemy: no pass.
    EXPECT_EQ(legal_actions(gs, Side::A),
              (std::vector<GameAction>{action::SelectToFight{marines, FightType::Normal}}));
    act(gs, action::SelectToFight{marines, FightType::Normal});
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::DeclareMeleeAttacks{marines}}));
    ScriptedDice misses = rolls(15, 1);
    const auto first = act(gs, first_melee_options(gs, marines), misses);
    EXPECT_EQ(events_of<event::MeleeAttacksDeclared>(first).at(0).selections.size(), 5u);
    EXPECT_EQ(events_of<event::AttackBatchesGathered>(first).at(0).batches.at(0).attack_dice, 15);
    EXPECT_EQ(misses.remaining(), 0u);

    // Side B has no Fights First unit and none is left: Resolve Remaining Combats, Side B selecting.
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::SelectUnitToFight{}}));
    EXPECT_EQ(sequence(gs).sub_step, FightSubStep::Remaining);
    EXPECT_EQ(legal_actions(gs, Side::B), (std::vector<GameAction>{action::SelectToFight{gants, FightType::Normal}}));
    act(gs, action::SelectToFight{gants, FightType::Normal});
    const action::Fight reply = first_melee_options(gs, gants);
    EXPECT_EQ(reply.declarations.size(), 5u);  // only the front rank is engaged
    ScriptedDice gant_misses = rolls(5, 1);
    const auto second = act(gs, reply, gant_misses);

    // Nothing is eligible: the Fight step ends. 12.07 Consolidate: Side A first, then Side B.
    EXPECT_EQ(events_of<event::FightStepEnded>(second).at(0).reason, "12.04: no units are eligible to fight");
    ASSERT_EQ(gs.flow.stage, Stage::Consolidate);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Consolidate{}}));
    EXPECT_EQ(legal_actions(gs, Side::A),
              (std::vector<GameAction>{action::EndConsolidations{}, action::StartConsolidation{marines, {gants}}}));
    act(gs, action::EndConsolidations{});
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::Consolidate{}}));
    act(gs, action::EndConsolidations{});

    // The turn ends, and Fights First (24.13) with it.
    EXPECT_EQ(turn_side(gs), Side::B);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::MoveUnits{}}));
    EXPECT_FALSE(is_fights_first_unit(gs.units[kMarines]));
    EXPECT_FALSE(gs.units[kMarines].turn_flags.made_charge_move);
}

TEST(R12_04_Fight, OnlyFightsFirstUnitsInTheFightsFirstStep) {
    OverrunSetup s = overrun_setup(3.0f);
    to_fight_step(s.gs);
    // Unit 3 is engaged but did not charge: it cannot be selected before the Fights First units.
    const auto reasons = rejection(s.gs, Side::A, action::SelectToFight{s.third, FightType::Normal});
    EXPECT_EQ(reasons.at(0), "12.04: only Fights First units can be selected in the Resolve Fights First Combats step");
}

// ==== 12.06 Overrun Fight (acceptance) ====

TEST(R12_06_OverrunFight, AUnitNoLongerEngagedWhenSelectedMakesAnOverrunFight) {
    OverrunSetup s = overrun_setup(3.0f);
    GameState& gs = s.gs;
    to_fight_step(gs);
    marines_destroy_the_termagants(gs);

    // Side B has nothing eligible; unit 3 was engaged at the start of the step but is now unengaged.
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::SelectUnitToFight{}}));
    EXPECT_EQ(legal_actions(gs, Side::A), (std::vector<GameAction>{action::SelectToFight{s.third, FightType::Overrun}}));
    EXPECT_EQ(rejection(gs, Side::A, action::SelectToFight{s.third, FightType::Normal}).at(0),
              "12.05: unit 3 is unengaged; a normal fight needs an engaged unit");

    act(gs, action::SelectToFight{s.third, FightType::Overrun});
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::OverrunPileIn{s.third}}));
    EXPECT_EQ(legal_actions(gs, Side::A),
              (std::vector<GameAction>{action::StartPileIn{s.third, {s.fourth}}, action::SkipPileIn{s.third}}));

    // 12.03 After Moving: the unit must be engaged; a failed move returns to the same decision (D-022).
    const ModelId m = gs.find_unit(s.third)->models[0].id;
    const Position start = gs.find_unit(s.third)->models[0].position;
    act(gs, action::StartPileIn{s.third, {s.fourth}});
    act(gs, action::ProposeModelMove{m, {Position{start.x - in(0.5f), start.y, 0}}});
    const auto reverted = events_of<event::UnitMoveReverted>(act(gs, action::CommitUnitMove{}));
    ASSERT_EQ(reverted.size(), 1u);
    EXPECT_TRUE(contains(reverted[0].reasons, "12.03: unit 3 must be engaged after its pile-in move"));
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::OverrunPileIn{s.third}}));

    act(gs, action::StartPileIn{s.third, {s.fourth}});
    act(gs, action::ProposeModelMove{m, {Position{start.x - in(2), start.y, 0}}});
    act(gs, action::CommitUnitMove{});
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::DeclareMeleeAttacks{s.third}}));
    ScriptedDice misses = rolls(3, 1);
    act(gs, first_melee_options(gs, s.third), misses);

    // Unit 4 is now engaged: eligible, and both fight types apply (it became engaged during the phase).
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::SelectUnitToFight{}}));
    EXPECT_EQ(legal_actions(gs, Side::B),
              (std::vector<GameAction>{action::SelectToFight{s.fourth, FightType::Normal},
                                       action::SelectToFight{s.fourth, FightType::Overrun}}));
}

TEST(R12_06_OverrunFight, NeedsTheUnitToStillBeEligibleToFight) {
    // Appendix FAQ: a unit eligible for an overrun fight but not eligible to fight cannot make one.
    GameState gs = far_apart_fighters(false);
    gs.units[kMarines].phase_flags.selected_to_fight = true;
    EXPECT_EQ(fight_type_problem(gs, gs.units[kMarines], FightType::Overrun), "12.04: unit 1 is not eligible to fight");
}

// ==== D-020: re-entering the Fights First step, and a pass in it ====

TEST(D020_FightSequence, ANewlyEligibleFightsFirstUnitFightsNextWhenTheAlternationReachesIt) {
    OverrunSetup s = overrun_setup(3.0f);
    GameState& gs = s.gs;
    gs.find_unit(s.fourth)->turn_flags.fights_first = true;  // test-only: Fights First from another source
    to_fight_step(gs);
    marines_destroy_the_termagants(gs);
    act(gs, action::SelectToFight{s.third, FightType::Overrun});
    const ModelId m = gs.find_unit(s.third)->models[0].id;
    const Position start = gs.find_unit(s.third)->models[0].position;
    act(gs, action::StartPileIn{s.third, {s.fourth}});
    act(gs, action::ProposeModelMove{m, {Position{start.x - in(2), start.y, 0}}});
    act(gs, action::CommitUnitMove{});
    ScriptedDice misses = rolls(3, 1);
    act(gs, first_melee_options(gs, s.third), misses);

    // 12.04: after a fight in Resolve Remaining Combats, a Fights First unit is eligible: back to that
    // step, with the opponent of the player who just fought selecting (D-020).
    EXPECT_EQ(sequence(gs).sub_step, FightSubStep::FightsFirst);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::SelectUnitToFight{}}));
}

TEST(D020_FightSequence, APassInTheFightsFirstStepMovesOnWhenTheOpponentCannotSelect) {
    GameState gs = far_apart_fighters(true);
    to_fight_step(gs);
    ASSERT_EQ(sequence(gs).sub_step, FightSubStep::FightsFirst);
    EXPECT_EQ(legal_actions(gs, Side::A),
              (std::vector<GameAction>{action::SelectToFight{id_of(gs, kMarines), FightType::Overrun},
                                       action::PassFight{}}));
    act(gs, action::PassFight{});
    // Side B has no Fights First unit: Resolve Remaining Combats, Side B selecting.
    EXPECT_EQ(sequence(gs).sub_step, FightSubStep::Remaining);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::SelectUnitToFight{}}));
    const auto events = act(gs, action::PassFight{});
    EXPECT_EQ(events_of<event::FightStepEnded>(events).at(0).reason, "Appendix: both players passed in succession");
}

// ==== Appendix: Eligible to Fight, but Unable to Fight (acceptance) ====

TEST(Appendix_PassingInTheFightPhase, RejectedWhileAnEligibleUnitIsWithin5InchesOfAnEnemy) {
    OverrunSetup s = overrun_setup(3.0f);
    to_fight_step(s.gs);
    marines_destroy_the_termagants(s.gs);
    EXPECT_FALSE(may_pass_fight(s.gs, Side::A));
    EXPECT_EQ(rejection(s.gs, Side::A, action::PassFight{}).at(0),
              "12.04: Side A cannot pass while a unit of theirs that is eligible to fight is within 5\" of an enemy "
              "unit (Appendix: Eligible to Fight, but Unable to Fight)");
}

TEST(Appendix_PassingInTheFightPhase, AcceptedBeyond5InchesAndEndsTheStepWhenTheOpponentHasNoEligibleUnits) {
    OverrunSetup s = overrun_setup(6.0f);
    to_fight_step(s.gs);
    marines_destroy_the_termagants(s.gs);
    expect_valid(s.gs, Side::A, action::PassFight{});
    const auto events = act(s.gs, action::PassFight{});
    EXPECT_EQ(events_of<event::FightPassed>(events).at(0).side, Side::A);
    EXPECT_EQ(events_of<event::FightStepEnded>(events).at(0).reason,
              "Appendix: Side A passed and Side B has no units eligible to fight");
}

TEST(Appendix_PassingInTheFightPhase, BothPlayersPassingInSuccessionEndsTheStep) {
    GameState gs = far_apart_fighters(false);
    to_fight_step(gs);
    EXPECT_EQ(sequence(gs).sub_step, FightSubStep::Remaining);  // no Fights First units
    act(gs, action::PassFight{});
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::SelectUnitToFight{}}));
    const auto events = act(gs, action::PassFight{});
    EXPECT_EQ(events_of<event::FightStepEnded>(events).at(0).reason, "Appendix: both players passed in succession");
    EXPECT_NE(gs.flow.stage, Stage::Fight);
}

// ==== 12.02 / 12.03 Pile-in moves ====

TEST(R12_03_PileInMove, ModelsInBaseContactCannotBeMovedAndMovedModelsMustEndCloser) {
    GameState gs = after_successful_charge();
    // Put the centre Intercessor in base contact with the Termagant in front of it.
    Model& centre = gs.units[kMarines].models[2];
    const Model& gant = *std::min_element(gs.units[kGants].models.begin(), gs.units[kGants].models.end(),
                                          [&](const Model& a, const Model& b) {
                                              return std::abs(a.position.x - centre.position.x) + a.position.y <
                                                     std::abs(b.position.x - centre.position.x) + b.position.y;
                                          });
    centre.position = Position{gant.position.x, gant.position.y - kGantRadius - kMarineRadius, 0.0f};
    act(gs, action::EndChargeStep{});
    act(gs, action::StartPileIn{id_of(gs, kMarines), {id_of(gs, kGants)}});

    const Position c = centre.position;
    // (Not sideways: its neighbours' bases touch it since M14, and D-027 rejects ending on one.)
    EXPECT_EQ(rejection(gs, Side::A, action::ProposeModelMove{centre.id, {Position{c.x, c.y - in(0.5f), 0}}}).at(0),
              "12.03: model 3 is in base contact with an enemy model and cannot be moved");
    const Model& west = gs.units[kMarines].models[0];
    const Position w = west.position;
    EXPECT_EQ(rejection(gs, Side::A, action::ProposeModelMove{west.id, {Position{w.x, w.y - in(0.5f), 0}}}),
              (std::vector<std::string>{"12.03: model 1 must end its move closer to the closest pile-in target"}));
    expect_valid(gs, Side::A, action::ProposeModelMove{west.id, {Position{w.x, w.y + in(0.5f), 0}}});
}

TEST(R12_02_PileIn, EachUnitPilesInOnceThenTheOpponentMoves) {
    GameState gs = after_successful_charge();
    act(gs, action::EndChargeStep{});
    act(gs, action::StartPileIn{id_of(gs, kMarines), {id_of(gs, kGants)}});
    const Model& west = gs.units[kMarines].models[0];
    act(gs, action::ProposeModelMove{west.id, {Position{west.position.x, west.position.y + in(0.5f), 0}}});
    act(gs, action::CommitUnitMove{});
    EXPECT_TRUE(gs.units[kMarines].phase_flags.piled_in);
    // Side A has no other unit that can pile in: its moves are over and Side B moves.
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::PileIn{}}));
}

TEST(R12_03_PileInMove, AnEngagedUnitSelectsEveryUnitItIsEngagedWith) {
    GameState gs = after_successful_charge();
    const Model& east = gs.units[kMarines].models[4];
    const UnitId fourth = add_one_model_unit(
        gs, kGants, Position{east.position.x + kMarineRadius + kGantRadius + in(1), east.position.y, 0});
    act(gs, action::EndChargeStep{});
    EXPECT_EQ(rejection(gs, Side::A, action::StartPileIn{id_of(gs, kMarines), {id_of(gs, kGants)}}).at(0),
              "12.03: unit 1 is engaged, so it must select every enemy unit it is engaged with as a pile-in target");
    expect_valid(gs, Side::A, action::StartPileIn{id_of(gs, kMarines), {id_of(gs, kGants), fourth}});
}

TEST(R12_03_PileInMove, ModelsThatStartedEngagedMustStayEngagedWithThatUnit) {
    GameState gs = after_successful_charge();
    std::vector<ModelStart> starts;
    for (const Model& m : gs.units[kMarines].models) starts.push_back(ModelStart{m.id, m.position});
    gs.units[kMarines].models[0].position.y -= in(2);
    EXPECT_EQ(still_engaged_problems(gs, gs.units[kMarines], starts, "12.03"),
              (std::vector<std::string>{"12.03: model 1 started the move engaged with unit 2 and must still be engaged "
                                        "with it"}));
}

// ==== 04.01 / 04.02 While Fighting (D-021) ====

namespace {

// After the charge, one more Termagant engaged with the easternmost Intercessor only; at Side A's
// melee declaration for the Intercessors.
struct SplitSetup {
    GameState gs;
    UnitId fourth;
};

SplitSetup split_setup() {
    SplitSetup s{after_successful_charge(), {}};
    const Model& east = s.gs.units[kMarines].models[4];
    s.fourth = add_one_model_unit(
        s.gs, kGants, Position{east.position.x + kMarineRadius + kGantRadius + in(1), east.position.y, 0});
    to_fight_step(s.gs);
    act(s.gs, action::SelectToFight{id_of(s.gs, kMarines), FightType::Normal});
    return s;
}

}  // namespace

TEST(R04_01_SelectWeapons, EveryModelThatCanFightSelectsOneMeleeWeapon) {
    SplitSetup s = split_setup();
    const UnitId marines = id_of(s.gs, kMarines);
    action::Fight a = first_melee_options(s.gs, marines);
    ASSERT_EQ(a.declarations.size(), 5u);
    expect_valid(s.gs, Side::A, a);

    action::Fight missing = a;
    missing.declarations.erase(missing.declarations.begin());
    EXPECT_EQ(rejection(s.gs, Side::A, missing),
              (std::vector<std::string>{
                  "04.01: model 1 is engaged with an enemy unit and must select one melee weapon (D-021)"}));
    action::Fight twice = a;
    twice.declarations.push_back(a.declarations[0]);
    EXPECT_TRUE(contains(rejection(s.gs, Side::A, twice), "04.01: model 1 must select one melee weapon, not more"));
    action::Fight ranged = a;
    ranged.declarations[0].weapon_index = kBoltRifle;
    EXPECT_EQ(rejection(s.gs, Side::A, ranged).at(0),
              "04.01: 'Bolt Rifle - Focused Fire' is not a melee weapon; models fight with melee weapons");
    EXPECT_EQ(rejection(s.gs, Side::A, action::Fight{marines, {}}).at(0).substr(0, 7), "D-021: ");
}

TEST(R04_02_SelectTargets, MeleeTargetsMustBeEngagedWithTheModel) {
    SplitSetup s = split_setup();
    action::Fight a = first_melee_options(s.gs, id_of(s.gs, kMarines));
    a.declarations[0].targets = {MeleeTarget{s.fourth, 3}};  // unit 4 is only engaged with model 5
    EXPECT_EQ(rejection(s.gs, Side::A, a).at(0), "04.02: unit 3 is not an enemy unit engaged with model 1");
}

TEST(R04_02_SplittingMeleeAttacks, AttacksAreSplitAcrossEngagedTargetsAtLeastOneEach) {
    SplitSetup s = split_setup();
    const UnitId gants = id_of(s.gs, kGants);
    action::Fight a = first_melee_options(s.gs, id_of(s.gs, kMarines));
    MeleeDeclaration& east = a.declarations[4];
    ASSERT_EQ(east.weapon_index, kKnives);

    east.targets = {MeleeTarget{gants, 2}, MeleeTarget{s.fourth, 1}};
    expect_valid(s.gs, Side::A, a);
    east.targets = {MeleeTarget{gants, 3}, MeleeTarget{s.fourth, 0}};
    EXPECT_EQ(rejection(s.gs, Side::A, a).at(0),
              "04.02: declare at least one attack against each target (Splitting Melee Attacks)");
    east.targets = {MeleeTarget{gants, 2}, MeleeTarget{s.fourth, 2}};
    EXPECT_EQ(rejection(s.gs, Side::A, a).at(0), "04.02: model 5's 'Knives and Fists' has 3 attacks but 4 are declared");

    east.targets = {MeleeTarget{gants, 2}, MeleeTarget{s.fourth, 1}};
    ScriptedDice misses = rolls(15, 1);
    const auto events = act(s.gs, a, misses);
    const auto batches = events_of<event::AttackBatchesGathered>(events).at(0).batches;
    ASSERT_EQ(batches.size(), 2u);  // one per target
    EXPECT_EQ(batches[0].attack_dice, 14);
    EXPECT_EQ(batches[1].attack_dice, 1);
}

TEST(R04_02_SplittingMeleeAttacks, AWeaponCannotSelectMoreTargetsThanItsAttacks) {
    // The Termagant at the west end of the front rank is engaged with the Intercessors and with unit 3;
    // its claws have A1.
    OverrunSetup s = overrun_setup(3.0f);
    to_fight_step(s.gs);
    act(s.gs, action::SelectToFight{id_of(s.gs, kMarines), FightType::Normal});
    ScriptedDice misses = rolls(15, 1);
    act(s.gs, first_melee_options(s.gs, id_of(s.gs, kMarines)), misses);
    act(s.gs, action::SelectToFight{id_of(s.gs, kGants), FightType::Normal});
    action::Fight a = first_melee_options(s.gs, id_of(s.gs, kGants));
    auto it = std::find_if(a.declarations.begin(), a.declarations.end(), [&](const MeleeDeclaration& d) {
        return enemy_units_engaged_with_model(s.gs, s.gs.units[kGants], *find_model(s.gs, d.model),
                                              find_model(s.gs, d.model)->position)
                   .size() == 2;
    });
    ASSERT_NE(it, a.declarations.end());
    it->targets = {MeleeTarget{id_of(s.gs, kMarines), 1}, MeleeTarget{s.third, 1}};
    EXPECT_EQ(rejection(s.gs, Side::B, a).at(0), "04.02: 'Chitinous claws and teeth' (A1) cannot select 2 targets");
}

// ==== 12.07 / 12.08 Consolidation (acceptance) ====

TEST(R12_08_ConsolidationMove, AnEngagedUnitMustMakeAnOngoingConsolidation) {
    GameState gs = after_successful_charge();
    to_fight_step(gs);
    act(gs, action::SelectToFight{id_of(gs, kMarines), FightType::Normal});
    ScriptedDice misses = rolls(15, 1);
    act(gs, first_melee_options(gs, id_of(gs, kMarines)), misses);
    act(gs, action::SelectToFight{id_of(gs, kGants), FightType::Normal});
    ScriptedDice gant_misses = rolls(5, 1);
    act(gs, first_melee_options(gs, id_of(gs, kGants)), gant_misses);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Consolidate{}}));
    EXPECT_EQ(consolidation_mode(gs, gs.units[kMarines]), ConsolidationMode::Ongoing);

    const auto events = act(gs, action::StartConsolidation{id_of(gs, kMarines), {id_of(gs, kGants)}});
    EXPECT_EQ(events_of<event::ConsolidationStarted>(events).at(0).mode, ConsolidationMode::Ongoing);
    const Model& west = gs.units[kMarines].models[0];
    act(gs, action::ProposeModelMove{west.id, {Position{west.position.x, west.position.y + in(0.5f), 0}}});
    act(gs, action::CommitUnitMove{});
    EXPECT_TRUE(gs.units[kMarines].phase_flags.consolidated);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::Consolidate{}}));
}

TEST(R12_08_ConsolidationMove, EngagingConsolidationForcesAnEnemyThatHasNotFoughtToFight) {
    GameState gs = after_successful_charge();
    const Model& east = gs.units[kMarines].models[4];
    const UnitId fourth = add_one_model_unit(
        gs, kGants, Position{east.position.x + kMarineRadius + kGantRadius + in(2.5f), east.position.y, 0});
    to_fight_step(gs);
    marines_destroy_the_termagants(gs);

    // Unit 4 is not eligible to fight, so the Fight step ends. The Intercessors are unengaged but within 3".
    ASSERT_EQ(gs.flow.stage, Stage::Consolidate);
    EXPECT_EQ(consolidation_mode(gs, gs.units[kMarines]), ConsolidationMode::Engaging);
    EXPECT_EQ(legal_actions(gs, Side::A),
              (std::vector<GameAction>{action::EndConsolidations{},
                                       action::StartConsolidation{id_of(gs, kMarines), {fourth}}}));
    act(gs, action::StartConsolidation{id_of(gs, kMarines), {fourth}});
    const Model& mover = gs.units[kMarines].models[4];
    act(gs, action::ProposeModelMove{mover.id, {Position{mover.position.x + in(0.9f), mover.position.y, 0}}});
    const auto events = act(gs, action::CommitUnitMove{});
    EXPECT_EQ(events_of<event::UnitsForcedToFight>(events).at(0).units, (std::vector<UnitId>{fourth}));

    // 12.08 After Moving: the opponent must select unit 4; it becomes eligible and is selected to fight.
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::B, decision::ForcedFight{}}));
    EXPECT_EQ(legal_actions(gs, Side::B),
              (std::vector<GameAction>{action::SelectToFight{fourth, FightType::Normal},
                                       action::SelectToFight{fourth, FightType::Overrun}}));
    const auto selected = act(gs, action::SelectToFight{fourth, FightType::Normal});
    EXPECT_TRUE(events_of<event::UnitSelectedToFight>(selected).at(0).forced);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::B, decision::DeclareMeleeAttacks{fourth}}));
    ScriptedDice miss = rolls(1, 1);
    act(gs, first_melee_options(gs, fourth), miss);
    EXPECT_EQ(miss.remaining(), 0u);
    EXPECT_EQ(gs.flow.stage, Stage::Consolidate);  // the fight happened after the Fight step
    // Unit 4 was eligible to fight this phase, and it is engaged: Side B's Ongoing Consolidation.
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::Consolidate{}}));
}

TEST(R12_08_ConsolidationMove, AUnitThatMeetsNoModeCannotConsolidate) {
    GameState gs = after_successful_charge();
    const Model& east = gs.units[kMarines].models[4];
    add_one_model_unit(gs, kGants,
                       Position{east.position.x + kMarineRadius + kGantRadius + in(3.5f), east.position.y, 0});
    to_fight_step(gs);
    marines_destroy_the_termagants(gs);
    // Unengaged, no enemy unit within 3" and no objectives in V1 (D-022): the Consolidate step asks nothing.
    EXPECT_FALSE(consolidation_mode(gs, gs.units[kMarines]).has_value());
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::MoveUnits{}}));
}

// ==== D-026: weapons with abilities V1 does not implement ====

TEST(D026_UnimplementedAbilities, ADevastatingWoundsMeleeWeaponIsRejectedAndTheModelNeedNotFightWithIt) {
    GameState gs = after_successful_charge();
    // Every Intercessor's only melee weapon gets [DEVASTATING WOUNDS] (24.10, not implemented in V1).
    for (Model& m : gs.units[kMarines].models) m.weapons[kKnives].profiles[0].abilities.push_back(ability::DevastatingWounds{});
    to_fight_step(gs);
    act(gs, action::SelectToFight{id_of(gs, kMarines), FightType::Normal});
    // No model can make a melee attack V1 can resolve, so the fight resolves with no attacks (D-021).
    EXPECT_TRUE(melee_options(gs, gs.units[kMarines]).empty());
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::B, decision::SelectUnitToFight{}}));

    // A declaration naming the weapon anyway is rejected with the rule.
    GameState probe = after_successful_charge();
    for (Model& m : probe.units[kMarines].models) m.weapons[kKnives].profiles[0].abilities.push_back(ability::DevastatingWounds{});
    probe.units[kMarines].models[0].weapons[kKnives].profiles[0].abilities.clear();  // one model can fight
    to_fight_step(probe);
    act(probe, action::SelectToFight{id_of(probe, kMarines), FightType::Normal});
    ASSERT_EQ(*probe.pending, (PendingDecision{Side::A, decision::DeclareMeleeAttacks{id_of(probe, kMarines)}}));
    action::Fight a = first_melee_options(probe, id_of(probe, kMarines));
    ASSERT_EQ(a.declarations.size(), 1u);
    a.declarations.push_back(MeleeDeclaration{probe.units[kMarines].models[1].id, kKnives, 0,
                                              {MeleeTarget{id_of(probe, kGants), 3}}});
    const auto reasons = rejection(probe, Side::A, a);
    EXPECT_EQ(reasons.at(0), "24.10: 'Knives and Fists' has [DEVASTATING WOUNDS], which V1 does not implement yet (D-026)");
}

// ==== D-007 with forced fights ====

TEST(D007_GameEnd, AWipeOutDuringForcedFightsEndsTheGameAtOnce) {
    // One Intercessor (1 wound left) has charged one Termagant. Two more one-Termagant units stand 2.5"
    // away, north-east and south-east of it.
    GameState gs = after_successful_charge();
    Unit& marines = gs.units[kMarines];
    const Model east = marines.models[4];
    marines.models = {east};
    marines.models[0].wounds_remaining = 1;
    const Model* front = &gs.units[kGants].models[0];
    for (const Model& m : gs.units[kGants].models) {
        if (base_distance(m.position, m.base, east.position, east.base) <
            base_distance(front->position, front->base, east.position, east.base)) {
            front = &m;
        }
    }
    const Model gant = *front;
    gs.units[kGants].models = {gant};
    const float d = 92.0f;  // centre offset east: about 2.5" edge to edge at +/-18 mm north/south
    const UnitId third = add_one_model_unit(gs, kGants, Position{east.position.x + d, east.position.y + 18.0f, 0});
    const UnitId fourth = add_one_model_unit(gs, kGants, Position{east.position.x + d, east.position.y - 18.0f, 0});
    ASSERT_FALSE(is_engaged(gs, *gs.find_unit(third)));
    to_fight_step(gs);

    // The Intercessor destroys the Termagant it charged; units 3 and 4 are not eligible to fight.
    act(gs, action::SelectToFight{id_of(gs, kMarines), FightType::Normal});
    ScriptedDice kill{6, 6, 6, 6, 6, 6, 1, 1, 1};
    act(gs, first_melee_options(gs, id_of(gs, kMarines)), kill);
    ASSERT_EQ(gs.flow.stage, Stage::Consolidate);

    // Engaging Consolidation into both: Side B must make both fight (12.08).
    act(gs, action::StartConsolidation{id_of(gs, kMarines), {third, fourth}});
    const Model& m = gs.units[kMarines].models[0];
    act(gs, action::ProposeModelMove{m.id, {Position{m.position.x + in(0.9f), m.position.y, 0}}});
    act(gs, action::CommitUnitMove{});
    ASSERT_EQ(gs.flow.forced_fights, (std::vector<UnitId>{third, fourth}));

    // The first forced fight destroys Side A's last model: the game ends now (D-007), with no fight for
    // unit 4.
    act(gs, action::SelectToFight{third, FightType::Normal});
    ScriptedDice lethal{6, 6, 1};
    const auto events = act(gs, first_melee_options(gs, third), lethal);
    EXPECT_EQ(gs.flow.stage, Stage::GameOver);
    ASSERT_TRUE(gs.result.has_value());
    EXPECT_EQ(gs.result->winner, Side::B);
    EXPECT_FALSE(gs.pending.has_value());
    EXPECT_EQ(events_of<event::GameOver>(events).size(), 1u);
}

TEST(R02_02_Profiles, AModelWithMoveDashCannotBeMovedInAPileIn) {
    // 02.02: M '-' means the model can be set up but cannot otherwise be moved. A stand-in: the charging
    // Intercessors lose their M characteristic before piling in.
    GameState gs = after_successful_charge();
    for (Model& m : gs.units[kMarines].models) m.characteristics.move_mm.reset();
    act(gs, action::EndChargeStep{});
    act(gs, action::StartPileIn{id_of(gs, kMarines), {id_of(gs, kGants)}});
    const Model& west = gs.units[kMarines].models[0];
    EXPECT_EQ(rejection(gs, Side::A, action::ProposeModelMove{west.id, {Position{west.position.x, west.position.y + in(0.5f), 0}}}),
              (std::vector<std::string>{"02.02: model 1 has M '-' and cannot be moved"}));
}
