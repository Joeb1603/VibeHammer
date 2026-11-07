// M39: the bot framework (D-054): the state hash, the simulator, the baseline bot (level Easy), the fallback
// and loop guard, the driver's pacing, and the arena.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <variant>
#include <vector>

#include "arena.hpp"
#include "auto_actions.hpp"
#include "bot.hpp"
#include "bot_driver.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "fallback.hpp"
#include "session.hpp"
#include "simulator.hpp"
#include "state_hash.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;

namespace {

BotConfig easy() { return BotConfig{BotLevel::Easy}; }

// Plays a bot game through GameSession (as the arena and the client do) and calls `visit` with the state and
// the deciding side before every bot decision, and the choice made.
template <typename Visit>
client::GameSession play(std::uint64_t seed, Visit visit, std::size_t cap = 5000) {
    client::GameSession s = client::GameSession::new_game(wh40k::test::load_v1_scenario(), seed);
    std::array<Bot, 2> bots{Bot(easy()), Bot(easy())};
    client::AutoSettings autos;
    autos.bot_controlled = {true, true};
    while (s.pending() && s.history().size() < cap) {
        (void)client::run_auto_actions(s, autos);
        if (!s.pending()) break;
        const Side side = s.pending()->side;
        const GameState before = s.state();
        const BotChoice c = bots[side_index(side)].decide(before, side);
        visit(before, side, c);
        if (c.stopped || !s.submit(c.action).ok()) break;
    }
    return s;
}

// The first state along a seeded game whose pending decision satisfies `pred` (for driver fixtures).
template <typename Pred>
std::optional<GameState> first_state(std::uint64_t seed, Pred pred) {
    std::optional<GameState> found;
    (void)play(seed, [&](const GameState& gs, Side, const BotChoice&) {
        if (!found && pred(gs)) found = gs;
    });
    return found;
}

}  // namespace

// ==== Acceptance: 40 seeded Easy-vs-Easy games, no rejections, no reverts ====

TEST(M39_Arena, FortySeededEasyGamesFinishWithNoRejectedActionsAndNoRevertedMoves) {
    const ArenaResult r = run_arena(wh40k::test::load_v1_scenario(), 40, 1, easy(), easy());
    ASSERT_EQ(r.games.size(), 40u);
    for (const GameRecord& g : r.games) {
        EXPECT_TRUE(g.finished) << "seed " << g.seed << ": " << (g.problems.empty() ? "" : g.problems.front());
        EXPECT_EQ(g.rejected, 0u) << "seed " << g.seed;
        EXPECT_EQ(g.reverted, 0u) << "seed " << g.seed;
        EXPECT_EQ(g.fallbacks, 0u) << "seed " << g.seed;  // Easy always has its own legal answer
        EXPECT_GT(g.end_round, 0);
    }
    EXPECT_EQ(r.rejected, 0u);
    EXPECT_EQ(r.reverted, 0u);
    EXPECT_EQ(r.unfinished, 0);
    EXPECT_EQ(r.wins[0] + r.wins[1] + r.draws, 40);
    EXPECT_TRUE(r.clean());
    EXPECT_NE(format_arena(r, wh40k::test::load_v1_scenario()).find("UnitMoveReverted 0"), std::string::npos);
}

TEST(M39_Arena, EasyGamesUseTheBoardTheEditorsAndTheMemo) {
    std::map<std::string, int> kinds;
    std::size_t memo_hits = 0;
    client::GameSession s = client::GameSession::new_game(wh40k::test::load_v1_scenario(), 3);
    std::array<Bot, 2> bots{Bot(easy()), Bot(easy())};
    client::AutoSettings autos;
    autos.bot_controlled = {true, true};
    while (s.pending()) {
        (void)client::run_auto_actions(s, autos);
        if (!s.pending()) break;
        const Side side = s.pending()->side;
        const BotChoice c = bots[side_index(side)].decide(s.state(), side);
        ASSERT_FALSE(c.stopped);
        ASSERT_FALSE(c.reason.empty());
        ASSERT_TRUE(s.submit(c.action).ok()) << action_name(c.action);
    }
    for (const auto& step : s.history()) ++kinds[std::string(action_name(step.action))];
    memo_hits = bots[0].memo_hits() + bots[1].memo_hits();
    for (const char* k : {"StartMove", "ProposeModelMove", "CommitUnitMove", "Shoot", "DeclareCharge",
                          "OrderCasualties"}) {
        EXPECT_GT(kinds[k], 0) << k;
    }
    EXPECT_EQ(kinds["ResetUnitMove"], 0);
    EXPECT_GT(memo_hits, 0u);  // the model moves and commits came from the plan memo
    EXPECT_TRUE(s.state().result.has_value());
}

// ==== Acceptance: changing only state.dice never changes a decision ====

TEST(M39_NeverPeeksAtDice, ChangingOnlyTheDiceNeverChangesADecision) {
    std::size_t checked = 0;
    std::set<std::size_t> kinds;
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        (void)play(seed, [&](const GameState& gs, Side side, const BotChoice& played) {
            GameState other = gs;
            other.dice.seed ^= 0x9E3779B97F4A7C15ull;
            other.dice.position += 37;
            EXPECT_EQ(state_hash(gs), state_hash(other));
            // Fresh bots (no memo) on both, and the playing bot (with its memo): all the same action.
            const BotChoice a = Bot(easy()).decide(gs, side);
            const BotChoice b = Bot(easy()).decide(other, side);
            EXPECT_EQ(a.action, b.action) << "seed " << seed << ": " << action_name(a.action);
            EXPECT_EQ(a.action, played.action) << "seed " << seed << ": the memo differs from a fresh plan";
            kinds.insert(gs.pending->kind.index());
            ++checked;
        });
    }
    EXPECT_GT(checked, 200u);
    EXPECT_GE(kinds.size(), 8u);  // many decision kinds were seen
}

TEST(M39_StateHash, IgnoresTheDiceButNotPositionsWoundsOrThePendingDecision) {
    const client::GameSession s(wh40k::test::load_v1_scenario());
    const GameState& gs = s.state();
    const std::uint64_t h = state_hash(gs);
    GameState dice = gs;
    dice.dice = DiceState{12345, 678};
    EXPECT_EQ(state_hash(dice), h);
    GameState moved = gs;
    moved.units[0].models[0].position.x += 0.5f;
    EXPECT_NE(state_hash(moved), h);
    GameState hurt = gs;
    hurt.units[1].models[0].wounds_remaining -= 1;
    EXPECT_NE(state_hash(hurt), h);
    GameState other_side = gs;
    other_side.pending->side = opponent(gs.pending->side);
    EXPECT_NE(state_hash(other_side), h);
    EXPECT_EQ(decision_seed(dice), decision_seed(gs));
}

TEST(M39_Simulator, UsesTheBotsOwnDiceAndLeavesTheGamesAlone) {
    // A charge roll in the simulator comes from the bot's dice; the game's dice state is not advanced.
    const std::optional<GameState> charge = first_state(1, [](const GameState& gs) {
        if (!std::holds_alternative<decision::Charge>(gs.pending->kind)) return false;
        const std::vector<GameAction> legal = legal_actions(gs, gs.pending->side);
        return std::any_of(legal.begin(), legal.end(),
                           [](const GameAction& a) { return std::holds_alternative<action::DeclareCharge>(a); });
    });
    ASSERT_TRUE(charge.has_value());
    GameAction declare;
    for (const GameAction& a : legal_actions(*charge, charge->pending->side)) {
        if (std::holds_alternative<action::DeclareCharge>(a)) declare = a;
    }
    Simulator sim(*charge, 777);
    ASSERT_TRUE(sim.step(declare).ok());
    EXPECT_EQ(sim.state().dice, charge->dice);  // SeededDice(state.dice) was never used
    EXPECT_EQ(sim.steps(), 1u);
    // An invalid action changes nothing.
    const GameState before = sim.state();
    EXPECT_FALSE(sim.step(action::EndShootStep{}).ok());
    EXPECT_EQ(sim.state(), before);
}

// ==== Acceptance: the guard test ====

TEST(M39_NoRulesInTheBot, OnlyTheSimulatorAppliesActionsAndNoDeviceHeadersOrClocks) {
    namespace fs = std::filesystem;
    const fs::path ai_dir = fs::path(WH40K_REPO_ROOT) / "src" / "ai";
    const std::regex apply(R"(\bapply_action\s*\()");
    const std::regex forbidden_include(
        R"(#\s*include\s*[<"]([^>"]*/)?(raylib|raymath|rlgl|rlimgui|imgui[a-z_]*|imconfig|windows|winuser|wingdi|glfw[0-9]*)(\.h|\.hpp)?[>"])",
        std::regex::icase);
    const std::regex randomness(R"(std::rand\b|\brandom_device\b|\bsrand\s*\()");
    const std::regex clock(R"(steady_clock|system_clock|high_resolution_clock|\btime\s*\(\s*(nullptr|NULL|0)?\s*\))");
    const std::regex trig(R"(\b(std::)?(sin|cos|tan|atan2?)f?\s*\()");
    int files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(ai_dir)) {
        const auto ext = entry.path().extension();
        if (ext != ".cpp" && ext != ".hpp") continue;
        ++files;
        std::ifstream in_file(entry.path());
        std::stringstream text;
        text << in_file.rdbuf();
        const std::string src = text.str();
        const std::string name = entry.path().filename().string();
        if (name != "simulator.cpp") {
            EXPECT_FALSE(std::regex_search(src, apply)) << name << " applies actions; use Simulator (D-054)";
        }
        EXPECT_FALSE(std::regex_search(src, forbidden_include)) << name << " includes a device/UI header";
        EXPECT_FALSE(std::regex_search(src, randomness)) << name << " uses non-deterministic randomness";
        EXPECT_FALSE(std::regex_search(src, trig)) << name << ": directions come from a constant table (D-054)";
        // Timing is measured by the arena only, never inside a decision.
        if (name != "arena.cpp") EXPECT_FALSE(std::regex_search(src, clock)) << name << " reads a clock";
    }
    EXPECT_GE(files, 14);
    // simulator.cpp does call it (with the bot's own dice; since M54 the overload that also takes the simulator's
    // reused rollback copy).
    std::ifstream sim(ai_dir / "simulator.cpp");
    std::stringstream text;
    text << sim.rdbuf();
    EXPECT_TRUE(std::regex_search(text.str(), std::regex(R"(apply_action\(state_, side, action, dice, backup_\))")));
}

// ==== Fallback and loop guard ====

TEST(M39_Fallback, EveryDecisionAlongSeveralGamesHasALegalFallback) {
    std::set<std::size_t> kinds;
    for (const std::uint64_t seed : {1ull, 4ull, 7ull}) {
        (void)play(seed, [&](const GameState& gs, Side side, const BotChoice&) {
            const FallbackResult fb = fallback_action(gs, side);
            ASSERT_TRUE(fb.action.has_value()) << decision_name(gs.pending->kind);
            EXPECT_FALSE(is_template(*fb.action));
            EXPECT_TRUE(validate_action(gs, side, *fb.action).ok()) << action_name(*fb.action);
            if (std::holds_alternative<action::CommitUnitMove>(*fb.action)) {
                EXPECT_TRUE(move_end_problems(gs).empty());
            }
            kinds.insert(gs.pending->kind.index());
        });
    }
    EXPECT_GE(kinds.size(), 8u);
}

TEST(M39_Fallback, MovesThatWouldNotStandAreResetNotCommitted) {
    // A Normal move with the westmost Intercessor moved 5" away from the others: out of coherency (03.03), so
    // a commit would be reverted. The fallback resets instead, and the bot never commits it.
    client::GameSession s(wh40k::test::load_v1_scenario());
    const Unit& marines = s.state().units[0];
    ASSERT_TRUE(s.submit(action::StartMove{marines.id, MoveType::Normal, std::nullopt}).ok());
    const Model* west = &s.state().units[0].models[0];
    for (const Model& m : s.state().units[0].models) {
        if (m.position.x < west->position.x) west = &m;
    }
    const Position away{west->position.x - wh40k::test::in(5), west->position.y, 0.0f};
    ASSERT_TRUE(s.submit(action::ProposeModelMove{west->id, {away}}).ok());
    ASSERT_FALSE(move_end_problems(s.state()).empty());
    const FallbackResult fb = fallback_action(s.state(), s.deciding_side());
    ASSERT_TRUE(fb.action.has_value());
    EXPECT_TRUE(std::holds_alternative<action::ResetUnitMove>(*fb.action));
    const BotChoice c = Bot(easy()).decide(s.state(), s.deciding_side());
    EXPECT_FALSE(c.stopped);
    EXPECT_FALSE(std::holds_alternative<action::CommitUnitMove>(c.action)) << c.reason;
    EXPECT_TRUE(validate_action(s.state(), s.deciding_side(), c.action).ok());
}

TEST(M39_LoopGuard, TheSameStateAThirdTimeGetsTheFallback) {
    const client::GameSession s(wh40k::test::load_v1_scenario());
    Bot bot(easy());
    const Side side = s.deciding_side();
    const BotChoice first = bot.decide(s.state(), side);
    EXPECT_EQ(first.reason.rfind("Loop guard", 0), std::string::npos);
    const BotChoice second = bot.decide(s.state(), side);
    EXPECT_EQ(second.reason.rfind("Loop guard", 0), std::string::npos);
    const BotChoice third = bot.decide(s.state(), side);
    EXPECT_EQ(third.reason.rfind("Loop guard", 0), 0u) << third.reason;
    EXPECT_FALSE(third.stopped);
    EXPECT_TRUE(validate_action(s.state(), side, third.action).ok());
    EXPECT_EQ(bot.fallbacks(), 1u);
    bot.forget();  // an undo or a new game: the history starts again
    EXPECT_EQ(bot.decide(s.state(), side).reason.rfind("Loop guard", 0), std::string::npos);
}

TEST(M39_LoopGuard, ABotThatKeepsSeeingTheSameStateStillTerminates) {
    // A driver loop that never applies the bot's actions sees the same state forever: the bot never throws or
    // hangs, and once the loop guard trips every answer is the (legal) fallback.
    const client::GameSession s(wh40k::test::load_v1_scenario());
    Bot bot(easy());
    for (int i = 0; i < 50; ++i) {
        const BotChoice c = bot.decide(s.state(), s.deciding_side());
        ASSERT_FALSE(c.stopped);
        if (i >= LoopGuard::kLoopLimit - 1) EXPECT_EQ(c.reason.rfind("Loop guard", 0), 0u);
    }
}

TEST(M39_LoopGuard, TheBotStopsWithReasonsWhenItHasNoDecision) {
    const client::GameSession s(wh40k::test::load_v1_scenario());
    const BotChoice c = Bot(easy()).decide(s.state(), opponent(s.deciding_side()));
    EXPECT_TRUE(c.stopped);
    EXPECT_FALSE(c.problems.empty());
    const FallbackResult fb = fallback_action(s.state(), opponent(s.deciding_side()));
    EXPECT_FALSE(fb.action.has_value());
    EXPECT_FALSE(fb.problems.empty());
}

TEST(M39_Levels, EveryLevelIsBuiltAndHardIsItsOwnLevel) {
    // M39 mapped Normal and Hard to Easy; M41 built Normal (Hard played as Normal until M44). Since M44 Hard is its
    // own level: it decides a move with the evaluator and the lookahead (M44_Levels checks what it does).
    EXPECT_EQ(effective_level(BotLevel::Easy), BotLevel::Easy);
    EXPECT_EQ(effective_level(BotLevel::Normal), BotLevel::Normal);
    EXPECT_EQ(effective_level(BotLevel::Hard), BotLevel::Hard);
    const std::optional<GameState> move = first_state(1, [](const GameState& gs) {
        return gs.pending->side == Side::A && std::holds_alternative<decision::MoveUnits>(gs.pending->kind);
    });
    ASSERT_TRUE(move.has_value());
    const BotChoice hard = Bot(BotConfig{BotLevel::Hard}).decide(*move, Side::A);
    const BotChoice normal = Bot(BotConfig{BotLevel::Normal}).decide(*move, Side::A);
    EXPECT_NE(hard.reason.find("Hard: win"), std::string::npos) << hard.reason;
    EXPECT_EQ(normal.reason.find("Hard:"), std::string::npos) << normal.reason;
}

// ==== Driver: pacing, pop-ups, stale results ====

namespace {

DriverInputs inputs_for(const GameState& gs, std::array<bool, 2> bots = {true, true}) {
    DriverInputs in;
    in.state = &gs;
    in.bot_sides = bots;
    return in;
}

// The time at which a synchronous driver first hands over the action, ticking every 10 ms from 0.
double first_delivery(BotSpeed speed, DriverInputs in) {
    BotDriver d(easy(), speed, BotDriver::Threading::Synchronous);
    for (int i = 0; i <= 1000; ++i) {
        const double t = i * 0.01;
        if (d.tick(t, in)) return t;
    }
    return -1.0;
}

}  // namespace

TEST(M39_Driver, PausesFollowTheActionKindAndTheSpeed) {
    EXPECT_DOUBLE_EQ(speed_factor(BotSpeed::Slow), 3.0);  // M48 (D-059): Slow = 3x Normal (2x before)
    EXPECT_DOUBLE_EQ(speed_factor(BotSpeed::Normal), 1.0);
    EXPECT_DOUBLE_EQ(speed_factor(BotSpeed::Fast), 0.4);
    EXPECT_DOUBLE_EQ(speed_factor(BotSpeed::Instant), 0.0);
    EXPECT_DOUBLE_EQ(base_pause_seconds(action::StartMove{}), 0.8);
    EXPECT_DOUBLE_EQ(base_pause_seconds(action::Shoot{}), 0.8);
    EXPECT_DOUBLE_EQ(base_pause_seconds(action::ProposeModelMove{}), 0.12);
    EXPECT_DOUBLE_EQ(base_pause_seconds(action::CommitUnitMove{}), 0.4);
    EXPECT_DOUBLE_EQ(base_pause_seconds(action::OrderCasualties{}), 0.6);
    EXPECT_DOUBLE_EQ(base_pause_seconds(action::DeclareAllocationOrder{}), 0.6);
    for (const BotSpeed sp : {BotSpeed::Slow, BotSpeed::Normal, BotSpeed::Fast, BotSpeed::Instant}) {
        EXPECT_DOUBLE_EQ(pause_seconds(action::StartMove{}, sp), 0.8 * speed_factor(sp));
        EXPECT_DOUBLE_EQ(bot_dice_dwell_seconds(sp), 1.2 * speed_factor(sp));
    }
}

TEST(M39_Driver, TheActionIsHandedOverOnceItsPauseHasPassed) {
    const client::GameSession s(wh40k::test::load_v1_scenario());  // Side A selects a unit to move
    const GameState& gs = s.state();
    const BotChoice expected = Bot(easy()).decide(gs, gs.pending->side);
    const double normal = pause_seconds(expected.action, BotSpeed::Normal);
    ASSERT_GT(normal, 0.0);
    EXPECT_NEAR(first_delivery(BotSpeed::Normal, inputs_for(gs)), normal, 0.011);
    EXPECT_NEAR(first_delivery(BotSpeed::Slow, inputs_for(gs)), 3.0 * normal, 0.011);  // M48: 3x
    EXPECT_NEAR(first_delivery(BotSpeed::Fast, inputs_for(gs)), 0.4 * normal, 0.011);
    EXPECT_DOUBLE_EQ(first_delivery(BotSpeed::Instant, inputs_for(gs)), 0.0);  // Instant: no pause

    BotDriver d(easy(), BotSpeed::Normal, BotDriver::Threading::Synchronous);
    EXPECT_FALSE(d.tick(0.0, inputs_for(gs)).has_value());
    ASSERT_TRUE(d.pending_reason().has_value());  // decided, waiting out the pause
    const std::optional<BotChoice> c = d.tick(normal + 0.001, inputs_for(gs));
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->action, expected.action);
    EXPECT_FALSE(d.tick(normal + 5.0, inputs_for(gs)).has_value());  // handed over once
}

TEST(M39_Driver, NothingForAHumansDecision) {
    const client::GameSession s(wh40k::test::load_v1_scenario());
    const GameState& gs = s.state();
    BotDriver d(easy(), BotSpeed::Instant, BotDriver::Threading::Synchronous);
    std::array<bool, 2> bots{true, true};
    bots[side_index(gs.pending->side)] = false;
    EXPECT_FALSE(d.tick(0.0, inputs_for(gs, bots)).has_value());
    EXPECT_FALSE(d.tick(100.0, inputs_for(gs, bots)).has_value());
}

TEST(M39_Driver, NeverActsWhileAPopUpIsOpenAndThePauseStartsWhenItCloses) {
    const client::GameSession s(wh40k::test::load_v1_scenario());
    const GameState& gs = s.state();
    BotDriver d(easy(), BotSpeed::Normal, BotDriver::Threading::Synchronous);
    DriverInputs open = inputs_for(gs);
    open.popup_open = true;
    for (int i = 0; i <= 100; ++i) EXPECT_FALSE(d.tick(i * 0.1, open).has_value());  // 10 s of dice
    const double pause = pause_seconds(Bot(easy()).decide(gs, gs.pending->side).action, BotSpeed::Normal);
    EXPECT_FALSE(d.tick(10.0 + pause * 0.5, inputs_for(gs)).has_value());
    EXPECT_TRUE(d.tick(10.0 + pause + 0.001, inputs_for(gs)).has_value());
    // Instant speed too: never while a pop-up is open.
    BotDriver fast(easy(), BotSpeed::Instant, BotDriver::Threading::Synchronous);
    EXPECT_FALSE(fast.tick(0.0, open).has_value());
    EXPECT_TRUE(fast.tick(0.0, inputs_for(gs)).has_value());
}

TEST(M39_Driver, PauseHoldsAndStepHandsOverAtOnce) {
    const client::GameSession s(wh40k::test::load_v1_scenario());
    const GameState& gs = s.state();
    BotDriver d(easy(), BotSpeed::Normal, BotDriver::Threading::Synchronous);
    DriverInputs paused = inputs_for(gs);
    paused.paused = true;
    for (int i = 0; i <= 50; ++i) EXPECT_FALSE(d.tick(i * 0.1, paused).has_value());
    DriverInputs step = paused;
    step.step_once = true;
    EXPECT_TRUE(d.tick(5.0, step).has_value());
}

TEST(M39_Driver, AStaleResultIsDroppedAfterAnInvalidate) {
    const client::GameSession s(wh40k::test::load_v1_scenario());
    const GameState& gs = s.state();
    BotDriver d(easy(), BotSpeed::Normal, BotDriver::Threading::Synchronous);
    EXPECT_FALSE(d.tick(0.0, inputs_for(gs)).has_value());  // decided at 0, due at 0.8
    const std::uint64_t gen = d.generation();
    d.invalidate();  // an undo
    EXPECT_EQ(d.generation(), gen + 1);
    EXPECT_FALSE(d.pending_reason().has_value());  // the old result is not offered
    EXPECT_FALSE(d.tick(5.0, inputs_for(gs)).has_value());  // decided again at 5: its pause starts at 5
    EXPECT_FALSE(d.tick(5.5, inputs_for(gs)).has_value());
    EXPECT_TRUE(d.tick(5.81, inputs_for(gs)).has_value());
}

TEST(M39_Driver, AWorkerResultForAnOlderStateIsDropped) {
    // Worker thread: decide for state 1, then the game moves on (state 2) before it is handed over.
    client::GameSession s(wh40k::test::load_v1_scenario());
    const GameState first = s.state();
    BotDriver d(easy(), BotSpeed::Normal, BotDriver::Threading::Worker);
    EXPECT_FALSE(d.tick(0.0, inputs_for(first)).has_value());
    for (int i = 0; i < 5000 && !d.pending_reason(); ++i) {  // wait for the worker
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        (void)d.tick(0.01, inputs_for(first));
    }
    ASSERT_TRUE(d.pending_reason().has_value());
    const BotChoice c1 = Bot(easy()).decide(first, first.pending->side);
    ASSERT_TRUE(s.submit(c1.action).ok());  // e.g. a human's input elsewhere moved the game on
    const GameState second = s.state();
    ASSERT_NE(state_hash(first), state_hash(second));
    const BotChoice c2 = Bot(easy()).decide(second, second.pending->side);
    std::optional<BotChoice> got;
    for (int i = 0; i < 5000 && !got; ++i) {
        got = d.tick(10.0 + i * 0.01, inputs_for(second));
        if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->action, c2.action);  // for the new state, not the stale one
    EXPECT_NE(got->action, c1.action);
}

TEST(M39_Driver, AWorkerDrivesAWholeGameWithTheSameActionsAsTheSynchronousBot) {
    // The driver on a worker thread, through a session, matches the synchronous bot step for step.
    client::GameSession live = client::GameSession::new_game(wh40k::test::load_v1_scenario(), 6);
    BotDriver d(easy(), BotSpeed::Instant, BotDriver::Threading::Worker);
    client::AutoSettings autos;
    autos.bot_controlled = {true, true};
    double t = 0.0;
    int idle = 0;
    while (live.pending() && live.history().size() < 5000 && idle < 100000) {
        (void)client::run_auto_actions(live, autos);
        if (!live.pending()) break;
        DriverInputs in = inputs_for(live.state());
        in.step = live.history().size();
        const std::optional<BotChoice> c = d.tick(t += 0.001, in);
        if (!c) {
            ++idle;
            std::this_thread::yield();
            continue;
        }
        idle = 0;
        ASSERT_FALSE(c->stopped);
        ASSERT_TRUE(live.submit(c->action).ok()) << action_name(c->action);
    }
    ASSERT_TRUE(live.state().result.has_value());
    const GameRecord sync = play_bot_game(wh40k::test::load_v1_scenario(), 6, easy(), easy());
    EXPECT_EQ(live.history().size(), sync.steps);
    EXPECT_EQ(live.state().result->winner, sync.winner);
}
