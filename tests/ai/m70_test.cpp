// M70: the bot-game replay tests/replays/m70_rosters_bot_game.json: a whole game played by the bot on both sides, level
// Hard vs Hard, on the 44"x60" portrait roster scenario (Chaos Space Marines against Tyranids, decided on points
// destroyed after battle round 5 unless one side is wiped out; D-076, D-078). As M58's Strike replay: the file replays
// with core alone, the bots play exactly the saved actions now, and a regenerate test writes it. The arena's two Hard
// bots share one chain cache (D-078); a cache never changes a decision, so two bots with caches of their own play the
// same actions.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "arena.hpp"
#include "auto_actions.hpp"
#include "bot.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "session.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;

namespace {

// Dice seed 93 (Release, Hard vs Hard over seeds 1-100, DISABLED_SearchRostersSeed below): a whole game of 501 steps,
// decided on points destroyed after battle round 5 (a Tyranid win). It is the only seed of the 100 in which both sides
// fight in melee and each side loses a whole unit. Hard vs Hard rarely fights there (Hard's Chaos Space Marines hold
// back and shoot, and its 9-unit Tyranid side moves as Normal does, D-078): 8 seeds of the 100 have a Fight from each
// side, and in the others the Chaos Space Marines lose no unit.
constexpr std::uint64_t kRostersSeed = 93;
constexpr const char* kRostersFile = "m70_rosters_bot_game.json";
constexpr std::size_t kStepCap = 5000;  // the arena's (M39); the saved game ends in a result well before it

constexpr int kCheckedRounds = 1;  // the Debug tests replay the bots' choices through this battle round

std::filesystem::path saved_path() { return std::filesystem::path(WH40K_REPLAY_DIR) / kRostersFile; }

// How many of the replay's steps come before battle round `round` starts (all of them if it never does).
std::size_t steps_before_round(const Replay& replay, int round) {
    GameState gs = replay_initial_state(replay, wh40k::test::repo_root());
    (void)start_game(gs);
    for (std::size_t i = 0; i < replay.steps.size(); ++i) {
        if (gs.flow.battle_round >= round) return i;
        (void)apply_action(gs, replay.steps[i].side, replay.steps[i].action);
    }
    return replay.steps.size();
}

struct BotGame {
    Replay replay;
    GameRecord record;
};

Replay rosters_replay_header(std::uint64_t seed) {
    Replay r;
    r.description =
        "M70: a whole game played by the bot on both sides, level Hard vs Hard, from the 44\"x60\" portrait roster "
        "scenario (Chaos Space Marines against Tyranids, decided on points destroyed after battle round 5 unless one "
        "side is wiped out) with dice seed " +
        std::to_string(seed) + " (D-076, D-078). Replaying it needs only the engine.";
    r.initial.scenario = wh40k::test::kRostersScenario;
    r.initial.seed = seed;
    return r;
}

// The arena's play (play_bot_session: both Hard bots share one chain cache, D-078) through GameSession (every action
// validated, the client's automatic skips included, nothing auto-allocated for a bot), turned into a scenario-form
// replay as M45 and M58 do. `step_cap` below the game's length stops it early: the history is then a prefix.
BotGame play_rosters_bot_game(std::uint64_t seed = kRostersSeed, std::size_t step_cap = kStepCap) {
    BotGame g;
    g.replay = rosters_replay_header(seed);
    client::GameSession session(replay_initial_state(g.replay, wh40k::test::repo_root()));
    g.record = play_bot_session(session, BotConfig{BotLevel::Hard}, BotConfig{BotLevel::Hard}, step_cap);
    g.replay.steps = session.history();
    g.replay.expect = replay_expectation(session.state());
    return g;
}

// The same game with two Hard bots that each keep a chain cache of their own (not shared): the same loop as
// play_bot_session (the client's automatic skips, then the side to act decides and the session validates).
struct SeparateGame {
    std::vector<ReplayStep> steps;
    std::size_t rejected = 0;
    std::size_t fallbacks = 0;
    bool finished = false;
};
SeparateGame play_with_separate_caches(std::uint64_t seed, std::size_t step_cap) {
    const Replay header = rosters_replay_header(seed);
    client::GameSession session(replay_initial_state(header, wh40k::test::repo_root()));
    std::array<Bot, 2> bots{Bot(BotConfig{BotLevel::Hard}), Bot(BotConfig{BotLevel::Hard})};
    client::AutoSettings autos;
    autos.skip_empty_steps = true;
    autos.auto_allocate = false;
    autos.bot_controlled = {true, true};
    SeparateGame out;
    while (session.pending() && session.history().size() < step_cap) {
        (void)client::run_auto_actions(session, autos);
        if (!session.pending()) break;
        const Side side = session.pending()->side;
        const BotChoice choice = bots[side_index(side)].decide(session.state(), side);
        if (choice.stopped || !session.submit(choice.action).ok()) {
            ++out.rejected;
            break;
        }
    }
    out.steps = session.history();
    out.finished = session.state().result.has_value();
    out.fallbacks = bots[0].fallbacks() + bots[1].fallbacks();
    return out;
}

}  // namespace

TEST(M70_Replay, TheRostersBotGameReplaysToItsResultWithCoreOnly) {
    const Replay saved = load_replay(saved_path());
    ASSERT_TRUE(saved.initial.scenario.has_value());
    EXPECT_EQ(*saved.initial.scenario, wh40k::test::kRostersScenario);
    EXPECT_EQ(saved.initial.seed, kRostersSeed);
    EXPECT_LT(saved.steps.size(), kStepCap);

    // The whole file, through the engine alone, twice: every step valid, the same final state, the saved expect block.
    const ReplayResult first = run_replay(saved, wh40k::test::repo_root());
    ASSERT_TRUE(first.ok()) << "step " << *first.failed_step << ": " << first.reasons.front();
    EXPECT_EQ(replay_expectation(first.final_state), saved.expect);
    const ReplayResult second = run_replay(saved, wh40k::test::repo_root());
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(first.final_state, second.final_state);  // deterministic

    // It ends in a result, not at the step cap: on points destroyed after battle round 5 (D-076).
    const GameState& end = first.final_state;
    ASSERT_TRUE(end.result.has_value());
    EXPECT_EQ(end.flow.stage, Stage::GameOver);
    EXPECT_TRUE(end.result->points.has_value());
    EXPECT_EQ(end.flow.battle_round, 5);
    EXPECT_EQ(end.units.size(), 14u);  // 5 Chaos Space Marine units and 9 Tyranid units

    // Both sides fight in melee, and each side loses at least one whole unit.
    const auto has = [&](auto pred) { return std::any_of(saved.steps.begin(), saved.steps.end(), pred); };
    for (const Side side : {Side::A, Side::B}) {
        SCOPED_TRACE(side == Side::A ? "side A" : "side B");
        EXPECT_TRUE(has([&](const ReplayStep& s) { return s.side == side && std::holds_alternative<action::Fight>(s.action); }));
        EXPECT_TRUE(std::any_of(end.units.begin(), end.units.end(),
                                [&](const Unit& u) { return u.side == side && u.models.empty(); }));
    }
}

TEST(M70_Replay, TheSavedFileMatchesTheGame) {
    // Hard vs Hard with the replay's seed plays exactly the saved actions now (D-054: the bots are deterministic), with
    // nothing rejected, reverted or answered by the fallback. The whole game is too slow for a Debug ctest, so this
    // checks every decision of both sides through battle round kCheckedRounds; the replay test above covers the whole
    // file, and DISABLED_TheWholeSavedFileMatchesTheGame and regenerating the file (git diff tests/replays) the whole
    // game.
    const Replay saved = load_replay(saved_path());
    const std::size_t checked = steps_before_round(saved, kCheckedRounds + 1);
    ASSERT_GT(checked, 0u);
    ASSERT_LT(checked, saved.steps.size());
    EXPECT_TRUE(std::any_of(saved.steps.begin(), saved.steps.begin() + static_cast<std::ptrdiff_t>(checked),
                            [](const ReplayStep& s) { return s.side == Side::B; }));
    const BotGame g = play_rosters_bot_game(kRostersSeed, checked);
    EXPECT_EQ(g.record.rejected, 0u);
    EXPECT_EQ(g.record.reverted, 0u);
    EXPECT_EQ(g.record.fallbacks, 0u);
    ASSERT_GE(g.replay.steps.size(), checked);
    for (std::size_t i = 0; i < checked; ++i) {
        ASSERT_EQ(Json(saved.steps[i]).dump(), Json(g.replay.steps[i]).dump()) << "step " << i;
    }
}

TEST(M70_Replay, BotsWithChainCachesOfTheirOwnPlayTheSameActions) {
    // D-078: the arena's two Hard bots share one chain cache; a cache never changes a decision (D-054), so two bots that
    // each build their own tables play the saved actions too (through battle round kCheckedRounds here; the whole game
    // in DISABLED_TheWholeSavedFileMatchesTheGame).
    const Replay saved = load_replay(saved_path());
    const std::size_t checked = steps_before_round(saved, kCheckedRounds + 1);
    ASSERT_GT(checked, 0u);
    const SeparateGame g = play_with_separate_caches(kRostersSeed, checked);
    EXPECT_EQ(g.rejected, 0u);
    EXPECT_EQ(g.fallbacks, 0u);
    ASSERT_GE(g.steps.size(), checked);
    for (std::size_t i = 0; i < checked; ++i) {
        ASSERT_EQ(Json(saved.steps[i]).dump(), Json(g.steps[i]).dump()) << "step " << i;
    }
}

// The whole game, with the shared cache (the arena's play) and with separate caches. Run on purpose:
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M70_Replay.DISABLED_TheWholeSavedFileMatchesTheGame
TEST(M70_Replay, DISABLED_TheWholeSavedFileMatchesTheGame) {
    const Replay saved = load_replay(saved_path());
    const BotGame g = play_rosters_bot_game();
    EXPECT_TRUE(g.record.finished);
    EXPECT_EQ(g.record.rejected, 0u);
    EXPECT_EQ(g.record.reverted, 0u);
    EXPECT_EQ(g.record.fallbacks, 0u);
    EXPECT_EQ(Json(saved).dump(2), Json(g.replay).dump(2));
    const SeparateGame s = play_with_separate_caches(kRostersSeed, kStepCap);
    EXPECT_TRUE(s.finished);
    EXPECT_EQ(s.rejected, 0u);
    EXPECT_EQ(s.fallbacks, 0u);
    ASSERT_EQ(s.steps.size(), saved.steps.size());
    for (std::size_t i = 0; i < saved.steps.size(); ++i) {
        ASSERT_EQ(Json(saved.steps[i]).dump(), Json(s.steps[i]).dump()) << "step " << i;
    }
}

// How the seed was chosen (Release, about 13 minutes): Hard vs Hard over seeds 1-100, printing for each game its length,
// winner, the Fight actions of each side and the units each side lost. Run on purpose only:
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M70_Replay.DISABLED_SearchRostersSeed
TEST(M70_Replay, DISABLED_SearchRostersSeed) {
    for (std::uint64_t seed = 1; seed <= 100; ++seed) {
        const BotGame g = play_rosters_bot_game(seed);
        const ReplayResult r = run_replay(g.replay, wh40k::test::repo_root());
        ASSERT_TRUE(r.ok());
        std::array<int, 2> fights{};
        std::array<int, 2> lost{};
        for (const ReplayStep& s : g.replay.steps) {
            if (std::holds_alternative<action::Fight>(s.action)) ++fights[side_index(s.side)];
        }
        for (const Unit& u : r.final_state.units) {
            if (u.models.empty()) ++lost[side_index(u.side)];
        }
        const std::string winner = !r.final_state.result         ? "unfinished"
                                   : !r.final_state.result->winner ? "draw"
                                   : *r.final_state.result->winner == Side::A ? "A"
                                                                               : "B";
        std::printf("seed %llu: steps %zu winner %s round %d fights A %d B %d units lost A %d B %d\n",
                    static_cast<unsigned long long>(seed), g.replay.steps.size(), winner.c_str(),
                    r.final_state.flow.battle_round, fights[0], fights[1], lost[0], lost[1]);
    }
}

// Writes tests/replays/m70_rosters_bot_game.json. Run on purpose only:
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M70_Replay.DISABLED_RegenerateRostersBotGameReplay
TEST(M70_Replay, DISABLED_RegenerateRostersBotGameReplay) {
    const BotGame g = play_rosters_bot_game();
    ASSERT_TRUE(g.record.finished);  // a result, not the step cap
    ASSERT_EQ(g.record.rejected, 0u);
    ASSERT_EQ(g.record.reverted, 0u);
    ASSERT_EQ(g.record.fallbacks, 0u);
    const ReplayResult r = run_replay(g.replay, wh40k::test::repo_root());
    ASSERT_TRUE(r.ok()) << "step " << *r.failed_step << ": " << r.reasons.front();
    ASSERT_TRUE(r.final_state.result.has_value());
    if (::testing::Test::HasFailure()) return;  // never overwrite the committed replay with a broken game
    save_replay(g.replay, saved_path());
}
