// M58: the bot-game replay tests/replays/m58_strike_bot_game.json: a whole game played by the bot on both sides,
// level Hard vs Hard, on the 44"x60" Strike scenario (3 Intercessor Squads against 4 Termagant units; D-064, D-067).

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "arena.hpp"
#include "bot.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "generation.hpp"
#include "session.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;

namespace {

// Dice seed 11 (Release arena, Hard vs Hard, seeds 1-200 on the Strike scenario, D-067 "Tuning (M58)"): a game of
// 159 steps over in battle round 3, a Space Marine wipe-out (D-007), with both sides shooting and taking casualties,
// a charge, pile-ins on both sides and a fight. Shorter games (seed 12: 89 steps, round 2) have no charge and no
// Space Marine losses; the mean is 183 steps.
constexpr std::uint64_t kStrikeSeed = 11;
constexpr const char* kStrikeScenario = "data/scenarios/strike_44x60.json";
constexpr const char* kStrikeFile = "m58_strike_bot_game.json";
constexpr std::size_t kStepCap = 5000;  // the arena's (M39); the saved game ends in a result well before it

constexpr int kCheckedRounds = 1;  // TheSavedFileMatchesTheGame replays the bots' choices through this round

std::filesystem::path saved_path() { return std::filesystem::path(WH40K_REPLAY_DIR) / kStrikeFile; }

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

// Plays the game through GameSession (every action validated; the client's automatic skips included, nothing
// auto-allocated for a bot) and turns its history into a scenario-form replay, as M45 does. Replaying it needs only
// core. `step_cap` below the game's length stops it early: the history is then a prefix of the whole game's.
// D-083 part 3 (M81): `generation`, the bots' (Generation::M77: the pre-batch Hard).
BotGame play_strike_bot_game(std::uint64_t seed = kStrikeSeed, std::size_t step_cap = kStepCap,
                             Generation generation = Generation::Current) {
    BotGame g;
    g.replay.description =
        "M58: a whole game played by the bot on both sides, level Hard vs Hard, from the 44\"x60\" Strike scenario "
        "(3 Intercessor Squads against 4 Termagant units) with dice seed " +
        std::to_string(seed) + " (D-067). Replaying it needs only the engine.";
    g.replay.initial.scenario = kStrikeScenario;
    g.replay.initial.seed = seed;
    client::GameSession session(replay_initial_state(g.replay, wh40k::test::repo_root()));
    BotConfig hard{BotLevel::Hard};
    hard.generation = generation;
    g.record = play_bot_session(session, hard, hard, step_cap);
    g.replay.steps = session.history();
    g.replay.expect = replay_expectation(session.state());
    return g;
}

}  // namespace

TEST(M58_Replay, TheStrikeBotGameReplaysToItsResultWithCoreOnly) {
    const Replay saved = load_replay(saved_path());
    ASSERT_TRUE(saved.initial.scenario.has_value());
    EXPECT_EQ(*saved.initial.scenario, kStrikeScenario);
    EXPECT_EQ(saved.initial.seed, kStrikeSeed);
    EXPECT_LT(saved.steps.size(), kStepCap);

    // The whole file, through the engine alone, twice: every step valid, the same final state, the saved expect block.
    const ReplayResult first = run_replay(saved, wh40k::test::repo_root());
    ASSERT_TRUE(first.ok()) << "step " << *first.failed_step << ": " << first.reasons.front();
    EXPECT_EQ(replay_expectation(first.final_state), saved.expect);
    const ReplayResult second = run_replay(saved, wh40k::test::repo_root());
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(first.final_state, second.final_state);  // deterministic

    // It ends in a result (D-007), not at the step cap; several units a side (D-064).
    ASSERT_TRUE(first.final_state.result.has_value());
    EXPECT_EQ(first.final_state.flow.stage, Stage::GameOver);
    // D-083 part 3 (M81): seed 11's game with the current Hard (focus fire) is a Space Marine win in battle round 4; the
    // pre-batch game, a wipe-out in battle round 3, is checked by ThePreBatchGameIsAWipeOutInBattleRoundThree.
    EXPECT_EQ(first.final_state.result->winner, Side::A);
    EXPECT_EQ(first.final_state.flow.battle_round, 4);
    EXPECT_EQ(first.final_state.units.size(), 7u);
    const auto units_of = [&](Side side) {
        return std::count_if(first.final_state.units.begin(), first.final_state.units.end(),
                             [&](const Unit& u) { return u.side == side; });
    };
    EXPECT_EQ(units_of(Side::A), 3);
    EXPECT_EQ(units_of(Side::B), 4);
    // Both sides decided for themselves: actions from each side are in the file.
    const auto has_side = [&](Side side) {
        return std::any_of(saved.steps.begin(), saved.steps.end(), [&](const ReplayStep& s) { return s.side == side; });
    };
    EXPECT_TRUE(has_side(Side::A));
    EXPECT_TRUE(has_side(Side::B));
}

TEST(M58_Replay, ThePreBatchGameIsAWipeOutInBattleRoundThree) {
    // The pre-batch Hard (Generation::M77) with the replay's seed: the whole game (about 60 s in Debug), replayed through
    // the engine alone, with the assertions the saved file was checked with before M81 (D-083 part 3).
    const BotGame g = play_strike_bot_game(kStrikeSeed, kStepCap, Generation::M77);
    ASSERT_TRUE(g.record.finished);
    EXPECT_EQ(g.record.rejected, 0u);
    const ReplayResult r = run_replay(g.replay, wh40k::test::repo_root());
    ASSERT_TRUE(r.ok());
    ASSERT_TRUE(r.final_state.result.has_value());
    EXPECT_EQ(r.final_state.flow.stage, Stage::GameOver);
    EXPECT_EQ(r.final_state.result->winner, Side::A);  // seed 11: the Tyranids wiped out in battle round 3
    EXPECT_EQ(r.final_state.flow.battle_round, 3);
}

TEST(M58_Replay, TheSavedFileMatchesTheGame) {
    // Hard vs Hard with the replay's seed plays exactly the saved actions now (D-054: the bots are deterministic),
    // with nothing rejected, reverted or answered by the fallback. The whole game takes about 30 s in a Debug build
    // (Hard's army lookahead), so this checks the actions of its first kCheckedRounds battle rounds (64 of the 159
    // steps, about 14 s in Debug), every decision of both sides in them. The replay test above covers the whole
    // file; the whole game is checked by DISABLED_TheWholeSavedFileMatchesTheGame and by regenerating the file
    // (git diff tests/replays).
    const Replay saved = load_replay(saved_path());
    const std::size_t checked = steps_before_round(saved, kCheckedRounds + 1);
    ASSERT_GT(checked, 0u);
    ASSERT_LT(checked, saved.steps.size());
    // Both sides' turns of battle round 1: every Hard decision of each side in it.
    EXPECT_TRUE(std::any_of(saved.steps.begin(), saved.steps.begin() + static_cast<std::ptrdiff_t>(checked),
                            [](const ReplayStep& s) { return s.side == Side::B; }));
    const BotGame g = play_strike_bot_game(kStrikeSeed, checked);
    EXPECT_EQ(g.record.rejected, 0u);
    EXPECT_EQ(g.record.reverted, 0u);
    EXPECT_EQ(g.record.fallbacks, 0u);
    ASSERT_GE(g.replay.steps.size(), checked);
    for (std::size_t i = 0; i < checked; ++i) {
        ASSERT_EQ(Json(saved.steps[i]).dump(), Json(g.replay.steps[i]).dump()) << "step " << i;
    }
}

// The whole game, as the M47 test checks its file (about 30 s in Debug). Run on purpose:
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M58_Replay.DISABLED_TheWholeSavedFileMatchesTheGame
TEST(M58_Replay, DISABLED_TheWholeSavedFileMatchesTheGame) {
    const Replay saved = load_replay(saved_path());
    const BotGame g = play_strike_bot_game();
    EXPECT_TRUE(g.record.finished);
    EXPECT_EQ(g.record.rejected, 0u);
    EXPECT_EQ(g.record.reverted, 0u);
    EXPECT_EQ(g.record.fallbacks, 0u);
    EXPECT_EQ(Json(saved).dump(2), Json(g.replay).dump(2));
}

// Writes tests/replays/m58_strike_bot_game.json. Run on purpose only:
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M58_Replay.DISABLED_RegenerateStrikeBotGameReplay
TEST(M58_Replay, DISABLED_RegenerateStrikeBotGameReplay) {
    const BotGame g = play_strike_bot_game();
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
