// M46 (D-057): the bots pile in and consolidate when it costs nothing. Normal and Hard make a pile-in,
// Overrun pile-in or consolidation whose gain ties with not moving if it leaves the unit tidier (closer to
// the enemy, more in base contact, tighter coherency), and still refuse one that costs them. Easy makes every
// one worth making, by Auto move (D-050). Plus the arena counts and the bot-game replay
// tests/replays/m46_bot_fight_moves.json.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "auto_fight_move.hpp"
#include "bot.hpp"
#include "bot_log.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "fight_move_skip.hpp"
#include "normal/fight.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

// The bot-game replay: Normal vs Normal on the 20-Termagant scenario (D-057; the seed is one whose game has
// pile-ins and consolidations by both sides).
constexpr std::uint64_t kFightMoveSeed = 1;
constexpr const char* kFightMoveScenario = "data/scenarios/bot_20_termagants.json";

// Keeps only the Termagants' front rank (the five closest to the Intercessors, who are south of them): then a
// pile-in or consolidation by the Intercessors brings no new Termagant into the fight.
void front_rank_only(GameState& gs) {
    std::vector<Model>& ms = gs.units[kGants].models;
    std::stable_sort(ms.begin(), ms.end(), [](const Model& a, const Model& b) { return a.position.y < b.position.y; });
    ms.resize(5);
}

client::FightMoveShape shape(const GameState& gs, std::size_t unit, std::size_t target) {
    return client::fight_move_shape(gs, gs.units[unit].id, {gs.units[target].id});
}

// Answers decisions with `bot` until the pending decision isn't `side`'s MoveModels any more.
void run_move(GameState& gs, Bot& bot, Side side) {
    for (int guard = 0; guard < 64 && gs.pending && gs.pending->side == side &&
                        std::holds_alternative<decision::MoveModels>(gs.pending->kind);
         ++guard) {
        const BotChoice c = bot.decide(gs, side);
        ASSERT_TRUE(validate_action(gs, side, c.action).ok()) << action_name(c.action);
        (void)apply_action(gs, side, c.action);
    }
}

// The first offered start of a fight move in `gs` for `side` and its simulated gain for the Normal level.
template <typename Start>
std::optional<double> first_gain(const GameState& gs, Side side, bool pile_in) {
    analysis::OddsCache cache;
    for (const GameAction& a : legal_actions(gs, side)) {
        if (!std::holds_alternative<Start>(a)) continue;
        const auto plan = normal::simulate_fight_move(gs, side, a, 1, "test");
        if (!plan) return std::nullopt;
        return pile_in ? normal::melee_exchange(cache, plan->after, side) - normal::melee_exchange(cache, gs, side)
                       : normal::consolidation_score(cache, plan->after, side) -
                             normal::consolidation_score(cache, gs, side);
    }
    return std::nullopt;
}

GameState pile_in_step() {
    GameState gs = wh40k::test::after_successful_charge();
    (void)apply_action(gs, Side::A, action::EndChargeStep{});
    EXPECT_TRUE(gs.pending && std::holds_alternative<decision::PileIn>(gs.pending->kind));
    EXPECT_EQ(gs.pending->side, Side::A);
    return gs;
}

struct BotGame {
    Replay replay;
    GameRecord record;
};

BotGame play_fight_move_game() {
    BotGame g;
    g.replay.description =
        "M46: a whole game played by the bot on both sides, level Normal vs Normal, from the 20-Termagant "
        "scenario with dice seed 1: both sides pile in and consolidate when it costs nothing (D-057). Replaying "
        "it needs only the engine.";
    g.replay.initial.scenario = kFightMoveScenario;
    g.replay.initial.seed = kFightMoveSeed;
    client::GameSession session(replay_initial_state(g.replay, wh40k::test::repo_root()));
    g.record = play_bot_session(session, BotConfig{BotLevel::Normal}, BotConfig{BotLevel::Normal});
    g.replay.steps = session.history();
    g.replay.expect = replay_expectation(session.state());
    return g;
}

}  // namespace

// ---- Normal and Hard: ties go to the tidier move ----

TEST(M46_PileIn, NormalAndHardPileIntoBaseContactWhenItCostsNothing) {
    // After the charge every Intercessor is engaged about 1" from the Termagants' front rank, and there is no
    // second rank to bring in: the pile-in changes nothing in the melee exchange, but closes the gap.
    GameState start = pile_in_step();
    front_rank_only(start);
    const std::optional<double> gain = first_gain<action::StartPileIn>(start, Side::A, true);
    ASSERT_TRUE(gain.has_value());
    EXPECT_NEAR(*gain, 0.0, 1e-6);
    for (const BotLevel level : {BotLevel::Normal, BotLevel::Hard}) {
        GameState gs = start;
        const client::FightMoveShape before = shape(gs, kMarines, kGants);
        Bot bot(BotConfig{level});
        const BotChoice c = bot.decide(gs, Side::A);
        ASSERT_TRUE(std::holds_alternative<action::StartPileIn>(c.action)) << c.reason;
        EXPECT_NE(c.reason.find("more in base contact"), std::string::npos) << c.reason;
        (void)apply_action(gs, Side::A, c.action);
        run_move(gs, bot, Side::A);
        EXPECT_TRUE(gs.units[kMarines].phase_flags.piled_in);  // committed, not reverted
        const client::FightMoveShape after = shape(gs, kMarines, kGants);
        EXPECT_GT(after.in_contact, before.in_contact);
        EXPECT_LT(after.total_gap, before.total_gap);
        EXPECT_EQ(bot.fallbacks(), 0u);
    }
}

TEST(M46_PileIn, APileInThatBringsMoreEnemyAttackersIsStillSkipped) {
    // With both Termagant ranks, the Intercessors closing in would engage the second rank too: more Termagant
    // attacks and none more of their own, so Normal and Hard end their pile-ins.
    const GameState gs = pile_in_step();
    const std::optional<double> gain = first_gain<action::StartPileIn>(gs, Side::A, true);
    ASSERT_TRUE(gain.has_value());
    EXPECT_LT(*gain, -1e-6);
    for (const BotLevel level : {BotLevel::Normal, BotLevel::Hard}) {
        const BotChoice c = Bot(BotConfig{level}).decide(gs, Side::A);
        EXPECT_TRUE(std::holds_alternative<action::EndPileIns>(c.action)) << c.reason;
    }
}

TEST(M46_Consolidate, NormalAndHardConsolidateWhenItCostsNothing) {
    // The Consolidate step after a fight in which every attack missed, with only the Termagants' front rank: an
    // Ongoing Consolidation (12.08) into base contact costs the Intercessors nothing.
    GameState start = wh40k::test::consolidate_step_state();
    front_rank_only(start);
    ASSERT_EQ(start.pending->side, Side::A);
    const std::optional<double> gain = first_gain<action::StartConsolidation>(start, Side::A, false);
    ASSERT_TRUE(gain.has_value());
    EXPECT_NEAR(*gain, 0.0, 1e-6);
    for (const BotLevel level : {BotLevel::Normal, BotLevel::Hard}) {
        GameState gs = start;
        const client::FightMoveShape before = shape(gs, kMarines, kGants);
        Bot bot(BotConfig{level});
        const BotChoice c = bot.decide(gs, Side::A);
        ASSERT_TRUE(std::holds_alternative<action::StartConsolidation>(c.action)) << c.reason;
        (void)apply_action(gs, Side::A, c.action);
        run_move(gs, bot, Side::A);
        EXPECT_TRUE(gs.units[kMarines].phase_flags.consolidated);
        EXPECT_GT(shape(gs, kMarines, kGants).in_contact, before.in_contact);
        EXPECT_EQ(bot.fallbacks(), 0u);
    }
}

TEST(M46_Consolidate, AConsolidationThatBringsMoreEnemyAttackersIsStillSkipped) {
    // Both ranks: consolidating closer would bring the second rank into the next fight (as M41's pointless
    // consolidation), so the position gets worse and Normal and Hard end their consolidations.
    const GameState gs = wh40k::test::consolidate_step_state();
    const std::optional<double> gain = first_gain<action::StartConsolidation>(gs, Side::A, false);
    ASSERT_TRUE(gain.has_value());
    EXPECT_LT(*gain, -1e-6);
    for (const BotLevel level : {BotLevel::Normal, BotLevel::Hard}) {
        const BotChoice c = Bot(BotConfig{level}).decide(gs, Side::A);
        EXPECT_TRUE(std::holds_alternative<action::EndConsolidations>(c.action)) << c.reason;
    }
}

// ---- Easy: every move worth making, by Auto move ----

TEST(M46_Easy, PilesInWithAutoMoveThenEndsAConsolidationWithNothingToDo) {
    GameState gs = pile_in_step();
    const client::FightMoveShape before = shape(gs, kMarines, kGants);
    Bot easy(BotConfig{BotLevel::Easy});
    const BotChoice c = easy.decide(gs, Side::A);
    ASSERT_TRUE(std::holds_alternative<action::StartPileIn>(c.action)) << c.reason;
    EXPECT_NE(c.reason.find("auto move"), std::string::npos) << c.reason;
    (void)apply_action(gs, Side::A, c.action);
    run_move(gs, easy, Side::A);
    ASSERT_TRUE(gs.units[kMarines].phase_flags.piled_in);
    const client::FightMoveShape after = shape(gs, kMarines, kGants);
    EXPECT_GT(after.in_contact, before.in_contact);
    EXPECT_EQ(easy.fallbacks(), 0u);

    // The Termagants (also Easy) pile in too, then both units fight and every attack misses.
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::PileIn>(gs.pending->kind));
    ASSERT_EQ(gs.pending->side, Side::B);
    const BotChoice t = easy.decide(gs, Side::B);
    (void)apply_action(gs, Side::B, t.action);
    run_move(gs, easy, Side::B);
    while (gs.flow.stage == Stage::PileIn) (void)apply_action(gs, gs.pending->side, action::EndPileIns{});
    for (const UnitId unit : {gs.units[kMarines].id, gs.units[kGants].id}) {
        (void)apply_action(gs, gs.pending->side, action::SelectToFight{unit, FightType::Normal});
        ScriptedDice misses(std::vector<int>(64, 1));
        (void)apply_action(gs, gs.pending->side, wh40k::test::first_melee_options(gs, unit), misses);
    }
    ASSERT_EQ(gs.flow.stage, Stage::Consolidate);
    ASSERT_EQ(gs.pending->side, Side::A);
    // Every Intercessor is in base contact, so none can move in an Ongoing Consolidation (12.08): nothing is
    // worth making and Easy ends its consolidations.
    for (const GameAction& a : legal_actions(gs, Side::A)) {
        if (std::holds_alternative<action::StartConsolidation>(a)) {
            EXPECT_FALSE(client::fight_move_worthwhile(gs, Side::A, a));
        }
    }
    const BotChoice e = easy.decide(gs, Side::A);
    EXPECT_TRUE(std::holds_alternative<action::EndConsolidations>(e.action)) << e.reason;
}

// ---- reasons, arena, replay ----

TEST(M46_BotLog, FightMoveReasonsKeepWhatTheMoveDoes) {
    GameState gs = pile_in_step();
    front_rank_only(gs);
    const BotChoice c = Bot(BotConfig{BotLevel::Normal}).decide(gs, Side::A);
    ASSERT_TRUE(std::holds_alternative<action::StartPileIn>(c.action)) << c.reason;
    const std::string s = short_reason(c.reason);
    EXPECT_EQ(s.rfind("Intercessor Squad: pile in towards Termagants - ", 0), 0u) << s;
    EXPECT_EQ(s.find("+0.000"), std::string::npos) << s;  // the score is dropped
    EXPECT_EQ(client::describe_shape_change({}), "no closer");
}

TEST(M46_Arena, BotsPileInAndConsolidateInCleanGames) {
    // Two seeded games per level on the 20-Termagant scenario: clean, and the bots make fight moves (the
    // Release arena's 200 seeds are recorded in PLAN.md, M46).
    const GameState scenario =
        load_scenario(wh40k::test::repo_root() / kFightMoveScenario, wh40k::test::repo_root());
    for (const BotLevel level : {BotLevel::Easy, BotLevel::Normal, BotLevel::Hard}) {
        const ArenaResult r = run_arena(scenario, 2, 1, BotConfig{level}, BotConfig{level});
        EXPECT_TRUE(r.clean()) << format_arena(r, scenario);
        EXPECT_EQ(r.fallbacks, 0u) << format_arena(r, scenario);
        EXPECT_GT(r.fight_moves[0] + r.fight_moves[1], 0) << format_arena(r, scenario);
    }
}

TEST(M46_Replay, TheFightMoveGameIsDeterministicAndReplaysWithCoreOnly) {
    const BotGame first = play_fight_move_game();
    const BotGame second = play_fight_move_game();
    EXPECT_EQ(Json(first.replay).dump(2), Json(second.replay).dump(2));
    EXPECT_TRUE(first.record.finished);
    EXPECT_EQ(first.record.rejected, 0u);
    EXPECT_EQ(first.record.reverted, 0u);
    EXPECT_EQ(first.record.fallbacks, 0u);
    EXPECT_GT(first.record.fight_moves[0], 0);  // both sides pile in or consolidate
    EXPECT_GT(first.record.fight_moves[1], 0);
    const ReplayResult r = run_replay(Json::parse(Json(first.replay).dump()).get<Replay>(), wh40k::test::repo_root());
    ASSERT_TRUE(r.ok()) << "step " << *r.failed_step << ": " << r.reasons.front();
    EXPECT_EQ(replay_expectation(r.final_state), first.replay.expect);
}

// Writes tests/replays/m46_bot_fight_moves.json. Run on purpose only:
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M46_Replay.DISABLED_RegenerateFightMoveReplay
TEST(M46_Replay, DISABLED_RegenerateFightMoveReplay) {
    const BotGame g = play_fight_move_game();
    ASSERT_TRUE(g.record.finished);
    ASSERT_EQ(g.record.rejected, 0u);
    ASSERT_EQ(g.record.reverted, 0u);
    ASSERT_GT(g.record.fight_moves[0], 0);
    ASSERT_GT(g.record.fight_moves[1], 0);
    const ReplayResult r = run_replay(g.replay, wh40k::test::repo_root());
    ASSERT_TRUE(r.ok()) << "step " << *r.failed_step << ": " << r.reasons.front();
    if (::testing::Test::HasFailure()) return;  // never overwrite the committed replay with a broken game
    save_replay(g.replay, std::filesystem::path(WH40K_REPLAY_DIR) / "m46_bot_fight_moves.json");
}

// Finds seeds for the replay (run on purpose only).
TEST(M46_Replay, DISABLED_PrintFightMoveCounts) {
    const GameState scenario =
        load_scenario(wh40k::test::repo_root() / kFightMoveScenario, wh40k::test::repo_root());
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        const GameRecord g = play_bot_game(scenario, seed, BotConfig{BotLevel::Normal}, BotConfig{BotLevel::Normal});
        std::printf("seed %llu: fight moves A %d B %d, round %d, steps %zu\n", static_cast<unsigned long long>(seed),
                    g.fight_moves[0], g.fight_moves[1], g.end_round, g.steps);
    }
}
