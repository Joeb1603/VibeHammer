// wh40k_bot_arena (M39, D-054): bot vs bot games, headless. Run it in Release for strength numbers.
//
// Usage: wh40k_bot_arena [--games N] [--a easy|normal|hard] [--b easy|normal|hard] [--scenario <file>]
//                        [--seed <first>] [--step-cap N]
//   Plays N games (default 200) with seeds first, first + 1, ... (default 1); side A is played at level --a,
//   side B at --b (default easy). The scenario is relative to the repo root (default
//   data/scenarios/v1_skirmish.json). Exits 1 if any action was rejected, any move was reverted, or a game
//   didn't finish within the step cap (default 5000); 2 for a bad flag.

#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

#include "arena.hpp"
#include "core/data/scenario_loader.hpp"

namespace {

namespace fs = std::filesystem;
using wh40k::ai::BotConfig;
using wh40k::ai::BotLevel;

BotLevel parse_level(const std::string& v) {
    if (v == "easy") return BotLevel::Easy;
    if (v == "normal") return BotLevel::Normal;
    if (v == "hard") return BotLevel::Hard;
    throw std::invalid_argument("not easy, normal or hard");
}

const char* level_name(BotLevel l) {
    switch (l) {
        case BotLevel::Easy: return "easy";
        case BotLevel::Normal: return "normal";
        case BotLevel::Hard: return "hard";
    }
    return "?";
}

}  // namespace

int main(int argc, char** argv) {
    int games = 200;
    BotConfig a;
    BotConfig b;
    std::string scenario = "data/scenarios/v1_skirmish.json";
    std::uint64_t seed = 1;
    std::size_t step_cap = 5000;
    for (int i = 1; i < argc; i += 2) {
        const std::string key = argv[i];
        if (i + 1 >= argc) {
            std::fprintf(stderr, "wh40k_bot_arena: %s needs a value\n", key.c_str());
            return 2;
        }
        const std::string value = argv[i + 1];
        try {
            if (key == "--games") {
                games = std::stoi(value);
                if (games < 0) throw std::invalid_argument("negative");
            } else if (key == "--a") {
                a.level = parse_level(value);
            } else if (key == "--b") {
                b.level = parse_level(value);
            } else if (key == "--scenario") {
                scenario = value;
            } else if (key == "--seed") {
                seed = std::stoull(value);
            } else if (key == "--step-cap") {
                step_cap = static_cast<std::size_t>(std::stoul(value));
            } else {
                throw std::invalid_argument("unknown flag");
            }
        } catch (const std::exception&) {
            std::fprintf(stderr, "wh40k_bot_arena: invalid value for %s: '%s'\n", key.c_str(), value.c_str());
            return 2;
        }
    }
    try {
        const fs::path root(WH40K_REPO_ROOT);
        fs::path file(scenario);
        if (!fs::exists(file)) file = root / scenario;
        const wh40k::core::GameState initial = wh40k::core::load_scenario(file, root);
        std::printf("arena: %d games, A %s vs B %s, %s, seeds %llu..%llu\n", games, level_name(a.level),
                    level_name(b.level), scenario.c_str(), static_cast<unsigned long long>(seed),
                    static_cast<unsigned long long>(seed + static_cast<std::uint64_t>(games > 0 ? games - 1 : 0)));
        const wh40k::ai::ArenaResult r = wh40k::ai::run_arena(initial, games, seed, a, b, step_cap);
        std::printf("%s", wh40k::ai::format_arena(r, initial).c_str());
        return r.clean() ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "wh40k_bot_arena: %s\n", e.what());
        return 2;
    }
}
