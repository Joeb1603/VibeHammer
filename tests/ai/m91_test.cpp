// M91 (D-092): the bot plays terrain and objectives in every phase. Five features, each behind a MoveWeights key and at
// 0 exactly as M90: the army plan with terrain (plan_terrain), ShootingAim::reply with cover and sight (aim_terrain),
// holding fire to stay hidden (hold_fire, 13.09), the objective terms in charges (objective_charge) and in pile-ins and
// consolidations (objective_fight, in either player's turn).

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "analysis/terrain_sight.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "core/data/datasheet.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/terrain_shape.hpp"
#include "core/rules/terrain.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/objectives.hpp"
#include "normal/army_plan.hpp"
#include "normal/charge.hpp"
#include "normal/fight.hpp"
#include "normal/move_score.hpp"
#include "normal/objectives.hpp"
#include "normal/shooting.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
namespace mf = wh40k::test::monsters;
using wh40k::test::at;
using wh40k::test::in;
using wh40k::test::repo_root;

namespace {

constexpr const char* kTerrainScenario = "data/scenarios/incursion_terrain_44x60.json";
constexpr const char* kObjectiveScenario = "data/scenarios/incursion_objectives_44x60.json";

TerrainShape rect_in(float x0, float x1, float y0, float y1) {
    return polygon_shape({at(x0, y0), at(x1, y0), at(x1, y1), at(x0, y1)});
}

// An area round a rectangle, with one feature of `category` in it (a Dense one 5" tall).
TerrainArea area_with(const std::string& name, const TerrainShape& area, const TerrainShape& feature,
                      TerrainCategory category) {
    TerrainFeature f;
    f.name = name + " feature";
    f.category = category;
    if (category == TerrainCategory::Dense) f.height_mm = in(5);
    f.shape = feature;
    TerrainArea a;
    a.name = name;
    a.shape = area;
    a.features.push_back(f);
    return a;
}

// The five M91 keys at 0: the M90 bot.
normal::MoveWeights m90_weights() {
    normal::MoveWeights w;
    w.plan_terrain = 0.0;
    w.aim_terrain = 0.0;
    w.hold_fire = 0.0;
    w.objective_charge = 0.0;
    w.objective_fight = 0.0;
    return w;
}

BotConfig config(const normal::MoveWeights& w) {
    BotConfig c;
    c.move = w;
    return c;
}

std::vector<Position> positions_of(const Unit& u) {
    std::vector<Position> out;
    for (const Model& m : u.models) out.push_back(m.position);
    return out;
}

std::string per_game_field(const std::string& line, const std::string& key) {
    const std::size_t at = line.find(key + " ");
    if (at == std::string::npos) return "";
    const std::size_t start = at + key.size() + 1;
    return line.substr(start, line.find_first_of(" \n", start) - start);
}

// A tall dense wall (13.10 / 13.11) across the board between y = 31" and 32", in its own area.
TerrainArea wall_across() {
    return area_with("Wall", rect_in(0.5f, 43.5f, 30.8f, 32.2f), rect_in(0.5f, 43.5f, 31.0f, 32.0f),
                     TerrainCategory::Dense);
}

}  // namespace

// ==== 1. The army plan with terrain (plan_terrain) ====

TEST(M91_ArmyPlan, TheSightHelpersSeeAndCoverAsTheRulesSay) {
    GameState gs = mf::fixture(false, Side::A);
    // The Havocs (east of the Tyrant on its line) see it in the open, and it has no cover.
    {
        analysis::TerrainSight sight(gs);
        const analysis::UnitView v = analysis::unit_view(sight, gs.units[mf::kHavocs], gs.units[mf::kTyrant], true);
        EXPECT_TRUE(v.visible);
        EXPECT_FALSE(v.cover);
    }
    // 13.08: the Tyrant Guard (INFANTRY) within an area has area cover; the Tyrant (MONSTER) in one doesn't.
    gs.terrain = {area_with("Crater", rect_in(1.0f, 30.0f, 28.0f, 44.0f), rect_in(1.2f, 1.6f, 43.0f, 43.5f),
                            TerrainCategory::Exposed)};
    analysis::TerrainSight sight(gs);
    EXPECT_TRUE(analysis::area_cover_now(sight, gs.units[mf::kGuard]));
    EXPECT_FALSE(analysis::area_cover_now(sight, gs.units[mf::kTyrant]));
    // 13.10 / 13.11: a dense wall between the Legionaries and the Lord (north-west, y 36) blocks every line.
    gs.terrain = {wall_across()};
    analysis::TerrainSight walled(gs);
    EXPECT_FALSE(analysis::unit_view(walled, gs.units[mf::kLegionaries], gs.units[mf::kLord], true).visible);
}

TEST(M91_ArmyPlan, AStandingShooterThatCantSeeItsTargetRatesItsShotsAtNothing) {
    // The Havocs at (39.5, 30) and the Lord at (16, 36), within lascannon range; a wall between them.
    GameState gs = mf::fixture(false, Side::A);
    start_game(gs);
    analysis::OddsCache cache;
    const normal::LoneExchange open = normal::lone_exchange(cache, gs, gs.units[mf::kHavocs].id, gs.units[mf::kLord].id);
    gs.terrain = {area_with("Wall", rect_in(27.0f, 29.0f, 20.0f, 44.0f), rect_in(27.5f, 28.5f, 20.0f, 44.0f),
                            TerrainCategory::Dense)};
    analysis::TerrainSight sight(gs);
    const normal::PlanTerrain off{&sight, 0.0};
    const normal::PlanTerrain on{&sight, 1.0};
    const normal::LoneExchange zero = normal::lone_exchange(cache, gs, gs.units[mf::kHavocs].id, gs.units[mf::kLord].id, &off);
    const normal::LoneExchange walled = normal::lone_exchange(cache, gs, gs.units[mf::kHavocs].id, gs.units[mf::kLord].id, &on);
    EXPECT_DOUBLE_EQ(zero.value, open.value);  // at weight 0, as before M91
    ASSERT_EQ(open.mode, normal::AttackMode::Shoot);
    // The standing Havocs' lascannons see nothing past the wall: their shooting rates nothing, so the exchange is worth
    // less (and a charge, if anything, is its better mode).
    EXPECT_LT(walled.value, open.value);
}

TEST(M91_ArmyPlan, ATargetInAnAreaIsHarderToShoot) {
    // The Havocs on the Tyrant Guard (INFANTRY): in a crater it has the benefit of cover (13.08: BS + 1).
    GameState gs = mf::fixture(false, Side::A);
    start_game(gs);
    analysis::OddsCache cache;
    gs.terrain = {area_with("Crater", rect_in(1.0f, 11.0f, 36.0f, 44.0f), rect_in(1.2f, 1.6f, 43.0f, 43.5f),
                            TerrainCategory::Exposed)};
    analysis::TerrainSight sight(gs);
    const normal::PlanTerrain off{&sight, 0.0};
    const normal::PlanTerrain on{&sight, 1.0};
    const normal::LoneExchange plain = normal::lone_exchange(cache, gs, gs.units[mf::kHavocs].id, gs.units[mf::kGuard].id, &off);
    const normal::LoneExchange cover = normal::lone_exchange(cache, gs, gs.units[mf::kHavocs].id, gs.units[mf::kGuard].id, &on);
    EXPECT_LT(cover.value, plain.value);
}

TEST(M91_ArmyPlan, AtWeightZeroTheArmyPlanIsM90s) {
    GameState gs = mf::fixture(false, Side::A);
    start_game(gs);
    gs.terrain = {wall_across()};
    analysis::OddsCache cache;
    analysis::TerrainSight sight(gs);
    const normal::PlanTerrain off{&sight, 0.0};
    EXPECT_EQ(normal::army_plan(cache, gs, Side::A, &off), normal::army_plan(cache, gs, Side::A));
    EXPECT_FALSE(normal::plan_terrain_on(gs, m90_weights()));
    EXPECT_TRUE(normal::plan_terrain_on(gs, normal::MoveWeights{}));
}

// ==== 2. and 3. ShootingAim::reply with terrain; holding fire to stay hidden (13.09) ====

namespace {

// The monsters fixture with Side A first and terrain at the start (the 13.09 record is kept only with terrain), the
// Havocs in a ruin with a Dense feature, every enemy unit more than 15" away, at Side A's Shoot decision.
GameState havocs_in_ruin(bool one_model) {
    GameState gs = mf::fixture(false, Side::A);
    mf::place_rank(gs.units[mf::kHavocs], 39.5f, 8.0f);
    // The Hive Tyrant with its datasheet's heavy venom cannon (36"): a reply from beyond the Havocs' 15" if they're seen.
    const Datasheet tyrant = load_datasheet(repo_root() / "data/units/hive_tyrant.json");
    gs.units[mf::kTyrant].models[0].weapons.push_back(*tyrant.find_weapon("Heavy venom cannon"));
    if (one_model) {
        // One lascannon Havoc (the champion, first, carries a plasma gun).
        Unit& h = gs.units[mf::kHavocs];
        h.models.erase(h.models.begin(), h.models.end() - 1);
    }
    gs.terrain = {area_with("Ruin", rect_in(33.0f, 44.0f, 6.0f, 10.0f), rect_in(33.2f, 33.6f, 6.2f, 6.6f),
                            TerrainCategory::Dense)};
    start_game(gs);
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::Shoot>(g.pending->kind);
    });
    return gs;
}

bool every_model_hidden(GameState& gs, std::size_t unit) {
    TerrainView view(gs);
    const Unit& u = gs.units[unit];
    for (const Model& m : u.models) {
        if (!view.hidden(u, m)) return false;
    }
    return true;
}

}  // namespace

TEST(M91_HoldFire, OnlyAHideableEndIsScoredHoldingFire) {
    GameState gs = havocs_in_ruin(false);
    ASSERT_TRUE(every_model_hidden(gs, mf::kHavocs));
    analysis::OddsCache cache;
    const normal::MoveScoreContext on(cache, gs, gs.units[mf::kHavocs].id, normal::MoveWeights{});
    EXPECT_TRUE(on.may_hold_fire(positions_of(gs.units[mf::kHavocs])));
    const normal::MoveScoreContext off(cache, gs, gs.units[mf::kHavocs].id, m90_weights());
    EXPECT_FALSE(off.may_hold_fire(positions_of(gs.units[mf::kHavocs])));
    // Outside the ruin there is nothing to hide in.
    std::vector<Position> out = positions_of(gs.units[mf::kHavocs]);
    for (Position& p : out) p.y += in(6.0f);
    EXPECT_FALSE(on.may_hold_fire(out));
}

TEST(M91_HoldFire, TheShootingPhaseAgreesWithTheMoveScore) {
    for (const bool one_model : {false, true}) {
        SCOPED_TRACE(one_model);
        GameState gs = havocs_in_ruin(one_model);
        ASSERT_TRUE(every_model_hidden(gs, mf::kHavocs));
        analysis::OddsCache cache;
        const normal::ShootTerrain terrain{std::make_shared<analysis::TerrainSight>(gs), normal::MoveWeights{}};
        std::vector<UnitId> held;
        const std::optional<normal::ShootingChoice> c =
            normal::choose_shooting(cache, gs, legal_actions(gs, Side::A), 0.0, &terrain, &held);
        // The move score of the Havocs' stay, as the Shooting phase judged it.
        const normal::ArmyPlan plan = normal::army_plan(cache, gs, Side::A);
        normal::MoveScoreContext ctx(cache, gs, gs.units[mf::kHavocs].id, normal::MoveWeights{}, &plan, terrain.sight);
        normal::PlannedEnd stay{MoveType::Normal, positions_of(gs.units[mf::kHavocs]), 0.0f};
        const normal::MoveScore s = normal::refine_move_score(ctx, stay, gs, 0, false);
        const bool havocs_held = std::find(held.begin(), held.end(), gs.units[mf::kHavocs].id) != held.end();
        EXPECT_EQ(havocs_held, s.holds_fire);
        if (havocs_held) EXPECT_TRUE(!c || c->unit != gs.units[mf::kHavocs].id);
        // At hold_fire 0 nothing is held.
        const normal::ShootTerrain zero{terrain.sight, m90_weights()};
        std::vector<UnitId> none;
        (void)normal::choose_shooting(cache, gs, legal_actions(gs, Side::A), 0.0, &zero, &none);
        EXPECT_TRUE(none.empty());
    }
}

TEST(M91_HoldFire, AWeakVolleyHoldsFireAndAStrongOneShoots) {
    // One lascannon Havoc hidden in the ruin: its volley is worth less than the reply it avoids, so it holds fire. The
    // whole unit's five heavy weapons are worth more.
    analysis::OddsCache cache;
    for (const bool one_model : {true, false}) {
        SCOPED_TRACE(one_model);
        GameState gs = havocs_in_ruin(one_model);
        const normal::ShootTerrain terrain{std::make_shared<analysis::TerrainSight>(gs), normal::MoveWeights{}};
        std::vector<UnitId> held;
        (void)normal::choose_shooting(cache, gs, legal_actions(gs, Side::A), 0.0, &terrain, &held);
        const bool havocs_held = std::find(held.begin(), held.end(), gs.units[mf::kHavocs].id) != held.end();
        EXPECT_EQ(havocs_held, one_model);
    }
}

// ==== 4. Objectives in charges (objective_charge) ====

namespace {

// After a successful charge in V1 (the Intercessors engaged with the Termagants), with a mission scoring from round 1
// and one area round both units' fight.
GameState charged_on_area() {
    GameState gs = wh40k::test::after_successful_charge();
    Mission mission;
    mission.first_scoring_round = 1;
    mission.objectives.push_back(wh40k::test::objective_area("Centre", gs.units[0].models[2].position, 6.0f));
    gs.mission = mission;
    gs.result_rule = ResultRule::Mission;
    for (Unit& u : gs.units) {
        u.points = u.side == Side::A ? 80 : 55;
        u.starting_wounds = 0;
        for (const Model& m : u.models) u.starting_wounds += m.characteristics.wounds;
    }
    return gs;
}

}  // namespace

TEST(M91_Charge, AChargeEndingOnAnAreaGainsTheObjectiveTerms) {
    const GameState gs = charged_on_area();
    analysis::OddsCache cache;
    const normal::ArmyPlan plan = normal::no_plan(gs, Side::A);
    const normal::ChargeObjectives objectives{&plan, 1.0};
    const std::vector<UnitId> targets{gs.units[1].id};
    const int turns = normal::turns_left(gs);
    const double plain = normal::charged_value(cache, gs, gs.units[0].id, targets, turns);
    const double with = normal::charged_value(cache, gs, gs.units[0].id, targets, turns, nullptr, &objectives);
    EXPECT_GT(with, plain);
    EXPECT_TRUE(normal::objective_charge_on(gs, normal::MoveWeights{}));
    EXPECT_FALSE(normal::objective_charge_on(gs, m90_weights()));
}

TEST(M91_Charge, ChargingAUnitThatHoldsAnAreaCountsTheControlItCosts) {
    const GameState gs = charged_on_area();
    const normal::ArmyPlan plan = normal::no_plan(gs, Side::A);
    const normal::ChargeObjectives objectives{&plan, 1.0};
    const int turns = normal::turns_left(gs);
    // The same end positions, the Termagants untouched or cut down to a tenth by the fight.
    const std::vector<normal::UnitShare> untouched{{gs.units[1].id, 1.0}};
    const std::vector<normal::UnitShare> cut{{gs.units[1].id, 0.1}};
    const double full = normal::charge_objective_value(gs, gs.units[0].id, 1.0, untouched, turns, objectives);
    const double broken = normal::charge_objective_value(gs, gs.units[0].id, 1.0, cut, turns, objectives);
    EXPECT_GT(broken, full);
    // The outlook counts the target's OC at the share left (rounded): 10 Termagants with OC 2 each in range.
    const std::optional<normal::ObjectiveOutlook> o =
        normal::objective_outlook(gs, gs.units[0], plan, turns, 1.0, cut);
    ASSERT_TRUE(o.has_value());
    const std::optional<normal::ObjectiveOutlook> o_full = normal::objective_outlook(gs, gs.units[0], plan, turns, 1.0);
    ASSERT_TRUE(o_full.has_value());
    EXPECT_LT(o->areas[0].them_now, o_full->areas[0].them_now);
}

// ==== 5. Objectives in pile-ins and consolidations (objective_fight) ====

TEST(M91_FightMoves, TheFightOutlookCoversTheEnemysTurnToo) {
    GameState gs = wh40k::test::objective_consolidation_state(2.0f);
    gs.mission->first_scoring_round = 1;
    // Side A's turn: our own outlook (the enemy scores next at the end of this turn, we after its turn).
    const std::optional<normal::ObjectiveOutlook> ours = normal::fight_outlook(gs, gs.units[0], 1.0);
    ASSERT_TRUE(ours.has_value());
    EXPECT_TRUE(ours->next.theirs);
    EXPECT_TRUE(ours->next.ours);
    // Side B's units in Side A's turn: control at the end of this turn is what Side B scores next, for certain.
    const std::optional<normal::ObjectiveOutlook> theirs = normal::fight_outlook(gs, gs.units[1], 1.0);
    ASSERT_TRUE(theirs.has_value());
    EXPECT_TRUE(theirs->next.ours);
    EXPECT_FALSE(theirs->next.theirs);
    ASSERT_EQ(theirs->areas.size(), 1u);
    EXPECT_EQ(theirs->areas[0].them_now, 0);  // Side A has no model in the area yet
    EXPECT_DOUBLE_EQ(theirs->areas[0].reach.p_below(1.0), 1.0);  // its level then: what it has now
    // No mission, or weight 0: none.
    EXPECT_FALSE(normal::fight_outlook(gs, gs.units[0], 0.0).has_value());
}

TEST(M91_FightMoves, AnObjectiveConsolidationIsWeighedWithTheObjectiveTerms) {
    GameState gs = wh40k::test::objective_consolidation_state(2.0f);
    analysis::OddsCache cache;
    const std::vector<GameAction> legal = legal_actions(gs, Side::A);
    const std::optional<PolicyResult> r = normal::decide_consolidation(cache, gs, Side::A, legal, 1, normal::MoveWeights{});
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(std::holds_alternative<action::StartConsolidation>(r->action)) << r->reason;
    EXPECT_NE(r->reason.find("for objectives"), std::string::npos) << r->reason;
    // At objective_fight 0: M90's rule, unweighed.
    const std::optional<PolicyResult> m90 = normal::decide_consolidation(cache, gs, Side::A, legal, 1, m90_weights());
    ASSERT_TRUE(m90.has_value());
    EXPECT_NE(m90->reason.find("more model"), std::string::npos) << m90->reason;
    EXPECT_EQ(m90->reason.find("for objectives"), std::string::npos) << m90->reason;
}

// ==== Regression, arena ====

TEST(M91_Regression, TheNewKeysAtZeroPlayTheTerrainAndObjectiveScenariosAsM90) {
    // The M90 Release arena, seed 1 (`--per-game 1`, default weights then).
    const struct {
        const char* scenario;
        const char* actions;
        const char* state;
        const char* trace;
    } pins[] = {{kTerrainScenario, "dd02111be5230c80", "e3f0d22c61823516", "54c0dfee123ead1b"},
                {kObjectiveScenario, "0f541058be8987ce", "7d146f09e35eecf6", "a4954478d7e5ca05"}};
    for (const auto& pin : pins) {
        SCOPED_TRACE(pin.scenario);
        const GameState scenario = load_scenario(repo_root() / pin.scenario, repo_root());
        const PerGameReport r = run_per_game(scenario, 1, 1, config(m90_weights()), config(m90_weights()));
        EXPECT_TRUE(r.clean);
        EXPECT_EQ(per_game_field(r.text, "actions"), pin.actions);
        EXPECT_EQ(per_game_field(r.text, "state"), pin.state);
        EXPECT_EQ(per_game_field(r.text, "trace"), pin.trace);
    }
}

TEST(M91_Arena, TheSummaryHasTheNewLines) {
    const GameState scenario = load_scenario(repo_root() / kTerrainScenario, repo_root());
    const ArenaResult r = run_arena(scenario, 1, 1, BotConfig{}, BotConfig{});
    EXPECT_TRUE(r.clean());
    const std::string text = format_arena(r, scenario);
    EXPECT_NE(text.find("holds of fire a game (M91)"), std::string::npos) << text;
    EXPECT_NE(text.find("charges onto an objective a game (M91)"), std::string::npos) << text;
}

