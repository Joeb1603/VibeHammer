// M73 (D-080, amends D-077 and D-078): bots on custom armies. (D-085, M85: Hard's guard on its chain tables went with
// Hard.)
// - Normal's points weighting: by points whenever every unit has points and either side has several datasheets (a side
//   of one datasheet was weighted 1 before); both sides of one datasheet each stay at 1.
// - Random armies finish clean, and a Normal vs Normal custom battle is a replay.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "arena.hpp"
#include "bot.hpp"
#include "core/data/army_list.hpp"
#include "core/data/datasheet.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "normal/value.hpp"
#include "session.hpp"
#include "support/army_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::repo_root;
namespace normal = wh40k::ai::normal;

namespace {

BotConfig normal_bot() { return BotConfig{}; }

// D-080's fixture armies (tests/data/armies): legal lists, written as save_army writes them.
ArmyList fixture_army(const std::string& file) { return load_army(repo_root() / "tests/data/armies" / file); }

// The custom battle of two fixture armies (not started), side A first.
GameState fixture_battle(const std::string& a, const std::string& b) {
    return wh40k::test::custom_battle(fixture_army(a), fixture_army(b));
}

GameState load(const char* file) { return load_scenario(repo_root() / file, repo_root()); }

const ArmySource& source() {
    static const ArmySource s = load_army_source(repo_root());
    return s;
}

}  // namespace

// ---- Normal's points weighting (D-080 amends D-077 item 7) ----

TEST(M73_Weights, AOneDatasheetSideIsWeightedByPointsWhenTheOtherHasSeveral) {
    // Random seed 6: two Chaos Lords (one datasheet) against two Hive Tyrants and Termagants. Before D-080 the Lords
    // weighed 1 while the Tyranids were weighted by points; now every unit is points x N / sum of points.
    const GameState gs = wh40k::test::random_armies_battle(6);
    double total = 0.0;
    int lords = 0;
    for (const Unit& u : gs.units) {
        total += u.points;
        if (u.side == Side::A) {
            EXPECT_EQ(u.name, "Chaos Lord");
            ++lords;
        }
    }
    EXPECT_EQ(lords, 2);
    const double n = static_cast<double>(gs.units.size());
    for (const Unit& u : gs.units) {
        EXPECT_DOUBLE_EQ(normal::share_weight(gs, u), u.points * n / total) << u.name;
    }
    EXPECT_NEAR(normal::share_weight(gs, gs.units[0]), 0.688, 0.001);
    // The replay's armies: two Hive Tyrants (one datasheet) against four Chaos Space Marine units, 985 points in all.
    const GameState replay_battle = fixture_battle("m73_chaos_space_marines.json", "m73_tyranids.json");
    for (const Unit& u : replay_battle.units) {
        if (u.side == Side::B) EXPECT_DOUBLE_EQ(normal::share_weight(replay_battle, u), 195.0 * 6.0 / 985.0);
    }
    // A unit's share follows its weight (D-077), and [HAZARDOUS]'s exchange rate with it.
    const Unit& lord = gs.units[0];
    EXPECT_DOUBLE_EQ(normal::unit_share(gs, lord, 1.0), lord.points * n / total);
}

TEST(M73_Weights, BothSidesOfOneDatasheetStayAtOne) {
    // A Chaos Lord against two Hive Tyrants: one datasheet a side, so every weight is 1 even with points (M67's
    // arithmetic, as before).
    ArmyList lord{"Lord", "chaos_space_marines", {}};
    lord.units.push_back({"data/units/chaos_lord.json",
                          {wh40k::test::group("Chaos Lord", 1, {"Accursed weapon", "Plasma pistol"})}});
    const GameState gs = wh40k::test::custom_battle(lord, fixture_army("m73_tyranids.json"));
    for (const Unit& u : gs.units) {
        EXPECT_GT(u.points, 0);
        EXPECT_EQ(normal::share_weight(gs, u), 1.0) << u.name;
        EXPECT_EQ(normal::unit_share(gs, u, 1.0), 1.0);
    }
}

TEST(M73_Weights, ShippedScenariosAreUnchanged) {
    // The rosters have several datasheets a side (weights as at M68: points x 14 / 1915); V1, the bot scenarios and
    // Strike have no points (1).
    const GameState rosters = load(wh40k::test::kRostersScenario);
    for (const Unit& u : rosters.units) {
        EXPECT_DOUBLE_EQ(normal::share_weight(rosters, u), u.points * 14.0 / 1915.0) << u.name;
    }
    for (const char* file : {"data/scenarios/v1_skirmish.json", "data/scenarios/bot_termagants_first.json",
                             "data/scenarios/bot_20_termagants.json", "data/scenarios/strike_44x60.json"}) {
        const GameState gs = load(file);
        for (const Unit& u : gs.units) EXPECT_EQ(normal::share_weight(gs, u), 1.0) << file;
    }
}

// ---- Random armies ----

TEST(M73_Arena, RandomArmiesFinishClean) {
    // The arena's random armies (--random-armies 1), a few short-enough games in Debug: seeds 1-2 and the old Hard
    // pairings' seeds 6 and 131 (two small armies a side), the bot on both sides (D-085, M85); the acceptance's 200
    // seeds per pairing are run in Release (PLAN.md, M73).
    const auto battles = [](std::uint64_t seed) { return random_armies_battle(source(), seed, Side::A); };
    struct Run {
        std::uint64_t first;
        int games;
    };
    for (const Run& run : {Run{1, 2}, Run{6, 1}, Run{131, 1}}) {
        const ArenaResult r = run_arena(battles, run.games, run.first, normal_bot(), normal_bot());
        EXPECT_TRUE(r.clean()) << format_arena(r, battles(run.first));
        EXPECT_EQ(r.fallbacks, 0u);
        for (const GameRecord& g : r.games) {
            EXPECT_TRUE(g.on_points || g.winner.has_value()) << "seed " << g.seed;  // D-076: decided either way
        }
    }
}

// ---- The replay: Normal vs Normal on two non-stock armies ----

namespace {

constexpr const char* kArmyA = "m73_chaos_space_marines.json";
constexpr const char* kArmyB = "m73_tyranids.json";
// The custom battle of the two fixture armies as a scenario file (custom_battle_scenario's JSON), so the replay names
// its scenario as M68's does instead of embedding a state.
constexpr const char* kBattleScenario = "tests/data/scenarios/m73_custom_battle_44x60.json";
constexpr const char* kReplayFile = "m73_custom_bot_game.json";
constexpr std::uint64_t kReplaySeed = 1;

std::filesystem::path saved_path() { return std::filesystem::path(WH40K_REPLAY_DIR) / kReplayFile; }

Json battle_scenario() {
    return custom_battle_scenario(fixture_army(kArmyA), fixture_army(kArmyB), Side::A, wh40k::test::factions(),
                                  wh40k::test::army_resolver());
}

struct BotGame {
    Replay replay;
    GameRecord record;
};

// Played through GameSession as the arena plays (every action validated, the client's automatic skips, nothing
// auto-allocated for a bot); its history a scenario-form replay. Replaying it needs only core.
BotGame play_custom_bot_game(std::uint64_t seed = kReplaySeed) {
    BotGame g;
    g.replay.description =
        "M73: a whole custom battle played by the bot on both sides, level Normal vs Normal, with dice seed " +
        std::to_string(seed) +
        ". Two non-stock legal armies (tests/data/armies), auto-placed on the 44\"x60\" portrait battlefield "
        "(Incursion, decided on points destroyed, D-076): side A, Chaos Space Marines (595 points): a Chaos Lord with "
        "an Astartes chainblade and a power fist; 10 Legionaries with a plasma gun, a heavy melee weapon and a champion "
        "with a heavy melee weapon and a plasma pistol; Havocs with four missile launchers; 5 Chaos Terminators with "
        "two power fists. Side B, Tyranids (390 points): two Hive Tyrants, one with a heavy venom cannon, one with a "
        "bonesword and lash whip. A side of one datasheet, so D-080's points weighting applies. Replaying it needs only "
        "the engine.";
    g.replay.initial.scenario = kBattleScenario;
    g.replay.initial.seed = seed;
    wh40k::client::GameSession session(replay_initial_state(g.replay, repo_root()));
    g.record = play_bot_session(session, normal_bot(), normal_bot(), 5000);
    g.replay.steps = session.history();
    g.replay.expect = replay_expectation(session.state());
    return g;
}

}  // namespace

TEST(M73_Replay, TheScenarioIsTheTwoArmyFiles) {
    // The scenario file is custom_battle_scenario of the two army files (written by the regenerate test).
    EXPECT_EQ(read_json_file(repo_root() / kBattleScenario), battle_scenario());
    const GameState gs = load(kBattleScenario);
    EXPECT_EQ(gs, wh40k::test::custom_battle(fixture_army(kArmyA), fixture_army(kArmyB)));
    EXPECT_EQ(gs.result_rule, ResultRule::PointsDestroyed);
}

TEST(M73_Replay, TheCustomBotGameReplaysToItsResultWithCoreOnly) {
    const Replay saved = load_replay(saved_path());
    ASSERT_TRUE(saved.initial.scenario.has_value());
    EXPECT_EQ(*saved.initial.scenario, kBattleScenario);
    EXPECT_EQ(saved.initial.seed, kReplaySeed);
    EXPECT_FALSE(saved.initial.state.has_value());
    const ReplayResult first = run_replay(saved, repo_root());
    ASSERT_TRUE(first.ok()) << "step " << *first.failed_step << ": " << first.reasons.front();
    EXPECT_EQ(replay_expectation(first.final_state), saved.expect);
    const ReplayResult second = run_replay(saved, repo_root());
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(first.final_state, second.final_state);  // deterministic
    ASSERT_TRUE(first.final_state.result.has_value());  // a result, not the step cap
    // Both sides shot. D-083 part 3 (M81): the current Normal's game with this seed has no fight; the flow the seed was
    // chosen for (both sides shoot and fight) is the pre-batch Normal's, checked by ThePreBatchGameShootsAndFightsOnBothSides.
    for (const Side side : {Side::A, Side::B}) {
        EXPECT_TRUE(std::any_of(saved.steps.begin(), saved.steps.end(), [&](const ReplayStep& s) {
            return s.side == side && std::holds_alternative<action::Shoot>(s.action);
        }));
    }
}

TEST(M73_Replay, TheSavedFileMatchesTheGame) {
    // Normal vs Normal with the replay's seed plays exactly the saved game now (D-054: the bots are deterministic),
    // with nothing rejected, reverted or answered by the fallback.
    const Replay saved = load_replay(saved_path());
    const BotGame g = play_custom_bot_game();
    EXPECT_TRUE(g.record.finished);
    EXPECT_EQ(g.record.rejected, 0u);
    EXPECT_EQ(g.record.reverted, 0u);
    EXPECT_EQ(g.record.fallbacks, 0u);
    ASSERT_EQ(saved.steps.size(), g.replay.steps.size());
    for (std::size_t i = 0; i < saved.steps.size(); ++i) {
        ASSERT_EQ(Json(saved.steps[i]).dump(), Json(g.replay.steps[i]).dump()) << "step " << i;
    }
    EXPECT_EQ(Json(saved).dump(2), Json(g.replay).dump(2));
}

// Writes tests/data/scenarios/m73_custom_battle_44x60.json and tests/replays/m73_custom_bot_game.json. Run on purpose
// only:
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M73_Replay.DISABLED_RegenerateCustomBotGameReplay
TEST(M73_Replay, DISABLED_RegenerateCustomBotGameReplay) {
    {
        std::ofstream out(repo_root() / kBattleScenario, std::ios::binary);
        out << battle_scenario().dump(2) << "\n";
    }
    const BotGame g = play_custom_bot_game();
    ASSERT_TRUE(g.record.finished);  // a result, not the step cap
    ASSERT_EQ(g.record.rejected, 0u);
    ASSERT_EQ(g.record.reverted, 0u);
    ASSERT_EQ(g.record.fallbacks, 0u);
    const ReplayResult r = run_replay(g.replay, repo_root());
    ASSERT_TRUE(r.ok()) << "step " << *r.failed_step << ": " << r.reasons.front();
    ASSERT_TRUE(r.final_state.result.has_value());
    if (::testing::Test::HasFailure()) return;  // never overwrite the committed replay with a broken game
    save_replay(g.replay, saved_path());
}

// Picks the seed: the first whose Normal vs Normal game finishes clean with a Shoot and a Fight from each side.
TEST(M73_Replay, DISABLED_SearchCustomBotGameSeed) {
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        const BotGame g = play_custom_bot_game(seed);
        std::array<bool, 2> shot{};
        std::array<bool, 2> fought{};
        for (const ReplayStep& s : g.replay.steps) {
            if (std::holds_alternative<action::Shoot>(s.action)) shot[side_index(s.side)] = true;
            if (std::holds_alternative<action::Fight>(s.action)) fought[side_index(s.side)] = true;
        }
        const bool clean =
            g.record.finished && g.record.rejected == 0 && g.record.reverted == 0 && g.record.fallbacks == 0;
        std::printf("seed %llu: clean %d, steps %zu, round %d, winner %s, shot %d/%d, fought %d/%d\n",
                    static_cast<unsigned long long>(seed), clean ? 1 : 0, g.replay.steps.size(), g.record.end_round,
                    g.record.winner ? (*g.record.winner == Side::A ? "A" : "B") : "none", shot[0] ? 1 : 0,
                    shot[1] ? 1 : 0, fought[0] ? 1 : 0, fought[1] ? 1 : 0);
    }
}
