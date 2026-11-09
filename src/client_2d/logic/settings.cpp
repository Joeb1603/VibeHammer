#include "settings.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <system_error>

namespace wh40k::client {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr std::array<const char*, 3> kLevelKeys{"easy", "normal", "hard"};
constexpr std::array<const char*, 4> kSpeedKeys{"slow", "normal", "fast", "instant"};
constexpr std::array<const char*, 3> kModeKeys{"hotseat", "vs_bot", "bot_vs_bot"};
constexpr std::array<const char*, 2> kDeploymentKeys{"auto", "hand"};

// An army list in the settings, or nullopt when it isn't one (core's from_json checks its format and kind).
std::optional<core::ArmyList> read_army(const json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end()) return std::nullopt;
    try {
        return it->get<core::ArmyList>();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void read_bool(const json& j, const char* key, bool& out) {
    if (const auto it = j.find(key); it != j.end() && it->is_boolean()) out = it->get<bool>();
}

void read_string(const json& j, const char* key, std::string& out) {
    if (const auto it = j.find(key); it != j.end() && it->is_string()) out = it->get<std::string>();
}

// An enum stored as one of `keys`; anything else keeps the default.
template <typename E, std::size_t N>
void read_enum(const json& j, const char* key, const std::array<const char*, N>& keys, E& out) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return;
    const std::string value = it->get<std::string>();
    for (std::size_t i = 0; i < N; ++i) {
        if (value == keys[i]) out = static_cast<E>(i);
    }
}

}  // namespace

json custom_battle_to_json(const CustomBattleSettings& s) {
    const CustomBattleSettings d;
    json j = json::object();
    if (s.army_a) j["army_a"] = *s.army_a;
    if (s.army_b) j["army_b"] = *s.army_b;
    if (s.first_turn != d.first_turn) j["first_turn"] = s.first_turn == core::Side::A ? "A" : "B";
    if (s.deployment != d.deployment) j["deployment"] = kDeploymentKeys[static_cast<std::size_t>(s.deployment)];
    if (s.play_mode != d.play_mode) j["play_mode"] = kModeKeys[static_cast<std::size_t>(s.play_mode)];
    return j;
}

CustomBattleSettings custom_battle_from_json(const json& j) {
    CustomBattleSettings s;
    if (!j.is_object()) return s;
    s.army_a = read_army(j, "army_a");
    s.army_b = read_army(j, "army_b");
    if (const auto it = j.find("first_turn"); it != j.end() && it->is_string()) {
        const std::string side = it->get<std::string>();
        if (side == "A") s.first_turn = core::Side::A;
        if (side == "B") s.first_turn = core::Side::B;
    }
    read_enum(j, "deployment", kDeploymentKeys, s.deployment);
    read_enum(j, "play_mode", kModeKeys, s.play_mode);
    return s;
}

json settings_to_json(const AppSettings& s) {
    json j{
        {"human_side", s.human_side == core::Side::A ? "A" : "B"},
        {"bot_level", kLevelKeys[static_cast<std::size_t>(s.bot_level)]},
        {"bot_speed", kSpeedKeys[static_cast<std::size_t>(s.bot_speed)]},
        {"scenario", s.scenario},
        {"fixed_seed", s.fixed_seed},
        {"seed", s.seed},
        {"show_dice", s.show_dice},
        {"skip_empty_steps", s.skip_empty_steps},
        {"fix_moves", s.fix_moves},
        {"auto_allocate", s.auto_allocate},
        {"warn_end_shooting", s.warn_end_shooting},
        {"ui_scale", s.ui_scale},
    };
    // M75: only when the builder's setup isn't the default, so an older file saves unchanged.
    if (s.custom_battle != CustomBattleSettings{}) j["custom_battle"] = custom_battle_to_json(s.custom_battle);
    return j;
}

AppSettings settings_from_json(const json& j) {
    AppSettings s;
    if (!j.is_object()) return s;
    if (const auto it = j.find("human_side"); it != j.end() && it->is_string()) {
        const std::string side = it->get<std::string>();
        if (side == "A") s.human_side = core::Side::A;
        if (side == "B") s.human_side = core::Side::B;
    }
    read_enum(j, "bot_level", kLevelKeys, s.bot_level);
    read_enum(j, "bot_speed", kSpeedKeys, s.bot_speed);
    // M51 (D-062): "bot_auto_dice" (the removed auto-dice setting) in an older file is ignored.
    read_string(j, "scenario", s.scenario);
    if (s.scenario.empty()) s.scenario = AppSettings{}.scenario;
    read_bool(j, "fixed_seed", s.fixed_seed);
    if (const auto it = j.find("seed"); it != j.end() && it->is_number_unsigned()) s.seed = it->get<std::uint64_t>();
    read_bool(j, "show_dice", s.show_dice);
    read_bool(j, "skip_empty_steps", s.skip_empty_steps);
    read_bool(j, "fix_moves", s.fix_moves);
    read_bool(j, "auto_allocate", s.auto_allocate);
    read_bool(j, "warn_end_shooting", s.warn_end_shooting);
    if (const auto it = j.find("ui_scale"); it != j.end() && it->is_number()) {
        const double scale = it->get<double>();
        if (std::isfinite(scale)) {
            s.ui_scale = std::clamp(static_cast<float>(scale), kMinUiScale, kMaxUiScale);
        }
    }
    if (const auto it = j.find("custom_battle"); it != j.end()) s.custom_battle = custom_battle_from_json(*it);
    return s;
}

AppSettings load_settings(const fs::path& path) {
    try {
        std::ifstream in(path);
        if (!in) return AppSettings{};
        const json j = json::parse(in, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded()) return AppSettings{};
        return settings_from_json(j);
    } catch (const std::exception&) {
        return AppSettings{};
    }
}

bool save_settings(const fs::path& path, const AppSettings& s) {
    try {
        std::error_code ec;
        if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::trunc);
        if (!out) return false;
        out << settings_to_json(s).dump(2) << '\n';
        return static_cast<bool>(out);
    } catch (const std::exception&) {
        return false;
    }
}

const char* bot_level_name(BotLevel level) {
    switch (level) {
        case BotLevel::Easy: return "Easy";
        case BotLevel::Normal: return "Normal";
        case BotLevel::Hard: return "Hard";
    }
    return "?";
}

const char* bot_speed_name(BotSpeed speed) {
    switch (speed) {
        case BotSpeed::Slow: return "Slow";
        case BotSpeed::Normal: return "Normal";
        case BotSpeed::Fast: return "Fast";
        case BotSpeed::Instant: return "Instant";
    }
    return "?";
}

const char* bot_level_key(BotLevel level) { return kLevelKeys[static_cast<std::size_t>(level)]; }
const char* bot_speed_key(BotSpeed speed) { return kSpeedKeys[static_cast<std::size_t>(speed)]; }

std::optional<BotLevel> bot_level_from_key(std::string_view key) {
    for (std::size_t i = 0; i < kLevelKeys.size(); ++i) {
        if (key == kLevelKeys[i]) return static_cast<BotLevel>(i);
    }
    return std::nullopt;
}

std::optional<BotSpeed> bot_speed_from_key(std::string_view key) {
    for (std::size_t i = 0; i < kSpeedKeys.size(); ++i) {
        if (key == kSpeedKeys[i]) return static_cast<BotSpeed>(i);
    }
    return std::nullopt;
}

const char* game_mode_key(GameMode mode) { return kModeKeys[static_cast<std::size_t>(mode)]; }

std::optional<GameMode> game_mode_from_key(std::string_view key) {
    for (std::size_t i = 0; i < kModeKeys.size(); ++i) {
        if (key == kModeKeys[i]) return static_cast<GameMode>(i);
    }
    return std::nullopt;
}

BotSides bot_sides_for(GameMode mode, core::Side human) {
    switch (mode) {
        case GameMode::Hotseat: return {false, false};
        case GameMode::VsBot: return {human != core::Side::A, human != core::Side::B};
        case GameMode::BotVsBot: return {true, true};
    }
    return {false, false};
}

std::vector<std::string> list_json_files(const fs::path& root, const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    const fs::path folder = root / dir;
    if (!fs::is_directory(folder, ec)) return out;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        if (p.extension() == ".json" && it->is_regular_file(ec)) {
            out.push_back((fs::path(dir) / p.filename()).generic_string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> list_scenarios(const fs::path& root) { return list_json_files(root, "data/scenarios"); }

std::vector<std::string> list_replays(const fs::path& root) { return list_json_files(root, "tests/replays"); }

fs::path resolve_data_path(const fs::path& root, const std::string& path) {
    const fs::path p(path);
    return p.is_absolute() ? p : root / p;
}

ScenarioInfo scenario_info(const fs::path& file) {
    ScenarioInfo info;
    info.name = file.stem().string();
    try {
        std::ifstream in(file);
        if (!in) return info;
        const json j = json::parse(in, nullptr, /*allow_exceptions=*/false);
        if (!j.is_object()) return info;
        if (const auto it = j.find("name"); it != j.end() && it->is_string()) info.name = it->get<std::string>();
        if (const auto sides = j.find("sides"); sides != j.end() && sides->is_object()) {
            for (std::size_t i = 0; i < 2; ++i) {
                const auto side = sides->find(i == 0 ? "A" : "B");
                if (side == sides->end() || !side->is_object()) continue;
                if (const auto n = side->find("name"); n != side->end() && n->is_string()) {
                    info.sides[i] = n->get<std::string>();
                }
            }
        }
    } catch (const std::exception&) {
    }
    return info;
}

}  // namespace wh40k::client
