// M45: the bot's reasons in the combat log (short form and grouping, D-054 M45 notes), the watch controls with
// the driver (speed changed live, Step), and the bot-game replay tests/replays/m45_bot_game.json.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "arena.hpp"
#include "bot.hpp"
#include "bot_driver.hpp"
#include "bot_log.hpp"
#include "core/engine/replay.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "session.hpp"
#include "test_helpers.hpp"
#include "watch_controls.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;

namespace {

// The bot-game replay: Hard vs Hard on the V1 scenario, dice seed 9 (a game into battle round 2 in which the
// Termagants advance away and shoot back; D-054 M45 notes).
constexpr std::uint64_t kBotGameSeed = 9;
constexpr const char* kBotGameScenario = "data/scenarios/v1_skirmish.json";

struct BotGame {
    Replay replay;
    GameRecord record;
    std::vector<std::string> trace;  // "R1 A StartMove: <reason>" per bot decision
};

// Plays the bot game through GameSession (every action validated; the client's automatic skips included) and
// turns its history into a scenario-form replay. Replaying it needs only core.
BotGame play_bot_game_replay() {
    BotGame g;
    g.replay.description =
        "M45: a whole game played by the bot on both sides, level Hard vs Hard, from the V1 scenario with dice "
        "seed 9 (D-054). Replaying it needs only the engine.";
    g.replay.initial.scenario = kBotGameScenario;
    g.replay.initial.seed = kBotGameSeed;
    client::GameSession session(replay_initial_state(g.replay, wh40k::test::repo_root()));
    g.record = play_bot_session(session, BotConfig{BotLevel::Hard}, BotConfig{BotLevel::Hard}, 5000, nullptr, &g.trace);
    g.replay.steps = session.history();
    g.replay.expect = replay_expectation(session.state());
    return g;
}

// The reason of a trace line ("R1 A StartMove: <reason>").
std::string trace_reason(const std::string& line) { return line.substr(line.find(": ") + 2); }

}  // namespace

// ---- the short form ----

TEST(M45_BotLog, HardMoveReasonKeepsTheMoveAndItsOdds) {
    const std::string reason =
        "Intercessor Squad: Normal move 6\" towards (Normal's plan) - 6.0\" from Termagants (Hard: win 100%, draw 0%, "
        "loss 0%, value +1.22; lookahead +1.22 over 6 rollouts, as Normal; Remain Stationary +1.19, Advance +1.21)";
    const ReasonSummary s = summarize_reason(reason);
    EXPECT_EQ(s.text, "Intercessor Squad: Normal move 6\" towards - 6.0\" from Termagants");
    ASSERT_TRUE(s.odds.has_value());
    EXPECT_EQ(*s.odds, (WinDrawLoss{100, 0, 0}));
    EXPECT_EQ(bot_log_line("Space Marines", reason),
              "Bot (Space Marines): Intercessor Squad: Normal move 6\" towards - 6.0\" from Termagants | win 100% / "
              "draw 0% / loss 0%");
}

TEST(M45_BotLog, HardAdvanceDropsTheScoreAndKeepsTheRoll) {
    EXPECT_EQ(short_reason("Termagants: Advance (-1.20 on average over the advance roll) (Hard: win 0%, draw 3%, loss "
                           "97%, value -1.20; lookahead -1.20 over 6 rollouts, as Normal; Remain Stationary -1.20)"),
              "Termagants: Advance | win 0% / draw 3% / loss 97%");
    EXPECT_EQ(short_reason("Termagants: Advance (roll 5) 9\" obliquely back - 11.1\" from Intercessor Squad (Hard: win "
                           "0%, draw 0%, loss 100%, value -1.20; lookahead -1.20 over 6 rollouts, Normal's plan)"),
              "Termagants: Advance (roll 5) 9\" obliquely back - 11.1\" from Intercessor Squad | win 0% / draw 0% / "
              "loss 100%");
}

TEST(M45_BotLog, TheChosenOptionsOddsAreShownForACharge) {
    // The chosen option's odds come first in every Hard reason (a declared charge: the fight's; no charge: holding).
    EXPECT_EQ(short_reason("Intercessor Squad: declare a charge (Hard: 58% charge odds against Termagants; the fight "
                           "+1.22 (win 100%, draw 0%, loss 0%), holding +1.20 (win 90%, draw 10%, loss 0%))"),
              "Intercessor Squad: declare a charge | win 100% / draw 0% / loss 0%");
    EXPECT_EQ(short_reason("Termagants: no charge (Hard: 3% charge odds against Intercessor Squad; holding -1.10 (win "
                           "0%, draw 8%, loss 92%), the fight -1.30 (win 0%, draw 0%, loss 100%))"),
              "Termagants: no charge | win 0% / draw 8% / loss 92%");
    EXPECT_EQ(short_reason("Intercessor Squad: charge move (roll 9): 5 of 5 models engaged (Hard: +1.24, win 100%, "
                           "draw 0%, loss 0%; holding +1.21)"),
              "Intercessor Squad: charge move (roll 9): 5 of 5 models engaged | win 100% / draw 0% / loss 0%");
    // No odds at all: just the action.
    EXPECT_EQ(short_reason("Termagants: no charge move (Hard: holding -1.20 against -1.30 for the charge)"),
              "Termagants: no charge move");
}

TEST(M45_BotLog, ARuleNumberBeforeHardsBlockStays) {
    EXPECT_EQ(short_reason("Termagants: remove model 12 to regain coherency (03.03; Hard: -1.20, win 0%, draw 0%, "
                           "loss 100%)"),
              "Termagants: remove model 12 to regain coherency (03.03) | win 0% / draw 0% / loss 100%");
    EXPECT_EQ(short_reason("Termagants: casualties front models first (Intercessor Squad may charge), keeping "
                           "coherency (Hard: -1.22, win 0%, draw 0%, loss 100%)"),
              "Termagants: casualties front models first (Intercessor Squad may charge), keeping coherency | win 0% / "
              "draw 0% / loss 100%");
}

TEST(M45_BotLog, NormalReasonsLoseTheirScoresAndExtraClauses) {
    EXPECT_EQ(short_reason("Intercessor Squad: Normal move 6\" towards - 6\" from Termagants, 5 of 5 models within half "
                           "range, within its half range, its charge odds next turn 42%, charge odds 42% (+0.82; Remain "
                           "Stationary +0.57, Advance +0.69)"),
              "Intercessor Squad: Normal move 6\" towards - 6\" from Termagants");
    EXPECT_EQ(short_reason("Intercessor Squad: declare a charge (42% charge odds against Termagants; a charge move "
                           "+0.30 on average, holding +0.26)"),
              "Intercessor Squad: declare a charge (42% charge odds against Termagants)");
    EXPECT_EQ(short_reason("Intercessor Squad: Normal fight (+0.282)"), "Intercessor Squad: Normal fight");
    EXPECT_EQ(short_reason("Intercessor Squad: charge move into Termagants (roll 9): 5 of 5 models engaged, 5 in base "
                           "contact (+0.30 against +0.26 for holding)"),
              "Intercessor Squad: charge move into Termagants (roll 9): 5 of 5 models engaged, 5 in base contact");
    // Already short (Easy, the shooting and fight reasons): unchanged.
    for (const char* kept : {"Intercessor Squad: 5x Bolt Rifle - Saturation at Termagants (expected 5.55 models)",
                             "Order casualties: Termagants (2) (default order)",
                             "Termagants: Normal move 2\" towards Intercessor Squad (more than 8\" away)"}) {
        EXPECT_EQ(short_reason(kept), kept);
    }
}

TEST(M45_BotLog, WrapWordsKeepsEveryWordAndTheWidth) {
    EXPECT_EQ(wrap_words("", 10), (std::vector<std::string>{""}));
    EXPECT_EQ(wrap_words("one two three four", 9), (std::vector<std::string>{"one two", "three", "four"}));
    EXPECT_EQ(wrap_words("a verylongwordindeed b", 6), (std::vector<std::string>{"a", "verylongwordindeed", "b"}));
}

// ---- grouping ----

TEST(M45_BotLog, AMovesPlacementsAndCommitShareOneNote) {
    BotLog log;
    const std::string move = "Termagants: Advance (roll 5) 9\" back - 11.1\" from Intercessor Squad";
    log.record(3, Side::B, "Tyranids", BotChoice{action::StartMove{UnitId{2}, MoveType::Advance}, "Termagants: Advance"});
    log.record(4, Side::B, "Tyranids", BotChoice{action::ProposeModelMove{ModelId{6}, {}}, move});
    log.record(5, Side::B, "Tyranids", BotChoice{action::ProposeModelMove{ModelId{7}, {}}, move});
    log.record(6, Side::B, "Tyranids", BotChoice{action::CommitUnitMove{}, move});
    // The next decision has its own note, even with the same reason, unless it continues a plan.
    log.record(7, Side::B, "Tyranids", BotChoice{action::EndShootStep{}, move});
    ASSERT_EQ(log.notes().size(), 3u);
    EXPECT_EQ(log.notes()[0].line, "Bot (Tyranids): Termagants: Advance");
    EXPECT_EQ(log.notes()[1].entries, (std::vector<std::size_t>{4, 5, 6}));
    EXPECT_EQ(log.notes()[1].reason, move);
    EXPECT_EQ(log.at(4), &log.notes()[1]);
    EXPECT_EQ(log.at(5), nullptr);  // shown once, at the plan's first action
    EXPECT_EQ(log.at(7), &log.notes()[2]);

    // The other side's placement with the same text is its own note.
    log.record(8, Side::A, "Space Marines", BotChoice{action::ProposeModelMove{ModelId{1}, {}}, move});
    EXPECT_EQ(log.notes().size(), 4u);

    // An undo back to 6 log entries drops the later notes and the undone placements.
    log.truncate(6);
    ASSERT_EQ(log.notes().size(), 2u);
    EXPECT_EQ(log.notes()[1].entries, (std::vector<std::size_t>{4, 5}));
    log.clear();
    EXPECT_TRUE(log.notes().empty());
}

// ---- the watch controls with the driver ----

TEST(M45_Watch, SpeedChangesLiveAndStepHandsOverOneAction) {
    const client::GameSession s(wh40k::test::load_v1_scenario());  // side A selects a unit to move
    const GameState& gs = s.state();
    client::WatchControls watch;
    BotSpeed speed = BotSpeed::Slow;
    BotDriver d(BotConfig{BotLevel::Easy}, speed, BotDriver::Threading::Synchronous);
    const auto inputs = [&] {
        DriverInputs in;
        in.state = &gs;
        in.bot_sides = {true, true};
        in.paused = watch.paused();
        in.step_once = watch.step_requested();
        return in;
    };
    const auto tick = [&](double now) {
        d.set_speed(speed);  // as the client does every frame
        const std::optional<BotChoice> c = d.tick(now, inputs());
        if (c) watch.delivered();
        return c;
    };
    // Slow: 1.6 s before a unit's activation. After 0.5 s the speed goes to Fast (0.32 s): due at once.
    EXPECT_FALSE(tick(0.0).has_value());
    EXPECT_FALSE(tick(0.5).has_value());
    watch.apply(client::WatchCommand::Faster, speed);
    watch.apply(client::WatchCommand::Faster, speed);
    EXPECT_EQ(speed, BotSpeed::Fast);
    EXPECT_TRUE(tick(0.51).has_value());

    // Paused: nothing, however long; Step hands over exactly one action and is used up.
    BotDriver paused_driver(BotConfig{BotLevel::Easy}, BotSpeed::Instant, BotDriver::Threading::Synchronous);
    watch.apply(client::WatchCommand::TogglePause, speed);
    DriverInputs in = inputs();
    for (int i = 0; i < 20; ++i) EXPECT_FALSE(paused_driver.tick(i * 1.0, in).has_value());
    watch.apply(client::WatchCommand::Step, speed);
    in = inputs();
    ASSERT_TRUE(paused_driver.tick(30.0, in).has_value());
    watch.delivered();
    EXPECT_FALSE(watch.step_requested());
    EXPECT_TRUE(watch.paused());  // a step doesn't resume
}

// ---- the bot-game replay ----

TEST(M45_Replay, TheBotGameIsDeterministicAndReplaysWithCoreOnly) {
    const BotGame first = play_bot_game_replay();
    const BotGame second = play_bot_game_replay();
    EXPECT_EQ(Json(first.replay).dump(2), Json(second.replay).dump(2));  // byte-identical when saved
    EXPECT_TRUE(first.record.finished);
    EXPECT_EQ(first.record.rejected, 0u);
    EXPECT_EQ(first.record.reverted, 0u);
    EXPECT_EQ(first.record.fallbacks, 0u);

    const ReplayResult r = run_replay(Json::parse(Json(first.replay).dump()).get<Replay>(), wh40k::test::repo_root());
    ASSERT_TRUE(r.ok()) << "step " << *r.failed_step << ": " << r.reasons.front();
    EXPECT_EQ(replay_expectation(r.final_state), first.replay.expect);
    EXPECT_TRUE(r.final_state.result.has_value());  // a whole game

    // Every reason of the game has a short form, Hard's with its odds, and the log groups the plans' steps.
    BotLog log;
    for (std::size_t i = 0; i < first.trace.size(); ++i) {
        const std::string reason = trace_reason(first.trace[i]);
        const ReasonSummary sum = summarize_reason(reason);
        EXPECT_FALSE(sum.text.empty()) << reason;
        EXPECT_LE(sum.text.size(), reason.size()) << reason;
        if (reason.find("Hard: ") != std::string::npos && reason.find("win ") != std::string::npos) {
            EXPECT_TRUE(sum.odds.has_value()) << reason;
        }
        EXPECT_EQ(sum.text.find("Hard:"), std::string::npos) << sum.text;
        const bool placement = first.trace[i].find(" ProposeModelMove: ") != std::string::npos ||
                               first.trace[i].find(" CommitUnitMove: ") != std::string::npos;
        const GameAction a = placement ? GameAction{action::ProposeModelMove{}} : GameAction{action::EndShootStep{}};
        log.record(i, first.trace[i][3] == 'A' ? Side::A : Side::B, "army", BotChoice{a, reason});
    }
    EXPECT_LT(log.notes().size() * 2, first.trace.size());  // far fewer lines than actions
}

// Writes tests/replays/m45_bot_game.json. Run on purpose only:
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M45_Replay.DISABLED_RegenerateBotGameReplay
TEST(M45_Replay, DISABLED_RegenerateBotGameReplay) {
    const BotGame g = play_bot_game_replay();
    ASSERT_TRUE(g.record.finished);
    ASSERT_EQ(g.record.rejected, 0u);
    ASSERT_EQ(g.record.reverted, 0u);
    const ReplayResult r = run_replay(g.replay, wh40k::test::repo_root());
    ASSERT_TRUE(r.ok()) << "step " << *r.failed_step << ": " << r.reasons.front();
    if (::testing::Test::HasFailure()) return;  // never overwrite the committed replay with a broken game
    save_replay(g.replay, std::filesystem::path(WH40K_REPLAY_DIR) / "m45_bot_game.json");
}
