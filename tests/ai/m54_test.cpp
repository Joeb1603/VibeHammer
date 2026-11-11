// M54: speed for big games with no change in play. The simulator reused with reset() steps exactly as a new one,
// the driver's Instant speed on the worker thread hands over the synchronous bot's actions, and the arena's
// per-game report (the before/after comparison) is a function of the games alone.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "arena.hpp"
#include "auto_actions.hpp"
#include "bot.hpp"
#include "bot_driver.hpp"
#include "core/engine/engine.hpp"
#include "session.hpp"
#include "simulator.hpp"
#include "state_hash.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::TestRng;
namespace client = wh40k::client;

namespace {

GameState strike_start(std::uint64_t seed) {
    const auto root = wh40k::test::repo_root();
    GameState gs = load_scenario(root / "data/scenarios/strike_44x60.json", root);
    gs.dice.seed = seed;
    start_game(gs);
    return gs;
}

// The text of a per-game report without its timing line.
std::string games_only(const std::string& report) {
    std::string out;
    std::size_t at = 0;
    while (at < report.size()) {
        const std::size_t end = report.find('\n', at);
        const std::string line = report.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (line.rfind("game ", 0) == 0) out += line + "\n";
        if (end == std::string::npos) break;
        at = end + 1;
    }
    return out;
}

}  // namespace

TEST(M54_Simulator, ResetIsTheSameAsANewSimulator) {
    // One simulator reset between lines (charge odds, advance odds and Normal's move candidates use it) steps
    // exactly as a fresh one: the same states, the same bot dice, the same step count.
    const GameState a = strike_start(54);
    GameState b = a;
    TestRng walk(54);
    for (int i = 0; i < 60 && b.pending; ++i) {
        (void)apply_action(b, b.pending->side, wh40k::test::random_action(b, walk));
    }
    Simulator reused(b, 1);  // starts somewhere else entirely, and makes a few steps first
    for (int i = 0; i < 5 && reused.state().pending; ++i) {
        (void)reused.step(legal_actions(reused.state(), reused.state().pending->side).front());
    }
    for (const std::uint64_t seed : {7u, 8u, 9u}) {
        for (const GameState* origin : {&a, static_cast<const GameState*>(&b)}) {
            reused.reset(*origin, seed);
            Simulator fresh(*origin, seed);
            ASSERT_EQ(reused.state(), fresh.state());
            ASSERT_EQ(reused.steps(), 0u);
            TestRng r1(seed);
            TestRng r2(seed);
            for (int i = 0; i < 150 && fresh.state().pending && !fresh.state().result; ++i) {
                const GameAction x = wh40k::test::random_action(fresh.state(), r1);
                const GameAction y = wh40k::test::random_action(reused.state(), r2);
                ASSERT_EQ(x, y);
                ASSERT_EQ(fresh.step(x).ok(), reused.step(y).ok());
                ASSERT_EQ(fresh.state(), reused.state()) << "step " << i;
            }
            EXPECT_EQ(fresh.steps(), reused.steps());
        }
    }
}

TEST(M54_Driver, InstantOnTheWorkerHandsOverTheSynchronousBotsActions) {
    // The client's Instant speed decides on the worker thread and waits for it a frame at most
    // (wait_for_decision): whole Normal-vs-Normal games get the actions a synchronous bot gives, step for step.
    for (const std::uint64_t seed : {3u, 4u}) {
        client::GameSession live = client::GameSession::new_game(wh40k::test::load_v1_scenario(), seed);
        client::GameSession mirror = client::GameSession::new_game(wh40k::test::load_v1_scenario(), seed);
        BotDriver d(BotConfig{}, BotSpeed::Instant, BotDriver::Threading::Worker);
        std::array<Bot, 2> sync{Bot(BotConfig{}), Bot(BotConfig{})};
        client::AutoSettings autos;
        autos.bot_controlled = {true, true};
        autos.auto_allocate = false;
        autos.skip_empty_steps = true;
        int decisions = 0;
        double t = 0.0;
        while (live.pending() && live.history().size() < 3000) {
            (void)client::run_auto_actions(live, autos);
            (void)client::run_auto_actions(mirror, autos);
            ASSERT_EQ(live.state(), mirror.state());
            if (!live.pending()) break;
            DriverInputs in;
            in.state = &live.state();
            in.step = live.history().size();
            in.bot_sides = {true, true};
            std::optional<BotChoice> c = d.tick(t += 0.016, in);
            for (int wait = 0; !c && wait < 2000; ++wait) {
                if (d.wait_for_decision(0.015)) c = d.tick(t, in);
            }
            ASSERT_TRUE(c.has_value()) << "no decision after waiting";
            const Side side = mirror.pending()->side;
            const BotChoice expected = sync[side_index(side)].decide(mirror.state(), side);
            ASSERT_EQ(c->action, expected.action) << "seed " << seed << ", decision " << decisions;
            ASSERT_TRUE(live.submit(c->action).ok());
            ASSERT_TRUE(mirror.submit(expected.action).ok());
            ++decisions;
            EXPECT_FALSE(d.wait_for_decision(0.0));  // handed over: nothing waiting until the next tick
        }
        EXPECT_GT(decisions, 20) << "seed " << seed;
    }
}

TEST(M54_Arena, ThePerGameReportIsAFunctionOfTheGames) {
    // Two runs give the same lines (only the timing line differs), one per game, each with its hashes.
    const GameState v1 = wh40k::test::load_v1_scenario();
    const PerGameReport first = run_per_game(v1, 3, 5, BotConfig{}, BotConfig{});
    const PerGameReport second = run_per_game(v1, 3, 5, BotConfig{}, BotConfig{});
    EXPECT_TRUE(first.clean);
    const std::string games = games_only(first.text);
    EXPECT_EQ(games, games_only(second.text));
    EXPECT_EQ(std::count(games.begin(), games.end(), '\n'), 3);
    EXPECT_NE(games.find("game 5: finished 1"), std::string::npos) << games;
    EXPECT_NE(games.find(" actions "), std::string::npos);
    EXPECT_NE(games.find(" state "), std::string::npos);
    EXPECT_NE(games.find(" trace "), std::string::npos);
    EXPECT_NE(first.text.find("decisions "), std::string::npos);
    // Other seeds play other games.
    const PerGameReport other = run_per_game(v1, 3, 6, BotConfig{}, BotConfig{});
    EXPECT_NE(games_only(other.text), games);
}
