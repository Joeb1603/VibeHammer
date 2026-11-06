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

namespace wh40k::ai {

struct GameRecord {
    std::uint64_t seed = 0;
    bool finished = false;              // state().result is set
    std::optional<core::Side> winner;   // nullopt: a draw (or unfinished)
    int end_round = 0;
    std::array<int, 2> surviving_models{};  // indexed by side_index
    std::size_t steps = 0;                  // accepted actions, the client's automatic ones included
    std::size_t rejected = 0;               // bot actions the session refused
    std::size_t reverted = 0;               // UnitMoveReverted events
    std::size_t fallbacks = 0;              // decisions the bots answered with the fallback
    std::vector<std::string> problems;      // why the game stopped early, if it did
};

// One game from `scenario` (not started) with the dice `seed`, side A played by `a` and side B by `b`. Stops at
// the game's end, at the first rejected action, when a bot stops, or after `step_cap` actions (unfinished).
// `decision_ms`, if given, gets each bot decision's time in milliseconds.
[[nodiscard]] GameRecord play_bot_game(const core::GameState& scenario, std::uint64_t seed, BotConfig a, BotConfig b,
                                       std::size_t step_cap = 5000, std::vector<double>* decision_ms = nullptr);

struct ArenaResult {
    std::vector<GameRecord> games;
    std::array<int, 2> wins{};  // by side
    int draws = 0;
    int unfinished = 0;
    std::size_t rejected = 0;
    std::size_t reverted = 0;
    double mean_end_round = 0.0;
    std::array<double, 2> mean_surviving{};
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    [[nodiscard]] bool clean() const noexcept { return rejected == 0 && reverted == 0 && unfinished == 0; }
};

// `games` games with seeds first_seed, first_seed + 1, ...
[[nodiscard]] ArenaResult run_arena(const core::GameState& scenario, int games, std::uint64_t first_seed, BotConfig a,
                                    BotConfig b, std::size_t step_cap = 5000);

// The arena's report: win / draw / loss counts, mean end round, mean surviving models, rejected actions,
// UnitMoveReverted count and p50 / p95 decision time.
[[nodiscard]] std::string format_arena(const ArenaResult& r, const core::GameState& scenario);

}  // namespace wh40k::ai
