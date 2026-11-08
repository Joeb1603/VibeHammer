#pragma once

// M39 (D-054): bot vs bot games, headless, through GameSession (so they take the client's own submit path:
// every action validated, the client's automatic skips run with auto-allocation left to the bots). Counts
// results, rejected actions and UnitMoveReverted events (both must be zero), and how long decisions take
// (measured with a steady clock here only, never inside a decision).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "bot.hpp"
#include "core/state/game_state.hpp"
#include "session.hpp"

namespace wh40k::ai {

struct GameRecord {
    std::uint64_t seed = 0;
    bool finished = false;              // state().result is set
    std::optional<core::Side> winner;   // nullopt: a draw (or unfinished)
    bool on_points = false;             // M67 (D-076): decided on points destroyed after the last battle round
    // M68 (D-077): in a points scenario, the points each side destroyed by the game's end (indexed by the side that
    // destroyed them, core::points_destroyed as a decimal), whatever decided it; 0 otherwise.
    std::array<double, 2> points_destroyed{};
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
    int unfinished = 0;
    std::size_t rejected = 0;
    std::size_t reverted = 0;
    double mean_end_round = 0.0;
    std::array<double, 2> mean_surviving{};
    std::array<double, 2> mean_wounds_lost{};  // M41: by side; side A's is the damage side B dealt
    std::array<int, 2> coherency_removed{};     // M41: total models removed for coherency, by side
    std::array<int, 2> fight_move_steps{};      // M46: totals, by side
    std::array<int, 2> fight_moves{};           // M46: totals, by side
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

// `games` games with seeds first_seed, first_seed + 1, ...
[[nodiscard]] ArenaResult run_arena(const core::GameState& scenario, int games, std::uint64_t first_seed, BotConfig a,
                                    BotConfig b, std::size_t step_cap = 5000);

// M54: `games` games as run_arena plays them, reported one line per game (its result and counts, and FNV-1a hashes
// of its accepted actions, its final state as JSON and its bot decisions' trace lines, reasons without times), then
// the decision times and the run's wall time. Two builds that play the same give the same lines.
struct PerGameReport {
    std::string text;
    bool clean = true;  // every game finished, no rejected action, no UnitMoveReverted
};
[[nodiscard]] PerGameReport run_per_game(const core::GameState& scenario, int games, std::uint64_t first_seed,
                                         BotConfig a, BotConfig b, std::size_t step_cap = 5000);

// The arena's report: win / draw / loss counts, mean end round, mean surviving models, mean wounds lost per
// side, rejected actions, UnitMoveReverted count, fallbacks, p50 / p95 / max decision time, (M43) the seeds that
// lost models to coherency and (M46) the Pile In / Consolidate steps the bots answered and the moves they made.
[[nodiscard]] std::string format_arena(const ArenaResult& r, const core::GameState& scenario);

}  // namespace wh40k::ai
