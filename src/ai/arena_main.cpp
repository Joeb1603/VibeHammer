// wh40k_bot_arena (M39, D-054): bot vs bot games, headless. Run it in Release for strength numbers.
//
// Usage: wh40k_bot_arena [--games N] [--a easy|normal|hard] [--b easy|normal|hard] [--scenario <file>]
//   (--a / --b also take the hidden regression levels easy-m55 and normal-m55, D-066: Easy and Normal as at M55)
//                        [--seed <first>] [--step-cap N] [--trace <seed>] [--weights key=value,...]
//   Plays N games (default 200) with seeds first, first + 1, ... (default 1); side A is played at level --a,
//   side B at --b (default easy). The scenario is relative to the repo root (default
//   data/scenarios/v1_skirmish.json). Exits 1 if any action was rejected, any move was reverted, or a game
//   didn't finish within the step cap (default 5000); 2 for a bad flag. --trace plays the one game with that
//   seed and prints every bot decision, its reason and (M44) its time instead. --weights (M44, for tuning) overrides the Hard
//   level's weights (hard/weights.hpp) for both sides: tempo, material, share, plans, rollouts, rollout_actions,
//   budget_actions, static_plans, margin, delay, survival, edge, coherency. --per-game 1 (M54, to show that an
//   optimisation changes no play) prints one line per game instead of the totals: its result, its counts and FNV-1a
//   hashes of the accepted actions, the final state (JSON) and the bot decisions' trace lines (reasons, no times),
//   then the decision times and the run's wall time.

#include <cstdio>
#include <exception>
#include <sstream>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "arena.hpp"
#include "core/data/scenario_loader.hpp"

namespace {

namespace fs = std::filesystem;
using wh40k::ai::BotConfig;
using wh40k::ai::BotLevel;

// The level, and (D-066, M56) the hidden regression levels "easy-m55" and "normal-m55": Easy and Normal exactly as
// they played at M55, for measuring the M56 changes in one binary. Arena and tests only; the client never offers them.
void parse_level(const std::string& v, BotConfig& config) {
    using wh40k::ai::Generation;
    if (v == "easy") {
        config.level = BotLevel::Easy;
        config.generation = Generation::Current;
    } else if (v == "normal") {
        config.level = BotLevel::Normal;
        config.generation = Generation::Current;
    } else if (v == "hard") {
        config.level = BotLevel::Hard;
        config.generation = Generation::Current;
    } else if (v == "easy-m55") {
        config.level = BotLevel::Easy;
        config.generation = Generation::M55;
    } else if (v == "normal-m55") {
        config.level = BotLevel::Normal;
        config.generation = Generation::M55;
    } else {
        throw std::invalid_argument("not easy, normal or hard");
    }
}

// "tempo=0.3,rollouts=8": Hard's weights (M44 tuning).
void apply_weights(const std::string& text, wh40k::ai::hard::BotWeights& w) {
    std::stringstream in(text);
    std::string item;
    while (std::getline(in, item, ',')) {
        const std::size_t eq = item.find('=');
        if (eq == std::string::npos) throw std::invalid_argument("expected key=value");
        const std::string key = item.substr(0, eq);
        const std::string value = item.substr(eq + 1);
        const double d = std::stod(value);
        const auto count = [&] {
            if (d < 0.0) throw std::invalid_argument("negative");
            return static_cast<std::size_t>(d);
        };
        if (key == "tempo") {
            w.chain.tempo = d;
        } else if (key == "material") {
            w.chain.material = d;
        } else if (key == "share") {
            w.lookahead.share = d;
        } else if (key == "plans") {
            w.lookahead.plans = count();
        } else if (key == "rollouts") {
            w.lookahead.rollouts = count();
        } else if (key == "rollout_actions") {
            w.lookahead.rollout_actions = count();
        } else if (key == "budget_actions") {
            w.lookahead.budget_actions = count();
        } else if (key == "static_plans") {
            w.static_plans = count();
        } else if (key == "margin") {
            w.margin = d;
        } else if (key == "delay") {
            w.move.delay = d;
        } else if (key == "survival") {
            w.move.survival = d;
        } else if (key == "edge") {
            w.move.edge = d;
        } else if (key == "coherency") {
            w.move.coherency = d;
        } else {
            throw std::invalid_argument("unknown weight");
        }
    }
}

std::string level_name(const BotConfig& c) {
    std::string name = "?";
    switch (c.level) {
        case BotLevel::Easy: name = "easy"; break;
        case BotLevel::Normal: name = "normal"; break;
        case BotLevel::Hard: name = "hard"; break;
    }
    if (c.generation == wh40k::ai::Generation::M55) name += "-m55";
    return name;
}

}  // namespace

int main(int argc, char** argv) {
    int games = 200;
    BotConfig a;
    BotConfig b;
    std::string scenario = "data/scenarios/v1_skirmish.json";
    std::uint64_t seed = 1;
    std::size_t step_cap = 5000;
    std::optional<std::uint64_t> trace;
    std::string weights_text;
    bool per_game = false;
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
                parse_level(value, a);
            } else if (key == "--b") {
                parse_level(value, b);
            } else if (key == "--scenario") {
                scenario = value;
            } else if (key == "--seed") {
                seed = std::stoull(value);
            } else if (key == "--trace") {
                trace = std::stoull(value);
            } else if (key == "--step-cap") {
                step_cap = static_cast<std::size_t>(std::stoul(value));
            } else if (key == "--per-game") {
                per_game = std::stoi(value) != 0;
            } else if (key == "--weights") {
                apply_weights(value, a.hard);
                apply_weights(value, b.hard);
                weights_text = value;
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
        if (trace) {
            std::vector<std::string> lines;
            std::vector<double> ms;
            const wh40k::ai::GameRecord g = wh40k::ai::play_bot_game(initial, *trace, a, b, step_cap, &ms, &lines);
            for (std::size_t i = 0; i < lines.size(); ++i) {
                // M44: each decision's time too (Release: the 500 ms p95 budget).
                std::printf("%s [%.1f ms]\n", lines[i].c_str(), i < ms.size() ? ms[i] : 0.0);
            }
            std::printf("finished %d, winner %s, round %d, surviving A %d B %d, wounds lost A %d B %d\n",
                        g.finished ? 1 : 0, g.winner ? (*g.winner == wh40k::core::Side::A ? "A" : "B") : "none",
                        g.end_round, g.surviving_models[0], g.surviving_models[1], g.wounds_lost[0], g.wounds_lost[1]);
            for (const std::string& p : g.problems) std::printf("problem: %s\n", p.c_str());
            return 0;
        }
        std::printf("arena: %d games, A %s vs B %s, %s, seeds %llu..%llu\n", games, level_name(a).c_str(),
                    level_name(b).c_str(), scenario.c_str(), static_cast<unsigned long long>(seed),
                    static_cast<unsigned long long>(seed + static_cast<std::uint64_t>(games > 0 ? games - 1 : 0)));
        if (per_game) {
            const wh40k::ai::PerGameReport r = wh40k::ai::run_per_game(initial, games, seed, a, b, step_cap);
            std::printf("%s", r.text.c_str());
            return r.clean ? 0 : 1;
        }
        const wh40k::ai::ArenaResult r = wh40k::ai::run_arena(initial, games, seed, a, b, step_cap);
        std::printf("%s", wh40k::ai::format_arena(r, initial).c_str());
        return r.clean() ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "wh40k_bot_arena: %s\n", e.what());
        return 2;
    }
}
