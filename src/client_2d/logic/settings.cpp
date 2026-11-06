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

json settings_to_json(const AppSettings& s) {
    return json{
        {"human_side", s.human_side == core::Side::A ? "A" : "B"},
        {"bot_level", kLevelKeys[static_cast<std::size_t>(s.bot_level)]},
        {"bot_speed", kSpeedKeys[static_cast<std::size_t>(s.bot_speed)]},
        {"bot_auto_dice", s.bot_auto_dice},
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
    read_bool(j, "bot_auto_dice", s.bot_auto_dice);
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
