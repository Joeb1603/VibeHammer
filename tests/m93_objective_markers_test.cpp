// M93 (D-094): 14.01.01 Objectives Not Within a Terrain Area (app-only, and the Rules Appendix): a 40 mm objective
// marker, a model within range of it while within 3" of it, measured to its closest part.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "core/data/datasheet.hpp"
#include "core/data/load_error.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/serialization/json.hpp"
#include "core/state/objectives.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::at;
using wh40k::test::in;
using wh40k::test::repo_root;

namespace {

constexpr const char* kObjectiveScenario = "data/scenarios/incursion_objectives_44x60.json";
constexpr const char* kTerrainScenario = "data/scenarios/incursion_terrain_44x60.json";

GameState build(const nlohmann::json& scenario) {
    return build_game_state(scenario, [](const std::string& unit_file) { return load_datasheet(repo_root() / unit_file); });
}

ObjectiveArea marker_at(Position centre) {
    ObjectiveArea a;
    a.name = "Marker";
    a.centre = centre;
    a.diameter_mm = kObjectiveMarkerDiameterMm + 2.0f * kObjectiveMarkerRangeMm;
    a.marker = true;
    return a;
}

}  // namespace

TEST(R14_01_ObjectiveMarkers, TheObjectiveScenarioHasMarkersAndTheTerrainScenarioTerrainObjectives) {
    const GameState gs = load_scenario(repo_root() / kObjectiveScenario, repo_root());
    ASSERT_TRUE(gs.mission.has_value());
    for (const ObjectiveArea& a : gs.mission->objectives) {
        EXPECT_TRUE(a.marker) << a.name;
        EXPECT_FLOAT_EQ(a.diameter_mm, 40.0f + 6.0f * 25.4f) << a.name;
    }
    const GameState terrain = load_scenario(repo_root() / kTerrainScenario, repo_root());
    for (const ObjectiveArea& a : terrain.mission->objectives) EXPECT_FALSE(a.marker) << a.name;
}

TEST(R14_01_ObjectiveMarkers, WithinRangeIsWithin3InchesOfTheMarkersClosestPart) {
    const ObjectiveArea m = marker_at(at(10.0f, 10.0f));
    const Base base{BaseShape::Round, 32.0f, 0.0f};
    // A 32 mm base whose edge is 2.99" / 3.01" from the marker's edge (the marker is 20 mm in radius).
    const auto pose = [&](float gap_in) {
        return BasePose{Position{at(10.0f, 10.0f).x + 20.0f + 16.0f + in(gap_in), at(10.0f, 10.0f).y, 0.0f}, base, 0.0f};
    };
    EXPECT_TRUE(within_objective(pose(2.99f), m));
    EXPECT_FALSE(within_objective(pose(3.01f), m));
}

TEST(R14_01_ObjectiveMarkers, Within3InchesOfAMarkerIsMeasuredToTheMarkerForConsolidation) {
    // 12.08: "within 3" of one or more objectives", measured to the closest part of the marker (14.01.01): the same as
    // within range for a marker, not 3" beyond its range.
    GameState gs = load_scenario(repo_root() / kObjectiveScenario, repo_root());
    Unit& u = gs.units.front();
    gs.mission->objectives = {marker_at(at(22.0f, 30.0f))};
    const float r = u.models.front().base.diameter_mm * 0.5f;
    for (Model& m : u.models) m.position = Position{at(22.0f, 30.0f).x + 20.0f + r + in(2.9f), at(22.0f, 30.0f).y, 0.0f};
    EXPECT_EQ(objectives_within(gs, u, 3.0f), (std::vector<std::size_t>{0}));
    for (Model& m : u.models) m.position.x += in(0.2f);  // 3.1" from the marker: out of range, and not within 3"
    EXPECT_TRUE(objectives_within(gs, u, 3.0f).empty());
}

TEST(R14_01_ObjectiveMarkers, TheScenarioFormatAndJson) {
    nlohmann::json j = read_json_file(repo_root() / kObjectiveScenario);
    // A marker gives no diameter of its own.
    nlohmann::json bad = j;
    bad["mission"]["objectives"][0]["diameter_in"] = 12;
    EXPECT_THROW((void)build(bad), DataError);
    bad = j;
    bad["mission"]["objectives"][0]["marker"] = "yes";
    EXPECT_THROW((void)build(bad), DataError);
    // A terrain area can't be a marker objective (14.01.01: a marker is an objective not within a terrain area).
    nlohmann::json with_terrain = read_json_file(repo_root() / kTerrainScenario);
    with_terrain["mission"] = j["mission"];
    try {
        (void)build(with_terrain);
        ADD_FAILURE() << "a terrain area linked to a marker loaded";
    } catch (const DataError& e) {
        EXPECT_NE(std::string(e.what()).find("14.01.01"), std::string::npos) << e.what();
    }
    // JSON: "marker" only when true; round trips.
    const ObjectiveArea m = marker_at(at(10.0f, 10.0f));
    EXPECT_EQ(Json(m).at("marker"), true);
    EXPECT_EQ(Json(m).get<ObjectiveArea>(), m);
    ObjectiveArea area = m;
    area.marker = false;
    EXPECT_FALSE(Json(area).contains("marker"));
    EXPECT_EQ(Json(area).get<ObjectiveArea>(), area);
}
