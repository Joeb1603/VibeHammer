#pragma once

// M38 (D-055): the client's saved settings, the start screen's game modes, and the scenario and replay lists
// it offers. Client only: nothing here is game state.

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/types/ids.hpp"

namespace wh40k::client {

enum class BotLevel : std::uint8_t { Easy, Normal, Hard };
enum class BotSpeed : std::uint8_t { Slow, Normal, Fast, Instant };

inline constexpr float kMinUiScale = 0.75f;  // the "UI scale" slider's range (D-038)
inline constexpr float kMaxUiScale = 2.5f;

// Everything the start screen and the sidebar toggles set. Saved to vibehammer_settings.json next to the exe.
struct AppSettings {
    core::Side human_side = core::Side::A;  // "You play" in a game against the bot
    BotLevel bot_level = BotLevel::Hard;
    BotSpeed bot_speed = BotSpeed::Normal;
    std::string scenario = "data/scenarios/v1_skirmish.json";  // relative to the data root
    bool fixed_seed = false;    // D-038
    std::uint64_t seed = 0;
    bool show_dice = true;        // D-033
    bool skip_empty_steps = true; // D-036
    bool fix_moves = true;        // D-052
    bool auto_allocate = false;   // D-038
    bool warn_end_shooting = true;  // D-038
    float ui_scale = 1.0f;        // the user's "UI scale" (D-028), kMinUiScale..kMaxUiScale

    friend bool operator==(const AppSettings&, const AppSettings&) = default;
};

[[nodiscard]] nlohmann::json settings_to_json(const AppSettings& s);
// Each key is read on its own: a missing, unknown-valued or wrong-typed key keeps its default. Unknown keys are
// ignored, and anything that isn't an object gives all the defaults.
[[nodiscard]] AppSettings settings_from_json(const nlohmann::json& j);

// The defaults when the file is missing, unreadable or not JSON. Never throws.
[[nodiscard]] AppSettings load_settings(const std::filesystem::path& path);
// False if the file couldn't be written. Never throws.
bool save_settings(const std::filesystem::path& path, const AppSettings& s);

[[nodiscard]] const char* bot_level_name(BotLevel level);
[[nodiscard]] const char* bot_speed_name(BotSpeed speed);
// The JSON and CLI spellings: "easy" / "normal" / "hard", "slow" / "normal" / "fast" / "instant" (M39).
[[nodiscard]] const char* bot_level_key(BotLevel level);
[[nodiscard]] const char* bot_speed_key(BotSpeed speed);
[[nodiscard]] std::optional<BotLevel> bot_level_from_key(std::string_view key);
[[nodiscard]] std::optional<BotSpeed> bot_speed_from_key(std::string_view key);

// The start screen's ways to play, and which sides the bot plays in each (client state, never GameState).
enum class GameMode : std::uint8_t { Hotseat, VsBot, BotVsBot };
using BotSides = std::array<bool, 2>;  // indexed by Side
[[nodiscard]] BotSides bot_sides_for(GameMode mode, core::Side human);

// The `*.json` files in `root/dir`, as paths relative to `root` (generic form, e.g.
// "data/scenarios/v1_skirmish.json"), sorted by name. Empty if the folder is missing.
[[nodiscard]] std::vector<std::string> list_json_files(const std::filesystem::path& root, const std::string& dir);
[[nodiscard]] std::vector<std::string> list_scenarios(const std::filesystem::path& root);  // data/scenarios
[[nodiscard]] std::vector<std::string> list_replays(const std::filesystem::path& root);    // tests/replays

// A settings path (relative to the data root, or absolute) as a file to open.
[[nodiscard]] std::filesystem::path resolve_data_path(const std::filesystem::path& root, const std::string& path);

// A scenario file's display name ("name") and its two sides' names; the file name and "Side A" / "Side B" when
// the file can't be read.
struct ScenarioInfo {
    std::string name;
    std::array<std::string, 2> sides{"Side A", "Side B"};
};
[[nodiscard]] ScenarioInfo scenario_info(const std::filesystem::path& file);

}  // namespace wh40k::client
