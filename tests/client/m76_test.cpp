// M76 (D-082 part 2; 03.02, 03.03, 03.04, D-009, D-064, D-081): placing units by hand in the client. The placement
// editor (DeployPlan) starts at core's proposal, is dragged and turned without touching the game, and takes every
// verdict from core; "Auto-place the rest" sets a side's units up at core's proposals while it is that side's turn, one
// Undo group per unit; "Can't be set up" is offered only when core offers it; the sidebar lists units not set up yet.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "auto_actions.hpp"
#include "core/data/army_list.hpp"
#include "core/data/datasheet.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/pose.hpp"
#include "core/state/deployment.hpp"
#include "deploy_plan.hpp"
#include "launch_options.hpp"
#include "session.hpp"
#include "support/army_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"
#include "undo_groups.hpp"

using namespace wh40k::client;
namespace core = wh40k::core;
using core::GameAction;
using core::GameState;
using core::Side;
using core::UnitId;
using wh40k::test::decider;
using wh40k::test::in;
using wh40k::test::starts_with;

namespace {

GameState stock_battle(Side first, core::DeploymentMode mode = core::DeploymentMode::Hand) {
    return wh40k::test::custom_battle(wh40k::test::stock_army(wh40k::test::csm()),
                                      wh40k::test::stock_army(wh40k::test::tyranids()), first, mode);
}

GameState build(const nlohmann::json& scenario) {
    return core::build_game_state(scenario, [](const std::string& unit_file) {
        return core::load_datasheet(wh40k::test::repo_root() / unit_file);
    });
}

nlohmann::json intercessors(int points = 0) {
    nlohmann::json u{{"unit_file", "data/units/intercessor_squad.json"},
                     {"models",
                      {{{"type", "Intercessor Sergeant"}, {"count", 1}, {"weapons", {"Bolt Rifle", "Bolt pistol", "Knives and Fists"}}},
                       {{"type", "Intercessor"}, {"count", 4}, {"weapons", {"Bolt Rifle", "Bolt pistol", "Knives and Fists"}}}}}};
    if (points > 0) u["points"] = points;
    return u;
}

nlohmann::json termagants(int points = 0) {
    nlohmann::json u{{"unit_file", "data/units/termagants.json"},
                     {"models", {{{"type", "Termagants"}, {"count", 10}, {"weapons", {"Fleshborer", "Chitinous claws and teeth"}}}}}};
    if (points > 0) u["points"] = points;
    return u;
}

// Set up by hand (D-081): side A's zone along the south edge, side B's along the north.
nlohmann::json hand_scenario(float width_in, float depth_in, float zone_a_in, float zone_b_in, nlohmann::json units_a,
                             nlohmann::json units_b, const char* first = "A") {
    return nlohmann::json{
        {"name", "M76 fixture"},
        {"battlefield", {{"width_in", width_in}, {"depth_in", depth_in}}},
        {"battle_rounds", 5},
        {"first_turn", first},
        {"deployment", "alternating"},
        {"sides",
         {{"A", {{"name", "South"}, {"deployment_zone", {{"edge", "south"}, {"depth_in", zone_a_in}}}, {"units", units_a}}},
          {"B", {{"name", "North"}, {"deployment_zone", {{"edge", "north"}, {"depth_in", zone_b_in}}}, {"units", units_b}}}}}};
}

// 03.04: zones 1" apart (24" x 25", 12" zones): Intercessors (A, unit 1), Termagants (B, unit 2). B sets up first.
GameState zones_one_inch_apart() {
    return build(hand_scenario(24.0f, 25.0f, 12.0f, 12.0f, nlohmann::json::array({intercessors()}),
                               nlohmann::json::array({termagants()})));
}

// D-081: a 12" x 24" battlefield whose 2.5" south zone holds two lines of 10 Termagants but not a third (units 1-3, 60
// points each); side B has an Intercessor Squad (unit 4, 90 points) in a 3" north zone. A takes the first turn, so B
// sets up first, then A, then A sets up the rest.
GameState narrow_zone() {
    nlohmann::json s = hand_scenario(12.0f, 24.0f, 2.5f, 3.0f,
                                     nlohmann::json::array({termagants(60), termagants(60), termagants(60)}),
                                     nlohmann::json::array({intercessors(90)}));
    s["result"] = "points_destroyed";
    return build(s);
}

std::vector<std::string> texts(const std::vector<core::SetUpProblem>& problems) {
    std::vector<std::string> out;
    for (const core::SetUpProblem& p : problems) out.push_back(p.reason);
    return out;
}

bool has(const std::vector<core::ModelId>& ids, core::ModelId id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

// One player input as main.cpp makes it (D-039): a group round the action and the automatic actions after it.
bool press(GameSession& s, UndoGroups& groups, const AutoSettings& autos, const GameAction& a,
           InputOwner owner = InputOwner::Human) {
    groups.begin(s.history().size(), owner);
    const bool ok = s.submit(a).ok();
    if (ok) (void)run_auto_actions_grouped(s, autos, groups);
    groups.end(s.history().size());
    return ok;
}

// The unit with this id, set up or not.
const core::Unit& unit_of(const GameState& gs, UnitId id) {
    if (const core::Unit* u = gs.find_unit(id)) return *u;
    return *core::find_undeployed(gs, id);
}

}  // namespace

// ==== M76_DeployPlan: the placement in progress ====

TEST(M76_DeployPlan, StartsAtTheProposal) {
    GameState gs = stock_battle(Side::B);  // A sets up first
    core::start_game(gs);
    ASSERT_EQ(decider(gs), Side::A);
    for (const core::Unit& u : gs.undeployed) {
        const std::optional<DeployPlan> plan = DeployPlan::start(gs, u.id);
        ASSERT_TRUE(plan.has_value()) << "unit " << u.id.v;
        EXPECT_EQ(plan->unit(), u.id);
        EXPECT_EQ(plan->side(), u.side);
        EXPECT_TRUE(plan->from_proposal());
        EXPECT_EQ(plan->action(), core::propose_set_up(gs, u.id).value()) << "unit " << u.id.v;
        ASSERT_EQ(plan->models().size(), u.models.size());
        for (std::size_t i = 0; i < u.models.size(); ++i) {
            EXPECT_EQ(plan->models()[i].model, u.models[i].id);  // in the unit's model order
            EXPECT_EQ(plan->models()[i].base, u.models[i].base);
        }
        EXPECT_TRUE(plan->problems(gs).empty()) << "unit " << u.id.v;
    }
    EXPECT_FALSE(DeployPlan::start(gs, UnitId{99}).has_value());
    // The default selection is the lowest-id unit, as the bots pick (D-081).
    EXPECT_EQ(first_undeployed(gs, Side::A), UnitId{1});
    EXPECT_EQ(first_undeployed(gs, Side::B), UnitId{6});
    ASSERT_TRUE(core::apply_action(gs, Side::A, core::legal_actions(gs, Side::A).front()).size() > 0);
    EXPECT_FALSE(DeployPlan::start(gs, UnitId{1}).has_value());  // set up already
    EXPECT_EQ(first_undeployed(gs, Side::A), UnitId{2});
    // An action given back (Undo) is the plan; one that misses a model is not.
    const core::action::DeployUnit p = core::propose_set_up(gs, UnitId{2}).value();
    const std::optional<DeployPlan> back = DeployPlan::from_action(gs, p);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->action(), p);
    core::action::DeployUnit short_one = core::propose_set_up(gs, UnitId{3}).value();
    short_one.models.pop_back();
    EXPECT_FALSE(DeployPlan::from_action(gs, short_one).has_value());
}

TEST(M76_DeployPlan, WithNoLegalSpotItStartsAtTheZoneFrontAndCoreRefusesIt) {
    GameState gs = narrow_zone();
    core::start_game(gs);
    // B's Intercessors, then A's first two Termagant units at their proposals: the third has no legal spot left.
    for (int i = 0; i < 3; ++i) core::apply_action(gs, decider(gs), core::legal_actions(gs, decider(gs)).front());
    ASSERT_EQ(decider(gs), Side::A);
    ASSERT_FALSE(core::propose_set_up(gs, UnitId{3}).has_value());
    const std::optional<DeployPlan> plan = DeployPlan::start(gs, UnitId{3});
    ASSERT_TRUE(plan.has_value());
    EXPECT_FALSE(plan->from_proposal());
    EXPECT_EQ(plan->models().size(), 10u);
    // One rank of 10 Termagants centred on the battlefield, their bases touching the zone's front line (2.5").
    EXPECT_NEAR(plan->centre().x, in(6.0f), 0.01f);
    for (const DeployPlan::Placed& m : plan->models()) EXPECT_FLOAT_EQ(m.position.y, in(2.5f) - m.base.diameter_mm * 0.5f);
    const std::vector<core::SetUpProblem> problems = plan->problems(gs);
    ASSERT_FALSE(problems.empty());
    EXPECT_EQ(texts(problems), core::set_up_problems(gs, Side::A, plan->action()));
}

TEST(M76_DeployPlan, TranslateKeepsTheShape) {
    GameState gs = stock_battle(Side::B);
    core::start_game(gs);
    const DeployPlan start = DeployPlan::start(gs, UnitId{3}).value();  // 10 Chaos Terminators
    DeployPlan moved = start;
    moved.translate(in(-3.0f), in(-1.5f));
    ASSERT_EQ(moved.models().size(), start.models().size());
    for (std::size_t i = 0; i < start.models().size(); ++i) {
        EXPECT_FLOAT_EQ(moved.models()[i].position.x, start.models()[i].position.x + in(-3.0f));
        EXPECT_FLOAT_EQ(moved.models()[i].position.y, start.models()[i].position.y + in(-1.5f));
        EXPECT_EQ(moved.models()[i].facing_deg, start.models()[i].facing_deg);
        EXPECT_EQ(moved.models()[i].base, start.models()[i].base);
    }
    // The block turned round its centre: each model's offset from it turns too; turned back, it is where it was.
    DeployPlan turned = start;
    turned.rotate_block(90.0f);
    const core::Position c = start.centre();
    for (std::size_t i = 0; i < start.models().size(); ++i) {
        const float dx = start.models()[i].position.x - c.x;
        const float dy = start.models()[i].position.y - c.y;
        EXPECT_NEAR(turned.models()[i].position.x, c.x - dy, 1e-3f);  // counter-clockwise (D-074)
        EXPECT_NEAR(turned.models()[i].position.y, c.y + dx, 1e-3f);
        EXPECT_EQ(turned.models()[i].facing_deg, 0.0f);  // round bases have no facing
    }
    turned.rotate_block(-90.0f);
    for (std::size_t i = 0; i < start.models().size(); ++i) {
        EXPECT_NEAR(turned.models()[i].position.x, start.models()[i].position.x, 1e-3f);
        EXPECT_NEAR(turned.models()[i].position.y, start.models()[i].position.y, 1e-3f);
    }
    // One model moved: only that one changes; a model of another unit doesn't move.
    DeployPlan one = start;
    const core::ModelId first = start.models().front().model;
    EXPECT_TRUE(one.move_model(first, core::Position{in(20.0f), in(5.0f), 3.0f}));
    EXPECT_EQ(one.find(first)->position, (core::Position{in(20.0f), in(5.0f), 0.0f}));  // z stays 0
    for (std::size_t i = 1; i < start.models().size(); ++i) EXPECT_EQ(one.models()[i], start.models()[i]);
    EXPECT_FALSE(one.move_model(core::ModelId{9999}, core::Position{}));
    // Hit-testing: the model whose base holds the point.
    EXPECT_EQ(start.model_at(start.models()[4].position), start.models()[4].model);
    EXPECT_FALSE(start.model_at(core::Position{-in(10.0f), -in(10.0f), 0.0f}).has_value());
}

TEST(M76_DeployPlan, ProblemsComeFromCore) {
    GameState gs = zones_one_inch_apart();
    core::start_game(gs);
    ASSERT_EQ(decider(gs), Side::B);
    core::apply_action(gs, Side::B, core::legal_actions(gs, Side::B).front());  // the Termagants on their zone's front
    ASSERT_EQ(decider(gs), Side::A);
    const DeployPlan start = DeployPlan::start(gs, UnitId{1}).value();
    EXPECT_TRUE(start.problems(gs).empty());  // core's proposal stands 1" back (R03_04_Engagement)
    const auto check = [&](const DeployPlan& plan, const char* rule) {
        const std::vector<core::SetUpProblem> problems = plan.problems(gs);
        // The same reasons, in the same order, as the engine's validation (D-024).
        EXPECT_EQ(texts(problems), core::set_up_problems(gs, Side::A, plan.action()));
        EXPECT_EQ(texts(problems), core::validate_action(gs, Side::A, plan.action()).reasons);
        EXPECT_TRUE(std::any_of(problems.begin(), problems.end(), [&](const core::SetUpProblem& p) {
            return starts_with(p.reason, rule);
        })) << rule;
        return problems;
    };
    const std::vector<core::ModelId> ids = [&] {
        std::vector<core::ModelId> out;
        for (const DeployPlan::Placed& p : start.models()) out.push_back(p.model);
        return out;
    }();
    // 03.02: on the battlefield. 14" west, every model is off its west edge.
    DeployPlan out_of_zone = start;
    out_of_zone.translate(in(-14.0f), 0.0f);
    {
        const auto p = check(out_of_zone, "03.02: model");
        EXPECT_EQ(models_at_fault(p).size(), 5u);
    }
    DeployPlan behind = start;
    // D-009: wholly within its own zone. One model's base across the zone's front line (12").
    behind.move_model(ids[2], core::Position{start.find(ids[2])->position.x, in(12.6f), 0.0f});
    {
        const auto p = check(behind, "D-009: model");
        EXPECT_TRUE(has(models_at_fault(p), ids[2]));
        EXPECT_FALSE(has(models_at_fault(p), ids[0]));
    }
    // D-064: a model on another of its unit: both are at fault.
    DeployPlan overlap = start;
    overlap.move_model(ids[1], start.find(ids[0])->position);
    {
        const auto p = check(overlap, "03.01: model");
        EXPECT_TRUE(has(models_at_fault(p), ids[0]));
        EXPECT_TRUE(has(models_at_fault(p), ids[1]));
    }
    // 03.03: a model more than 2" from every other model of its unit.
    DeployPlan apart = start;
    const core::Position last = start.find(ids[4])->position;
    apart.move_model(ids[4], core::Position{last.x, last.y - in(3.5f), 0.0f});  // still within 9" of every model
    {
        const auto p = check(apart, "03.03: model");
        EXPECT_EQ(models_at_fault(p), std::vector<core::ModelId>{ids[4]});
    }
    // 03.04: within 2" of the Termagants (engaged): 1/4" further north than the proposal.
    DeployPlan engaged = start;
    engaged.translate(0.0f, in(0.25f));
    {
        const auto p = check(engaged, "03.02: unit 1 would be set up engaged with unit 2 (03.04");
        EXPECT_FALSE(models_at_fault(p).empty());
        for (const core::ModelId id : models_at_fault(p)) EXPECT_TRUE(has(ids, id));
    }
}

TEST(M76_DeployPlan, OvalTurnKeepsFacingModulo180AndRoundBasesStayAtZero) {
    GameState gs = stock_battle(Side::A);  // B sets up first
    core::start_game(gs);
    ASSERT_EQ(decider(gs), Side::B);
    std::optional<UnitId> tyrannofex;
    for (const core::Unit& u : gs.undeployed) {
        if (u.models.front().base.shape == core::BaseShape::Oval) tyrannofex = u.id;
    }
    ASSERT_TRUE(tyrannofex.has_value());
    DeployPlan plan = DeployPlan::start(gs, *tyrannofex).value();
    const core::ModelId oval = plan.models().front().model;
    EXPECT_EQ(plan.find(oval)->facing_deg, 0.0f);
    EXPECT_TRUE(plan.turn_model(oval, 30.0f));
    EXPECT_FLOAT_EQ(plan.find(oval)->facing_deg, 30.0f);
    EXPECT_TRUE(plan.turn_model(oval, 170.0f));  // 200 degrees: the same footprint as 20
    EXPECT_FLOAT_EQ(plan.find(oval)->facing_deg, 20.0f);
    EXPECT_TRUE(plan.turn_model(oval, -110.0f));  // -90: kept as 90, in (-90, 90] (footprint_facing_deg)
    EXPECT_FLOAT_EQ(plan.find(oval)->facing_deg, 90.0f);
    EXPECT_EQ(plan.action().models.front().facing_deg, 90.0f);
    // The block turned with an oval in it: the oval turns too.
    plan.rotate_block(45.0f);
    EXPECT_FLOAT_EQ(plan.find(oval)->facing_deg, -45.0f);  // 135, kept as -45
    // Round bases: no facing to turn.
    DeployPlan gaunts = DeployPlan::start(gs, UnitId{6}).value();
    const core::ModelId round = gaunts.models().front().model;
    EXPECT_FALSE(gaunts.turn_model(round, 30.0f));
    gaunts.rotate_block(30.0f);
    for (const DeployPlan::Placed& p : gaunts.models()) EXPECT_EQ(p.facing_deg, 0.0f);
    for (const core::ModelPlacement& m : gaunts.action().models) EXPECT_EQ(m.facing_deg, 0.0f);
    // The Tyrannofex at its proposal turned a quarter turn is still wholly within the zone (D-009), and is set up so.
    DeployPlan quarter = DeployPlan::start(gs, *tyrannofex).value();
    EXPECT_TRUE(quarter.turn_model(oval, 90.0f));
    EXPECT_TRUE(quarter.problems(gs).empty());
    GameSession s = GameSession::new_game(stock_battle(Side::A), 1);
    ASSERT_TRUE(s.submit(quarter.action()).ok());
    EXPECT_EQ(s.state().find_unit(*tyrannofex)->models.front().facing_deg, 90.0f);
}

TEST(M76_DeployPlan, SubmitIsAccepted) {
    GameSession s = GameSession::new_game(stock_battle(Side::B), 1);  // A sets up first
    DeployPlan plan = DeployPlan::start(s.state(), UnitId{1}).value();
    plan.translate(in(-2.0f), in(-1.0f));  // still wholly within the zone
    ASSERT_TRUE(plan.problems(s.state()).empty());
    const core::ValidationResult r = s.submit(plan.action());
    ASSERT_TRUE(r.ok()) << (r.reasons.empty() ? "" : r.reasons.front());
    const core::Unit* set_up = s.state().find_unit(UnitId{1});
    ASSERT_NE(set_up, nullptr);
    for (std::size_t i = 0; i < set_up->models.size(); ++i) {
        EXPECT_EQ(set_up->models[i].position, plan.models()[i].position);
    }
    // A placement core refuses is refused with exactly its problems, and changes nothing.
    ASSERT_EQ(s.pending()->side, Side::B);
    DeployPlan bad = DeployPlan::start(s.state(), UnitId{6}).value();
    bad.translate(0.0f, in(-20.0f));  // out of B's zone, into no man's land
    const std::vector<std::string> problems = texts(bad.problems(s.state()));
    ASSERT_FALSE(problems.empty());
    const std::size_t steps = s.history().size();
    const core::ValidationResult refused = s.submit(bad.action());
    EXPECT_FALSE(refused.ok());
    EXPECT_EQ(refused.reasons, problems);
    EXPECT_EQ(s.history().size(), steps);
}

// ==== M76_AutoDeploy: "Auto-place the rest" ====

TEST(M76_AutoDeploy, TheRestPlacesAtTheProposalsAndPausesForTheOpponent) {
    GameSession s = GameSession::new_game(stock_battle(Side::A), 1);  // B (9 units) sets up first, then A (5)
    UndoGroups groups;
    AutoSettings autos;
    autos.auto_deploy = {false, true};  // B asked to auto-place the rest
    // The click: B's lowest-id unit goes to its proposal, then A's decision waits for A.
    ASSERT_EQ(s.pending()->side, Side::B);
    const core::action::DeployUnit first = core::propose_set_up(s.state(), UnitId{6}).value();
    groups.begin(s.history().size());
    const std::vector<GameAction> done = run_auto_actions_grouped(s, autos, groups);
    groups.end(s.history().size());
    ASSERT_EQ(done.size(), 1u);
    EXPECT_EQ(done.front(), GameAction{first});
    EXPECT_EQ(s.pending()->side, Side::A);
    // A places each of its units by hand (here at its proposal); after each, B's next unit follows by itself.
    int a_presses = 0;
    while (s.state().flow.stage == core::Stage::Deploy) {
        ASSERT_EQ(s.pending()->side, Side::A) << "B never waits for a click";
        const std::size_t b_before = static_cast<std::size_t>(std::count_if(
            s.state().units.begin(), s.state().units.end(), [](const core::Unit& u) { return u.side == Side::B; }));
        ASSERT_TRUE(press(s, groups, autos, s.legal().front()));
        ++a_presses;
        const std::size_t b_after = static_cast<std::size_t>(std::count_if(
            s.state().units.begin(), s.state().units.end(), [](const core::Unit& u) { return u.side == Side::B; }));
        // B sets up one unit after each of A's, and the rest once A is done (D-081).
        EXPECT_EQ(b_after - b_before, a_presses < 5 ? 1u : 4u);
    }
    EXPECT_EQ(a_presses, 5);
    // Every unit stands where Auto-place puts it: the same battle as auto-placed (D-081's proposals).
    GameState placed = stock_battle(Side::A, core::DeploymentMode::Auto);
    core::start_game(placed);
    ASSERT_EQ(s.state().units.size(), placed.units.size());
    for (std::size_t i = 0; i < placed.units.size(); ++i) {
        ASSERT_EQ(s.state().units[i].id, placed.units[i].id);
        for (std::size_t m = 0; m < placed.units[i].models.size(); ++m) {
            EXPECT_EQ(s.state().units[i].models[m].position, placed.units[i].models[m].position);
        }
    }
    // Not for a side the bot plays (it sets its own units up), nor while it is the other side's turn.
    GameSession vs = GameSession::new_game(stock_battle(Side::A), 1);
    AutoSettings bot = autos;
    bot.bot_controlled = {false, true};
    EXPECT_FALSE(next_auto_action(vs.state(), vs.legal(), bot).has_value());
    AutoSettings a_only;
    a_only.auto_deploy = {true, false};
    EXPECT_FALSE(next_auto_action(vs.state(), vs.legal(), a_only).has_value());  // B's turn to set up
}

TEST(M76_AutoDeploy, AUnitWithNoLegalSpotIsLeftToThePlayer) {
    GameSession s = GameSession::new_game(narrow_zone(), 1);  // B's Intercessors first, then A's three Termagant units
    UndoGroups groups;
    AutoSettings autos;
    autos.auto_deploy = {true, false};
    ASSERT_TRUE(press(s, groups, autos, s.legal().front()));  // B, by hand; then A's two units by themselves
    EXPECT_EQ(s.state().flow.stage, core::Stage::Deploy);
    ASSERT_EQ(s.pending()->side, Side::A);
    ASSERT_EQ(s.state().undeployed.size(), 1u);
    EXPECT_EQ(s.state().undeployed.front().id, UnitId{3});
    // Only CannotSetUp is offered for it (and its template): "Auto-place the rest" never declares it.
    EXPECT_FALSE(next_auto_action(s.state(), s.legal(), autos).has_value());
    EXPECT_EQ(s.legal().front(), GameAction{core::action::CannotSetUp{UnitId{3}}});
}

// ==== M76_Undo: one group per unit set up ====

TEST(M76_Undo, OneSetUpPerGroup) {
    GameSession s = GameSession::new_game(stock_battle(Side::A), 1);  // B first
    UndoGroups groups;
    AutoSettings autos;
    // B places one unit by hand, A one, then B asks to auto-place the rest: B's second unit, by itself.
    ASSERT_TRUE(press(s, groups, autos, s.legal().front()));  // B: unit 6
    ASSERT_TRUE(press(s, groups, autos, s.legal().front()));  // A: unit 1
    autos.auto_deploy = {false, true};
    groups.begin(s.history().size());
    (void)run_auto_actions_grouped(s, autos, groups);  // the click: B's unit 7
    groups.end(s.history().size());
    ASSERT_TRUE(press(s, groups, autos, s.legal().front()));  // A: unit 2, then B's unit 8 by itself
    ASSERT_EQ(s.history().size(), 5u);
    EXPECT_EQ(groups.starts(), (std::vector<std::size_t>{0, 1, 2, 3, 4}));  // five set-ups, five groups
    // Undo takes back the last set-up only: B's automatic one, then A's, then B's.
    ASSERT_TRUE(groups.undo(s));
    EXPECT_NE(core::find_undeployed(s.state(), UnitId{8}), nullptr);
    EXPECT_EQ(s.state().find_unit(UnitId{2})->side, Side::A);
    EXPECT_EQ(s.pending()->side, Side::B);
    ASSERT_TRUE(groups.undo(s));
    EXPECT_NE(core::find_undeployed(s.state(), UnitId{2}), nullptr);
    EXPECT_EQ(s.pending()->side, Side::A);
    ASSERT_TRUE(groups.undo(s));
    EXPECT_NE(core::find_undeployed(s.state(), UnitId{7}), nullptr);
    EXPECT_EQ(s.state().units.size(), 2u);
    // Against a bot (D-054): the automatic set-ups are the human's inputs; the human's undo takes back the bot's set-up
    // after the last of them too.
    GameSession vs = GameSession::new_game(stock_battle(Side::A), 1);  // the human plays B, the bot A
    UndoGroups g2;
    AutoSettings human;
    human.bot_controlled = {true, false};
    human.auto_deploy = {false, true};
    g2.begin(vs.history().size());
    (void)run_auto_actions_grouped(vs, human, g2);  // B's unit 6
    g2.end(vs.history().size());
    ASSERT_TRUE(press(vs, g2, human, vs.legal().front(), InputOwner::Bot));  // the bot's unit 1, then B's unit 7
    ASSERT_EQ(vs.history().size(), 3u);
    ASSERT_TRUE(g2.undo_human(vs));
    EXPECT_EQ(vs.history().size(), 2u);
    EXPECT_NE(core::find_undeployed(vs.state(), UnitId{7}), nullptr);
    ASSERT_TRUE(g2.undo_human(vs));  // B's unit 6 and the bot's unit 1 after it
    EXPECT_TRUE(vs.state().units.empty());
    EXPECT_FALSE(is_set_up(GameAction{core::action::EndShootStep{}}));
    EXPECT_TRUE(is_set_up(GameAction{core::action::CannotSetUp{UnitId{3}}}));
}

// ==== M76_CannotSetUp: offered only when core offers it ====

TEST(M76_CannotSetUp, OfferedOnlyWhenCoreOffersIt) {
    GameSession s = GameSession::new_game(narrow_zone(), 1);
    ASSERT_TRUE(s.submit(s.legal().front()).ok());  // B's Intercessors
    // Units with a legal spot: "Auto-place this unit" (their proposal), never "Can't be set up".
    for (const UnitId id : {UnitId{1}, UnitId{2}, UnitId{3}}) {
        const DeployChoices c = deploy_choices(s.legal(), id);
        EXPECT_EQ(c.auto_place, core::propose_set_up(s.state(), id)) << id.v;
        EXPECT_FALSE(c.cannot_set_up.has_value()) << id.v;
    }
    ASSERT_TRUE(s.submit(s.legal().front()).ok());  // A: unit 1
    ASSERT_TRUE(s.submit(s.legal().front()).ok());  // A: unit 2; unit 3 has no spot left
    const DeployChoices none = deploy_choices(s.legal(), UnitId{3});
    EXPECT_FALSE(none.auto_place.has_value());
    ASSERT_TRUE(none.cannot_set_up.has_value());
    EXPECT_TRUE(core::cannot_set_up_problems(s.state(), Side::A, UnitId{3}).empty());
    // Its consequence: destroyed, and the opponent scores its points (D-081, D-076).
    EXPECT_EQ(cannot_set_up_text(s.state(), UnitId{3}),
              "No legal spot is left for Termagants 3: it counts as destroyed and Side B scores its 60 points (D-081, "
              "D-076).");
    EXPECT_FALSE(deploy_choices(s.legal(), UnitId{4}).cannot_set_up.has_value());  // not A's unit
    ASSERT_TRUE(s.submit(*none.cannot_set_up).ok());
    EXPECT_TRUE(s.state().find_unit(UnitId{3})->models.empty());
    EXPECT_NE(s.state().flow.stage, core::Stage::Deploy);  // battle round 1
    EXPECT_EQ(s.state().flow.battle_round, 1);
}

// ==== M76_Sidebar: the units not set up yet, as the Game panel lists them ====

TEST(M76_Sidebar, UnitsNotSetUpYetShowTheirModelsAndPoints) {
    GameState gs = stock_battle(Side::A);
    core::start_game(gs);
    for (int i = 0; i < 3; ++i) core::apply_action(gs, decider(gs), core::legal_actions(gs, decider(gs)).front());
    ASSERT_EQ(gs.undeployed.size(), 11u);  // listed in id order (D-081), none left out
    for (std::size_t i = 1; i < gs.undeployed.size(); ++i) EXPECT_LT(gs.undeployed[i - 1].id, gs.undeployed[i].id);
    const core::Unit& gaunts = unit_of(gs, UnitId{14});
    EXPECT_EQ(models_and_points_text(gaunts),
              std::to_string(gaunts.models.size()) + " models, " + std::to_string(gaunts.points) + " pts");
    const core::Unit& lord = unit_of(gs, UnitId{2});
    EXPECT_EQ(models_and_points_text(lord), "1 model, " + std::to_string(lord.points) + " pts");
    core::Unit no_points = lord;
    no_points.points = 0;
    EXPECT_EQ(models_and_points_text(no_points), "1 model");
}

// ==== M76_LaunchOptions: --deploy-unit ====

TEST(M76_LaunchOptions, ParsesDeployUnit) {
    EXPECT_FALSE(parse_launch_args({}).deploy_unit.has_value());
    const LaunchOptions a = parse_launch_args({"--deploy-unit", "7:1.5,-2"});
    ASSERT_TRUE(a.deploy_unit.has_value());
    EXPECT_EQ(a.deploy_unit->unit, 7u);
    EXPECT_FLOAT_EQ(a.deploy_unit->dx_in, 1.5f);
    EXPECT_FLOAT_EQ(a.deploy_unit->dy_in, -2.0f);
    EXPECT_FALSE(a.deploy_unit->model.has_value());
    const LaunchOptions b = parse_launch_args({"--deploy-unit", "11:0,0:61:20.5,50", "--turn", "61:30"});
    ASSERT_TRUE(b.deploy_unit.has_value());
    EXPECT_EQ(b.deploy_unit->unit, 11u);
    EXPECT_EQ(b.deploy_unit->model, 61u);
    EXPECT_FLOAT_EQ(b.deploy_unit->x_in, 20.5f);
    EXPECT_FLOAT_EQ(b.deploy_unit->y_in, 50.0f);
    EXPECT_EQ(b.turn, (CliTurn{61, 30.0f, 0.0f, 0.0f}));
    // It goes with a custom battle's flags (a hand deployment is one).
    EXPECT_NO_THROW((void)parse_launch_args({"--deploy", "hand", "--first", "B", "--deploy-unit", "3:0,1"}));
    for (const std::string bad : {"", "7", "7:", "7:1", "x:1,2", "-1:0,0", "7:1,2:3", "7:1,2:3:4", "7:a,b",
                                  "7:1,2:3:4,5:6", "7:1,2,3", "7:1,2:x:4,5", "7:nan,1"}) {
        EXPECT_THROW((void)parse_launch_args({"--deploy-unit", bad}), std::invalid_argument) << bad;
    }
}
