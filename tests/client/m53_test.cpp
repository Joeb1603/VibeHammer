// M53: the 60"x44" Strike scenario (D-064) shows up in the start screen's scenario list (D-055).

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "settings.hpp"
#include "test_helpers.hpp"

using namespace wh40k::client;
using wh40k::test::repo_root;

TEST(M53_Scenarios, TheListIncludesTheStrikeScenario) {
    const std::vector<std::string> scenarios = list_scenarios(repo_root());
    EXPECT_NE(std::find(scenarios.begin(), scenarios.end(), "data/scenarios/strike_44x60.json"), scenarios.end());
    EXPECT_NE(std::find(scenarios.begin(), scenarios.end(), "data/scenarios/v1_skirmish.json"), scenarios.end());
    EXPECT_TRUE(std::is_sorted(scenarios.begin(), scenarios.end()));
}

TEST(M53_Scenarios, ScenarioInfoGivesTheStrikeNameAndSides) {
    const ScenarioInfo info = scenario_info(repo_root() / "data/scenarios/strike_44x60.json");
    EXPECT_EQ(info.name, "Strike - 3 Intercessor Squads vs 4 Termagant units (44x60)");
    EXPECT_EQ(info.sides[0], "Space Marines");
    EXPECT_EQ(info.sides[1], "Tyranids");
}
