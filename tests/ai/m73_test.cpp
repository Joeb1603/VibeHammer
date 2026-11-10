// M73 (D-080, amends D-077 and D-078): bots on custom armies.
// - Hard's guard: before any Evaluator is built, the decision's chain tables (hard::chain_plan_size: their count and
//   estimated bytes, plan_chain_tables' figures without building a spec) are checked against BotWeights'
//   chain_budget_mib (1 GiB) and chain_budget_tables (80); over either, Normal answers, its reason prefixed
//   "D-080: ...".
//   It never triggers on V1, the bot scenarios, Strike or the rosters.
// - Normal's points weighting: by points whenever every unit has points and either side has several datasheets (a side
//   of one datasheet was weighted 1 before); both sides of one datasheet each stay at 1.
// - Random armies finish clean at every level, and a Normal vs Normal custom battle is a replay.

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
#include "generation.hpp"
#include "hard/evaluator.hpp"
#include "hard/hard_bot.hpp"
#include "normal/value.hpp"
#include "session.hpp"
#include "support/army_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::repo_root;
namespace hard = wh40k::ai::hard;
namespace normal = wh40k::ai::normal;

namespace {

BotConfig level(BotLevel l, Generation g = Generation::Current) {
    BotConfig c;
    c.level = l;
    c.generation = g;
    return c;
}

// D-080's fixture armies (tests/data/armies): legal lists, written as save_army writes them.
ArmyList fixture_army(const std::string& file) { return load_army(repo_root() / "tests/data/armies" / file); }

// The custom battle of two fixture armies (not started), side A first.
GameState fixture_battle(const std::string& a, const std::string& b) {
    return wh40k::test::custom_battle(fixture_army(a), fixture_army(b));
}

// The most bytes two legal armies can plan (D-080): 7 x 8 types, 56 tables, 1,449 MB.
GameState most_bytes() {
    return fixture_battle("m73_max_bytes_chaos_space_marines.json", "m73_max_bytes_tyranids.json");
}

// The most tables two legal armies can plan (D-080): 8 x 10 types, 80 tables, 857 MB.
GameState most_tables() {
    return fixture_battle("m73_max_tables_chaos_space_marines.json", "m73_max_tables_tyranids.json");
}

GameState load(const char* file) { return load_scenario(repo_root() / file, repo_root()); }

// plan_chain_tables' count and the sum of its estimates: what chain_plan_size must give.
hard::ChainPlanSize full_plan(const GameState& gs) {
    hard::ChainPlanSize out;
    for (const hard::ChainTableInfo& i : hard::plan_chain_tables(gs, hard::ChainWeights{})) {
        ++out.tables;
        out.bytes += i.bytes;
    }
    return out;
}

// Started, at the first turn side's first Move Units decision (the engine's first legal action at every decision
// before it: nothing to do in the Command phase).
GameState at_first_move(GameState gs) {
    start_game(gs);
    for (int i = 0; i < 50 && gs.pending && !std::holds_alternative<decision::MoveUnits>(gs.pending->kind); ++i) {
        const Side side = gs.pending->side;
        apply_action(gs, side, legal_actions(gs, side).front());
    }
    return gs;
}

const ArmySource& source() {
    static const ArmySource s = load_army_source(repo_root());
    return s;
}

constexpr const char* kPrefixMostBytes =
    "D-080: Hard's chain tables here would be 56 tables of 1381.5 MiB, over its budget of 1024 MiB and 80 tables; "
    "Normal decides: ";

}  // namespace

// ---- Hard's guard ----

TEST(M73_HardGuard, ShippedScenariosAreUnderTheBudget) {
    // D-080: the budget is 1 GiB and 80 tables; the shipped scenarios plan at most the rosters' 30 tables of 525 MB (at
    // the first decision; the bytes only fall as the game goes on), so the guard never answers there.
    const hard::BotWeights w{};
    EXPECT_EQ(w.chain_budget_mib, 1024u);
    EXPECT_EQ(w.chain_budget_tables, 80u);
    struct Expected {
        const char* file;
        hard::ChainPlanSize size;
    };
    for (const Expected& e : {Expected{"data/scenarios/v1_skirmish.json", {1, 2'112'000}},
                              Expected{"data/scenarios/bot_termagants_first.json", {1, 2'112'000}},
                              Expected{"data/scenarios/bot_20_termagants.json", {1, 4'224'000}},
                              Expected{"data/scenarios/strike_44x60.json", {1, 20'592'000}},
                              Expected{"data/scenarios/rosters_44x60.json", {30, 525'096'000}}}) {
        for (const GameState& gs : {load(e.file), at_first_move(load(e.file))}) {
            const hard::ChainPlanSize size = hard::chain_plan_size(gs);
            const double bytes = static_cast<double>(size.bytes);
            std::printf("%s: %zu tables, %.1f MB (%.1f MiB)\n", e.file, size.tables, bytes / 1e6, bytes / (1 << 20));
            EXPECT_EQ(size, full_plan(gs)) << e.file;
            EXPECT_EQ(size, e.size) << e.file;
            EXPECT_FALSE(hard::chain_budget_problem(gs, w).has_value()) << e.file;
            // Well inside it: at most half the bytes and 30 tables.
            EXPECT_LE(size.bytes, (std::size_t{w.chain_budget_mib} << 20) / 2) << e.file;
            EXPECT_LE(size.tables, 30u) << e.file;
        }
    }
}

TEST(M73_HardGuard, ChainPlanSizeIsTheFullPlanWithoutBuildingIt) {
    // chain_plan_size counts the tables and bytes plan_chain_tables would give (the same types, grids, line, half-turns
    // and compact form) without building a spec; held equal on random armies, the worst cases, later half-turns, units
    // destroyed and the duel.
    std::vector<GameState> states;
    for (std::uint64_t seed = 1; seed <= 12; ++seed) states.push_back(wh40k::test::random_armies_battle(seed));
    states.push_back(most_bytes());
    states.push_back(most_tables());
    states.push_back(wh40k::test::load_v1_scenario());
    states.push_back(load(wh40k::test::kRostersScenario));
    const std::size_t base = states.size();
    for (std::size_t i = 0; i < base; ++i) {
        GameState later = states[i];  // battle round 3, the second turn: half-turn 5
        later.flow.battle_round = 3;
        later.flow.turn_in_round = 1;
        states.push_back(later);
        GameState fewer = states[i];  // every other unit destroyed
        for (std::size_t u = 0; u < fewer.units.size(); u += 2) fewer.units[u].models.clear();
        states.push_back(fewer);
        GameState wounded = states[i];  // models lost: types and grids are by starting strength, unchanged
        for (Unit& u : wounded.units) {
            if (u.models.size() > 1) u.models.pop_back();
        }
        states.push_back(wounded);
    }
    for (std::size_t i = 0; i < states.size(); ++i) {
        EXPECT_EQ(hard::chain_plan_size(states[i]), full_plan(states[i])) << "state " << i;
    }
    // The worst cases (D-080).
    EXPECT_EQ(hard::chain_plan_size(most_bytes()), (hard::ChainPlanSize{56, 1'448'647'200}));
    EXPECT_EQ(hard::chain_plan_size(most_tables()), (hard::ChainPlanSize{80, 856'627'200}));
    // No table without both sides.
    GameState one_side = most_bytes();
    for (Unit& u : one_side.units) {
        if (u.side == Side::B) u.models.clear();
    }
    EXPECT_EQ(hard::chain_plan_size(one_side), (hard::ChainPlanSize{}));
}

TEST(M73_HardGuard, AnOversizedArmyAnswersAsNormalWithTheReason) {
    // The most-bytes armies (1,382 MiB of tables) are over the budget all through battle round 1 (the bytes fall with
    // the half-turns left: 1,243 MiB in the second), so Hard (side A) answers every decision as Normal does, building
    // no table: the decisions Hard answers itself with the D-080 prefix, the others with Normal's reason as it is.
    GameState gs = at_first_move(most_bytes());
    ASSERT_EQ(gs.pending->side, Side::A);
    ASSERT_EQ(hard::chain_budget_problem(gs, hard::BotWeights{}), std::string(kPrefixMostBytes));
    Bot hard_a(level(BotLevel::Hard));
    Bot normal_a(level(BotLevel::Normal));
    Bot normal_b(level(BotLevel::Normal));
    int prefixed = 0;
    int plain = 0;
    while (gs.pending && gs.flow.battle_round == 1 && gs.flow.turn_in_round == 0) {
        const Side side = gs.pending->side;
        BotChoice choice;
        if (side == Side::A) {
            choice = hard_a.decide(gs, side);
            const BotChoice as_normal = normal_a.decide(gs, side);
            ASSERT_EQ(choice.action, as_normal.action) << choice.reason;
            if (choice.reason.rfind("D-080: ", 0) == 0) {
                ++prefixed;
                EXPECT_EQ(choice.reason, kPrefixMostBytes + as_normal.reason);
            } else {
                ++plain;
                EXPECT_EQ(choice.reason, as_normal.reason);
            }
        } else {
            choice = normal_b.decide(gs, side);
        }
        ASSERT_FALSE(choice.stopped);
        ASSERT_TRUE(validate_action(gs, side, choice.action).ok());
        apply_action(gs, side, choice.action);
    }
    EXPECT_GT(prefixed, 7);  // every unit's Move Units decision at least
    EXPECT_GT(plain, 0);     // shooting is Normal's anyway
    EXPECT_EQ(hard_a.chain_tables_built(), 0u);
    EXPECT_EQ(hard_a.fallbacks(), 0u);
}

TEST(M73_HardGuard, PureFunctionOfTheState) {
    // The guard reads the state (its unit types, wounds and half-turn) and the two weights, nothing else: not the
    // cache, not what the bot decided before.
    const hard::BotWeights w{};
    const GameState start = at_first_move(most_bytes());
    Bot fresh(level(BotLevel::Hard));
    Bot used(level(BotLevel::Hard));
    (void)used.decide(at_first_move(wh40k::test::load_v1_scenario()), Side::A);  // a table built, a memo kept
    ASSERT_GT(used.chain_tables_built(), 0u);
    const BotChoice a = fresh.decide(start, Side::A);
    const BotChoice b = used.decide(start, Side::A);
    EXPECT_EQ(a.action, b.action);
    EXPECT_EQ(a.reason, b.reason);
    EXPECT_EQ(a.reason.rfind(kPrefixMostBytes, 0), 0u);

    // The same army later in the game: the half-turns left fall, and from the fourth (battle round 2, second turn) it
    // is under the budget (1,014 MB), so Hard plays.
    for (const auto& [round, turn, over] : {std::tuple{1, 0, true}, std::tuple{1, 1, true}, std::tuple{2, 0, true},
                                            std::tuple{2, 1, false}, std::tuple{5, 1, false}}) {
        GameState later = start;
        later.flow.battle_round = round;
        later.flow.turn_in_round = turn;
        EXPECT_EQ(hard::chain_budget_problem(later, w).has_value(), over) << "round " << round << " turn " << turn;
    }
    // Two unit types destroyed (two 20-wound Termagant broods: 94 cells of B's 134 left, 1,016 MB): under it.
    GameState fewer = start;
    int cleared = 0;
    for (Unit& u : fewer.units) {
        if (u.name == "Termagants" && cleared < 2) {
            u.models.clear();
            ++cleared;
        }
    }
    EXPECT_EQ(hard::chain_plan_size(fewer).bytes, 1'016'215'200u);
    EXPECT_FALSE(hard::chain_budget_problem(fewer, w).has_value());

    // The two limits, each 0 for none.
    hard::BotWeights no_bytes = w;
    no_bytes.chain_budget_mib = 0;
    EXPECT_FALSE(hard::chain_budget_problem(start, no_bytes).has_value());  // 56 tables, within 80
    hard::BotWeights few_tables = no_bytes;
    few_tables.chain_budget_tables = 55;
    EXPECT_EQ(hard::chain_budget_problem(start, few_tables),
              std::string("D-080: Hard's chain tables here would be 56 tables of 1381.5 MiB, over its budget of no "
                          "limit on bytes and 55 tables; Normal decides: "));
    hard::BotWeights none = no_bytes;
    none.chain_budget_tables = 0;
    EXPECT_FALSE(hard::chain_budget_problem(most_tables(), none).has_value());
    hard::BotWeights no_tables = w;
    no_tables.chain_budget_tables = 0;
    EXPECT_TRUE(hard::chain_budget_problem(start, no_tables).has_value());  // the bytes still count

    // A lower budget hands the rosters to Normal: 30 tables over 29, or 500.8 MiB over 500.
    const GameState rosters = at_first_move(load(wh40k::test::kRostersScenario));
    EXPECT_FALSE(hard::chain_budget_problem(rosters, w).has_value());
    hard::BotWeights tables29 = w;
    tables29.chain_budget_tables = 29;
    EXPECT_EQ(hard::chain_budget_problem(rosters, tables29),
              std::string("D-080: Hard's chain tables here would be 30 tables of 500.8 MiB, over its budget of "
                          "1024 MiB and 29 tables; Normal decides: "));
    hard::BotWeights mib500 = w;
    mib500.chain_budget_mib = 500;
    EXPECT_TRUE(hard::chain_budget_problem(rosters, mib500).has_value());
    // And the bot then answers as Normal, building nothing.
    BotConfig low = level(BotLevel::Hard);
    low.hard = tables29;
    Bot guarded(low);
    Bot as_normal(level(BotLevel::Normal));
    const BotChoice g = guarded.decide(rosters, Side::A);
    const BotChoice n = as_normal.decide(rosters, Side::A);
    EXPECT_EQ(g.action, n.action);
    EXPECT_EQ(g.reason, *hard::chain_budget_problem(rosters, tables29) + n.reason);
    EXPECT_EQ(guarded.chain_tables_built(), 0u);
}

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

// ---- Random armies at every level ----

TEST(M73_Arena, RandomArmiesFinishCleanAtEveryLevel) {
    // The arena's random armies (--random-armies 1) at each level, a few short-enough games in Debug; the acceptance's
    // 200 seeds per pairing are run in Release (PLAN.md, M73). Hard on seeds 6 and 131 (two small armies a side: 4
    // tables of 47 MB and 41 MB).
    const auto battles = [](std::uint64_t seed) { return random_armies_battle(source(), seed, Side::A); };
    struct Run {
        BotLevel a;
        BotLevel b;
        std::uint64_t first;
        int games;
    };
    for (const Run& run : {Run{BotLevel::Easy, BotLevel::Easy, 1, 2}, Run{BotLevel::Normal, BotLevel::Normal, 1, 2},
                           Run{BotLevel::Hard, BotLevel::Normal, 6, 1}, Run{BotLevel::Normal, BotLevel::Hard, 131, 1},
                           Run{BotLevel::Hard, BotLevel::Hard, 6, 1}}) {
        const ArenaResult r = run_arena(battles, run.games, run.first, level(run.a), level(run.b));
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
// D-083 part 3 (M81): `generation`, the bots' (Generation::M77: the pre-batch Normal).
BotGame play_custom_bot_game(std::uint64_t seed = kReplaySeed, Generation generation = Generation::Current) {
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
    g.record = play_bot_session(session, level(BotLevel::Normal, generation), level(BotLevel::Normal, generation), 5000);
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

TEST(M73_Replay, ThePreBatchGameShootsAndFightsOnBothSides) {
    // The pre-batch Normal (Generation::M77) with the replay's seed, replayed through the engine alone, with the
    // assertions the saved file was checked with before M81 (D-083 part 3).
    const BotGame g = play_custom_bot_game(kReplaySeed, Generation::M77);
    ASSERT_TRUE(g.record.finished);
    EXPECT_EQ(g.record.rejected, 0u);
    const ReplayResult first = run_replay(g.replay, repo_root());
    ASSERT_TRUE(first.ok()) << "step " << *first.failed_step << ": " << first.reasons.front();
    ASSERT_TRUE(first.final_state.result.has_value());  // a result, not the step cap
    // Both sides fought (12.x) and both shot.
    for (const Side side : {Side::A, Side::B}) {
        const auto by = [&](auto pred) {
            return std::any_of(g.replay.steps.begin(), g.replay.steps.end(),
                               [&](const ReplayStep& s) { return s.side == side && pred(s.action); });
        };
        EXPECT_TRUE(by([](const GameAction& a) { return std::holds_alternative<action::Shoot>(a); }));
        EXPECT_TRUE(by([](const GameAction& a) { return std::holds_alternative<action::Fight>(a); }));
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
