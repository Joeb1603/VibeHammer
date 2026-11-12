// wh40k_bot_arena (M39, D-054): bot vs bot games, headless. Run it in Release for strength numbers.
//
// Usage: wh40k_bot_arena [--games N] [--scenario <file>] [--seed <first>] [--step-cap N] [--trace <seed>]
//                        [--weights key=value,...] [--weights-a key=value,...] [--weights-b key=value,...]
//   Plays N games (default 200) with seeds first, first + 1, ... (default 1), the bot on both sides (D-085, M85: one
//   bot; --a / --b and the levels are gone). The scenario is relative to the repo root (default
//   data/scenarios/v1_skirmish.json). Exits 1 if any action was rejected, any move was reverted, or a game
//   didn't finish within the step cap (default 5000); 2 for a bad flag. --trace plays the one game with that
//   seed and prints every bot decision, its reason and (M44) its time instead. --weights (for tuning) overrides the
//   move score's weights (normal::MoveWeights, normal/move_score.hpp) for both sides, --weights-a / --weights-b for one
//   side (applied in the order given): delay, survival, edge, coherency, approach, cohesion, follow, approach_alone,
//   objective, objective_approach, objective_fire (M87, D-086; all 0: a mission game plays exactly as before M87).
//   --per-game 1 (M54, to show that an optimisation changes no play) prints one line per
//   game instead of the totals: its result, its counts and FNV-1a
//   hashes of the accepted actions, the final state (JSON) and the bot decisions' trace lines (reasons, no times),
//   then the decision times and the run's wall time.
// M72 (D-079 part 2), custom battles instead of --scenario: --army-a <file> / --army-b <file> (army lists, relative to
//   the repo root; a side not given plays the default army of the other faction), --random-armies 1 (each game a
//   random legal Chaos Space Marines army for side A and Tyranid army for side B, drawn from the game's seed) and
//   --first A|B (the side with the first turn: the custom battle's, default A; with a scenario, it overrides the
//   scenario's). Without these flags the arena runs exactly as before.
// M74 (D-081): --deploy hand|auto (default auto), for --army-a / --army-b / --random-armies only: hand has the bots
//   set the units up in the deployment step (alternating, the side taking the second turn first); auto places them
//   by the auto-layout as before. With a scenario (which fixes its own deployment) it exits 2.
// M87 (D-086): --mission <scenario file>, for --army-a / --army-b / --random-armies only: the scenario whose mission block
//   the battles play (default data/scenarios/incursion_objectives_44x60.json, M86); for tuning the mission's numbers.
//   With --scenario it exits 2.
// M88 (D-087): --terrain <scenario file>, for --army-a / --army-b / --random-armies only: the scenario whose terrain
//   block the battles are fought over (e.g. data/scenarios/incursion_terrain_44x60.json; its areas may link to the
//   mission's objectives). Without it, a bare battlefield as before. With --scenario it exits 2.

#include <cstdio>
#include <exception>
#include <sstream>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "arena.hpp"
#include "core/data/army_list.hpp"
#include "core/data/load_error.hpp"
#include "core/data/scenario_loader.hpp"

namespace {

namespace fs = std::filesystem;
using wh40k::ai::BotConfig;

// "delay=0.4,follow=0.2": the move score's weights (D-085, M85 tuning).
void apply_weights(const std::string& text, wh40k::ai::normal::MoveWeights& w) {
    std::stringstream in(text);
    std::string item;
    while (std::getline(in, item, ',')) {
        const std::size_t eq = item.find('=');
        if (eq == std::string::npos) throw std::invalid_argument("expected key=value");
        const std::string key = item.substr(0, eq);
        const std::string value = item.substr(eq + 1);
        const double d = std::stod(value);
        if (key == "delay") {
            w.delay = d;
        } else if (key == "survival") {
            w.survival = d;
        } else if (key == "edge") {
            w.edge = d;
        } else if (key == "coherency") {
            w.coherency = d;
        } else if (key == "approach") {  // D-083 part 2 (M80)
            w.approach = d;
        } else if (key == "cohesion") {
            w.cohesion = d;
        } else if (key == "follow") {
            w.follow = d;
        } else if (key == "approach_alone") {  // D-083 part 5 (M83)
            w.approach_alone = d;
        } else if (key == "objective") {  // M87 (D-086)
            w.objective = d;
        } else if (key == "objective_approach") {
            w.objective_approach = d;
        } else if (key == "objective_fire") {
            w.objective_fire = d;
        } else {
            throw std::invalid_argument("unknown weight");
        }
    }
}

// For the header line: the weights given for a side, or "default".
std::string weights_name(const std::string& text) { return text.empty() ? "default" : "weights " + text; }

}  // namespace

int main(int argc, char** argv) {
    int games = 200;
    BotConfig a;
    BotConfig b;
    std::string scenario = "data/scenarios/v1_skirmish.json";
    std::uint64_t seed = 1;
    std::size_t step_cap = 5000;
    std::optional<std::uint64_t> trace;
    std::string weights_a;  // as given, for the header line
    std::string weights_b;
    bool per_game = false;
    bool scenario_given = false;
    std::optional<std::string> army_a;  // M72
    std::optional<std::string> army_b;
    std::optional<wh40k::core::Side> first;
    bool random_armies = false;
    std::optional<wh40k::core::DeploymentMode> deploy;  // M74 (D-081)
    std::optional<std::string> mission;                 // M87 (D-086)
    std::optional<std::string> terrain;                 // M88 (D-087)
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
            } else if (key == "--scenario") {
                scenario = value;
                scenario_given = true;
            } else if (key == "--army-a") {
                army_a = value;
            } else if (key == "--army-b") {
                army_b = value;
            } else if (key == "--first") {
                if (value != "A" && value != "B") throw std::invalid_argument("not A or B");
                first = value == "A" ? wh40k::core::Side::A : wh40k::core::Side::B;
            } else if (key == "--deploy") {
                if (value != "hand" && value != "auto") throw std::invalid_argument("not hand or auto");
                deploy = value == "hand" ? wh40k::core::DeploymentMode::Hand : wh40k::core::DeploymentMode::Auto;
            } else if (key == "--mission") {
                mission = value;
            } else if (key == "--terrain") {
                terrain = value;
            } else if (key == "--random-armies") {
                const int v = std::stoi(value);
                if (v != 0 && v != 1) throw std::invalid_argument("not 0 or 1");
                random_armies = v == 1;
            } else if (key == "--seed") {
                seed = std::stoull(value);
            } else if (key == "--trace") {
                trace = std::stoull(value);
            } else if (key == "--step-cap") {
                step_cap = static_cast<std::size_t>(std::stoul(value));
            } else if (key == "--per-game") {
                per_game = std::stoi(value) != 0;
            } else if (key == "--weights") {
                apply_weights(value, a.move);
                apply_weights(value, b.move);
                weights_a += (weights_a.empty() ? "" : ",") + value;
                weights_b += (weights_b.empty() ? "" : ",") + value;
            } else if (key == "--weights-a") {
                apply_weights(value, a.move);
                weights_a += (weights_a.empty() ? "" : ",") + value;
            } else if (key == "--weights-b") {
                apply_weights(value, b.move);
                weights_b += (weights_b.empty() ? "" : ",") + value;
            } else {
                throw std::invalid_argument("unknown flag");
            }
        } catch (const std::exception&) {
            std::fprintf(stderr, "wh40k_bot_arena: invalid value for %s: '%s'\n", key.c_str(), value.c_str());
            return 2;
        }
    }
    const bool armies = army_a.has_value() || army_b.has_value();
    if ((armies || random_armies) && scenario_given) {
        std::fprintf(stderr, "wh40k_bot_arena: --scenario can't be combined with --army-a, --army-b or --random-armies\n");
        return 2;
    }
    if (armies && random_armies) {
        std::fprintf(stderr, "wh40k_bot_arena: --random-armies can't be combined with --army-a or --army-b\n");
        return 2;
    }
    if (deploy && !armies && !random_armies) {
        std::fprintf(stderr, "wh40k_bot_arena: --deploy needs --army-a, --army-b or --random-armies (a scenario fixes "
                             "its own deployment)\n");
        return 2;
    }
    if (mission && !armies && !random_armies) {
        std::fprintf(stderr, "wh40k_bot_arena: --mission needs --army-a, --army-b or --random-armies (a scenario has its "
                             "own mission)\n");
        return 2;
    }
    if (terrain && !armies && !random_armies) {
        std::fprintf(stderr, "wh40k_bot_arena: --terrain needs --army-a, --army-b or --random-armies (a scenario has its "
                             "own terrain)\n");
        return 2;
    }
    const wh40k::core::DeploymentMode deploy_mode = deploy.value_or(wh40k::core::DeploymentMode::Auto);
    try {
        const fs::path root(WH40K_REPO_ROOT);
        const auto in_repo = [&root](const std::string& name) {
            fs::path file(name);
            if (!fs::exists(file)) file = root / name;
            return file;
        };
        wh40k::ai::ScenarioForSeed scenario_for;
        if (armies || random_armies) {
            namespace core = wh40k::core;
            auto loaded = wh40k::ai::load_army_source(root);
            // M86 (D-084): custom and random battles play the objective scenario's mission, as in the client.
            // M87 (D-086): --mission names another scenario to read the block from.
            loaded.mission =
                core::load_mission_block(mission ? in_repo(*mission) : root / "data/scenarios/incursion_objectives_44x60.json");
            // M88 (D-087): --terrain names a scenario to read a "terrain" block from (its areas may link to the
            // mission's objectives by name). Without it, a bare battlefield as before.
            if (terrain) loaded.terrain = core::load_terrain_block(in_repo(*terrain));
            const auto source = std::make_shared<wh40k::ai::ArmySource>(std::move(loaded));
            const core::Side first_turn = first.value_or(core::Side::A);
            if (random_armies) {
                scenario = "random armies (D-079)";
                scenario_for = [source, first_turn, deploy_mode](std::uint64_t s) {
                    return wh40k::ai::random_armies_battle(*source, s, first_turn, deploy_mode);
                };
            } else {
                std::optional<core::ArmyList> lists[2];
                if (army_a) lists[0] = core::load_army(in_repo(*army_a));
                if (army_b) lists[1] = core::load_army(in_repo(*army_b));
                for (std::size_t side = 0; side < 2; ++side) {  // a side not given: the other faction's default army
                    if (lists[side]) continue;
                    const std::string& given = lists[1 - side]->faction;
                    for (const core::Faction& f : source->factions) {
                        if (f.id != given && !lists[side]) lists[side] = core::load_army(root / f.default_army);
                    }
                    if (!lists[side]) throw core::DataError("D-079: no faction other than '" + given + "'");
                }
                scenario = "army " + lists[0]->name + " vs " + lists[1]->name;
                const core::GameState state =
                    wh40k::ai::custom_battle_state(*source, *lists[0], *lists[1], first_turn, deploy_mode);
                scenario_for = [state](std::uint64_t) { return state; };
            }
            if (deploy_mode == core::DeploymentMode::Hand) scenario += ", deployment by hand (D-081)";
            if (terrain) scenario += ", terrain from " + *terrain + " (D-087)";
        } else {
            wh40k::core::GameState state = wh40k::core::load_scenario(in_repo(scenario), root);
            if (first) state.first_turn = *first;
            scenario_for = [state](std::uint64_t) { return state; };
        }
        const wh40k::core::GameState initial = scenario_for(trace ? *trace : seed);
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
        std::printf("arena: %d games, A %s vs B %s, %s, seeds %llu..%llu\n", games, weights_name(weights_a).c_str(),
                    weights_name(weights_b).c_str(), scenario.c_str(), static_cast<unsigned long long>(seed),
                    static_cast<unsigned long long>(seed + static_cast<std::uint64_t>(games > 0 ? games - 1 : 0)));
        if (per_game) {
            const wh40k::ai::PerGameReport r = wh40k::ai::run_per_game(scenario_for, games, seed, a, b, step_cap);
            std::printf("%s", r.text.c_str());
            return r.clean ? 0 : 1;
        }
        const wh40k::ai::ArenaResult r = wh40k::ai::run_arena(scenario_for, games, seed, a, b, step_cap);
        std::printf("%s", wh40k::ai::format_arena(r, initial).c_str());
        return r.clean() ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "wh40k_bot_arena: %s\n", e.what());
        return 2;
    }
}
