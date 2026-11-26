// D-099 (M97): the own-turn core stratagems that end in mortal wounds: 15.05 Explosives, offered at the Shoot step, and
// 15.06 Crushing Impact, in a window just after a charge move. Each starts an effect (FlowState::effect) once its CP are
// paid: a model and an enemy unit selected together, the roll, then the casualties (06.02), the enemy's first (D-007).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/data/load_error.hpp"
#include "core/engine/casualties.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/distance.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/shooting.hpp"
#include "core/rules/stratagem_effects.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/points_destroyed.hpp"
#include "core/state/queries.hpp"
#include "core/geometry/terrain_shape.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::add_unit_copy;
using wh40k::test::at;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::in;
using wh40k::test::is_rule_cited;
using wh40k::test::model_ids;
using wh40k::test::rejection;
namespace mf = wh40k::test::monsters;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

std::vector<GameEvent> paced(GameState& gs, const GameAction& a, DiceSource& dice) {
    return apply_action_paced(gs, gs.pending->side, a, dice);
}
std::vector<GameEvent> paced(GameState& gs, const GameAction& a) {
    ScriptedDice none{std::vector<int>{}};
    return paced(gs, a, none);
}

// Makes the pending roll, paced, from `faces`.
std::vector<GameEvent> roll(GameState& gs, std::vector<int> faces) {
    EXPECT_TRUE(roll_pending(gs)) << (gs.pending ? describe(gs, *gs.pending) : std::string("no decision"));
    const auto& d = std::get<decision::RollDice>(gs.pending->kind);
    ScriptedDice dice(std::move(faces));
    std::vector<GameEvent> e = paced(gs, action::RollDice{d.roll, d.unit}, dice);
    EXPECT_EQ(dice.remaining(), 0u);
    return e;
}

action::UseStratagem use(Stratagem s, UnitId unit) { return action::UseStratagem{s, unit, std::nullopt}; }

std::string label(UnitId id) { return "unit " + std::to_string(id.v); }
std::string model_label(ModelId id) { return "model " + std::to_string(id.v); }

template <typename E>
std::size_t count(const std::vector<GameEvent>& events) {
    return events_of<E>(events).size();
}

bool offered(const GameState& gs, const GameAction& a) {
    const std::vector<GameAction> legal = legal_actions(gs, gs.pending->side);
    return std::find(legal.begin(), legal.end(), a) != legal.end();
}

// Answers the pending casualty order with the unit's own model order (D-013), paced.
std::vector<GameEvent> order_casualties(GameState& gs) {
    const UnitId target = std::get<decision::OrderCasualties>(gs.pending->kind).target;
    return paced(gs, action::OrderCasualties{target, model_ids(*gs.find_unit(target))});
}

int wounds_left(const Unit& u) {
    int w = 0;
    for (const Model& m : u.models) w += m.wounds_remaining;
    return w;
}

// ---- 15.05 fixtures: V1 at Side A's Shoot step ----

// The V1 scenario at Side A's Shoot step (round 1), the Termagants moved so the closest of them is `gap_in` from the
// Intercessors (edge to edge), Side A with `cp_a` CP.
GameState explosives_step(float gap_in = 6.0f, int cp_a = 1) {
    GameState gs = wh40k::test::shoot_step_state(Side::A);
    const float gap = mm_to_inches(*unit_distance(gs.units[kMarines], gs.units[kGants]));
    for (Model& m : gs.units[kGants].models) m.position.y -= in(gap - gap_in);
    gs.command_points[0] = cp_a;
    gs.pending = compute_decision(gs);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
    return gs;
}

// Explosives used on the Intercessors and the first legal pair selected: the roll waits.
GameState explosives_roll_waits(float gap_in = 6.0f, int cp_a = 1) {
    GameState gs = explosives_step(gap_in, cp_a);
    paced(gs, use(Stratagem::Explosives, gs.units[kMarines].id));
    const std::vector<GameAction> pairs = legal_actions(gs, Side::A);
    EXPECT_FALSE(pairs.empty());
    if (!pairs.empty()) paced(gs, pairs.front());
    return gs;
}

// ---- 15.06 fixtures: the monsters fixture (D-073), the Hive Tyrant charging the Legionaries ----

// Side B's Charge decision in round 1 (every unit remained stationary, no shooting), Side B with `cp_b` CP.
GameState b_charge_step(int cp_b) {
    GameState gs = mf::at_movement();
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::Charge>(g.pending->kind) && g.pending->side == Side::B;
    });
    gs.command_points[1] = cp_b;
    gs.pending = compute_decision(gs);
    return gs;
}

UnitId tyrant(const GameState& gs) { return gs.units[mf::kTyrant].id; }
UnitId legionaries(const GameState& gs) { return gs.units[mf::kLegionaries].id; }

// The Tyrant declares a charge (a 6: 3+3, inline: any window declined), selects the Legionaries and moves straight south
// to `gap_in` from their rank; the move is committed paced, so 15.06's window waits.
std::vector<GameEvent> tyrant_charges(GameState& gs, float gap_in = 0.5f) {
    ScriptedDice six{3, 3};
    apply_action(gs, Side::B, action::DeclareCharge{tyrant(gs)}, six);
    EXPECT_TRUE(gs.pending && std::holds_alternative<decision::SelectChargeTargets>(gs.pending->kind));
    apply_action(gs, Side::B, action::StartChargeMove{tyrant(gs), {legionaries(gs)}});
    const Model& t = gs.units[mf::kTyrant].models[0];
    const Position end{t.position.x, in(mf::kRankY + mf::tyrant_centre_gap(gap_in)), 0.0f};
    apply_action(gs, Side::B, action::ProposeModelMove{t.id, {end}});
    return paced(gs, action::CommitUnitMove{});
}

// Side B's Crushing Impact window, just after the Tyrant's charge move.
GameState crushing_window(int cp_b = 1) {
    GameState gs = b_charge_step(cp_b);
    tyrant_charges(gs);
    return gs;
}

PendingDecision crushing_decision(const GameState& gs) {
    return PendingDecision{Side::B, decision::UseStratagems{WindowTiming::AfterChargeMove, tyrant(gs)}};
}

// Crushing Impact used and its pair selected: the T D6 wait.
GameState crushing_roll_waits(int cp_b = 1) {
    GameState gs = crushing_window(cp_b);
    paced(gs, use(Stratagem::CrushingImpact, tyrant(gs)));
    paced(gs, action::SelectModelAndEnemy{gs.units[mf::kTyrant].models[0].id, legionaries(gs)});
    return gs;
}

}  // namespace

// ==== 15.05 Explosives: the offer at the Shoot step ====

TEST(R15_05_Explosives, IsOfferedAtTheShootStepAfterEveryShootTemplate) {
    const GameState gs = explosives_step();
    const UnitId marines = gs.units[kMarines].id;
    const std::vector<GameAction> legal = legal_actions(gs, Side::A);
    ASSERT_GE(legal.size(), 3u);
    EXPECT_EQ(legal.front(), GameAction{action::EndShootStep{}});
    EXPECT_EQ(legal.back(), GameAction{use(Stratagem::Explosives, marines)});
    // Every Shoot template comes before it, so the earlier answers keep their indices.
    const auto first_use = std::find_if(legal.begin(), legal.end(), [](const GameAction& a) {
        return std::holds_alternative<action::UseStratagem>(a);
    });
    EXPECT_TRUE(std::none_of(first_use, legal.end(), [](const GameAction& a) {
        return std::holds_alternative<action::Shoot>(a);
    }));
    EXPECT_TRUE(std::holds_alternative<action::Shoot>(legal[1]));
    EXPECT_TRUE(validate_action(gs, Side::A, use(Stratagem::Explosives, marines)).ok());
}

TEST(R15_05_Explosives, UsingItPaysOneCpAndAsksForAModelAndAnEnemyUnit) {
    GameState gs = explosives_step(6.0f, 2);
    const UnitId marines = gs.units[kMarines].id;
    const UnitId gants = gs.units[kGants].id;
    const std::vector<GameEvent> e = paced(gs, use(Stratagem::Explosives, marines));
    const auto used = events_of<event::StratagemUsed>(e);
    ASSERT_EQ(used.size(), 1u);
    EXPECT_EQ(used[0], (event::StratagemUsed{Side::A, Stratagem::Explosives, marines, 1, 1}));
    EXPECT_EQ(event_rule(GameEvent{used[0]}), "15.01");
    EXPECT_EQ(gs.command_points[0], 1);
    EXPECT_EQ(gs.stratagems[0].used_this_phase, std::vector<Stratagem>{Stratagem::Explosives});
    EXPECT_EQ(gs.stratagems[0].targeted_this_phase, std::vector<UnitId>{marines});
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::SelectModelAndEnemy{Stratagem::Explosives, marines}}));
    EXPECT_EQ(compute_decision(gs), gs.pending);
    EXPECT_EQ(decision_rule(gs, *gs.pending), "15.05");
    // Every model is an EXPLOSIVES model; the Termagants are within 8" of (and visible to) some of them: the pairs in
    // model order.
    const std::vector<GameAction> legal = legal_actions(gs, Side::A);
    const std::vector<EffectPair> pairs = explosives_pairs(gs, gs.units[kMarines]);
    ASSERT_FALSE(pairs.empty());
    ASSERT_EQ(legal.size(), pairs.size());
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        EXPECT_EQ(legal[i], GameAction{(action::SelectModelAndEnemy{pairs[i].model, gants})});
        EXPECT_EQ(pairs[i].enemy, gants);
    }
    EXPECT_TRUE(legal_actions(gs, Side::B).empty());
}

TEST(R15_05_Explosives, SixDiceEachFourPlusIsAMortalWoundOnTheEnemyUnit) {
    GameState gs = explosives_step();
    const UnitId marines = gs.units[kMarines].id;
    const UnitId gants = gs.units[kGants].id;
    paced(gs, use(Stratagem::Explosives, marines));
    const ModelId model = gs.units[kMarines].models[2].id;
    const std::vector<GameEvent> selected = paced(gs, action::SelectModelAndEnemy{model, gants});
    EXPECT_EQ(events_of<event::ModelAndEnemySelected>(selected),
              (std::vector<event::ModelAndEnemySelected>{{Stratagem::Explosives, marines, model, gants}}));
    // The roll is the unit's player's: six D6, each needing 4+.
    ASSERT_EQ(gs.pending, (PendingDecision{Side::A, decision::RollDice{RollKind::Explosives, marines}}));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "15.05");
    const RollDiceInfo info = roll_dice_info(gs).value();
    EXPECT_EQ(info.roller, Side::A);
    EXPECT_EQ(info.dice, 6);
    EXPECT_EQ(info.expression, "D6");
    EXPECT_EQ(info.need, 4);
    EXPECT_EQ(info.target, gants);
    EXPECT_EQ(info.model, model);
    const std::size_t before = gs.units[kGants].models.size();
    const std::vector<GameEvent> rolled = roll(gs, {1, 2, 3, 4, 5, 6});
    const auto dice = events_of<event::StratagemDiceRolled>(rolled);
    ASSERT_EQ(dice.size(), 1u);
    EXPECT_EQ(dice[0], (event::StratagemDiceRolled{Stratagem::Explosives, marines, model, gants, {1, 2, 3, 4, 5, 6}, 3, 0}));
    EXPECT_EQ(event_rule(GameEvent{dice[0]}), "15.05");
    // No window follows (15.02 lists no such roll), even with CP left: 06.02's casualties, the Termagants' player's.
    EXPECT_FALSE(window_pending(gs));
    ASSERT_EQ(gs.pending, (PendingDecision{Side::B, decision::OrderCasualties{gants}}));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "06.02");
    const std::vector<GameEvent> inflicted = order_casualties(gs);
    const auto mw = events_of<event::MortalWoundsInflicted>(inflicted);
    ASSERT_EQ(mw.size(), 1u);
    EXPECT_EQ(mw[0].unit, gants);
    EXPECT_EQ(mw[0].result.steps.size(), 3u);
    EXPECT_EQ(count<event::ModelDestroyed>(inflicted), 3u);  // W1 each
    EXPECT_EQ(gs.units[kGants].models.size(), before - 3);
    // The effect is over: the Shoot decision again.
    EXPECT_FALSE(gs.flow.effect.has_value());
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
}

TEST(R15_05_Explosives, TheUnitCanStillShootAfterwards) {
    GameState gs = explosives_roll_waits();
    roll(gs, {1, 1, 1, 1, 1, 1});  // no mortal wound: no casualty decision
    ASSERT_EQ(gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
    const UnitId marines = gs.units[kMarines].id;
    // Explosives doesn't select the unit to shoot (10.02 Select Unit): its Shoot template is still offered.
    EXPECT_FALSE(gs.units[kMarines].phase_flags.selected_to_shoot);
    EXPECT_TRUE(offered(gs, action::Shoot{marines, ShootingType::Normal, {}}));
    // Once per phase (15.01): Explosives is no longer offered, and is refused.
    EXPECT_FALSE(offered(gs, use(Stratagem::Explosives, marines)));
}

TEST(R15_05_Explosives, NoOtherStratagemTargetsTheUnitThisPhase) {
    // 15.01: the unit was targeted by Explosives, so Command Re-roll can't re-roll its shooting rolls this phase.
    GameState gs = explosives_roll_waits(6.0f, 3);
    roll(gs, {1, 1, 1, 1, 1, 1});
    const UnitId marines = gs.units[kMarines].id;
    action::Shoot volley{marines, ShootingType::Normal, {}};
    for (const Model& m : gs.units[kMarines].models) {
        volley.shots.push_back(ShotDeclaration{m.id, 0, 0, gs.units[kGants].id});
    }
    ASSERT_TRUE(validate_action(gs, Side::A, volley).ok());
    paced(gs, volley);
    ASSERT_TRUE(roll_pending(gs));
    roll(gs, std::vector<int>(static_cast<std::size_t>(roll_dice_info(gs)->dice), 1));  // every hit roll misses
    EXPECT_FALSE(window_pending(gs)) << describe(gs, *gs.pending);
    EXPECT_EQ(gs.command_points[0], 2);
}

TEST(R15_05_Explosives, TheUnitMustBeExplosivesOrGrenades) {
    GameState gs = explosives_step();
    Unit& marines = gs.units[kMarines];
    std::erase_if(marines.keywords, [](const std::string& k) { return k == "Explosives"; });
    ASSERT_FALSE(is_explosives_or_grenades(marines, marines.models[0]));
    EXPECT_FALSE(offered(gs, use(Stratagem::Explosives, marines.id)));
    const auto reasons = rejection(gs, Side::A, use(Stratagem::Explosives, marines.id));
    ASSERT_FALSE(reasons.empty());
    EXPECT_EQ(reasons.front(), "15.05: Explosives targets an EXPLOSIVES/GRENADES unit (" + label(marines.id) + " is not)");
    // App 02.05.01: singular and plural alike, either keyword.
    for (const char* k : {"Grenades", "Grenade", "Explosive"}) {
        marines.keywords.push_back(k);
        EXPECT_TRUE(offered(gs, use(Stratagem::Explosives, marines.id))) << k;
        marines.keywords.pop_back();
    }
}

TEST(R15_05_Explosives, TheUnitMustBeUnengaged) {
    GameState gs = explosives_step(1.0f);  // the Termagants 1" away: engaged (03.04)
    const UnitId marines = gs.units[kMarines].id;
    ASSERT_TRUE(is_engaged(gs, gs.units[kMarines]));
    EXPECT_FALSE(offered(gs, use(Stratagem::Explosives, marines)));
    EXPECT_EQ(rejection(gs, Side::A, use(Stratagem::Explosives, marines)).front(),
              "15.05: Explosives targets an unengaged unit, and " + label(marines) + " is engaged");
}

TEST(R15_05_Explosives, TheUnitMustBeEligibleToShootAndNotHaveAdvanced) {
    const GameState base = explosives_step();
    const UnitId marines = base.units[kMarines].id;
    {
        GameState gs = base;
        gs.units[kMarines].turn_flags.advanced = true;
        EXPECT_FALSE(offered(gs, use(Stratagem::Explosives, marines)));
        EXPECT_EQ(rejection(gs, Side::A, use(Stratagem::Explosives, marines)).front(),
                  "15.05: " + label(marines) + " made an advance move this turn");
    }
    {
        GameState gs = base;
        gs.units[kMarines].turn_flags.fell_back = true;  // 09.07: not eligible to shoot
        EXPECT_FALSE(offered(gs, use(Stratagem::Explosives, marines)));
        EXPECT_EQ(rejection(gs, Side::A, use(Stratagem::Explosives, marines)).front(),
                  "09.07: " + label(marines) + " made a fall-back move this turn and is not eligible to shoot");
    }
    {
        GameState gs = base;
        gs.units[kMarines].phase_flags.selected_to_shoot = true;  // 10.02: it has shot
        EXPECT_FALSE(offered(gs, use(Stratagem::Explosives, marines)));
        EXPECT_EQ(rejection(gs, Side::A, use(Stratagem::Explosives, marines)).front(),
                  "10.02: " + label(marines) + " has already been selected to shoot this phase");
    }
}

TEST(R15_05_Explosives, CpOncePerPhaseAndBattleShockAreRuleCited) {
    {
        GameState gs = explosives_step(6.0f, 0);
        const UnitId marines = gs.units[kMarines].id;
        EXPECT_FALSE(offered(gs, use(Stratagem::Explosives, marines)));
        EXPECT_EQ(rejection(gs, Side::A, use(Stratagem::Explosives, marines)).front(),
                  "15.01: Side A has 0 CP; Explosives costs 1CP");
    }
    {
        GameState gs = explosives_step();
        gs.units[kMarines].battle_shocked = true;
        const UnitId marines = gs.units[kMarines].id;
        EXPECT_FALSE(offered(gs, use(Stratagem::Explosives, marines)));
        EXPECT_EQ(rejection(gs, Side::A, use(Stratagem::Explosives, marines)).front(),
                  "01.07: " + label(marines) + " is battle-shocked, so its player can't target it with stratagems");
    }
    {
        // A second Intercessor unit beside the first: once Explosives is used, the other can't use it this phase.
        GameState gs = explosives_step(6.0f, 2);
        const UnitId second = add_unit_copy(gs, kMarines);
        for (Model& m : gs.find_unit(second)->models) m.position.x += in(8.0f);
        gs.pending = compute_decision(gs);
        ASSERT_TRUE(offered(gs, use(Stratagem::Explosives, second)));
        paced(gs, use(Stratagem::Explosives, gs.units[kMarines].id));
        paced(gs, legal_actions(gs, Side::A).front());
        roll(gs, {1, 1, 1, 1, 1, 1});
        EXPECT_FALSE(offered(gs, use(Stratagem::Explosives, second)));
        EXPECT_EQ(rejection(gs, Side::A, use(Stratagem::Explosives, second)).front(),
                  "15.01: Side A has already used Explosives this phase");
    }
}

TEST(R15_05_Explosives, NeedsAnUnengagedEnemyUnitWithin8InchesOfAModel) {
    {
        GameState gs = explosives_step(8.5f);
        const UnitId marines = gs.units[kMarines].id;
        EXPECT_FALSE(offered(gs, use(Stratagem::Explosives, marines)));
        EXPECT_EQ(rejection(gs, Side::A, use(Stratagem::Explosives, marines)).front(),
                  "15.05: no model of " + label(marines) +
                      " has an unengaged enemy unit within 8\" and visible to it (D-099)");
    }
    // At exactly 8" (01.04 edge to edge, D-009) it is within.
    EXPECT_TRUE(offered(explosives_step(8.0f), use(Stratagem::Explosives, UnitId{explosives_step(8.0f).units[kMarines].id})));
}

TEST(R15_05_Explosives, TheSelectionRefusesEveryIllegalPair) {
    GameState gs = explosives_step(7.5f);
    const UnitId marines = gs.units[kMarines].id;
    const UnitId gants = gs.units[kGants].id;
    // A second Termagant unit, engaged with a second Intercessor unit far to the east.
    const UnitId engaged = add_unit_copy(gs, kGants);
    const UnitId blockers = add_unit_copy(gs, kMarines);
    for (Model& m : gs.find_unit(engaged)->models) m.position.x += in(30.0f);
    for (Model& m : gs.find_unit(blockers)->models) {
        m.position.x += in(30.0f);
        m.position.y += in(6.5f);  // 1" from the copied Termagants: engaged
    }
    gs.units[kMarines].models.back().position.y -= in(4.0f);  // one Intercessor 4" further back: beyond 8"
    gs.pending = compute_decision(gs);
    ASSERT_TRUE(is_engaged(gs, *gs.find_unit(engaged)));
    paced(gs, use(Stratagem::Explosives, marines));
    const std::vector<EffectPair> pairs = explosives_pairs(gs, gs.units[kMarines]);
    ASSERT_FALSE(pairs.empty());
    for (const EffectPair& p : pairs) EXPECT_EQ(p.enemy, gants);  // never the engaged unit, nor a friendly one
    // A model beyond 8" of the Termagants (the unit is 7.5" away at its closest).
    const Model* far = nullptr;
    for (const Model& m : gs.units[kMarines].models) {
        if (std::none_of(pairs.begin(), pairs.end(), [&m](const EffectPair& p) { return p.model == m.id; })) far = &m;
    }
    ASSERT_NE(far, nullptr) << "every Intercessor is within 8\"";
    EXPECT_EQ(rejection(gs, Side::A, action::SelectModelAndEnemy{far->id, gants}).front(),
              "15.05: " + label(gants) + " is not within 8\" of " + model_label(far->id));
    const ModelId near = pairs.front().model;
    EXPECT_EQ(rejection(gs, Side::A, action::SelectModelAndEnemy{near, engaged}).front(),
              "15.05: " + label(engaged) + " is engaged; Explosives selects an unengaged enemy unit");
    EXPECT_EQ(rejection(gs, Side::A, action::SelectModelAndEnemy{near, blockers}).front(),
              "15.05: " + label(blockers) + " is not an enemy unit on the battlefield");
    const ModelId gant_model = gs.units[kGants].models[0].id;
    EXPECT_EQ(rejection(gs, Side::A, action::SelectModelAndEnemy{gant_model, gants}).front(),
              "15.05: " + model_label(gant_model) + " is not in " + label(marines));
    EXPECT_TRUE(validate_action(gs, Side::A, action::SelectModelAndEnemy{near, gants}).ok());
}

TEST(R15_05_Explosives, TheEnemyUnitMustBeVisibleToTheModel) {
    // A solid wall (13.11: 5" Dense) right in front of the Intercessors: no line of sight to the Termagants.
    GameState gs = explosives_step(6.0f);
    float north = -1e9f;
    for (const Model& m : gs.units[kMarines].models) north = std::max(north, m.position.y);
    TerrainFeature wall;
    wall.name = "Wall";
    wall.category = TerrainCategory::Dense;
    wall.height_mm = in(5.0f);
    const float y0 = north + 16.0f + in(0.5f);
    wall.shape = polygon_shape({Position{in(-10.0f), y0, 0}, Position{in(40.0f), y0, 0}, Position{in(40.0f), y0 + in(0.5f), 0},
                                Position{in(-10.0f), y0 + in(0.5f), 0}});
    TerrainArea area;
    area.name = "Ruin";
    area.shape = wall.shape;
    area.features.push_back(wall);
    gs.terrain.push_back(area);
    const UnitId marines = gs.units[kMarines].id;
    ASSERT_FALSE(unit_visible_to_model(gs, gs.units[kMarines].models[0], gs.units[kGants]));
    EXPECT_FALSE(offered(gs, use(Stratagem::Explosives, marines)));
    EXPECT_EQ(explosives_pair_problem(gs, gs.units[kMarines], gs.units[kMarines].models[0].id, gs.units[kGants].id),
              "15.05: " + label(gs.units[kGants].id) + " is not visible to " +
                  model_label(gs.units[kMarines].models[0].id) + " (06.01)");
}

TEST(R15_05_Explosives, RefusedOutsideItsWhen) {
    // The opponent's Shoot step: Side B's decision, so Side A can't act; Side B has no EXPLOSIVES/GRENADES unit.
    {
        GameState gs = wh40k::test::shoot_step_state(Side::B);
        gs.command_points = {3, 3};
        gs.pending = compute_decision(gs);
        EXPECT_FALSE(rejection(gs, Side::A, use(Stratagem::Explosives, gs.units[kMarines].id)).empty());
        EXPECT_FALSE(offered(gs, use(Stratagem::Explosives, gs.units[kMarines].id)));
    }
    // In a window (Command Re-roll's, after Side A's hit rolls): refused with its WHEN.
    {
        GameState gs = explosives_step(6.0f, 2);
        const UnitId marines = gs.units[kMarines].id;
        action::Shoot volley{marines, ShootingType::Normal, {}};
        for (const Model& m : gs.units[kMarines].models) {
            volley.shots.push_back(ShotDeclaration{m.id, 0, 0, gs.units[kGants].id});
        }
        paced(gs, volley);
        roll(gs, std::vector<int>(static_cast<std::size_t>(roll_dice_info(gs)->dice), 1));
        ASSERT_TRUE(window_pending(gs));
        const auto reasons = rejection(gs, Side::A, use(Stratagem::Explosives, marines));
        ASSERT_FALSE(reasons.empty());
        EXPECT_EQ(reasons.front(),
                  "15.05: Explosives is used in your Shooting phase, when you select the next unit to shoot (10.02)");
    }
    // At the Charge decision: UseStratagem doesn't answer it (rule-cited).
    {
        GameState gs = wh40k::test::after_successful_charge();
        gs.command_points[0] = 2;
        gs.pending = compute_decision(gs);
        const auto reasons = rejection(gs, Side::A, use(Stratagem::Explosives, gs.units[kMarines].id));
        ASSERT_FALSE(reasons.empty());
        EXPECT_TRUE(is_rule_cited(reasons.front())) << reasons.front();
        EXPECT_EQ(reasons.front().substr(0, 6), "15.05:");
    }
    // At the Shoot decision every other stratagem is refused with its WHEN.
    {
        GameState gs = explosives_step(6.0f, 2);
        const UnitId marines = gs.units[kMarines].id;
        EXPECT_EQ(rejection(gs, Side::A, use(Stratagem::CommandReroll, marines)).front().substr(0, 6), "15.02:");
        EXPECT_EQ(rejection(gs, Side::A, use(Stratagem::CrushingImpact, marines)).front().substr(0, 6), "15.06:");
        EXPECT_EQ(rejection(gs, Side::A, action::UseStratagem{Stratagem::Explosives, marines, std::size_t{0}}).front(),
                  "15.05: Explosives picks no die");
        EXPECT_EQ(rejection(gs, Side::A,
                            action::UseStratagem{Stratagem::Explosives, marines, std::nullopt, StratagemMode::IntoTheFray})
                      .front(),
                  "15.05: Explosives has no modes");
    }
}

TEST(R15_05_Explosives, MortalWoundsLeftWhenTheUnitIsDestroyedAreLost) {
    // Two Termagants left (and a second Termagant unit, so the game goes on): six 4+ kill both, 4 mortal wounds lost.
    GameState gs = explosives_step();
    const UnitId other = add_unit_copy(gs, kGants);
    for (Model& m : gs.find_unit(other)->models) m.position.x += in(25.0f);
    gs.units[kGants].models.resize(2);
    gs.pending = compute_decision(gs);
    paced(gs, use(Stratagem::Explosives, gs.units[kMarines].id));
    paced(gs, action::SelectModelAndEnemy{explosives_pairs(gs, gs.units[kMarines]).front().model, gs.units[kGants].id});
    roll(gs, {6, 6, 6, 6, 6, 6});
    const std::vector<GameEvent> e = order_casualties(gs);
    const auto mw = events_of<event::MortalWoundsInflicted>(e);
    ASSERT_EQ(mw.size(), 1u);
    EXPECT_EQ(mw[0].result.models_destroyed, 2);
    EXPECT_EQ(mw[0].result.not_inflicted, 4);
    EXPECT_EQ(count<event::UnitDestroyed>(e), 1u);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
}

TEST(D099_Effect, ExplosivesDestroyingTheLastEnemyModelEndsTheGame) {
    GameState gs = explosives_step();
    gs.units[kGants].models.resize(3);
    gs.pending = compute_decision(gs);
    paced(gs, use(Stratagem::Explosives, gs.units[kMarines].id));
    paced(gs, legal_actions(gs, Side::A).front());
    roll(gs, {4, 4, 4, 1, 1, 1});
    const std::vector<GameEvent> e = order_casualties(gs);
    const auto over = events_of<event::GameOver>(e);
    ASSERT_EQ(over.size(), 1u);
    EXPECT_EQ(over[0].winner, Side::A);
    EXPECT_EQ(gs.flow.stage, Stage::GameOver);
    EXPECT_FALSE(gs.flow.effect.has_value());
    EXPECT_FALSE(gs.pending.has_value());
}

// ==== 15.06 Crushing Impact: the window just after a charge move ====

TEST(R15_06_CrushingImpact, TheWindowOpensJustAfterTheUnitsChargeMove) {
    GameState gs = b_charge_step(1);
    const std::vector<GameEvent> e = tyrant_charges(gs);
    // The charge move ends and the charge is resolved (11.02), then the window.
    const auto resolved = events_of<event::ChargeResolved>(e);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_TRUE(resolved[0].charge_move_made);
    EXPECT_FALSE(gs.flow.charge.has_value());
    ASSERT_EQ(gs.pending, crushing_decision(gs));
    EXPECT_EQ(compute_decision(gs), gs.pending);
    EXPECT_EQ(decision_rule(gs, *gs.pending), "15.06");
    const WindowInfo w = window_info(gs).value();
    EXPECT_EQ(w.timing, WindowTiming::AfterChargeMove);
    EXPECT_EQ(w.unit, tyrant(gs));
    EXPECT_FALSE(w.roll.has_value());
    ASSERT_EQ(w.options.size(), 1u);
    EXPECT_EQ(w.options[0].stratagem, Stratagem::CrushingImpact);
    EXPECT_EQ(w.options[0].cost, 1);
    EXPECT_EQ(w.options[0].rule, "15.06");
    EXPECT_EQ(w.options[0].targets, std::vector<UnitId>{tyrant(gs)});
    EXPECT_EQ(legal_actions(gs, Side::B),
              (std::vector<GameAction>{action::DeclineStratagems{}, use(Stratagem::CrushingImpact, tyrant(gs))}));
    EXPECT_TRUE(legal_actions(gs, Side::A).empty());
}

TEST(R15_06_CrushingImpact, DecliningChangesNothing) {
    GameState gs = crushing_window();
    GameState inline_twin = b_charge_step(1);
    {
        ScriptedDice six{3, 3};
        apply_action(inline_twin, Side::B, action::DeclareCharge{tyrant(inline_twin)}, six);
        apply_action(inline_twin, Side::B, action::StartChargeMove{tyrant(inline_twin), {legionaries(inline_twin)}});
        const Model& t = inline_twin.units[mf::kTyrant].models[0];
        apply_action(inline_twin, Side::B,
                     action::ProposeModelMove{t.id, {Position{t.position.x, in(mf::kRankY + mf::tyrant_centre_gap(0.5f)), 0.0f}}});
        apply_action(inline_twin, Side::B, action::CommitUnitMove{});  // inline: the window is declined
    }
    const std::vector<GameEvent> declined = paced(gs, action::DeclineStratagems{});
    EXPECT_TRUE(declined.empty());
    EXPECT_EQ(gs, inline_twin);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::Charge{}}));
    EXPECT_EQ(gs.command_points[1], 1);
}

TEST(R15_06_CrushingImpact, UsingItAsksForAnEngagedEnemyUnitAndAModelEngagedWithIt) {
    GameState gs = crushing_window(2);
    const std::vector<GameEvent> e = paced(gs, use(Stratagem::CrushingImpact, tyrant(gs)));
    EXPECT_EQ(events_of<event::StratagemUsed>(e),
              (std::vector<event::StratagemUsed>{{Side::B, Stratagem::CrushingImpact, tyrant(gs), 1, 1}}));
    EXPECT_FALSE(gs.flow.window.has_value());  // closed, as Fire Overwatch's and Heroic Intervention's
    ASSERT_EQ(gs.pending,
              (PendingDecision{Side::B, decision::SelectModelAndEnemy{Stratagem::CrushingImpact, tyrant(gs)}}));
    const ModelId t = gs.units[mf::kTyrant].models[0].id;
    EXPECT_EQ(legal_actions(gs, Side::B), (std::vector<GameAction>{action::SelectModelAndEnemy{t, legionaries(gs)}}));
    // The Havocs aren't engaged with the Tyrant.
    const UnitId havocs = gs.units[mf::kHavocs].id;
    EXPECT_EQ(rejection(gs, Side::B, action::SelectModelAndEnemy{t, havocs}).front(),
              "15.06: " + label(havocs) + " is not engaged with " + label(tyrant(gs)));
    const std::vector<GameEvent> selected = paced(gs, action::SelectModelAndEnemy{t, legionaries(gs)});
    EXPECT_EQ(count<event::ModelAndEnemySelected>(selected), 1u);
    // T D6, the charging unit's player's (the Hive Tyrant is T10).
    ASSERT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::CrushingImpact, tyrant(gs)}}));
    const RollDiceInfo info = roll_dice_info(gs).value();
    EXPECT_EQ(info.roller, Side::B);
    EXPECT_EQ(info.dice, gs.units[mf::kTyrant].models[0].characteristics.toughness);
    EXPECT_EQ(info.dice, 10);
    EXPECT_EQ(info.need, 5);
    EXPECT_EQ(info.target, legionaries(gs));
}

TEST(R15_06_CrushingImpact, EachFivePlusWoundsTheEnemyUnitThenEachOneTheUnit) {
    GameState gs = crushing_roll_waits(2);
    const int legion_before = wounds_left(gs.units[mf::kLegionaries]);
    const int tyrant_before = wounds_left(gs.units[mf::kTyrant]);
    const std::vector<GameEvent> rolled = roll(gs, {5, 6, 1, 2, 3, 4, 5, 1, 2, 3});
    const auto dice = events_of<event::StratagemDiceRolled>(rolled);
    ASSERT_EQ(dice.size(), 1u);
    EXPECT_EQ(dice[0].enemy_mortal_wounds, 3);
    EXPECT_EQ(dice[0].unit_mortal_wounds, 2);
    EXPECT_EQ(event_rule(GameEvent{dice[0]}), "15.06");
    EXPECT_FALSE(window_pending(gs));  // 15.02 lists no such roll
    // The enemy's mortal wounds first (06.02, its player's order), then the unit's.
    ASSERT_EQ(gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{legionaries(gs)}}));
    order_casualties(gs);
    EXPECT_EQ(wounds_left(gs.units[mf::kLegionaries]), legion_before - 3);
    ASSERT_EQ(gs.pending, (PendingDecision{Side::B, decision::OrderCasualties{tyrant(gs)}}));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "06.02");
    const auto preview = preview_casualties(gs, {});
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->damage_left, 2);
    order_casualties(gs);
    EXPECT_EQ(wounds_left(gs.units[mf::kTyrant]), tyrant_before - 2);
    EXPECT_FALSE(gs.flow.effect.has_value());
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::Charge{}}));
    EXPECT_EQ(gs.command_points[1], 1);
}

TEST(R15_06_CrushingImpact, EachUnitSuffersAtMostSixMortalWounds) {
    // The user's ruling (D-099): the cap is per unit, the enemy unit's and the charging unit's.
    EXPECT_EQ(judge_crushing_impact(std::vector<int>(12, 6)), (EffectMortalWounds{6, 0}));
    EXPECT_EQ(judge_crushing_impact(std::vector<int>(12, 1)), (EffectMortalWounds{0, 6}));
    EXPECT_EQ(judge_crushing_impact(std::vector<int>{5, 5, 5, 5, 5, 5, 5, 1, 1, 1, 1, 1}), (EffectMortalWounds{6, 5}));
    EXPECT_EQ(judge_crushing_impact(std::vector<int>{2, 3, 4}), (EffectMortalWounds{0, 0}));
    EXPECT_EQ(judge_explosives(std::vector<int>{4, 4, 4, 4, 4, 4}), (EffectMortalWounds{6, 0}));
    EXPECT_EQ(judge_explosives(std::vector<int>{1, 2, 3, 3, 2, 1}), (EffectMortalWounds{0, 0}));
    // In play: seven 5+s on the Legionaries (10 models, W2) inflict six.
    GameState gs = crushing_roll_waits();
    const int before = wounds_left(gs.units[mf::kLegionaries]);
    const auto dice = events_of<event::StratagemDiceRolled>(roll(gs, {5, 5, 5, 5, 5, 5, 6, 2, 3, 4}));
    ASSERT_EQ(dice.size(), 1u);
    EXPECT_EQ(dice[0].enemy_mortal_wounds, 6);
    order_casualties(gs);
    EXPECT_EQ(wounds_left(gs.units[mf::kLegionaries]), before - 6);
    EXPECT_FALSE(gs.flow.effect.has_value());  // no 1s: nothing on the Tyrant
}

TEST(R15_06_CrushingImpact, WhenTheEnemyHasNoModelsLeftTheGameEndsBeforeTheOnes) {
    // Side A down to one Legionary (W2) in the middle of the rank; two 5+s destroy it: D-007 ends the game at once and
    // the 1s are never inflicted (the user's ruling, D-099 section 6).
    GameState gs = b_charge_step(1);
    gs.units[mf::kLord].models.clear();
    gs.units[mf::kHavocs].models.clear();
    Unit& legion = gs.units[mf::kLegionaries];
    legion.models.erase(legion.models.begin(), legion.models.begin() + 5);
    legion.models.resize(1);
    gs.pending = compute_decision(gs);
    tyrant_charges(gs);
    ASSERT_EQ(gs.pending, crushing_decision(gs));
    paced(gs, use(Stratagem::CrushingImpact, tyrant(gs)));
    paced(gs, legal_actions(gs, Side::B).front());
    const int tyrant_wounds = wounds_left(gs.units[mf::kTyrant]);
    roll(gs, {5, 6, 1, 1, 1, 2, 2, 2, 3, 3});
    ASSERT_EQ(gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{legionaries(gs)}}));
    const std::vector<GameEvent> e = order_casualties(gs);
    const auto over = events_of<event::GameOver>(e);
    ASSERT_EQ(over.size(), 1u);
    EXPECT_EQ(over[0].winner, Side::B);
    EXPECT_NE(over[0].reason.find("D-007"), std::string::npos) << over[0].reason;
    EXPECT_EQ(count<event::MortalWoundsInflicted>(e), 1u);  // the Legionaries' only
    EXPECT_EQ(wounds_left(gs.units[mf::kTyrant]), tyrant_wounds);
    EXPECT_EQ(gs.flow.stage, Stage::GameOver);
}

TEST(R15_06_CrushingImpact, TheChargingUnitKeepsFightsFirstWhenItDestroysItsTarget) {
    // 12's example: a MONSTER "unengaged as it destroyed its charge target in the Charge phase using the Crushing
    // Impact stratagem (15.06)" still has Fights First (11.04, until the end of the turn).
    GameState gs = b_charge_step(1);
    Unit& legion = gs.units[mf::kLegionaries];
    legion.models.erase(legion.models.begin(), legion.models.begin() + 4);
    legion.models.resize(2);
    gs.pending = compute_decision(gs);
    tyrant_charges(gs);
    paced(gs, use(Stratagem::CrushingImpact, tyrant(gs)));
    paced(gs, legal_actions(gs, Side::B).front());
    roll(gs, {5, 5, 5, 5, 2, 2, 2, 3, 3, 3});
    const std::vector<GameEvent> e = order_casualties(gs);
    EXPECT_EQ(count<event::UnitDestroyed>(e), 1u);
    EXPECT_TRUE(gs.units[mf::kTyrant].turn_flags.fights_first);
    EXPECT_TRUE(gs.units[mf::kTyrant].turn_flags.made_charge_move);
    EXPECT_FALSE(is_engaged(gs, gs.units[mf::kTyrant]));
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::Charge{}}));
}

TEST(R15_06_CrushingImpact, NoWindowWithoutCpOrAfterACommandRerollOfTheChargeRoll) {
    {
        GameState gs = b_charge_step(0);
        tyrant_charges(gs);
        EXPECT_FALSE(window_pending(gs));
        EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::Charge{}}));
    }
    {
        // 15.01: the Tyrant was targeted by Command Re-roll this phase, so Crushing Impact can't target it.
        GameState gs = b_charge_step(2);
        ScriptedDice dice{1, 1, 3, 3};
        paced(gs, action::DeclareCharge{tyrant(gs)}, dice);
        roll(gs, {1, 1});  // a 2: too short
        ASSERT_TRUE(window_pending(gs));
        ScriptedDice reroll{3, 3};
        paced(gs, action::UseStratagem{Stratagem::CommandReroll, tyrant(gs), std::nullopt}, reroll);
        ASSERT_TRUE(gs.pending && std::holds_alternative<decision::SelectChargeTargets>(gs.pending->kind));
        apply_action(gs, Side::B, action::StartChargeMove{tyrant(gs), {legionaries(gs)}});
        const Model& t = gs.units[mf::kTyrant].models[0];
        apply_action(gs, Side::B,
                     action::ProposeModelMove{t.id, {Position{t.position.x, in(mf::kRankY + mf::tyrant_centre_gap(0.5f)), 0.0f}}});
        paced(gs, action::CommitUnitMove{});
        EXPECT_FALSE(window_pending(gs));
        EXPECT_EQ(gs.command_points[1], 1);
        // Opened by hand, the window refuses it with 15.01's reason.
        gs.flow.window = StratagemWindow{WindowTiming::AfterChargeMove, std::nullopt, tyrant(gs), 0, {Side::B}, {}, {}};
        gs.pending = compute_decision(gs);
        EXPECT_EQ(rejection(gs, Side::B, use(Stratagem::CrushingImpact, tyrant(gs))).front(),
                  "15.01: " + label(tyrant(gs)) + " has already been targeted by a stratagem of Side B this phase");
    }
}

TEST(R15_06_CrushingImpact, TargetsOnlyThatMonsterOrVehicleUnit) {
    GameState gs = crushing_window(2);
    // Another unit than the window's.
    const UnitId screamer = gs.units[mf::kScreamer].id;
    EXPECT_EQ(rejection(gs, Side::B, use(Stratagem::CrushingImpact, screamer)).front(),
              "15.06: Crushing Impact targets the unit that has just ended its charge move (" + label(tyrant(gs)) +
                  "), not " + label(screamer));
    // A unit without MONSTER/VEHICLE: no window opens for it; opened by hand, it is refused.
    GameState guard = gs;
    const UnitId g = guard.units[mf::kGuard].id;
    guard.flow.window = StratagemWindow{WindowTiming::AfterChargeMove, std::nullopt, g, 0, {Side::B}, {}, {}};
    guard.pending = compute_decision(guard);
    EXPECT_EQ(rejection(guard, Side::B, use(Stratagem::CrushingImpact, g)).front(),
              "15.06: Crushing Impact targets a MONSTER/VEHICLE unit (" + label(g) + " is not)");
    // 01.07: a battle-shocked unit can't be targeted.
    GameState shocked = gs;
    shocked.units[mf::kTyrant].battle_shocked = true;
    EXPECT_EQ(rejection(shocked, Side::B, use(Stratagem::CrushingImpact, tyrant(shocked))).front(),
              "01.07: " + label(tyrant(shocked)) + " is battle-shocked, so its player can't target it with stratagems");
    // Explosives in this window: refused with its WHEN.
    EXPECT_EQ(rejection(gs, Side::B, use(Stratagem::Explosives, tyrant(gs))).front().substr(0, 6), "15.05:");
}

TEST(R15_06_CrushingImpact, NotAfterAHeroicCharge) {
    // 15.11's charge is made in the opponent's Charge phase (its End of Charge phase step): 15.06's WHEN is "your Charge
    // phase", so no window opens after it, even with CP to spare.
    GameState gs = mf::at_movement(false, Side::A);
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::Charge>(g.pending->kind) && g.pending->side == Side::A;
    });
    gs.command_points[1] = 4;
    gs.pending = compute_decision(gs);
    paced(gs, action::EndChargeStep{});
    ASSERT_TRUE(window_pending(gs));
    ASSERT_EQ(gs.pending->side, Side::B);
    paced(gs, action::UseStratagem{Stratagem::HeroicIntervention, tyrant(gs), std::nullopt, StratagemMode::IntoTheFray});
    roll(gs, {3, 3});
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::SelectChargeTargets>(gs.pending->kind))
        << describe(gs, *gs.pending);
    paced(gs, action::StartChargeMove{tyrant(gs), {legionaries(gs)}});
    const Model& t = gs.units[mf::kTyrant].models[0];
    paced(gs, action::ProposeModelMove{t.id, {Position{t.position.x, in(mf::kRankY + mf::tyrant_centre_gap(0.5f)), 0.0f}}});
    const std::vector<GameEvent> e = paced(gs, action::CommitUnitMove{});
    ASSERT_EQ(count<event::ChargeResolved>(e), 1u);
    EXPECT_TRUE(events_of<event::ChargeResolved>(e)[0].charge_move_made);
    EXPECT_FALSE(window_pending(gs));
    EXPECT_EQ(gs.command_points[1], 2);
}

TEST(R15_06_CrushingImpact, PointsDestroyedCountTheUnitsOwnWounds) {
    // D-076: every wound lost counts for the opponent, the charging unit's own 1s too (as hazard wounds do).
    GameState gs = crushing_roll_waits();
    for (Unit& u : gs.units) {  // the fixture's units have no points: give them some (D-076)
        u.points = 100;
        u.starting_wounds = 0;
        for (const Model& m : u.models) u.starting_wounds += m.characteristics.wounds;
    }
    const PointsTally before = points_destroyed(gs);
    roll(gs, {1, 1, 2, 2, 3, 3, 4, 4, 2, 3});
    ASSERT_EQ(gs.pending, (PendingDecision{Side::B, decision::OrderCasualties{tyrant(gs)}}));
    order_casualties(gs);
    const PointsTally after = points_destroyed(gs);
    EXPECT_GT(after.destroyed[side_index(Side::A)], before.destroyed[side_index(Side::A)]);
    EXPECT_EQ(after.destroyed[side_index(Side::B)], before.destroyed[side_index(Side::B)]);
}

// ==== D-099: the effect's state, JSON and the closure ====

TEST(D099_Effect, EachStepRoundTripsAndIsOmittedOtherwise) {
    const auto round_trip = [](const GameState& gs) { return Json(gs).get<GameState>(); };
    GameState gs = crushing_window(2);
    EXPECT_EQ(round_trip(gs), gs);  // the AfterChargeMove window
    EXPECT_EQ(Json(gs).at("flow").at("window").at("timing"), "AfterChargeMove");
    paced(gs, use(Stratagem::CrushingImpact, tyrant(gs)));
    ASSERT_TRUE(gs.flow.effect.has_value());
    EXPECT_EQ(round_trip(gs), gs);
    Json j = Json(gs).at("flow").at("effect");
    EXPECT_EQ(j, (Json{{"stratagem", "CrushingImpact"}, {"unit", tyrant(gs).v}}));
    paced(gs, legal_actions(gs, Side::B).front());
    EXPECT_EQ(round_trip(gs), gs);
    roll(gs, {5, 1, 2, 2, 2, 2, 2, 2, 2, 2});
    EXPECT_EQ(round_trip(gs), gs);
    j = Json(gs).at("flow").at("effect");
    EXPECT_TRUE(j.at("rolled").get<bool>());
    EXPECT_EQ(j.at("enemy_mortal_wounds"), 1);
    EXPECT_EQ(j.at("unit_mortal_wounds"), 1);
    order_casualties(gs);
    EXPECT_EQ(round_trip(gs), gs);
    EXPECT_FALSE(Json(gs).at("flow").at("effect").contains("enemy_mortal_wounds"));
    order_casualties(gs);
    EXPECT_FALSE(Json(gs).at("flow").contains("effect"));
    // A negative count is corrupt data.
    Json bad = Json(gs);
    bad["flow"]["effect"] = Json{{"stratagem", "Explosives"}, {"unit", 1}, {"enemy_mortal_wounds", -1}};
    EXPECT_THROW((void)bad.get<GameState>(), DataError);
}

TEST(D099_Effect, EveryDecisionOfTheEffectIsTheComputedOne) {
    GameState gs = explosives_roll_waits(6.0f, 1);
    EXPECT_EQ(compute_decision(gs), gs.pending);
    roll(gs, {6, 6, 1, 1, 1, 1});
    EXPECT_EQ(compute_decision(gs), gs.pending);
    order_casualties(gs);
    EXPECT_EQ(compute_decision(gs), gs.pending);
}

TEST(D099_Effect, InlinePlayMakesTheRollAsPacedPlayDoes) {
    // apply_action makes the effect's roll inline (the roll closure, D-095): the same dice, events and state as paced.
    GameState paced_gs = explosives_step();
    GameState inline_gs = paced_gs;
    const UnitId marines = paced_gs.units[kMarines].id;
    paced(paced_gs, use(Stratagem::Explosives, marines));
    apply_action(inline_gs, Side::A, use(Stratagem::Explosives, marines));
    EXPECT_EQ(paced_gs, inline_gs);
    const GameAction pick = legal_actions(paced_gs, Side::A).front();
    ScriptedDice d1{4, 5, 6, 1, 2, 3};
    std::vector<GameEvent> e1 = paced(paced_gs, pick);
    const std::vector<GameEvent> r1 = paced(paced_gs, action::RollDice{RollKind::Explosives, marines}, d1);
    e1.insert(e1.end(), r1.begin(), r1.end());
    ScriptedDice d2{4, 5, 6, 1, 2, 3};
    const std::vector<GameEvent> e2 = apply_action(inline_gs, Side::A, pick, d2);
    EXPECT_EQ(e1, e2);
    EXPECT_EQ(paced_gs, inline_gs);
    EXPECT_EQ(inline_gs.pending, (PendingDecision{Side::B, decision::OrderCasualties{inline_gs.units[kGants].id}}));
}

TEST(D099_Effect, TheSelectionAndRollRefuseOtherAnswers) {
    GameState gs = explosives_step();
    paced(gs, use(Stratagem::Explosives, gs.units[kMarines].id));
    // Only the selection answers now.
    EXPECT_FALSE(rejection(gs, Side::A, action::EndShootStep{}).empty());
    EXPECT_FALSE(rejection(gs, Side::B, legal_actions(gs, Side::A).front()).empty());
    paced(gs, legal_actions(gs, Side::A).front());
    EXPECT_FALSE(rejection(gs, Side::A, action::SelectModelAndEnemy{gs.units[kMarines].models[0].id, gs.units[kGants].id})
                     .empty());
    EXPECT_FALSE(rejection(gs, Side::A, action::RollDice{RollKind::CrushingImpact, gs.units[kMarines].id}).empty());
    EXPECT_TRUE(validate_action(gs, Side::A, action::RollDice{RollKind::Explosives, gs.units[kMarines].id}).ok());
}

// ==== D-099: format 5 and the regeneration check (format 4 -> 5 adds only declines) ====

namespace {

// The steps of `with` that aren't in `without` (a subsequence of it), matched in order; nullopt if `without` isn't one.
std::optional<std::vector<ReplayStep>> added_steps(const std::vector<ReplayStep>& without,
                                                   const std::vector<ReplayStep>& with) {
    std::vector<ReplayStep> added;
    std::size_t i = 0;
    for (const ReplayStep& s : with) {
        if (i < without.size() && s == without[i]) {
            ++i;
        } else {
            added.push_back(s);
        }
    }
    if (i != without.size()) return std::nullopt;
    return added;
}

}  // namespace

TEST(D099_Replays, AFormat4ReplayWithAMonsterChargeDeclinesItsNewWindows) {
    // tests/data/legacy/m60_hazard_and_extra_attacks_format4.json is m60 as M96 wrote it: the Hive Tyrant charges in its
    // own Charge phase with CP to spend, so Crushing Impact's window now waits there. It runs as before, and
    // with_roll_steps turns it into exactly the regenerated file, with that window declined.
    const Replay legacy =
        load_replay(wh40k::test::repo_root() / "tests/data/legacy/m60_hazard_and_extra_attacks_format4.json");
    const Replay current = load_replay(std::filesystem::path(WH40K_REPLAY_DIR) / "m60_hazard_and_extra_attacks.json");
    EXPECT_EQ(legacy.format, kReplayFormatReactionsRecorded);
    EXPECT_EQ(current.format, kReplayFormatOwnTurnStratagems);
    const ReplayResult run = run_replay(legacy, wh40k::test::repo_root());
    ASSERT_TRUE(run.ok()) << "step " << *run.failed_step << ": " << run.reasons.front();
    EXPECT_EQ(replay_expectation(run.final_state), legacy.expect);
    const Replay converted = with_roll_steps(legacy, wh40k::test::repo_root());
    EXPECT_EQ(Json(converted).dump(2), Json(current).dump(2));
    const auto added = added_steps(legacy.steps, current.steps);
    ASSERT_TRUE(added.has_value());
    ASSERT_EQ(added->size(), 1u);
    EXPECT_EQ(added->front(), (ReplayStep{Side::B, action::DeclineStratagems{}}));
    // Played as format 4 (without the decline), the format-5 file fails where the window waits.
    Replay as_format4 = current;
    as_format4.format = kReplayFormatReactionsRecorded;
    EXPECT_FALSE(run_replay(as_format4, wh40k::test::repo_root()).ok());
}

// M97 regenerated every replay as format 5. Each new file must be exactly its format-4 file (git show HEAD:) converted
// by with_roll_steps: the same description, initial state and expect block, and the same steps with only the answers to
// D-099's windows added (each declined). Run on purpose only, with the old files in $WH40K_FORMAT4_DIR:
//   WH40K_FORMAT4_DIR=<dir> wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D099_Replays.DISABLED_CompareWithFormat4Files
TEST(D099_Replays, DISABLED_CompareWithFormat4Files) {
    const char* dir = std::getenv("WH40K_FORMAT4_DIR");
    if (dir == nullptr) GTEST_SKIP() << "set WH40K_FORMAT4_DIR";
    int compared = 0;
    std::size_t declines = 0;
    for (const auto& entry : std::filesystem::directory_iterator(WH40K_REPLAY_DIR)) {
        if (entry.path().extension() != ".json") continue;
        const std::filesystem::path old_path = std::filesystem::path(dir) / entry.path().filename();
        if (!std::filesystem::exists(old_path)) continue;
        SCOPED_TRACE(entry.path().filename().string());
        const Replay old_file = load_replay(old_path);
        const Replay new_file = load_replay(entry.path());
        EXPECT_EQ(old_file.format, kReplayFormatReactionsRecorded);
        EXPECT_EQ(new_file.format, kReplayFormatOwnTurnStratagems);
        EXPECT_EQ(old_file.description, new_file.description);
        EXPECT_EQ(old_file.initial, new_file.initial);
        EXPECT_EQ(old_file.expect, new_file.expect);
        EXPECT_EQ(Json(with_roll_steps(old_file, wh40k::test::repo_root())).dump(2), Json(new_file).dump(2));
        const auto added = added_steps(old_file.steps, new_file.steps);
        ASSERT_TRUE(added.has_value());
        for (const ReplayStep& s : *added) EXPECT_TRUE(std::holds_alternative<action::DeclineStratagems>(s.action));
        if (!added->empty()) std::printf("%s: %zu declines added\n", entry.path().filename().string().c_str(), added->size());
        declines += added->size();
        ++compared;
    }
    std::printf("compared %d files, %zu declines added\n", compared, declines);
    EXPECT_GT(compared, 0);
}

// ==== The M97 replay: tests/replays/m97_stratagems.json (format 5: both stratagems, each a step) ====

namespace {

const std::filesystem::path kM97Replay = std::filesystem::path(WH40K_REPLAY_DIR) / "m97_stratagems.json";
constexpr std::uint64_t kM97Seed = 1;  // picked by D099_Replay.DISABLED_SearchM97Seed

// The milestone's replay, played through a paced match. `ok` says whether the seed shows what the replay is about:
// Explosives wounds the Hive Tyrant; the Legionaries then shoot; the Tyrant's charge reaches; Crushing Impact's dice have
// both a 5+ and a 1, so both units suffer mortal wounds, and the game goes on. `problem`: the first refused step.
Replay m97_replay(std::uint64_t seed, bool& ok, std::string& problem) {
    ok = false;
    Replay r;
    r.format = kReplayFormatOwnTurnStratagems;
    r.description =
        "M97 (D-099): the own-turn stratagems that end in mortal wounds, each a step of its own. The monsters fixture "
        "(D-073): the Legionaries (GRENADES) 2.04\" south of the Hive Tyrant (MONSTER, T10), Side A first. Round 1, "
        "Side A's Shoot step: Explosives (15.05, 1CP) on the Legionaries, one of them and the Hive Tyrant selected; six "
        "D6, each 4+ a mortal wound (06.02, Side B's casualty order); the Legionaries then shoot at the Tyrant "
        "(Explosives doesn't select a unit to shoot, 10.02). Side B's Charge step: the Hive Tyrant charges the "
        "Legionaries (11.04, to 0.5\" from their rank) and Side B uses Crushing Impact (15.06, 1CP) just after the "
        "charge move: T D6, each 5+ a mortal wound on the Legionaries (Side A's casualty order), then each 1 one on the "
        "Tyrant (Side B's), at most 6 each. Every other window declined, casualties in unit order. Stops at the Pile In "
        "step.";
    GameState init = mf::fixture(false, Side::A);
    init.dice = DiceState{seed, 0};
    r.initial.state = init;
    const UnitId legion = init.units[mf::kLegionaries].id;
    const UnitId tyr = init.units[mf::kTyrant].id;
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
    // The builder's answers: Crushing Impact on the Tyrant when offered, every other window declined, rolls made,
    // neutral answers otherwise (Explosives is used by hand below).
    auto answer = [&](const GameState& gs) -> GameAction {
        if (window_pending(gs)) {
            const GameAction crush = use(Stratagem::CrushingImpact, tyr);
            for (const GameAction& a : legal_actions(gs, gs.pending->side)) {
                if (a == crush) return a;
            }
            return action::DeclineStratagems{};
        }
        if (roll_pending(gs)) {
            const auto& d = std::get<decision::RollDice>(gs.pending->kind);
            return action::RollDice{d.roll, d.unit};
        }
        return wh40k::test::neutral_action(gs);
    };
    auto play_until = [&](auto done) {
        for (int i = 0; i < 400 && !failed && match.state().pending && !done(match.state()); ++i) act(answer(match.state()));
    };
    const auto waiting = [](const GameState& gs) { return roll_pending(gs) || window_pending(gs); };
    // Side A's Shoot step: Explosives at the Tyrant.
    play_until([&](const GameState& gs) { return gs.pending == PendingDecision{Side::A, decision::Shoot{}}; });
    if (failed || match.state().pending != PendingDecision{Side::A, decision::Shoot{}}) return r;
    act(use(Stratagem::Explosives, legion));
    if (failed) return r;
    for (const GameAction& a : legal_actions(match.state(), Side::A)) {
        if (std::get<action::SelectModelAndEnemy>(a).enemy == tyr) {
            act(a);
            break;
        }
    }
    play_until([&](const GameState& gs) { return gs.pending == PendingDecision{Side::A, decision::Shoot{}}; });
    if (failed || match.state().pending != PendingDecision{Side::A, decision::Shoot{}}) return r;
    // The Legionaries shoot at the Tyrant: each model's first shot option at it.
    {
        const GameState& gs = match.state();
        action::Shoot s{legion, ShootingType::Normal, {}};
        for (const ShotDeclaration& o : shot_options(gs, *gs.find_unit(legion), ShootingType::Normal)) {
            const bool have = std::any_of(s.shots.begin(), s.shots.end(), [&o](const ShotDeclaration& d) {
                return d.model == o.model;
            });
            if (o.target == tyr && !have) s.shots.push_back(o);
        }
        act(s);
    }
    // Side B's Charge step: the Tyrant charges the Legionaries, then Crushing Impact.
    play_until([&](const GameState& gs) {
        return gs.flow.stage == Stage::Charge && turn_side(gs) == Side::B && !waiting(gs);
    });
    if (failed || match.state().pending != PendingDecision{Side::B, decision::Charge{}}) return r;
    act(action::DeclareCharge{tyr});
    play_until([&](const GameState& gs) { return !waiting(gs); });
    if (failed || match.state().pending != PendingDecision{Side::B, decision::SelectChargeTargets{tyr}}) return r;
    act(action::StartChargeMove{tyr, {legion}});
    {
        const Model& t = match.state().find_unit(tyr)->models.at(0);
        act(action::ProposeModelMove{t.id, {Position{t.position.x, in(mf::kRankY + mf::tyrant_centre_gap(0.5f)), 0.0f}}});
    }
    act(action::CommitUnitMove{});
    play_until([&](const GameState& gs) {
        return std::holds_alternative<decision::SelectModelAndEnemy>(gs.pending->kind);
    });
    if (failed || !match.state().pending ||
        !std::holds_alternative<decision::SelectModelAndEnemy>(match.state().pending->kind)) {
        return r;
    }
    act(legal_actions(match.state(), Side::B).front());
    play_until([&](const GameState& gs) { return gs.flow.stage == Stage::PileIn; });
    r.steps = match.steps();
    r.expect = replay_expectation(match.state());
    if (failed || match.state().flow.stage != Stage::PileIn) return r;
    // What the seed shows.
    std::vector<std::pair<Side, Stratagem>> used;
    int explosives_wounds = 0;
    bool shot_after = false;
    bool charged = false;
    std::optional<event::StratagemDiceRolled> crush;
    for (const GameEvent& e : match.events()) {
        if (const auto* s = std::get_if<event::StratagemUsed>(&e)) used.emplace_back(s->side, s->stratagem);
        if (const auto* d = std::get_if<event::StratagemDiceRolled>(&e)) {
            if (d->stratagem == Stratagem::Explosives) explosives_wounds = d->enemy_mortal_wounds;
            if (d->stratagem == Stratagem::CrushingImpact) crush = *d;
        }
        if (const auto* s = std::get_if<event::ShootingStarted>(&e); s != nullptr && s->unit == legion) {
            shot_after = explosives_wounds > 0;
        }
        if (const auto* c = std::get_if<event::ChargeResolved>(&e); c != nullptr && c->unit == tyr) {
            charged = c->charge_move_made;
        }
    }
    ok = used == std::vector<std::pair<Side, Stratagem>>{{Side::A, Stratagem::Explosives},
                                                         {Side::B, Stratagem::CrushingImpact}} &&
         explosives_wounds > 0 && shot_after && charged && crush && crush->enemy_mortal_wounds > 0 &&
         crush->unit_mortal_wounds > 0 && !match.state().result;
    return r;
}

}  // namespace

TEST(D099_Replay, TheStratagemsReplayUsesBothStratagemsAsSteps) {
    const Replay saved = load_replay(kM97Replay);
    EXPECT_EQ(saved.format, kReplayFormatOwnTurnStratagems);
    const ReplayResult run = run_replay(saved, wh40k::test::repo_root());
    ASSERT_TRUE(run.ok()) << "step " << *run.failed_step << ": " << run.reasons.front();
    EXPECT_EQ(replay_expectation(run.final_state), saved.expect);
    std::vector<std::pair<Side, Stratagem>> used;
    std::vector<event::StratagemDiceRolled> dice;
    for (const GameEvent& e : run.events) {
        if (const auto* s = std::get_if<event::StratagemUsed>(&e)) {
            used.emplace_back(s->side, s->stratagem);
            EXPECT_GE(s->cp_left, 0);
        }
        if (const auto* d = std::get_if<event::StratagemDiceRolled>(&e)) dice.push_back(*d);
    }
    EXPECT_EQ(used, (std::vector<std::pair<Side, Stratagem>>{{Side::A, Stratagem::Explosives},
                                                              {Side::B, Stratagem::CrushingImpact}}));
    ASSERT_EQ(dice.size(), 2u);
    EXPECT_EQ(dice[0].dice.size(), 6u);
    EXPECT_EQ(dice[0].enemy_mortal_wounds, judge_explosives(dice[0].dice).enemy);
    EXPECT_EQ(dice[1].dice.size(), 10u);  // the Hive Tyrant's T
    EXPECT_EQ(dice[1].enemy_mortal_wounds, judge_crushing_impact(dice[1].dice).enemy);
    EXPECT_EQ(dice[1].unit_mortal_wounds, judge_crushing_impact(dice[1].dice).unit);
    // The steps hold both selections and both rolls.
    const auto step_count = [&saved](auto pred) { return std::count_if(saved.steps.begin(), saved.steps.end(), pred); };
    EXPECT_EQ(step_count([](const ReplayStep& s) { return std::holds_alternative<action::SelectModelAndEnemy>(s.action); }),
              2);
    EXPECT_EQ(step_count([](const ReplayStep& s) {
                  const auto* r = std::get_if<action::RollDice>(&s.action);
                  return r != nullptr && (r->roll == RollKind::Explosives || r->roll == RollKind::CrushingImpact);
              }),
              2);
    // The file is what the builder plays now.
    bool ok = false;
    std::string problem;
    const Replay again = m97_replay(kM97Seed, ok, problem);
    EXPECT_TRUE(problem.empty()) << problem;
    EXPECT_TRUE(ok);
    EXPECT_EQ(Json(again).dump(2), Json(saved).dump(2));
}

TEST(D099_Replay, DISABLED_SearchM97Seed) {
    for (std::uint64_t seed = 1; seed <= 1000; ++seed) {
        bool ok = false;
        std::string problem;
        (void)m97_replay(seed, ok, problem);
        if (!problem.empty()) std::printf("seed %llu: %s\n", static_cast<unsigned long long>(seed), problem.c_str());
        if (ok) {
            std::printf("M97 seed %llu\n", static_cast<unsigned long long>(seed));
            return;
        }
    }
    ADD_FAILURE() << "no seed in 1..1000";
}

// Writes tests/replays/m97_stratagems.json. Run on purpose only:
//   wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D099_Replay.DISABLED_RegenerateM97Replay
TEST(D099_Replay, DISABLED_RegenerateM97Replay) {
    bool ok = false;
    std::string problem;
    const Replay r = m97_replay(kM97Seed, ok, problem);
    ASSERT_TRUE(problem.empty()) << problem;
    ASSERT_TRUE(ok);
    const ReplayResult run = run_replay(r, wh40k::test::repo_root());
    ASSERT_TRUE(run.ok()) << "step " << *run.failed_step << ": " << run.reasons.front();
    save_replay(r, kM97Replay);
}

// ==== Random play that uses the two stratagems (D-004 style): every answer valid, the invariants hold ====

TEST(D099_RandomPlay, GamesThatUseTheNewStratagemsStayValidAndReplay) {
    // V1 with the Termagants 6" from the Intercessors at Side A's Shoot step (Explosives), the Crushing Impact window
    // just after the Hive Tyrant's charge, the same with Crushing Impact used (its selection waiting), and the rosters
    // scenario from the start.
    GameState crush_used = crushing_window(4);
    paced(crush_used, use(Stratagem::CrushingImpact, tyrant(crush_used)));
    const std::vector<GameState> scenarios{explosives_step(6.0f, 4), crushing_window(4), crush_used,
                                           wh40k::test::load_rosters_scenario()};
    std::map<Stratagem, int> used;
    std::map<Stratagem, int> selections;
    for (std::size_t n = 0; n < scenarios.size(); ++n) {
        for (std::uint64_t seed = 1; seed <= 6; ++seed) {
            SCOPED_TRACE("scenario " + std::to_string(n) + " seed " + std::to_string(seed));
            GameState initial = scenarios[n];
            initial.dice = DiceState{seed, 0};
            if (initial.flow.stage == Stage::NotStarted) initial.command_points = {6, 6};
            if (initial.flow.stage != Stage::NotStarted) initial.pending = compute_decision(initial);
            Match match(initial, RollMode::Paced);
            wh40k::test::TestRng rng(seed * 37 + n);
            for (int i = 0; i < 600 && match.state().pending; ++i) {
                const GameState& gs = match.state();
                ASSERT_EQ(gs.pending, compute_decision(gs));
                ASSERT_GE(gs.command_points[0], 0);
                ASSERT_GE(gs.command_points[1], 0);
                if (const auto* d = std::get_if<decision::SelectModelAndEnemy>(&gs.pending->kind)) {
                    ASSERT_TRUE(gs.flow.effect.has_value());
                    ++selections[d->stratagem];
                }
                if (gs.flow.effect) ASSERT_FALSE(gs.flow.window.has_value());
                // Every listed answer is valid at the Shoot step and in the effect (D-004).
                if (std::holds_alternative<decision::Shoot>(gs.pending->kind) ||
                    std::holds_alternative<decision::SelectModelAndEnemy>(gs.pending->kind) || window_pending(gs)) {
                    for (const GameAction& a : legal_actions(gs, gs.pending->side)) {
                        if (is_template(a)) continue;
                        EXPECT_TRUE(validate_action(gs, gs.pending->side, a).ok()) << action_name(a);
                    }
                }
                const std::size_t before = match.events().size();
                const GameAction a = wh40k::test::random_action(gs, rng);
                ASSERT_TRUE(match.submit(decider(gs), a).ok()) << action_name(a);
                for (std::size_t k = before; k < match.events().size(); ++k) {
                    if (const auto* s = std::get_if<event::StratagemUsed>(&match.events()[k])) ++used[s->stratagem];
                }
            }
            const Replay replay = match.to_replay("random play with the M97 stratagems");
            EXPECT_EQ(replay.format, kReplayFormatOwnTurnStratagems);
            const ReplayResult again = run_replay(Json(replay).get<Replay>(), wh40k::test::repo_root());
            ASSERT_TRUE(again.ok()) << "step " << *again.failed_step << ": " << again.reasons.front();
            EXPECT_EQ(again.final_state, match.state());
            EXPECT_EQ(again.events, match.events());
        }
    }
    EXPECT_GT(used[Stratagem::Explosives], 0);
    EXPECT_GT(selections[Stratagem::Explosives], 0);
    EXPECT_GT(selections[Stratagem::CrushingImpact], 0);
}

TEST(R15_06_CrushingImpact, ATwelveToughnessTyrannofexRollsTwelveDiceCappedAtSix) {
    // The Tyrannofex fixture (D-074: a 92 x 120 mm oval, T12): it charges the Legionaries, its south end 0.5" from their
    // rank; Crushing Impact rolls twelve D6, and seven 5+s still inflict six mortal wounds (the user's ruling, D-099).
    namespace tf = wh40k::test::tyrannofex;
    GameState gs = tf::at_movement();
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::Charge>(g.pending->kind) && g.pending->side == Side::B;
    });
    gs.command_points[1] = 1;
    gs.pending = compute_decision(gs);
    const UnitId tyrannofex = gs.units[tf::kTyrannofex].id;
    const UnitId legion = gs.units[tf::kLegionaries].id;
    ScriptedDice eight{4, 4};
    apply_action(gs, Side::B, action::DeclareCharge{tyrannofex}, eight);
    apply_action(gs, Side::B, action::StartChargeMove{tyrannofex, {legion}});
    const Model& t = tf::the_tyrannofex(gs);
    apply_action(gs, Side::B,
                 action::ProposeModelMove{t.id, {at(22.0f, tf::kRankY + mm_to_inches(16.0f + 60.0f) + 0.5f)}});
    paced(gs, action::CommitUnitMove{});
    ASSERT_EQ(gs.pending, (PendingDecision{Side::B, decision::UseStratagems{WindowTiming::AfterChargeMove, tyrannofex}}));
    paced(gs, use(Stratagem::CrushingImpact, tyrannofex));
    paced(gs, legal_actions(gs, Side::B).front());
    const RollDiceInfo info = roll_dice_info(gs).value();
    EXPECT_EQ(info.dice, 12);
    const int before = wounds_left(gs.units[tf::kLegionaries]);
    const auto dice = events_of<event::StratagemDiceRolled>(roll(gs, {5, 5, 5, 5, 5, 6, 6, 2, 3, 4, 2, 3}));
    ASSERT_EQ(dice.size(), 1u);
    EXPECT_EQ(dice[0].dice.size(), 12u);
    EXPECT_EQ(dice[0].enemy_mortal_wounds, 6);
    EXPECT_EQ(dice[0].unit_mortal_wounds, 0);
    order_casualties(gs);
    EXPECT_EQ(wounds_left(gs.units[tf::kLegionaries]), before - 6);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::Charge{}}));
}
