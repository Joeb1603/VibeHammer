#pragma once

// M39 (D-054): bot vs bot games, headless, through GameSession (so they take the client's own submit path:
// every action validated, the client's automatic skips run with auto-allocation left to the bots). Counts
// results, rejected actions and UnitMoveReverted events (both must be zero), and how long decisions take
// (measured with a steady clock here only, never inside a decision).

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "bot.hpp"
#include "core/data/army_list.hpp"
#include "core/engine/events.hpp"
#include "core/state/game_state.hpp"
#include "session.hpp"

namespace wh40k::ai {

// ---- M78 (D-083 part 0): teamwork statistics, counted per side in play_bot_session ----

// The engagements at one moment, by side (indexed by side_index), for M78's statistics. Only units with models count.
struct EngagementSnapshot {
    // Gang-ups: the enemy units that two or more of this side's units are engaged with (03.04, engaged_enemy_units), in
    // GameState order.
    std::array<std::vector<core::UnitId>, 2> ganged_up;
    // Lone engagements: this side's units engaged with an enemy unit that no other unit of this side is within 12" of
    // (edge to edge, unit_distance; an engaged unit is within 2", so "engaged with it or within 12" of it" is the same),
    // in GameState order, each unit once.
    std::array<std::vector<core::UnitId>, 2> lone;
    // M87 (D-086), the blob measure: the most units (both sides) in one connected group of engagements (a unit engaged
    // with another links them), and the units on the battlefield with models.
    int largest_cluster = 0;
    int on_battlefield = 0;
};
[[nodiscard]] EngagementSnapshot engagement_snapshot(const core::GameState& state);

// The distinct target units of one Shoot's weapon selections (10.02, ShootingStarted).
[[nodiscard]] int distinct_targets(const core::event::ShootingStarted& shoot);

// One game's teamwork statistics, by side (indexed by side_index).
struct TeamworkStats {
    // Taken at the start of each Fight phase (the state after the action whose events start it), both sides:
    std::array<int, 2> gang_ups{};          // (Fight phase, enemy unit ganged up on) pairs
    std::array<int, 2> ganged_units{};      // distinct enemy units ever ganged up on
    std::array<int, 2> lone_engagements{};  // (Fight phase, our unit in a lone engagement) pairs
    // Shooting spread (ShootingStarted): Shoots, their distinct targets summed, and per shooting phase with a selection,
    // the share of the side's weapon selections (one model's weapon profile at one target, not attack dice) aimed at
    // its most-selected target unit, summed over those phases.
    std::array<int, 2> shoots{};
    std::array<int, 2> shoot_targets{};
    std::array<int, 2> shooting_phases{};
    std::array<double, 2> top_share_sum{};
    // Contact, checked on the initial state and after every accepted action: the units ever on the battlefield with
    // models, those of them never within 12" of an enemy unit with models (destroyed or not), and the battle round of
    // the side's first engagement (0: none).
    std::array<int, 2> units{};
    std::array<int, 2> never_within_12{};
    std::array<int, 2> first_engagement_round{};
    // M87 (D-086), not by side: the game's largest connected group of engaged units at a Fight phase's start, in units
    // and as a share of the units then on the battlefield (each its maximum over the game).
    int largest_cluster = 0;
    double largest_cluster_share = 0.0;
    // M87 (D-086): under a mission, the VP each side scored at each objective area ([area][side], VictoryPointsScored).
    std::vector<std::array<int, 2>> area_vp;
    // M88 (D-087-D-089), by the shooting side: its ranged weapon selections (ShootingStarted) and those whose target
    // had the benefit of cover (13.08); and, at each of its Shoots, the enemy units within the unit's longest range
    // that none of its models could see because of 13.09 Hidden or of 13.10 / 13.11 terrain (core's
    // visibility_problem). Counted only on a battlefield with terrain.
    std::array<int, 2> ranged_selections{};
    std::array<int, 2> covered_selections{};
    std::array<int, 2> refused_hidden{};
    std::array<int, 2> refused_terrain{};
};

// Counts TeamworkStats from the accepted actions' events and the state after each one.
class TeamworkCounter {
public:
    // The state before any action (the game started), then after every accepted action with that action's events.
    void observe(const std::vector<core::GameEvent>& events, const core::GameState& after);
    // M88: the state just before `unit` shoots (a Shoot action about to be submitted): the targets refused for 13.09 or
    // for terrain. Nothing without terrain.
    void observe_shoot(const core::GameState& before, core::UnitId unit);
    // The statistics so far (a shooting phase still open counts as ended).
    [[nodiscard]] TeamworkStats stats() const;

private:
    void flush_shooting();
    TeamworkStats stats_;
    std::array<std::vector<std::uint32_t>, 2> ganged_;  // distinct enemy unit ids ganged up on, sorted
    std::array<std::vector<std::pair<std::uint32_t, int>>, 2> phase_shots_;  // target id, selections (this phase)
    std::vector<std::uint32_t> seen_;       // units ever on the battlefield with models, sorted
    std::vector<std::uint32_t> contacted_;  // of those, the ones that came within 12" of an enemy unit, sorted
};

struct GameRecord {
    std::uint64_t seed = 0;
    bool finished = false;              // state().result is set
    std::optional<core::Side> winner;   // nullopt: a draw (or unfinished)
    bool on_points = false;             // M67 (D-076): decided on points destroyed after the last battle round
    // M68 (D-077): in a points scenario, the points each side destroyed by the game's end (indexed by the side that
    // destroyed them, core::points_destroyed as a decimal), whatever decided it; 0 otherwise.
    std::array<double, 2> points_destroyed{};
    // M86 (D-084): in a mission game, decided on victory points after the last battle round (on_points then means equal
    // VP decided on points destroyed), and each side's objective and kill VP by the game's end, whatever decided it.
    bool on_vp = false;
    std::array<int, 2> objective_vp{};
    std::array<int, 2> kill_vp{};
    int end_round = 0;
    std::array<int, 2> surviving_models{};  // indexed by side_index
    std::array<int, 2> wounds_lost{};       // M41: wounds each side's models lost (destroyed models count all theirs)
    std::array<int, 2> coherency_removed{};  // M41: models each side removed to regain coherency (03.03)
    std::array<int, 2> fight_move_steps{};   // M46: bot decisions in a Pile In or Consolidate step (12.02, 12.07)
    std::array<int, 2> fight_moves{};        // M46: pile-ins and consolidations committed (12.03, 12.08)
    std::size_t steps = 0;                 // accepted actions, the client's automatic ones included
    std::size_t rejected = 0;               // bot actions the session refused
    std::size_t reverted = 0;               // UnitMoveReverted events
    std::size_t fallbacks = 0;              // decisions the bots answered with the fallback
    std::vector<std::string> problems;      // why the game stopped early, if it did
    TeamworkStats teamwork;                 // M78 (D-083 part 0); not on the --per-game line
};

// One game from `scenario` (not started) with the dice `seed`, side A played by `a` and side B by `b`. Stops at
// the game's end, at the first rejected action, when a bot stops, or after `step_cap` actions (unfinished).
// `decision_ms`, if given, gets each bot decision's time in milliseconds; `trace`, if given, a line per bot
// decision ("R1 A Shoot: <reason>"). M68: `decision_sides`, if given, the side of each decision timed.
[[nodiscard]] GameRecord play_bot_game(const core::GameState& scenario, std::uint64_t seed, BotConfig a, BotConfig b,
                                       std::size_t step_cap = 5000, std::vector<double>* decision_ms = nullptr,
                                       std::vector<std::string>* trace = nullptr,
                                       std::vector<core::Side>* decision_sides = nullptr);

// M45: the same, on a session the caller owns (the bot-game replay is built from its history).
[[nodiscard]] GameRecord play_bot_session(client::GameSession& session, BotConfig a, BotConfig b,
                                          std::size_t step_cap = 5000, std::vector<double>* decision_ms = nullptr,
                                          std::vector<std::string>* trace = nullptr,
                                          std::vector<core::Side>* decision_sides = nullptr);

struct ArenaResult {
    std::vector<GameRecord> games;
    std::array<int, 2> wins{};  // by side
    int draws = 0;
    std::array<int, 2> points_wins{};  // M67 (D-076): of those, the wins and draws decided on points destroyed
    int points_draws = 0;
    std::array<double, 2> mean_points_destroyed{};  // M68 (D-077): by the side that destroyed them
    std::array<int, 2> vp_wins{};                   // M86 (D-084): the wins decided on victory points
    std::array<double, 2> mean_objective_vp{};      // M86 (D-084): by side
    std::array<double, 2> mean_kill_vp{};
    int unfinished = 0;
    std::size_t rejected = 0;
    std::size_t reverted = 0;
    double mean_end_round = 0.0;
    std::array<double, 2> mean_surviving{};
    std::array<double, 2> mean_wounds_lost{};  // M41: by side; side A's is the damage side B dealt
    std::array<int, 2> coherency_removed{};     // M41: total models removed for coherency, by side
    std::array<int, 2> fight_move_steps{};      // M46: totals, by side
    std::array<int, 2> fight_moves{};           // M46: totals, by side
    // M78 (D-083 part 0): every game's TeamworkStats summed (first_engagement_round: the rounds summed over the games
    // with an engagement), and by side the games with an engagement.
    TeamworkStats teamwork;
    std::array<int, 2> engaged_games{};
    std::size_t fallbacks = 0;
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    double max_ms = 0.0;  // M43: the slowest decision (M44 has a 500 ms p95 budget)
    // M68: the same for each side's bot alone (indexed by side_index).
    std::array<double, 2> side_p50_ms{};
    std::array<double, 2> side_p95_ms{};
    std::array<double, 2> side_max_ms{};
    [[nodiscard]] bool clean() const noexcept { return rejected == 0 && reverted == 0 && unfinished == 0; }
    // M44: the mean game result from `side`'s point of view (win +1, draw 0, loss -1; an unfinished game 0).
    [[nodiscard]] double mean_result(core::Side side) const noexcept {
        if (games.empty()) return 0.0;
        const int own = wins[core::side_index(side)];
        const int other = wins[core::side_index(core::opponent(side))];
        return static_cast<double>(own - other) / static_cast<double>(games.size());
    }
};

// M72: the initial state (not started) of the game with a seed. A fixed scenario gives the same state for every seed;
// --random-armies a custom battle of random armies per seed (random_armies_battle).
using ScenarioForSeed = std::function<core::GameState(std::uint64_t seed)>;

// `games` games with seeds first_seed, first_seed + 1, ...
[[nodiscard]] ArenaResult run_arena(const core::GameState& scenario, int games, std::uint64_t first_seed, BotConfig a,
                                    BotConfig b, std::size_t step_cap = 5000);
// M72: the same, each game from scenario_for(its seed).
[[nodiscard]] ArenaResult run_arena(const ScenarioForSeed& scenario_for, int games, std::uint64_t first_seed,
                                    BotConfig a, BotConfig b, std::size_t step_cap = 5000);

// M54: `games` games as run_arena plays them, reported one line per game (its result and counts, and FNV-1a hashes
// of its accepted actions, its final state as JSON and its bot decisions' trace lines, reasons without times), then
// the decision times and the run's wall time. Two builds that play the same give the same lines.
struct PerGameReport {
    std::string text;
    bool clean = true;  // every game finished, no rejected action, no UnitMoveReverted
};
[[nodiscard]] PerGameReport run_per_game(const core::GameState& scenario, int games, std::uint64_t first_seed,
                                         BotConfig a, BotConfig b, std::size_t step_cap = 5000);
// M72: the same, each game from scenario_for(its seed).
[[nodiscard]] PerGameReport run_per_game(const ScenarioForSeed& scenario_for, int games, std::uint64_t first_seed,
                                         BotConfig a, BotConfig b, std::size_t step_cap = 5000);

// ---- M72 (D-079 part 2): custom battles in the arena (--army-a, --army-b, --first, --random-armies) ----

struct ArmySource {
    std::vector<core::Faction> factions;  // data/factions.json
    core::DatasheetResolver resolve;      // caching, from the repo root
    // M86 (D-084): the objective mission its battles play (a scenario file's "mission" block; the arena's command line
    // gives it data/scenarios/incursion_objectives_44x60.json's). nullopt: decided on points destroyed (D-076).
    std::optional<nlohmann::json> mission;
    // M88 (D-087): the terrain its battles are fought over (a scenario file's "terrain" block, the arena's --terrain).
    // nullopt (the default): a bare battlefield, as before.
    std::optional<nlohmann::json> terrain;
};
[[nodiscard]] ArmySource load_army_source(const std::filesystem::path& repo_root);

// The custom battle of two army lists (custom_battle_scenario through build_game_state). Throws DataError for lists
// that can't be taken (army_problems, battle_problems). M74 (D-081): `mode` Hand (--deploy hand) has the units set up
// by hand in a deployment step; Auto (the default) places them by the auto-layout as before.
[[nodiscard]] core::GameState custom_battle_state(const ArmySource& source, const core::ArmyList& a,
                                                  const core::ArmyList& b, core::Side first_turn,
                                                  core::DeploymentMode mode = core::DeploymentMode::Auto);

// --random-armies 1: the game with `seed` is a custom battle of a random legal Chaos Space Marines army (side A) and
// a random legal Tyranid army (side B), drawn with core::random_army_seed(side, seed).
[[nodiscard]] core::GameState random_armies_battle(const ArmySource& source, std::uint64_t seed, core::Side first_turn,
                                                   core::DeploymentMode mode = core::DeploymentMode::Auto);

// The arena's report: win / draw / loss counts, mean end round, mean surviving models, mean wounds lost per
// side, rejected actions, UnitMoveReverted count, fallbacks, p50 / p95 / max decision time, (M43) the seeds that
// lost models to coherency, (M46) the Pile In / Consolidate steps the bots answered and the moves they made and (M78)
// the teamwork statistics (TeamworkStats) per game.
[[nodiscard]] std::string format_arena(const ArenaResult& r, const core::GameState& scenario);

}  // namespace wh40k::ai
