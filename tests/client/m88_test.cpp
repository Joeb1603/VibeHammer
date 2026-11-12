// M88 (D-087, D-088, D-089): the client's view of terrain: the board's terrain marks and hover text, the Game panel's
// lines, why a target can't be selected (13.09 Hidden, 13.10 / 13.11), the cover line, the move reach and drop fixing
// round terrain features, custom battles with terrain and the default scenario.

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "army_builder.hpp"
#include "core/data/load_error.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/rules/terrain.hpp"
#include "drop_fix.hpp"
#include "reach_area.hpp"
#include "rules_help.hpp"
#include "settings.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "terrain_view.hpp"
#include "test_helpers.hpp"

using namespace wh40k::client;
using wh40k::core::GameState;
using wh40k::core::Position;
using wh40k::core::Side;
using wh40k::test::at;
using wh40k::test::in;
using wh40k::test::repo_root;
namespace core = wh40k::core;
namespace mf = wh40k::test::monsters;

namespace {

GameState terrain_scenario() { return core::load_scenario(repo_root() / kTerrainScenario, repo_root()); }

core::TerrainShape rect_in(float x0, float x1, float y0, float y1) {
    return core::polygon_shape({at(x0, y0), at(x1, y0), at(x1, y1), at(x0, y1)});
}

core::TerrainArea ruin(core::TerrainShape area, core::TerrainShape wall, core::TerrainCategory c = core::TerrainCategory::Dense) {
    core::TerrainFeature f;
    f.name = "Wall";
    f.category = c;
    if (c == core::TerrainCategory::Dense) f.height_mm = in(5);
    f.shape = std::move(wall);
    core::TerrainArea a;
    a.name = "Ruin";
    a.shape = std::move(area);
    a.features.push_back(std::move(f));
    return a;
}

bool contains(const std::vector<std::string>& lines, const std::string& part) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
}

}  // namespace

TEST(M88_Board, TheAreasAndThenTheFeaturesAreMarked) {
    const GameState gs = terrain_scenario();
    const std::vector<TerrainMark> marks = terrain_marks(gs);
    std::size_t features = 0;
    for (const core::TerrainArea& a : gs.terrain) features += a.features.size();
    ASSERT_EQ(marks.size(), gs.terrain.size() + features);
    EXPECT_TRUE(marks[0].is_area);
    EXPECT_EQ(marks[0].label, "Centre");
    EXPECT_TRUE(marks[0].obscuring);
    EXPECT_FALSE(marks[1].obscuring);  // A home: an exposed crater
    const TerrainMark& wall = marks[gs.terrain.size()];
    EXPECT_FALSE(wall.is_area);
    EXPECT_EQ(wall.category, core::TerrainCategory::Dense);
    EXPECT_TRUE(terrain_marks(wh40k::test::load_v1_scenario()).empty());
}

TEST(M88_Board, HoveringAnAreaExplainsItsRules) {
    const GameState gs = terrain_scenario();
    const std::vector<std::string> centre = terrain_tooltip(gs, at(22, 30));
    ASSERT_FALSE(centre.empty());
    EXPECT_NE(centre[0].find("Centre (objective)"), std::string::npos) << centre[0];
    EXPECT_TRUE(contains(centre, "obscuring (13.10)"));
    EXPECT_TRUE(contains(centre, "hidden"));
    EXPECT_TRUE(contains(centre, "5.00\" tall"));
    const std::vector<std::string> barricades = terrain_tooltip(gs, at(34, 20));
    EXPECT_TRUE(contains(barricades, "light, no model ends a move on it"));
    EXPECT_FALSE(contains(barricades, "hidden"));
    EXPECT_TRUE(terrain_tooltip(gs, at(22, 45)).empty());  // open ground
    const std::vector<std::string> summary = terrain_summary(gs);
    ASSERT_EQ(summary.size(), gs.terrain.size());
    EXPECT_EQ(summary[0], "Centre (objective): 4 dense features; obscuring; hides");
    EXPECT_EQ(summary[1], "A home (objective): 1 exposed feature");
}

TEST(M88_Shoot, AHiddenOrObscuredTargetSaysWhyItCantBeSelected) {
    // The V1 armies at Side A's Shoot step: the Termagants (INFANTRY) in a ruin with a dense back wall, 15.6" away.
    GameState gs = wh40k::test::shoot_step_state();
    for (std::size_t i = 0; i < gs.units[0].models.size(); ++i) gs.units[0].models[i].position = at(10.0f + 1.5f * i, 0.8f);
    for (std::size_t i = 0; i < gs.units[1].models.size(); ++i) gs.units[1].models[i].position = at(4.0f + 1.3f * i, 17.0f);
    gs.terrain = {ruin(rect_in(2, 22, 15.5f, 18.5f), rect_in(2.5f, 21.5f, 18, 18.4f))};
    const std::optional<std::string> hidden =
        target_refusal_text(gs, gs.units[0].id, core::ShootingType::Normal, gs.units[1].id);
    ASSERT_TRUE(hidden.has_value());
    EXPECT_EQ(hidden->rfind("13.09:", 0), 0u) << *hidden;
    // A barricade area between, nobody hidden: the area obscures them.
    GameState obscured = wh40k::test::shoot_step_state();
    obscured.terrain = {ruin(rect_in(0.5f, 23.5f, 9, 11), rect_in(1, 23, 9.9f, 10.1f), core::TerrainCategory::Light)};
    const std::optional<std::string> blocked =
        target_refusal_text(obscured, obscured.units[0].id, core::ShootingType::Normal, obscured.units[1].id);
    ASSERT_TRUE(blocked.has_value());
    EXPECT_EQ(blocked->rfind("13.10 / 13.11:", 0), 0u) << *blocked;
    // Without terrain the Termagants can be targeted: nothing to explain.
    const GameState open = wh40k::test::shoot_step_state();
    EXPECT_FALSE(target_refusal_text(open, open.units[0].id, core::ShootingType::Normal, open.units[1].id).has_value());
}

TEST(M88_Shoot, TheCoverLineSaysWhatBsTheAttacksNeed) {
    std::vector<core::WeaponSelection> sels(2);
    EXPECT_TRUE(cover_lines(sels, 3).empty());
    sels[0].benefit_of_cover = true;
    sels[1].benefit_of_cover = true;
    const std::vector<EffectLine> all = cover_lines(sels, 3);
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].text, "Benefit of cover: BS 3+ worsened to 4+ (13.08)");
    EXPECT_EQ(all[0].kind, EffectKind::Active);
    sels[1].benefit_of_cover = false;
    EXPECT_EQ(cover_lines(sels, 4)[0].text, "Benefit of cover: BS 4+ worsened to 5+ (13.08) (1 of 2 models)");
    EXPECT_TRUE(cover_lines(sels, std::nullopt).empty());  // [TORRENT]
    EXPECT_EQ(benefit_of_cover_help().rule, "13.08");
}

namespace {

// The monsters fixture at Side B's Move Units decision, a 5" wall across the Tyrant's way north (y 32.5-33), the
// Tyrant's (or `unit`'s) Normal move started.
GameState moving_by_wall(std::size_t unit = mf::kTyrant) {
    GameState gs = mf::at_movement();
    gs.terrain = {ruin(rect_in(18, 26, 32, 34), rect_in(20, 24, 32.5f, 33))};
    core::apply_action(gs, Side::B, core::action::StartMove{gs.units[unit].id, core::MoveType::Normal, std::nullopt});
    return gs;
}

}  // namespace

TEST(M88_Reach, NoDropOnAWallAndAMonsterCantDropBeyondIt) {
    const GameState gs = moving_by_wall();
    const core::Model& tyrant = gs.units[mf::kTyrant].models[0];
    const ReachRegion region = model_region(*core::move_reach(gs, tyrant.id));
    EXPECT_FALSE(in_region(region, at(22, 32.75f)));  // on the wall
    EXPECT_FALSE(in_region(region, at(22, 35.5f)));   // beyond it in a straight line
    EXPECT_TRUE(in_region(region, at(22, 31.0f)));    // short of it
    EXPECT_TRUE(in_region(region, at(28, 31.0f)));    // beside it, the line clear of its corner
    EXPECT_FALSE(in_region(region, at(27, 34.0f)));   // the line clips its east corner
    EXPECT_TRUE(region.has_shapes());
    // The spans leave the wall's row out where it stands.
    bool row_seen = false;
    for (const ReachSpan& s : reach_spans(region, in(0.25f))) {
        if (std::fabs(s.y - in(32.75f)) > in(0.13f)) continue;
        row_seen = true;
        EXPECT_FALSE(s.x0 < in(22) && s.x1 > in(22)) << "a span crosses the wall at x " << s.x0 << ".." << s.x1;
    }
    EXPECT_TRUE(row_seen);
    // The drop problems.
    EXPECT_EQ(drop_problem(region, at(22, 35.5f)), DropProblem::ThroughTerrain);
    const std::optional<FixedDrop> fixed = fix_drop(region, tyrant.position, at(22, 35.5f));
    ASSERT_TRUE(fixed.has_value());
    EXPECT_TRUE(in_region(region, fixed->to));
    EXPECT_EQ(fix_message(*fixed).find("terrain feature it can't move through (13.06)") != std::string::npos, true);
}

TEST(M88_Reach, InfantryCrossesTheWallButCantDropOnIt) {
    GameState gs = mf::at_movement();
    // The Tyrant Guard at (6, 40): a 5" wall east of it.
    gs.terrain = {ruin(rect_in(8, 10, 37, 43), rect_in(9, 9.5f, 37.5f, 42.5f))};
    core::apply_action(gs, Side::B, core::action::StartMove{gs.units[mf::kGuard].id, core::MoveType::Normal, std::nullopt});
    const core::Model& guard = gs.units[mf::kGuard].models.back();
    const ReachRegion region = model_region(*core::move_reach(gs, guard.id));
    EXPECT_TRUE(region.outside_terrain_shadows.empty());
    EXPECT_TRUE(in_region(region, Position{guard.position.x + in(3), guard.position.y, 0}));
    const Position on{in(9.25f), guard.position.y, 0};
    EXPECT_FALSE(in_region(region, on));
    EXPECT_EQ(drop_problem(region, on), DropProblem::OnTerrain);
    const std::optional<FixedDrop> fixed = fix_drop(region, guard.position, on);
    ASSERT_TRUE(fixed.has_value());
    EXPECT_TRUE(in_region(region, fixed->to));
    EXPECT_LT(std::fabs(fixed->to.x - on.x), in(1.5f));  // the nearest side of the wall
}

TEST(M88_Builder, ACustomBattleTakesTheTerrainScenariosTerrain) {
    ArmyBuilder b(repo_root());
    EXPECT_EQ(b.terrain(), nullptr);
    EXPECT_TRUE(b.battle_state().terrain.empty());  // without it, as before
    b.load_mission(repo_root() / kObjectiveScenario);
    b.load_terrain(repo_root() / kTerrainScenario);
    ASSERT_NE(b.terrain(), nullptr);
    const GameState gs = b.battle_state();
    EXPECT_EQ(gs.terrain.size(), 9u);
    EXPECT_EQ(b.battle_scenario().at("terrain"), core::load_terrain_block(repo_root() / kTerrainScenario));
    EXPECT_EQ(custom_battle_state(b.setup(), b.factions(), b.resolver(), b.mission(), b.terrain()), gs);
    b.set_terrain(std::nullopt);
    EXPECT_TRUE(b.battle_state().terrain.empty());
    EXPECT_THROW(b.load_terrain(repo_root() / "data/scenarios/rosters_44x60.json"), core::DataError);
}

TEST(M88_Settings, TheTerrainScenarioIsTheDefaultAndInTheList) {
    EXPECT_EQ(AppSettings{}.scenario, kTerrainScenario);
    EXPECT_EQ(settings_from_json(nlohmann::json{{"scenario", ""}}).scenario, kTerrainScenario);
    const std::vector<std::string> scenarios = list_scenarios(repo_root());
    EXPECT_NE(std::find(scenarios.begin(), scenarios.end(), kTerrainScenario), scenarios.end());
    const ScenarioInfo info = scenario_info(repo_root() / kTerrainScenario);
    EXPECT_EQ(info.name, "Incursion terrain - Chaos Space Marines vs Tyranids (44x60 portrait)");
    EXPECT_EQ(info.sides[0], "Chaos Space Marines");
    EXPECT_EQ(info.sides[1], "Tyranids");
}
