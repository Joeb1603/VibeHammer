// M88: terrain in 2D (13, D-087, D-088, D-089). The data (13.01), movement and set-up (13.06, 03.01), visibility
// (13.09 Hidden, 13.10 Obscuring, 13.11 Solid, 06.01 Fully Visible) and the benefit of cover (13.08). Coordinates are
// in inches; every attack uses ScriptedDice.

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <cmath>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/data/army_list.hpp"
#include "core/data/datasheet.hpp"
#include "core/data/load_error.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/oval.hpp"
#include "core/geometry/path.hpp"
#include "core/geometry/terrain_shape.hpp"
#include "core/geometry/visibility.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/shooting.hpp"
#include "core/rules/terrain.hpp"
#include "core/serialization/json.hpp"
#include "core/state/deployment.hpp"
#include "core/state/queries.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::at;
using wh40k::test::in;
using wh40k::test::repo_root;
using wh40k::test::starts_with;
using Json = nlohmann::json;

namespace {

constexpr const char* kTerrainScenario = "data/scenarios/incursion_terrain_44x60.json";

TerrainShape rect_in(float x0, float x1, float y0, float y1) {
    return polygon_shape({at(x0, y0), at(x1, y0), at(x1, y1), at(x0, y1)});
}

TerrainFeature feature(const std::string& name, TerrainCategory c, TerrainShape shape,
                       std::optional<float> height_in = std::nullopt) {
    TerrainFeature f;
    f.name = name;
    f.category = c;
    f.shape = std::move(shape);
    if (height_in) f.height_mm = in(*height_in);
    return f;
}

TerrainFeature dense_wall(const std::string& name, TerrainShape shape, float height_in = 5.0f) {
    return feature(name, TerrainCategory::Dense, std::move(shape), height_in);
}

TerrainArea area(const std::string& name, TerrainShape shape, std::vector<TerrainFeature> features) {
    TerrainArea a;
    a.name = name;
    a.shape = std::move(shape);
    a.features = std::move(features);
    return a;
}

PosedModel posed(std::uint32_t id, Position p, const Base& base = wh40k::test::kIntercessorBase, float facing = 0.0f) {
    return PosedModel{ModelId{id}, p, base, facing};
}

std::vector<const TerrainShape*> ptrs(const std::vector<TerrainShape>& shapes) {
    std::vector<const TerrainShape*> out;
    for (const TerrainShape& s : shapes) out.push_back(&s);
    return out;
}

// Puts unit `u`'s models in a row along x from (x_in, y_in), `step_in` apart.
void row(Unit& u, float x_in, float y_in, float step_in) {
    for (std::size_t i = 0; i < u.models.size(); ++i) u.models[i].position = at(x_in + step_in * static_cast<float>(i), y_in);
}

GameState load_terrain_scenario() { return load_scenario(repo_root() / kTerrainScenario, repo_root()); }

Json terrain_scenario_json() {
    std::ifstream f(repo_root() / kTerrainScenario);
    return Json::parse(f);
}

GameState build(const Json& scenario) {
    return build_game_state(scenario, [](const std::string& unit_file) { return load_datasheet(repo_root() / unit_file); });
}

// The V1 scenario at Side A's Shoot step (the Intercessors, unit 0, south; the Termagants, unit 1, north), with
// `terrain` and both units placed in rows.
GameState v1_shooting(std::vector<TerrainArea> terrain, float marines_y, float gants_y, float gants_x = 4.0f) {
    GameState gs = wh40k::test::shoot_step_state();
    row(gs.units[0], 10.0f, marines_y, 1.5f);
    row(gs.units[1], gants_x, gants_y, 1.3f);
    gs.terrain = std::move(terrain);
    return gs;
}

}  // namespace

// ---- Geometry ----

TEST(D087_Geometry, ARoundBaseIsMeasuredExactlyToAPolygon) {
    const TerrainShape wall = rect_in(10, 12, 10, 11);
    const Base b{BaseShape::Round, in(1), 0.0f};  // radius 0.5"
    // 1" south of the wall's south edge, edge to edge 0.5".
    EXPECT_NEAR(shape_signed_distance(BasePose{at(11, 9), b, 0.0f}, wall), in(0.5f), 1e-3f);
    // Off the corner (12, 11): the distance to the corner (a 3-4-5 triangle) less the radius.
    EXPECT_NEAR(shape_signed_distance(BasePose{at(15, 15), b, 0.0f}, wall), in(5.0f - 0.5f), 1e-3f);
    // Centre inside: minus (distance to the nearest edge + radius).
    EXPECT_NEAR(shape_signed_distance(BasePose{at(11, 10.25f), b, 0.0f}, wall), -in(0.25f + 0.5f), 1e-3f);
    EXPECT_TRUE(within_shape(BasePose{at(11, 9.5f), b, 0.0f}, wall));    // touching
    EXPECT_FALSE(overlaps_shape(BasePose{at(11, 9.5f), b, 0.0f}, wall));  // base contact is not "on"
    EXPECT_TRUE(overlaps_shape(BasePose{at(11, 9.6f), b, 0.0f}, wall));
    EXPECT_FALSE(within_shape(BasePose{at(11, 9.4f), b, 0.0f}, wall));
}

TEST(D087_Geometry, AnOvalBaseIsMeasuredToAPolygonByGjk) {
    const TerrainShape wall = rect_in(20, 21, 0, 40);
    const Base oval{BaseShape::Oval, 92.0f, 120.0f};
    // Facing 0: the length along y, half the width (46 mm) towards the wall.
    const Position c{in(20) - 46.0f - 10.0f, in(20), 0.0f};
    EXPECT_NEAR(shape_signed_distance(BasePose{c, oval, 0.0f}, wall), 10.0f, 1e-3f);
    // Facing 90: the length along x, 60 mm towards the wall: overlapping by 4 mm.
    EXPECT_NEAR(shape_signed_distance(BasePose{c, oval, 90.0f}, wall), -4.0f, 1e-3f);
    EXPECT_TRUE(overlaps_shape(BasePose{c, oval, 90.0f}, wall));
    EXPECT_FALSE(overlaps_shape(BasePose{c, oval, 0.0f}, wall));
}

TEST(D087_Geometry, ACircleShapeIsMeasuredAsTheObjectivesRoundBase) {
    const TerrainShape crater = circle_shape(at(10, 10), in(4));
    const Base b{BaseShape::Round, in(1), 0.0f};
    EXPECT_NEAR(shape_signed_distance(BasePose{at(13, 10), b, 0.0f}, crater), in(0.5f), 1e-3f);
    EXPECT_TRUE(within_shape(BasePose{at(12.5f, 10), b, 0.0f}, crater));
}

TEST(D087_Geometry, ConvexCounterClockwisePolygonsOnly) {
    EXPECT_TRUE(is_convex_ccw(rect_in(0, 1, 0, 1).vertices));
    std::vector<Position> cw = rect_in(0, 1, 0, 1).vertices;
    std::reverse(cw.begin(), cw.end());
    EXPECT_FALSE(is_convex_ccw(cw));
    EXPECT_FALSE(is_convex_ccw(std::vector<Position>{at(0, 0), at(2, 0), at(1, 0.5f), at(2, 2), at(0, 2)}));  // dented
    EXPECT_FALSE(is_convex_ccw(std::vector<Position>{at(0, 0), at(1, 0), at(2, 0), at(2, 1)}));  // three in a line
    EXPECT_FALSE(is_convex_ccw(std::vector<Position>{at(0, 0), at(1, 0)}));
}

TEST(D087_Geometry, AFeatureIsWhollyWithinItsAreaExactly) {
    const TerrainShape area_shape = rect_in(0, 10, 0, 10);
    EXPECT_TRUE(shape_wholly_within(rect_in(0, 1, 0, 10), area_shape));  // on the edge counts
    EXPECT_FALSE(shape_wholly_within(rect_in(9, 10.01f, 0, 1), area_shape));
    EXPECT_TRUE(shape_wholly_within(circle_shape(at(5, 5), in(10)), area_shape));
    EXPECT_FALSE(shape_wholly_within(circle_shape(at(5, 5), in(10.1f)), area_shape));
    EXPECT_TRUE(shape_wholly_within(rect_in(4, 6, 4, 6), circle_shape(at(5, 5), in(3))));
    EXPECT_FALSE(shape_wholly_within(rect_in(3, 7, 3, 7), circle_shape(at(5, 5), in(3))));
}

TEST(R13_06_TerrainAndMovement, AStraightLineAndAnOvalTurnAgainstAWall) {
    const TerrainShape wall = rect_in(10, 10.5f, 0, 20);
    const Base b{BaseShape::Round, in(1), 0.0f};
    EXPECT_TRUE(translation_crosses_shape(BasePose{at(8, 5), b, 0.0f}, at(13, 5), wall));
    EXPECT_FALSE(translation_crosses_shape(BasePose{at(8, 5), b, 0.0f}, at(8, 15), wall));  // along it
    EXPECT_FALSE(translation_crosses_shape(BasePose{at(8, 21), b, 0.0f}, at(13, 21), wall));  // past its end
    // A 92 x 120 mm oval facing 0 beside the wall, 10 mm clear: turning to 90 swings its length into it.
    const Base oval{BaseShape::Oval, 92.0f, 120.0f};
    const BasePose beside{Position{in(10) - 56.0f, in(10), 0.0f}, oval, 0.0f};
    EXPECT_TRUE(rotation_against_shape(beside, 90.0f, wall).blocked);
    EXPECT_FALSE(rotation_against_shape(BasePose{Position{in(10) - 75.0f, in(10), 0.0f}, oval, 0.0f}, 90.0f, wall).blocked);
}

// ---- 06.01 / 13.10 / 13.11 line of sight with terrain shapes ----

TEST(R13_11_Solid, AWallBetweenTwoModelsBlocksEveryLineAndALinePastItsEndIsClear) {
    const std::vector<TerrainShape> walls{rect_in(9.75f, 10.25f, 5, 15)};
    const PosedModel a = posed(1, at(5, 10));
    const PosedModel b = posed(2, at(15, 10));
    EXPECT_FALSE(has_line_of_sight(a, b, {}, ptrs(walls)));
    // Past the wall's north end.
    EXPECT_TRUE(has_line_of_sight(posed(1, at(5, 16)), posed(2, at(15, 16)), {}, ptrs(walls)));
    // Only the corner of the wall hides part of the line: a line round its end still exists.
    EXPECT_TRUE(has_line_of_sight(posed(1, at(5, 14.5f)), posed(2, at(15, 14.5f)), {}, ptrs(walls)));
}

TEST(R13_10_Obscuring, AnAreaCrossingEveryLineBlocksIt) {
    const std::vector<TerrainShape> areas{circle_shape(at(10, 10), in(4))};
    EXPECT_FALSE(has_line_of_sight(posed(1, at(4, 10)), posed(2, at(16, 10)), {}, ptrs(areas)));
    EXPECT_TRUE(has_line_of_sight(posed(1, at(4, 13)), posed(2, at(16, 13)), {}, ptrs(areas)));
}

TEST(D088_LineOfSight, WithNoTerrainNearTheAnswerIsTheOldOne) {
    std::mt19937 rng(88);
    std::uniform_real_distribution<float> coord(0.0f, 24.0f);
    const std::vector<TerrainShape> far{rect_in(40, 41, 40, 41)};
    for (int i = 0; i < 200; ++i) {
        const PosedModel a = posed(1, at(coord(rng), coord(rng)));
        const PosedModel b = posed(2, at(coord(rng), coord(rng)));
        std::vector<PosedModel> blockers;
        for (std::uint32_t k = 0; k < 4; ++k) blockers.push_back(posed(10 + k, at(coord(rng), coord(rng))));
        EXPECT_EQ(has_line_of_sight(a, b, blockers, ptrs(far)), has_line_of_sight(a, b, blockers));
    }
}

TEST(D088_LineOfSight, ModelsStillBlockAlongsideTerrain) {
    const std::vector<TerrainShape> wall{rect_in(9.75f, 10.25f, 12, 20)};  // north of the line
    const PosedModel a = posed(1, at(5, 10));
    const PosedModel b = posed(2, at(15, 10));
    EXPECT_TRUE(has_line_of_sight(a, b, {}, ptrs(wall)));
    // A row of bases south of the wall closes the gap.
    std::vector<PosedModel> blockers;
    for (std::uint32_t k = 0; k < 4; ++k) blockers.push_back(posed(10 + k, at(10, 8.8f + 1.26f * static_cast<float>(k))));
    EXPECT_FALSE(has_line_of_sight(a, b, blockers, ptrs(wall)));
}

TEST(R06_01_FullyVisible, AWallCornerHidingPartOfTheFacingHalf) {
    const PosedModel observer = posed(1, at(5, 10));
    const PosedModel target = posed(2, at(15, 10));
    EXPECT_TRUE(fully_visible_past(observer, target, {}));
    // A wall whose end covers the target's south side, seen from the observer.
    const std::vector<TerrainShape> wall{rect_in(12, 12.5f, 0, 9.9f)};
    EXPECT_TRUE(has_line_of_sight(observer, target, {}, ptrs(wall)));
    EXPECT_FALSE(fully_visible_past(observer, target, ptrs(wall)));
    // Behind the target the same wall hides nothing that faces the observer.
    const std::vector<TerrainShape> behind{rect_in(17, 17.5f, 0, 20)};
    EXPECT_TRUE(fully_visible_past(observer, target, ptrs(behind)));
}

// ---- 13.01 the data ----

TEST(R13_01_PlacingTerrain, TheTerrainScenarioLoadsItsAreasAndLinksTheObjectives) {
    const GameState gs = load_terrain_scenario();
    ASSERT_EQ(gs.terrain.size(), 9u);
    const TerrainArea& centre = gs.terrain[0];
    EXPECT_EQ(centre.name, "Centre");
    ASSERT_TRUE(centre.objective.has_value());
    EXPECT_EQ(*centre.objective, "Centre");
    // 14.01: the area is the objective's circle.
    EXPECT_EQ(centre.shape, circle_shape(gs.mission->objectives[0].centre, gs.mission->objectives[0].diameter_mm));
    EXPECT_EQ(centre.features.size(), 4u);
    EXPECT_EQ(centre.features[0].category, TerrainCategory::Dense);
    EXPECT_NEAR(*centre.features[0].height_mm, in(5), 1e-3f);
    EXPECT_TRUE(is_obscuring(centre));
    EXPECT_TRUE(has_dense(centre));
    EXPECT_FALSE(is_obscuring(gs.terrain[1]));  // A home: an exposed crater only
    // Every polygon is stored counter-clockwise.
    for (const TerrainArea& a : gs.terrain) {
        for (const TerrainFeature& f : a.features) {
            if (!f.shape.is_circle()) EXPECT_TRUE(is_convex_ccw(f.shape.vertices)) << f.name;
        }
    }
}

TEST(R13_01_PlacingTerrain, AClockwisePolygonIsStoredCounterClockwise) {
    Json j = terrain_scenario_json();
    Json& poly = j["terrain"]["areas"][3]["polygon_in"];
    std::reverse(poly.begin(), poly.end());
    const GameState gs = build(j);
    EXPECT_TRUE(is_convex_ccw(gs.terrain[3].shape.vertices));
}

TEST(R13_01_PlacingTerrain, TheLoaderRefusesTerrainThatBreaksTheFormat) {
    const auto refused = [](const auto& edit) {
        Json j = terrain_scenario_json();
        edit(j);
        try {
            (void)build(j);
        } catch (const DataError& e) {
            return std::string(e.what());
        }
        return std::string();
    };
    Json& (*areas)(Json&) = [](Json& j) -> Json& { return j["terrain"]["areas"]; };
    EXPECT_NE(refused([&](Json& j) { areas(j)[3]["polygon_in"] = Json::parse("[[4,18],[14,18],[9,20],[14,24],[4,24]]"); }),
              "");  // not convex
    EXPECT_NE(refused([&](Json& j) { areas(j)[3]["polygon_in"] = Json::parse("[[4,18],[14,18]]"); }), "");
    EXPECT_NE(refused([&](Json& j) { areas(j)[7]["circle_in"]["diameter"] = 0; }), "");
    EXPECT_NE(refused([&](Json& j) { areas(j)[3]["features"][0]["polygon_in"] = Json::parse("[[3,23],[13.5,23],[13.5,23.5],[3,23.5]]"); }),
              "");  // the wall leaves its area
    EXPECT_NE(refused([&](Json& j) { areas(j)[3]["polygon_in"] = Json::parse("[[-1,18],[14,18],[14,24],[-1,24]]"); }),
              "");  // the area leaves the battlefield
    EXPECT_NE(refused([&](Json& j) { areas(j)[3]["features"][0].erase("height_in"); }), "");  // dense, no height
    EXPECT_NE(refused([&](Json& j) { areas(j)[5]["features"][0]["height_in"] = 1; }), "");     // light with a height
    EXPECT_NE(refused([&](Json& j) { areas(j)[0]["objective"] = "Nowhere"; }), "");
    EXPECT_NE(refused([&](Json& j) { areas(j)[1]["objective"] = "Centre"; }), "");  // linked twice
    EXPECT_NE(refused([&](Json& j) { areas(j)[3]["features"] = Json::array(); }), "");
    EXPECT_NE(refused([&](Json& j) { areas(j)[3]["name"] = ""; }), "");
    EXPECT_NE(refused([&](Json& j) { areas(j)[3]["features"][0]["category"] = "rubble"; }), "");
    EXPECT_NE(refused([&](Json& j) {
                  areas(j)[3]["circle_in"] = Json{{"x", 9}, {"y", 21}, {"diameter", 4}};  // both shapes
              }),
              "");
    // A dense wall across a deployment zone, on top of the Legionaries' formation.
    const std::string on_wall = refused([&](Json& j) {
        areas(j)[7]["circle_in"] = Json{{"x", 7}, {"y", 11}, {"diameter", 4}};
        areas(j)[7]["features"][0] = Json{{"name", "Wall"},
                                          {"category", "dense"},
                                          {"height_in", 5},
                                          {"polygon_in", Json::parse("[[6,10],[8,10],[8,12],[6,12]]")}};
    });
    EXPECT_NE(on_wall.find("set up on terrain feature 'Wall'"), std::string::npos) << on_wall;
    // An exposed crater under the formation is fine.
    EXPECT_EQ(refused([&](Json& j) {
                  areas(j)[7]["circle_in"] = Json{{"x", 7}, {"y", 11}, {"diameter", 4}};
                  areas(j)[7]["features"][0]["circle_in"] = Json{{"x", 7}, {"y", 11}, {"diameter", 3}};
              }),
              "");
}

TEST(D087_State, TheTerrainAndTheRangedAttacksRecordRoundTripAndOldStatesLoadUnchanged) {
    GameState gs = load_terrain_scenario();
    start_game(gs);
    gs.units[0].ranged_attacks = RangedAttackRecord{true, false};
    gs.units[1].ranged_attacks = RangedAttackRecord{false, true};
    const Json j = gs;
    EXPECT_TRUE(j.contains("terrain"));
    EXPECT_TRUE(j["units"][0].contains("ranged_attacks"));
    EXPECT_FALSE(j["units"][2].contains("ranged_attacks"));  // omitted when both are false
    const GameState back = j.get<GameState>();
    EXPECT_EQ(back, gs);
    EXPECT_EQ(Json(back).dump(), j.dump());
    // A state without terrain writes no "terrain" key and reads back the same.
    GameState v1 = wh40k::test::load_v1_scenario();
    start_game(v1);
    const Json old = v1;
    EXPECT_FALSE(old.contains("terrain"));
    EXPECT_EQ(Json(old.get<GameState>()).dump(), old.dump());
    // The state JSON is checked as the loader is.
    Json bad = j;
    bad["terrain"][3]["features"][0]["shape"]["polygon"][0]["x"] = 0.0;  // the wall leaves its area
    EXPECT_THROW((void)bad.get<GameState>(), DataError);
}

TEST(D089_State, BenefitOfCoverOnASelectionRoundTripsAndIsOmittedWhenFalse) {
    WeaponSelection s;
    EXPECT_FALSE(Json(s).contains("benefit_of_cover"));
    s.benefit_of_cover = true;
    const Json j = s;
    EXPECT_TRUE(j.at("benefit_of_cover").get<bool>());
    EXPECT_EQ(j.get<WeaponSelection>(), s);
}

TEST(D087_CustomBattle, TheTerrainBlockIsCopiedIntoACustomBattle) {
    const Json block = load_terrain_block(repo_root() / kTerrainScenario);
    EXPECT_EQ(block.at("areas").size(), 9u);
    EXPECT_THROW((void)load_terrain_block(repo_root() / "data/scenarios/rosters_44x60.json"), DataError);
}

// ---- 13.06 movement ----

namespace {

namespace mf = wh40k::test::monsters;

// The monsters fixture at Side B's Move Units decision with `terrain`.
GameState monsters_with(std::vector<TerrainArea> terrain) {
    GameState gs = mf::at_movement();
    gs.terrain = std::move(terrain);
    return gs;
}

std::vector<std::string> propose(GameState& gs, std::size_t unit, MoveType type, std::vector<Position> path) {
    const Unit& u = gs.units[unit];
    if (!gs.flow.move) apply_action(gs, u.side, action::StartMove{u.id, type, std::nullopt});
    return validate_action(gs, u.side, action::ProposeModelMove{u.models[0].id, std::move(path)}).reasons;
}

std::vector<TerrainArea> wall_north_of_tyrant(float height_in = 5.0f) {
    // The Tyrant stands at (22, 30): a wall across its path north, at y 32.5-33.
    return {area("Ruin", rect_in(18, 26, 32, 34), {dense_wall("Ruin wall", rect_in(20, 24, 32.5f, 33), height_in)})};
}

bool any_starts_with(const std::vector<std::string>& v, const std::string& prefix) {
    return std::any_of(v.begin(), v.end(), [&](const std::string& s) { return starts_with(s, prefix); });
}

}  // namespace

TEST(R13_06_TerrainAndMovement, AMonsterCannotMoveThroughADenseWallOverTwoInches) {
    GameState gs = monsters_with(wall_north_of_tyrant());
    const std::vector<std::string> r = propose(gs, mf::kTyrant, MoveType::Normal, {at(22, 36)});
    ASSERT_TRUE(any_starts_with(r, "13.06:")) << (r.empty() ? "" : r.front());
    // Round the wall's east end in two straight lines: legal.
    EXPECT_TRUE(validate_action(gs, Side::B,
                                action::ProposeModelMove{gs.units[mf::kTyrant].models[0].id, {at(25.5f, 31), at(25.5f, 35)}})
                    .reasons.empty());
}

TEST(R13_06_TerrainAndMovement, AMonsterCrossesADenseFeatureTwoInchesTall) {
    GameState gs = monsters_with(wall_north_of_tyrant(2.0f));
    EXPECT_TRUE(propose(gs, mf::kTyrant, MoveType::Normal, {at(22, 36)}).empty());
}

TEST(R13_06_TerrainAndMovement, LightAndExposedFeaturesBlockNoMove) {
    std::vector<TerrainArea> t{area("Barricades", rect_in(18, 26, 32, 34),
                                    {feature("Barricade", TerrainCategory::Light, rect_in(20, 24, 32.5f, 33)),
                                     feature("Crater", TerrainCategory::Exposed, circle_shape(at(22, 33.7f), in(0.5f)))})};
    GameState gs = monsters_with(std::move(t));
    EXPECT_TRUE(propose(gs, mf::kTyrant, MoveType::Normal, {at(22, 36)}).empty());
}

TEST(R13_06_TerrainAndMovement, InfantryMovesThroughADenseWallButCannotEndOnIt) {
    // The Tyrant Guard (INFANTRY) at (6, 40): a 5" wall east of it at x 9-9.5.
    std::vector<TerrainArea> t{area("Ruin", rect_in(8, 10, 37, 43), {dense_wall("Ruin wall", rect_in(9, 9.5f, 37.5f, 42.5f))})};
    GameState gs = monsters_with(std::move(t));
    // The east end of the rank (at x 7.97) moves 3" east, through the wall, ending clear of it.
    const ModelId guard = gs.units[mf::kGuard].models.back().id;
    apply_action(gs, Side::B, action::StartMove{gs.units[mf::kGuard].id, MoveType::Normal, std::nullopt});
    const Position start = gs.units[mf::kGuard].models.back().position;
    const auto through = validate_action(gs, Side::B, action::ProposeModelMove{guard, {Position{start.x + in(3), start.y, 0}}}).reasons;
    EXPECT_TRUE(through.empty()) << through.front();
    const std::vector<std::string> on =
        validate_action(gs, Side::B, action::ProposeModelMove{guard, {Position{in(9.25f), start.y, 0}}}).reasons;
    ASSERT_FALSE(on.empty());
    EXPECT_TRUE(any_starts_with(on, "03.01:")) << on.front();
    EXPECT_NE(on.front().find("partway through terrain feature 'Ruin wall'"), std::string::npos) << on.front();
}

TEST(R13_06_TerrainAndMovement, AModelMayEndOnAnExposedFeatureAndInBaseContactWithAWall) {
    std::vector<TerrainArea> t{area("Ruin", rect_in(18, 26, 32, 36),
                                    {dense_wall("Ruin wall", rect_in(20, 24, 34.5f, 35)),
                                     feature("Crater", TerrainCategory::Exposed, circle_shape(at(22, 33), in(2)))})};
    GameState gs = monsters_with(std::move(t));
    EXPECT_TRUE(propose(gs, mf::kTyrant, MoveType::Normal, {at(22, 33)}).empty());  // on the crater
    // Base contact with the wall's south face: the Tyrant's 60 mm base, its north edge at y 34.5.
    EXPECT_TRUE(validate_action(gs, Side::B,
                                action::ProposeModelMove{gs.units[mf::kTyrant].models[0].id,
                                                         {Position{in(22), in(34.5f) - 30.0f, 0}}})
                    .reasons.empty());
}

TEST(R13_06_TerrainAndMovement, MoveEndProblemsNameAModelOnAWall) {
    GameState gs = monsters_with(wall_north_of_tyrant());
    apply_action(gs, Side::B, action::StartMove{gs.units[mf::kTyrant].id, MoveType::Normal, std::nullopt});
    EXPECT_TRUE(move_end_problems(gs).empty());
    gs.units[mf::kTyrant].models[0].position = at(22, 32.75f);  // as if it had got there
    const std::vector<std::string> p = move_end_problems(gs);
    ASSERT_FALSE(p.empty());
    EXPECT_NE(p.front().find("partway through terrain feature 'Ruin wall'"), std::string::npos) << p.front();
}

TEST(R13_06_TerrainAndMovement, TheMoveReachHasTheWallsAndAMonstersShadow) {
    GameState gs = monsters_with(wall_north_of_tyrant());
    apply_action(gs, Side::B, action::StartMove{gs.units[mf::kTyrant].id, MoveType::Normal, std::nullopt});
    const std::optional<MoveReach> reach = move_reach(gs, gs.units[mf::kTyrant].models[0].id);
    ASSERT_TRUE(reach.has_value());
    ASSERT_EQ(reach->forbidden_terrain.size(), 1u);
    ASSERT_EQ(reach->blocking_terrain.size(), 1u);
    EXPECT_TRUE(reach_terrain_contains(reach->forbidden_terrain[0], at(22, 32.75f)));
    EXPECT_FALSE(reach_terrain_contains(reach->forbidden_terrain[0], at(22, 36)));
    const Position c = reach_terrain_centre(reach->forbidden_terrain[0]);
    EXPECT_TRUE(reach_terrain_contains(reach->forbidden_terrain[0], c));
    EXPECT_GT(reach_terrain_outer_radius_mm(reach->forbidden_terrain[0]), reach_terrain_inner_radius_mm(reach->forbidden_terrain[0]));
    // The Tyrant Guard (INFANTRY) moves through walls: no shadow.
    GameState g2 = monsters_with(wall_north_of_tyrant());
    apply_action(g2, Side::B, action::StartMove{g2.units[mf::kGuard].id, MoveType::Normal, std::nullopt});
    const std::optional<MoveReach> r2 = move_reach(g2, g2.units[mf::kGuard].models[0].id);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->forbidden_terrain.size(), 1u);
    EXPECT_TRUE(r2->blocking_terrain.empty());
    // Without terrain neither list is filled.
    GameState g3 = mf::at_movement();
    apply_action(g3, Side::B, action::StartMove{g3.units[mf::kTyrant].id, MoveType::Normal, std::nullopt});
    const std::optional<MoveReach> r3 = move_reach(g3, g3.units[mf::kTyrant].models[0].id);
    ASSERT_TRUE(r3.has_value());
    EXPECT_TRUE(r3->forbidden_terrain.empty());
    EXPECT_TRUE(r3->blocking_terrain.empty());
}

TEST(R13_06_TerrainAndMovement, ADesperateEscapeStillCannotCrossAWall) {
    // check_model_path's terrain checks don't depend on passing through enemy models.
    const std::vector<TerrainArea> t = wall_north_of_tyrant();
    const PathFeature f{&t[0].features[0].shape, "'Ruin wall'"};
    const PosedPathContext ctx{Rect{0, 0, in(44), in(60)}, {}, {}, true, {}, std::span<const PathFeature>(&f, 1), {}};
    const std::vector<Position> path{at(22, 30), at(22, 36)};
    const auto v = check_model_path(ModelId{1}, Base{BaseShape::Round, 60.0f, 0.0f}, path, {}, ctx);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].kind, PathViolationKind::CrossesTerrain);
}

TEST(R13_06_TerrainAndMovement, AnOvalCannotTurnIntoAWallItCannotCross) {
    const std::vector<TerrainArea> t{area("Ruin", rect_in(9, 12, 0, 20), {dense_wall("Wall", rect_in(10, 10.5f, 0, 20))})};
    const PathFeature f{&t[0].features[0].shape, "'Wall'"};
    const PosedPathContext ctx{Rect{0, 0, in(44), in(60)}, {}, {}, false, {}, std::span<const PathFeature>(&f, 1), {}};
    const Base oval{BaseShape::Oval, 92.0f, 120.0f};
    const Position p{in(10) - 56.0f, in(10), 0.0f};
    const std::vector<Position> path{p, p};
    const std::vector<float> facings{0.0f, 90.0f};
    const auto v = check_model_path(ModelId{1}, oval, path, facings, ctx);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].kind, PathViolationKind::TurnCrossesTerrain);
}

TEST(R13_06_TerrainAndMovement, AChargeMoveCannotCrossAWallEither) {
    GameState gs = mf::at_movement();
    gs.terrain = wall_north_of_tyrant();
    // The engine's path checks are shared by every move type; a charge move's validation reports 13.06 too.
    apply_action(gs, Side::B, action::StartMove{gs.units[mf::kTyrant].id, MoveType::Normal, std::nullopt});
    gs.flow.move->type = MoveType::Charge;
    gs.units[mf::kTyrant].phase_flags.charge_roll = 12;  // 11.02: the charge's maximum distance
    const auto r = validate_action(gs, Side::B, action::ProposeModelMove{gs.units[mf::kTyrant].models[0].id, {at(22, 36)}}).reasons;
    EXPECT_TRUE(any_starts_with(r, "13.06:")) << (r.empty() ? std::string("(valid)") : r.front());
}

// ---- Set-up ----

TEST(D087_SetUp, NoModelIsSetUpOnALightOrDenseFeatureAndTheProposalAvoidsThem) {
    GameState gs = load_scenario(repo_root() / "tests/data/scenarios/hand_deployment_44x60.json", repo_root());
    start_game(gs);
    ASSERT_TRUE(gs.pending.has_value());
    const Side side = gs.pending->side;
    const Unit& first = gs.undeployed.front();
    const std::optional<action::DeployUnit> before = propose_set_up(gs, first.id);
    ASSERT_TRUE(before.has_value());
    // A wall right where the proposal puts the first model.
    const Position spot = before->models.front().position;
    gs.terrain = {area("Ruin", rect_in(mm_to_inches(spot.x) - 1.5f, mm_to_inches(spot.x) + 1.5f, mm_to_inches(spot.y) - 1.5f,
                                       mm_to_inches(spot.y) + 1.5f),
                       {dense_wall("Wall", rect_in(mm_to_inches(spot.x) - 0.25f, mm_to_inches(spot.x) + 0.25f,
                                                   mm_to_inches(spot.y) - 1.0f, mm_to_inches(spot.y) + 1.0f))})};
    const std::vector<SetUpProblem> p = set_up_problem_details(gs, side, *before);
    ASSERT_FALSE(p.empty());
    EXPECT_TRUE(starts_with(p.front().reason, "D-087 / 03.01:")) << p.front().reason;
    EXPECT_EQ(p.front().models, std::vector<ModelId>{before->models.front().model});
    const std::optional<action::DeployUnit> after = propose_set_up(gs, first.id);
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(set_up_problems(gs, side, *after).empty());
    for (const auto& m : after->models) {
        EXPECT_FALSE(overlaps_shape(BasePose{m.position, find_model(first, m.model)->base, m.facing_deg},
                                    gs.terrain[0].features[0].shape));
    }
}

// ---- 13.09 Hidden ----

namespace {

// The Termagants (INFANTRY) in a ruin with a dense wall, at y 17; the Intercessors in a row at y `marines_y`.
std::vector<TerrainArea> gants_ruin() {
    return {area("Ruin", rect_in(2, 22, 15.5f, 18.5f), {dense_wall("Back wall", rect_in(2.5f, 21.5f, 18, 18.4f))})};
}

}  // namespace

TEST(R13_09_Hidden, AHiddenModelIsVisibleOnlyWithin15Inches) {
    // Gants' centres at y 17 (radius 14.25 mm); an Intercessor's (16 mm) edge 14.9" / 15.1" south of them.
    const float gap = mm_to_inches(14.25f + 16.0f);
    const auto marine_y = [&](float edge_in) { return 17.0f - gap - edge_in; };
    GameState near = v1_shooting(gants_ruin(), marine_y(14.9f), 17.0f);
    const Unit& gants = near.units[1];
    EXPECT_TRUE(is_hidden(near, gants, gants.models[0]));
    // The Intercessors stand east of x 10, so the nearest Termagant (x 9.2) is about 14.9" from Intercessor 0.
    for (Model& m : near.units[0].models) m.position.x = gants.models[6].position.x;
    EXPECT_TRUE(unit_visible_to_model(near, near.units[0].models[0], gants));
    GameState far = v1_shooting(gants_ruin(), marine_y(15.1f), 17.0f);
    for (Model& m : far.units[0].models) m.position.x = far.units[1].models[6].position.x;
    EXPECT_FALSE(unit_visible_to_model(far, far.units[0].models[0], far.units[1]));
    const VisibilityProblem why = visibility_problem(far, far.units[0].models[0], far.units[1]);
    EXPECT_EQ(why.kind, VisibilityProblemKind::Hidden);
    EXPECT_TRUE(starts_with(why.text, "13.09:")) << why.text;
    // The engine refuses the target with that reason.
    action::Shoot a{far.units[0].id, ShootingType::Normal, {ShotDeclaration{far.units[0].models[0].id, 0, 0, far.units[1].id}}};
    const auto r = validate_action(far, Side::A, a).reasons;
    ASSERT_FALSE(r.empty());
    EXPECT_TRUE(starts_with(r.front(), "13.09:")) << r.front();
}

TEST(R13_09_Hidden, OnlyInfantryBeastsOrSwarmInAnAreaWithADenseFeature) {
    GameState gs = v1_shooting(gants_ruin(), 2.0f, 17.0f);
    EXPECT_TRUE(is_hidden(gs, gs.units[1], gs.units[1].models[0]));
    // Not within the area.
    GameState out = v1_shooting(gants_ruin(), 2.0f, 21.0f);
    EXPECT_FALSE(is_hidden(out, out.units[1], out.units[1].models[0]));
    // An area with only a light feature hides nobody.
    GameState light = v1_shooting({area("Barricades", rect_in(2, 22, 15.5f, 18.5f),
                                        {feature("Barricade", TerrainCategory::Light, rect_in(2.5f, 21.5f, 18, 18.4f))})},
                                  2.0f, 17.0f);
    EXPECT_FALSE(is_hidden(light, light.units[1], light.units[1].models[0]));
    // Without the INFANTRY keyword.
    gs.units[1].keywords.clear();
    EXPECT_FALSE(is_hidden(gs, gs.units[1], gs.units[1].models[0]));
}

TEST(R13_09_Hidden, ShootingEndsItForThisTurnAndTheNextThenItReturns) {
    // The Intercessors shoot from inside a ruin; afterwards they aren't hidden until the end of Side B's turn.
    std::vector<TerrainArea> t{area("Ruin", rect_in(8, 18, 4, 7), {dense_wall("Wall", rect_in(8.5f, 17.5f, 4.2f, 4.6f))})};
    GameState gs = v1_shooting(std::move(t), 5.5f, 17.0f);
    const Unit& marines = gs.units[0];
    EXPECT_TRUE(is_hidden(gs, marines, marines.models[0]));
    action::Shoot a{marines.id, ShootingType::Normal, {}};
    for (const Model& m : marines.models) a.shots.push_back(ShotDeclaration{m.id, 0, 0, gs.units[1].id});
    ASSERT_TRUE(validate_action(gs, Side::A, a).reasons.empty()) << validate_action(gs, Side::A, a).reasons.front();
    ScriptedDice misses(std::vector<int>(64, 1));
    apply_action(gs, Side::A, a, misses);
    EXPECT_EQ(gs.units[0].ranged_attacks, (RangedAttackRecord{true, false}));
    EXPECT_FALSE(is_hidden(gs, gs.units[0], gs.units[0].models[0]));
    // Through Side A's End of Turn and Side B's turn: previous_turn, then clear.
    wh40k::test::monsters::play_until(gs, [](const GameState& g) { return turn_side(g) == Side::B; });
    EXPECT_EQ(gs.units[0].ranged_attacks, (RangedAttackRecord{false, true}));
    EXPECT_FALSE(is_hidden(gs, gs.units[0], gs.units[0].models[0]));
    wh40k::test::monsters::play_until(gs, [](const GameState& g) { return turn_side(g) == Side::A; });
    EXPECT_EQ(gs.units[0].ranged_attacks, RangedAttackRecord{});
    EXPECT_TRUE(is_hidden(gs, gs.units[0], gs.units[0].models[0]));
}

TEST(D088_Record, WithoutTerrainShootingRecordsNothing) {
    GameState gs = wh40k::test::shoot_step_state();
    action::Shoot a{gs.units[0].id, ShootingType::Normal, {}};
    for (const ShotDeclaration& s : shot_options(gs, gs.units[0], ShootingType::Normal)) {
        if (s.weapon_index == 0 && s.profile_index == 0) a.shots.push_back(s);
    }
    ASSERT_FALSE(a.shots.empty());
    ScriptedDice misses(std::vector<int>(64, 1));
    apply_action(gs, Side::A, a, misses);
    EXPECT_EQ(gs.units[0].ranged_attacks, RangedAttackRecord{});
}

// ---- 13.10 Obscuring and 13.11 Solid through the engine ----

TEST(R13_10_Obscuring, AnObscuringAreaBetweenRefusesTheTargetUnlessAModelIsWithinIt) {
    // A barricade area between the units at y 10-13 (a light feature: obscuring, not hiding anyone).
    const auto t = [] {
        return std::vector<TerrainArea>{area("Barricades", rect_in(0.5f, 23.5f, 10, 13),
                                             {feature("Barricade", TerrainCategory::Light, rect_in(1, 23, 11.4f, 11.6f))})};
    };
    GameState gs = v1_shooting(t(), 6.0f, 17.0f);
    const VisibilityProblem why = visibility_problem(gs, gs.units[0].models[0], gs.units[1]);
    EXPECT_EQ(why.kind, VisibilityProblemKind::Terrain);
    EXPECT_TRUE(starts_with(why.text, "13.10 / 13.11:")) << why.text;
    EXPECT_TRUE(shot_options(gs, gs.units[0], ShootingType::Normal).empty());
    // An Intercessor within the area sees out of it (13.10 excludes areas either model is within).
    GameState within = v1_shooting(t(), 6.0f, 17.0f);
    within.units[0].models[0].position = at(10, 10.2f);
    EXPECT_TRUE(unit_visible_to_model(within, within.units[0].models[0], within.units[1]));
    // An area with only an exposed feature doesn't obscure.
    GameState exposed = v1_shooting({area("Craters", rect_in(0.5f, 23.5f, 10, 13),
                                          {feature("Crater", TerrainCategory::Exposed, circle_shape(at(12, 11.5f), in(2)))})},
                                    6.0f, 17.0f);
    EXPECT_TRUE(unit_visible_to_model(exposed, exposed.units[0].models[0], exposed.units[1]));
}

TEST(R13_11_Solid, ADenseWallBlocksEvenInsideTheAreaBothModelsAreWithin) {
    // Both units within one big ruin; a wall between them still blocks (13.11), the area doesn't (13.10 excludes it).
    std::vector<TerrainArea> t{area("Big ruin", rect_in(0.5f, 23.5f, 4, 20), {dense_wall("Wall", rect_in(1, 23, 11.4f, 11.6f))})};
    GameState gs = v1_shooting(std::move(t), 6.0f, 17.0f);
    for (Unit& u : gs.units) u.ranged_attacks.this_turn = true;  // not hidden: only the wall counts here
    const VisibilityProblem why = visibility_problem(gs, gs.units[0].models[0], gs.units[1]);
    EXPECT_EQ(why.kind, VisibilityProblemKind::Terrain);
    // A shorter wall leaves lines round its ends.
    GameState open = gs;
    open.terrain[0].features[0].shape = rect_in(9, 15, 11.4f, 11.6f);
    EXPECT_TRUE(unit_visible_to_model(open, open.units[0].models[0], open.units[1]));
}

// ---- 13.08 Benefit of cover (D-089) ----

TEST(R13_08_BenefitOfCover, InfantryWithinAnyTerrainAreaHasIt) {
    std::vector<TerrainArea> t{area("Craters", rect_in(0.5f, 23.5f, 15.5f, 18.5f),
                                    {feature("Crater", TerrainCategory::Exposed, circle_shape(at(20, 17), in(1)))})};
    GameState gs = v1_shooting(std::move(t), 6.0f, 17.0f);
    EXPECT_TRUE(benefit_of_cover(gs, gs.units[0].models[0], gs.units[1]));
    // One Termagant outside the area: the unit has no cover (it is fully visible).
    gs.units[1].models[9].position = at(20, 20.5f);
    EXPECT_FALSE(benefit_of_cover(gs, gs.units[0].models[0], gs.units[1]));
    // Without the keywords, being within an area isn't enough.
    GameState plain = v1_shooting({area("Craters", rect_in(0.5f, 23.5f, 15.5f, 18.5f),
                                        {feature("Crater", TerrainCategory::Exposed, circle_shape(at(20, 17), in(1)))})},
                                  6.0f, 17.0f);
    plain.units[1].keywords.clear();
    EXPECT_FALSE(benefit_of_cover(plain, plain.units[0].models[0], plain.units[1]));
}

TEST(R13_08_BenefitOfCover, AUnitNotFullyVisibleDueToTerrainHasIt) {
    // A light barricade in an area the Termagants are not within, partly hiding each of them from Intercessor 0.
    std::vector<TerrainArea> t{area("Barricades", rect_in(0.5f, 23.5f, 14, 15.4f),
                                    {feature("Barricade", TerrainCategory::Light, rect_in(1, 23, 14.6f, 15.2f))})};
    GameState gs = v1_shooting(std::move(t), 6.0f, 16.0f);
    gs.units[1].keywords.clear();  // only the second condition applies
    for (Unit& u : gs.units) u.ranged_attacks.this_turn = true;
    // The barricade's area is obscuring and lies between: nothing is visible at all from the far side.
    EXPECT_FALSE(unit_visible_to_model(gs, gs.units[0].models[0], gs.units[1]));
    // From inside the barricade's area, the barricade (an intervening light feature) still stops the targets being
    // fully visible: cover, though they are visible.
    gs.units[0].models[0].position = at(12, 14.2f);
    EXPECT_TRUE(unit_visible_to_model(gs, gs.units[0].models[0], gs.units[1]));
    EXPECT_TRUE(benefit_of_cover(gs, gs.units[0].models[0], gs.units[1]));
}

TEST(R13_08_BenefitOfCover, TheBsIsWorsenedByOneAndAnUnmodified6StillHits) {
    std::vector<TerrainArea> t{area("Craters", rect_in(0.5f, 23.5f, 15.5f, 18.5f),
                                    {feature("Crater", TerrainCategory::Exposed, circle_shape(at(20, 17), in(1)))})};
    GameState gs = v1_shooting(std::move(t), 6.0f, 17.0f);
    action::Shoot a{gs.units[0].id, ShootingType::Normal, {ShotDeclaration{gs.units[0].models[0].id, 0, 0, gs.units[1].id}}};
    ASSERT_TRUE(validate_action(gs, Side::A, a).reasons.empty());
    const std::vector<WeaponSelection> sels = make_weapon_selections(gs, gs.units[0], ShootingType::Normal, a.shots);
    ASSERT_EQ(sels.size(), 1u);
    EXPECT_TRUE(sels[0].benefit_of_cover);
    const std::vector<AttackBatch> batches = gather_attack_batches(gs, sels, shooting_context(gs, gs.units[0]));
    ASSERT_EQ(batches.size(), 1u);
    ASSERT_TRUE(batches[0].profile.skill.has_value());
    const int bs = *selected_profile(gs, sels[0]).skill;
    EXPECT_EQ(*batches[0].profile.skill, bs + 1);  // 3+ becomes 4+
    // On the dice: a 3 that would have hit misses.
    AttackProfile no_heavy = batches[0].profile;  // the Bolt Rifle's [HEAVY] +1 aside
    no_heavy.hit_modifier = 0;
    ScriptedDice three{3, 3};
    EXPECT_EQ(make_hit_rolls(three, no_heavy, 1).rolls[0].outcome, HitOutcome::Fail);
    // A BS worsened to 7+ hits only on an unmodified 6.
    AttackProfile seven = batches[0].profile;
    seven.skill = 7;
    ScriptedDice five_six{5, 6};
    const HitRolls r = make_hit_rolls(five_six, seven, 2);
    EXPECT_EQ(r.rolls[0].outcome, HitOutcome::Fail);
    EXPECT_EQ(r.rolls[1].outcome, HitOutcome::CriticalHit);
}

TEST(R13_08_BenefitOfCover, AttacksWithAndWithoutCoverAreSeparateBatchesAndTorrentIsUnaffected) {
    std::vector<TerrainArea> t{area("Craters", rect_in(0.5f, 23.5f, 15.5f, 18.5f),
                                    {feature("Crater", TerrainCategory::Exposed, circle_shape(at(20, 17), in(1)))})};
    GameState gs = v1_shooting(std::move(t), 6.0f, 17.0f);
    std::vector<WeaponSelection> sels;
    for (std::size_t i = 0; i < 2; ++i) {
        WeaponSelection s = wh40k::test::select(gs, 0, i, 0, 0, gs.units[1].id);
        s.benefit_of_cover = i == 0;
        sels.push_back(s);
    }
    const std::vector<AttackBatch> batches = gather_attack_batches(gs, sels, shooting_context(gs, gs.units[0]));
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_NE(batches[0].profile.skill, batches[1].profile.skill);
    // [TORRENT]: no hit roll, no BS to worsen.
    GameState torrent = gs;
    torrent.units[0].models[0].weapons[0].profiles[0].abilities.push_back(ability::Torrent{});
    const std::vector<AttackBatch> tb = gather_attack_batches(torrent, std::vector<WeaponSelection>{sels[0]},
                                                              shooting_context(torrent, torrent.units[0]));
    ASSERT_EQ(tb.size(), 1u);
    EXPECT_TRUE(tb[0].profile.torrent);
    EXPECT_FALSE(tb[0].profile.skill.has_value());
}

TEST(R13_08_BenefitOfCover, WithoutTerrainNoSelectionHasIt) {
    const GameState gs = wh40k::test::shoot_step_state();
    std::vector<ShotDeclaration> shots;
    for (const ShotDeclaration& s : shot_options(gs, gs.units[0], ShootingType::Normal)) {
        if (s.weapon_index == 0 && s.profile_index == 0) shots.push_back(s);
    }
    ASSERT_FALSE(shots.empty());
    for (const WeaponSelection& s : make_weapon_selections(gs, gs.units[0], ShootingType::Normal, shots)) {
        EXPECT_FALSE(s.benefit_of_cover);
    }
}

TEST(D088_Redundancy, AFeatureInsideAnObscuringAreaBlocksNothingTheAreaDoesNot) {
    // The view leaves out the Dense features of areas neither model is within: every segment a feature blocks, its
    // area blocks too, because features lie wholly within their areas.
    const GameState gs = load_terrain_scenario();
    std::mt19937 rng(13);
    std::uniform_real_distribution<float> x(0.0f, 44.0f);
    std::uniform_real_distribution<float> y(0.0f, 60.0f);
    const double limit = static_cast<double>(kLineOfSightWidthMm) / 2.0 - static_cast<double>(EPS_MM);
    int blocked = 0;
    for (int i = 0; i < 2000; ++i) {
        const oval_detail::Vec p{in(x(rng)), in(y(rng))};
        const oval_detail::Vec q{in(x(rng)), in(y(rng))};
        for (const TerrainArea& a : gs.terrain) {
            if (!is_obscuring(a)) continue;
            for (const TerrainFeature& f : a.features) {
                if (segment_shape_distance(p, q, f.shape) < limit) {
                    ++blocked;
                    EXPECT_LT(segment_shape_distance(p, q, a.shape), limit);
                }
            }
        }
    }
    EXPECT_GT(blocked, 0);
}

// ---- The M88 replay (tests/replays/m88_terrain.json): checked with core only ----

TEST(M88_Replay, TheTerrainReplayShowsCoverADetourAndAHiddenTargetWithCoreOnly) {
    const Replay saved = load_replay(std::filesystem::path(WH40K_REPLAY_DIR) / "m88_terrain.json");
    ASSERT_TRUE(saved.initial.scenario.has_value());
    EXPECT_EQ(*saved.initial.scenario, kTerrainScenario);
    const ReplayResult r = run_replay(saved, repo_root());
    ASSERT_TRUE(r.ok()) << "step " << *r.failed_step << ": " << r.reasons.front();
    EXPECT_EQ(replay_expectation(r.final_state), saved.expect);
    // Step by step: 13.08 cover on a selection; a MONSTER's two-leg move whose straight line would cross a dense wall
    // it can't move through (13.06); a target refused for 13.09 at a Shoot.
    GameState gs = replay_initial_state(saved, repo_root());
    start_game(gs);
    int covered = 0;
    int detours = 0;
    int hidden_refused = 0;
    for (const ReplayStep& step : saved.steps) {
        if (const auto* shoot = std::get_if<action::Shoot>(&step.action)) {
            const Unit* u = gs.find_unit(shoot->unit);
            for (const Unit& e : gs.units) {
                if (u == nullptr || e.side == u->side || e.models.empty()) continue;
                for (const Model& m : u->models) {
                    if (visibility_problem(gs, m, e).kind == VisibilityProblemKind::Hidden) {
                        ++hidden_refused;
                        break;
                    }
                }
            }
        }
        if (const auto* p = std::get_if<action::ProposeModelMove>(&step.action); p != nullptr && p->path.size() >= 2) {
            const Unit* u = find_unit_of_model(gs, p->model);
            const Model* m = u == nullptr ? nullptr : find_model(*u, p->model);
            if (m != nullptr && is_monster_or_vehicle(*u, *m)) {
                for (const PathFeature& f : path_features(gs, *u).uncrossable) {
                    if (translation_crosses_shape(pose_of(*m), p->path.back(), *f.shape)) {
                        ++detours;
                        break;
                    }
                }
            }
        }
        ASSERT_TRUE(validate_action(gs, step.side, step.action).ok());
        for (const GameEvent& e : apply_action(gs, step.side, step.action)) {
            if (const auto* sh = std::get_if<event::ShootingStarted>(&e)) {
                for (const WeaponSelection& s : sh->selections) covered += s.benefit_of_cover ? 1 : 0;
            }
        }
    }
    EXPECT_GT(covered, 0);
    EXPECT_EQ(detours, 1);
    EXPECT_GT(hidden_refused, 0);
    EXPECT_EQ(gs, r.final_state);
}
