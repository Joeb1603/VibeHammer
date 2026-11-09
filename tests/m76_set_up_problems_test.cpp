// M76 (D-082 part 2; 03.02, 03.03, 03.04, D-009, D-064): set_up_problem_details, the read-only query the client's
// placement editor marks models with. Its reasons are set_up_problems' (the engine's validation of a DeployUnit, D-081),
// in the same order; each carries the models of the unit being set up that cause it.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/data/army_list.hpp"
#include "core/data/datasheet.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/oval.hpp"
#include "core/geometry/pose.hpp"
#include "core/state/deployment.hpp"
#include "support/army_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::decider;
using wh40k::test::in;
using wh40k::test::repo_root;
using wh40k::test::starts_with;

namespace {

GameState stock_battle(Side first) {
    return wh40k::test::custom_battle(wh40k::test::stock_army(wh40k::test::csm()),
                                      wh40k::test::stock_army(wh40k::test::tyranids()), first, DeploymentMode::Hand);
}

// Zones 1" apart (24" x 25", 12" zones): Intercessors (A, unit 1) and Termagants (B, unit 2); B sets up first.
GameState zones_one_inch_apart() {
    const nlohmann::json intercessors{
        {"unit_file", "data/units/intercessor_squad.json"},
        {"models",
         {{{"type", "Intercessor Sergeant"}, {"count", 1}, {"weapons", {"Bolt Rifle", "Bolt pistol", "Knives and Fists"}}},
          {{"type", "Intercessor"}, {"count", 4}, {"weapons", {"Bolt Rifle", "Bolt pistol", "Knives and Fists"}}}}}};
    const nlohmann::json termagants{
        {"unit_file", "data/units/termagants.json"},
        {"models", {{{"type", "Termagants"}, {"count", 10}, {"weapons", {"Fleshborer", "Chitinous claws and teeth"}}}}}};
    const nlohmann::json s{
        {"name", "M76 fixture"},
        {"battlefield", {{"width_in", 24.0f}, {"depth_in", 25.0f}}},
        {"battle_rounds", 5},
        {"first_turn", "A"},
        {"deployment", "alternating"},
        {"sides",
         {{"A", {{"name", "South"}, {"deployment_zone", {{"edge", "south"}, {"depth_in", 12.0f}}}, {"units", {intercessors}}}},
          {"B", {{"name", "North"}, {"deployment_zone", {{"edge", "north"}, {"depth_in", 12.0f}}}, {"units", {termagants}}}}}}};
    return build_game_state(s, [](const std::string& unit_file) { return load_datasheet(repo_root() / unit_file); });
}

std::vector<std::string> reasons_of(const std::vector<SetUpProblem>& problems) {
    std::vector<std::string> out;
    for (const SetUpProblem& p : problems) out.push_back(p.reason);
    return out;
}

// The details, checked against set_up_problems: the same reasons in the same order.
std::vector<SetUpProblem> details(const GameState& gs, Side side, const action::DeployUnit& a) {
    std::vector<SetUpProblem> d = set_up_problem_details(gs, side, a);
    EXPECT_EQ(reasons_of(d), set_up_problems(gs, side, a));
    return d;
}

action::DeployUnit moved(action::DeployUnit a, float dx_mm, float dy_mm) {
    for (ModelPlacement& m : a.models) {
        m.position.x += dx_mm;
        m.position.y += dy_mm;
    }
    return a;
}

}  // namespace

TEST(D082_SetUpProblems, TheReasonsOfSetUpProblemsWithTheModelsAtFault) {
    GameState gs = stock_battle(Side::B);  // A sets up first
    start_game(gs);
    const action::DeployUnit p = propose_set_up(gs, UnitId{1}).value();  // 10 Legionaries
    EXPECT_TRUE(details(gs, Side::A, p).empty());
    // About the unit as a whole: no models.
    for (const SetUpProblem& d : details(gs, Side::B, p)) EXPECT_TRUE(d.models.empty()) << d.reason;
    EXPECT_EQ(details(gs, Side::A, action::DeployUnit{UnitId{99}, {}}).front().models, std::vector<ModelId>{});
    // Placements: a model left out is named; one not of the unit is not.
    action::DeployUnit missing = p;
    const ModelId left_out = missing.models.back().model;
    missing.models.pop_back();
    missing.models.push_back(ModelPlacement{ModelId{999}, missing.models.front().position, 0.0f});
    const std::vector<SetUpProblem> m = details(gs, Side::A, missing);
    ASSERT_EQ(m.size(), 2u);
    EXPECT_TRUE(m[0].models.empty());  // "model 999 is not a model of unit 1"
    EXPECT_EQ(m[1].models, std::vector<ModelId>{left_out});
    // 03.02 / D-009: every model off the battlefield or out of the zone is named in its own reason.
    const std::vector<SetUpProblem> out = details(gs, Side::A, moved(p, 0.0f, in(14.0f)));
    ASSERT_EQ(out.size(), p.models.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        EXPECT_TRUE(starts_with(out[i].reason, "D-009: ")) << out[i].reason;
        EXPECT_EQ(out[i].models, std::vector<ModelId>{p.models[i].model});
    }
    // D-064: two models of the unit on each other: both.
    action::DeployUnit stacked = p;
    stacked.models[1].position = stacked.models[0].position;
    const std::vector<SetUpProblem> on = details(gs, Side::A, stacked);
    const auto both = std::find_if(on.begin(), on.end(), [](const SetUpProblem& d) { return starts_with(d.reason, "03.01: "); });
    ASSERT_NE(both, on.end());
    EXPECT_EQ(both->models, (std::vector<ModelId>{p.models[0].model, p.models[1].model}));
    // 03.03: the model too far from the others.
    action::DeployUnit apart = p;
    apart.models.back().position.y -= in(4.0f);
    const std::vector<SetUpProblem> c = details(gs, Side::A, apart);
    ASSERT_EQ(c.size(), 1u);
    EXPECT_TRUE(starts_with(c.front().reason, "03.03: ")) << c.front().reason;
    EXPECT_EQ(c.front().models, std::vector<ModelId>{p.models.back().model});
}

TEST(R03_04_Engagement, EveryPlacedModelWithinEngagementRangeIsAtFault) {
    GameState gs = zones_one_inch_apart();
    start_game(gs);
    apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());  // the Termagants on their zone's front
    ASSERT_EQ(decider(gs), Side::A);
    // The Intercessors on their own zone's front: 1" from the Termagants, every one of them engaged.
    action::DeployUnit front = propose_set_up(gs, UnitId{1}).value();
    for (ModelPlacement& m : front.models) m.position.y = in(12.0f) - 16.0f;
    const std::vector<SetUpProblem> d = details(gs, Side::A, front);
    ASSERT_EQ(d.size(), 1u);
    EXPECT_TRUE(starts_with(d.front().reason, "03.02: unit 1 would be set up engaged with unit 2 (03.04: model "));
    std::vector<ModelId> expected;
    for (const ModelPlacement& m : front.models) {
        const Unit& gaunts = *gs.find_unit(UnitId{2});
        const bool engaged = std::any_of(gaunts.models.begin(), gaunts.models.end(), [&](const Model& t) {
            return within(base_distance(BasePose{m.position, find_undeployed(gs, UnitId{1})->models.front().base, 0.0f},
                                        pose_of(t)),
                          kEngagementRangeMm);
        });
        if (engaged) expected.push_back(m.model);
    }
    ASSERT_EQ(expected.size(), front.models.size());
    std::vector<ModelId> named = d.front().models;
    std::sort(named.begin(), named.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(named, expected);
    // 1" back (the proposal) none is; 3/4" back one pair is, and only those models are named.
    const action::DeployUnit p = propose_set_up(gs, UnitId{1}).value();
    EXPECT_TRUE(details(gs, Side::A, p).empty());
    const std::vector<SetUpProblem> closer = details(gs, Side::A, moved(p, 0.0f, in(0.25f)));
    ASSERT_EQ(closer.size(), 1u);
    EXPECT_FALSE(closer.front().models.empty());
    EXPECT_LE(closer.front().models.size(), front.models.size());
}
