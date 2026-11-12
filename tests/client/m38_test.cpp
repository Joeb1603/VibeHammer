// M38: the start screen's saved settings, game modes and scenario / replay lists (D-055).

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "settings.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::repo_root;
namespace fs = std::filesystem;

namespace {

AppSettings changed_settings() {
    AppSettings s;
    s.human_side = Side::B;
    s.bot_speed = BotSpeed::Instant;
    s.scenario = "data/scenarios/other.json";
    s.fixed_seed = true;
    s.seed = 123456789012ull;
    s.show_dice = false;
    s.skip_empty_steps = false;
    s.fix_moves = false;
    s.auto_allocate = true;
    s.warn_end_shooting = false;
    s.ui_scale = 1.5f;
    return s;
}

// A file in a fresh folder under the system temp directory, removed with it.
class TempDir {
public:
    TempDir() : dir_(fs::temp_directory_path() / ("vibehammer_m38_" + std::string(
                         ::testing::UnitTest::GetInstance()->current_test_info()->name()))) {
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    fs::path file(const std::string& name) const { return dir_ / name; }
    void write(const std::string& name, const std::string& text) const { std::ofstream(file(name)) << text; }

private:
    fs::path dir_;
};

}  // namespace

TEST(M38_Settings, DefaultsMatchThePlan) {
    const AppSettings s;
    EXPECT_EQ(s.human_side, Side::A);
    EXPECT_EQ(s.bot_speed, BotSpeed::Normal);
    EXPECT_EQ(s.scenario, "data/scenarios/incursion_terrain_44x60.json");  // M88 (D-087): the objective scenario with terrain
    EXPECT_FALSE(s.fixed_seed);
    EXPECT_TRUE(s.show_dice);
    EXPECT_TRUE(s.skip_empty_steps);
    EXPECT_TRUE(s.fix_moves);
    EXPECT_FALSE(s.auto_allocate);
    EXPECT_TRUE(s.warn_end_shooting);
    EXPECT_FLOAT_EQ(s.ui_scale, 1.0f);
}

TEST(M38_Settings, RoundTripsThroughJson) {
    const AppSettings s = changed_settings();
    EXPECT_EQ(settings_from_json(settings_to_json(s)), s);
    EXPECT_EQ(settings_from_json(settings_to_json(AppSettings{})), AppSettings{});
    // Through the text form too, as the file stores it.
    EXPECT_EQ(settings_from_json(nlohmann::json::parse(settings_to_json(s).dump())), s);
}

TEST(M38_Settings, RoundTripsThroughAFile) {
    const TempDir dir;
    const AppSettings s = changed_settings();
    ASSERT_TRUE(save_settings(dir.file("vibehammer_settings.json"), s));
    EXPECT_EQ(load_settings(dir.file("vibehammer_settings.json")), s);
}

TEST(M38_Settings, MissingFileGivesDefaults) {
    const TempDir dir;
    EXPECT_EQ(load_settings(dir.file("no_such_file.json")), AppSettings{});
}

TEST(M38_Settings, CorruptFileGivesDefaults) {
    const TempDir dir;
    dir.write("corrupt.json", "{ \"show_dice\": false, \"fix_mo");
    EXPECT_EQ(load_settings(dir.file("corrupt.json")), AppSettings{});
    dir.write("array.json", "[1, 2, 3]");
    EXPECT_EQ(load_settings(dir.file("array.json")), AppSettings{});
    dir.write("empty.json", "");
    EXPECT_EQ(load_settings(dir.file("empty.json")), AppSettings{});
}

TEST(M38_Settings, MissingKeysKeepTheirDefaults) {
    const AppSettings s = settings_from_json(nlohmann::json{{"show_dice", false}, {"bot_speed", "fast"}});
    AppSettings expected;
    expected.show_dice = false;
    expected.bot_speed = BotSpeed::Fast;
    EXPECT_EQ(s, expected);
}

TEST(M38_Settings, AnOldFileWithABotLevelLoadsAndIgnoresIt) {
    // D-085 (M85): one bot. A settings file written before M85 names a level ("easy", "normal" or "hard"): it loads with
    // every other setting, and saving it drops the key.
    for (const char* level : {"easy", "normal", "hard", "impossible"}) {
        nlohmann::json old = settings_to_json(changed_settings());
        old["bot_level"] = level;
        EXPECT_EQ(settings_from_json(old), changed_settings()) << level;
        EXPECT_FALSE(settings_to_json(settings_from_json(old)).contains("bot_level")) << level;
    }
}

TEST(M38_Settings, UnknownKeysAreIgnored) {
    nlohmann::json j = settings_to_json(changed_settings());
    j["from_a_later_version"] = {{"x", 1}};
    j["players"] = "something";
    EXPECT_EQ(settings_from_json(j), changed_settings());
}

TEST(M38_Settings, WrongTypedOrUnknownValuesGiveTheDefaultForThatKeyOnly) {
    nlohmann::json j = settings_to_json(changed_settings());
    j["show_dice"] = "no";           // a string, not a bool
    j["bot_speed"] = 3;              // a number, not a name
    j["human_side"] = "C";           // not a side
    j["seed"] = -5;                  // negative
    j["ui_scale"] = "big";           // not a number
    j["scenario"] = 7;               // not a string
    const AppSettings s = settings_from_json(j);
    AppSettings expected = changed_settings();
    const AppSettings defaults;
    expected.show_dice = defaults.show_dice;
    expected.bot_speed = defaults.bot_speed;
    expected.human_side = defaults.human_side;
    expected.seed = defaults.seed;
    expected.ui_scale = defaults.ui_scale;
    expected.scenario = defaults.scenario;
    EXPECT_EQ(s, expected);
}

TEST(M38_Settings, UiScaleIsClampedToTheSlidersRange) {
    EXPECT_FLOAT_EQ(settings_from_json(nlohmann::json{{"ui_scale", 9.0}}).ui_scale, kMaxUiScale);
    EXPECT_FLOAT_EQ(settings_from_json(nlohmann::json{{"ui_scale", 0.1}}).ui_scale, kMinUiScale);
}

TEST(M38_GameMode, BotSidesFollowTheMode) {
    EXPECT_EQ(bot_sides_for(GameMode::Hotseat, Side::A), (BotSides{false, false}));
    EXPECT_EQ(bot_sides_for(GameMode::VsBot, Side::A), (BotSides{false, true}));
    EXPECT_EQ(bot_sides_for(GameMode::VsBot, Side::B), (BotSides{true, false}));
    EXPECT_EQ(bot_sides_for(GameMode::BotVsBot, Side::A), (BotSides{true, true}));
}

TEST(M38_Lists, ScenarioListFindsTheV1Skirmish) {
    const std::vector<std::string> scenarios = list_scenarios(repo_root());
    EXPECT_NE(std::find(scenarios.begin(), scenarios.end(), "data/scenarios/v1_skirmish.json"), scenarios.end());
    EXPECT_TRUE(std::is_sorted(scenarios.begin(), scenarios.end()));
}

TEST(M38_Lists, ReplayListFindsEveryReplayFileSorted) {
    std::vector<std::string> expected;
    for (const auto& entry : fs::directory_iterator(repo_root() / "tests/replays")) {
        if (entry.path().extension() == ".json") expected.push_back("tests/replays/" + entry.path().filename().string());
    }
    std::sort(expected.begin(), expected.end());
    ASSERT_FALSE(expected.empty());
    EXPECT_EQ(list_replays(repo_root()), expected);
    const std::vector<std::string> replays = list_replays(repo_root());
    EXPECT_NE(std::find(replays.begin(), replays.end(), "tests/replays/m11_full_game.json"), replays.end());
}

TEST(M38_Lists, MissingFolderGivesAnEmptyList) {
    const TempDir dir;
    EXPECT_TRUE(list_scenarios(dir.file("")).empty());
    EXPECT_TRUE(list_replays(dir.file("")).empty());
}

TEST(M38_Lists, ScenarioInfoReadsTheNameAndSides) {
    const ScenarioInfo info = scenario_info(repo_root() / "data/scenarios/v1_skirmish.json");
    EXPECT_EQ(info.name, "V1 Skirmish - Intercessors vs Termagants");
    EXPECT_EQ(info.sides[0], "Space Marines");
    EXPECT_EQ(info.sides[1], "Tyranids");
}

TEST(M38_Lists, ScenarioInfoFallsBackForAnUnreadableFile) {
    const TempDir dir;
    dir.write("broken.json", "not json");
    const ScenarioInfo info = scenario_info(dir.file("broken.json"));
    EXPECT_EQ(info.name, "broken");
    EXPECT_EQ(info.sides[0], "Side A");
    EXPECT_EQ(info.sides[1], "Side B");
}
