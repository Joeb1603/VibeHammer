// M69 (D-078, amends D-067): Hard for the roster units. One chain cache per game holding every pair of unit types in play
// under a memory cap (least recently used first, never a table in use, the half-turns already played dropped), built by
// several workers with the same values; the coarse wounds grid only above 20 wounds a side and the compact (float)
// tables only when the game's tables would be too big; a unit's volley as the sum of its loadout groups; [HAZARDOUS]
// priced in the chain; 17.01 in the rollouts' translations; the points tiebreak (D-076) in the army value, from the
// engine's own tally; the rollout budget scaled by the units with models when each Monte Carlo starts; and clean games
// of Hard against Normal on the roster scenario. The old scenarios' tables are the M68 code's, value for value.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "auto_actions.hpp"
#include "bot.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/state/points_destroyed.hpp"
#include "core/state/queries.hpp"
#include "hard/evaluator.hpp"
#include "hard/hard_bot.hpp"
#include "hard/lookahead.hpp"
#include "normal/movement.hpp"
#include "normal/normal_bot.hpp"
#include "normal/value.hpp"
#include "reach_area.hpp"
#include "session.hpp"
#include "simulator.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::decider;
using wh40k::test::in;
using wh40k::test::repo_root;
namespace mf = wh40k::test::monsters;
namespace normal = wh40k::ai::normal;

namespace {

// The roster scenario's units, by index (D-076: numbered west to east, Side A first).
constexpr std::size_t kChainswords = 0;
constexpr std::size_t kTerminators = 2;
constexpr std::size_t kHavocs = 3;
constexpr std::size_t kTermagants = 5;
constexpr std::size_t kGuard = 8;
constexpr std::size_t kTyrant = 9;

BotConfig level(BotLevel l) {
    BotConfig c;
    c.level = l;
    return c;
}

GameState scenario(const char* file) { return load_scenario(repo_root() / "data/scenarios" / file, repo_root()); }

// The roster scenario started, at Side A's first Move Units decision.
GameState rosters_at_start() {
    GameState gs = wh40k::test::load_rosters_scenario();
    start_game(gs);
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind);
    });
    return gs;
}

// Only the units at `keep` (indices into the roster scenario), the rest left out: a smaller army, same rules.
GameState rosters_with(std::initializer_list<std::size_t> keep) {
    const GameState all = rosters_at_start();
    GameState gs = all;
    gs.units.clear();
    for (const std::size_t i : keep) gs.units.push_back(all.units[i]);
    gs.pending = compute_decision(gs);
    return gs;
}

int count_units_with_models(const GameState& gs) {
    int n = 0;
    for (const Unit& u : gs.units) n += u.models.empty() ? 0 : 1;
    return n;
}

}  // namespace

// ==== 1. The cache ====

TEST(M69_Cache, TheRostersArmyNeedsThirtyTablesAndOneCacheHoldsThemAll) {
    // D-078: 5 Chaos Space Marine unit types x 6 Tyranid ones (the two Termagant, Genestealer and Screamer-killer units
    // share their types). The cache keeps all 30, so a second Evaluator of the game builds none (M68 kept 8 and rebuilt
    // all 30 for every Evaluator).
    const GameState gs = rosters_at_start();
    ASSERT_EQ(hard::plan_chain_tables(gs, hard::ChainWeights{}).size(), 30u);
    analysis::OddsCache odds;
    hard::ChainCache cache;
    {
        const hard::Evaluator e(odds, gs, Side::A, hard::ChainWeights{}, &cache);
        ASSERT_TRUE(e.ok());
        EXPECT_TRUE(e.army());
        EXPECT_EQ(e.tables_info().size(), 30u);
    }
    EXPECT_EQ(cache.size(), 30u);
    EXPECT_EQ(cache.builds(), 30u);
    EXPECT_EQ(cache.capacity(), 30u);
    // The tables' bytes: within the cap, and what the plan estimated (the pmfs aside).
    std::size_t estimated = 0;
    for (const hard::ChainTableInfo& i : hard::plan_chain_tables(gs, hard::ChainWeights{})) estimated += i.bytes;
    EXPECT_LE(cache.bytes(), hard::ChainCache::cap_bytes());
    EXPECT_GE(cache.bytes(), estimated);
    EXPECT_LT(cache.bytes(), estimated + estimated / 20);
    std::printf("the rosters' 30 tables: %.0f MB\n", static_cast<double>(cache.bytes()) / 1e6);
    // A later decision of the same game (Side B's first, after Side A's units remained stationary): covered.
    GameState later = gs;
    mf::play_until(later, [](const GameState& g) { return g.pending && g.pending->side == Side::B; });
    const hard::Evaluator again(odds, later, Side::B, hard::ChainWeights{}, &cache);
    EXPECT_EQ(cache.builds(), 30u);
}

TEST(M69_Cache, EvictionTakesTheLeastRecentlyUsedTableAndNeverOneInUse) {
    // D-078: four tables of one size (V1's duel at its start with four tempo weights: four keys). With the cap set to two
    // and a half tables' bytes, each new table evicts the least recently used one that no Evaluator holds.
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    const auto weights = [](int k) {
        hard::ChainWeights w;
        w.tempo = 0.2 + 0.01 * k;
        return w;
    };
    analysis::OddsCache odds;
    hard::ChainCache cache;
    std::vector<std::uint64_t> keys;
    for (int k = 0; k < 4; ++k) {
        const hard::Evaluator e(odds, gs, Side::A, weights(k));
        keys.push_back(e.tables_info().front().key);
    }
    ASSERT_EQ(std::set<std::uint64_t>(keys.begin(), keys.end()).size(), 4u);
    std::size_t one = 0;
    { const hard::Evaluator e(odds, gs, Side::A, weights(0), &cache); }
    one = cache.bytes();
    cache.set_cap_bytes(2 * one + one / 2);
    const auto held = [&] {
        std::vector<std::uint64_t> out;
        for (const hard::ChainTableInfo& i : cache.info()) out.push_back(i.key);
        return out;
    };
    const auto evaluate = [&](int k) { const hard::Evaluator e(odds, gs, Side::A, weights(k), &cache); };
    evaluate(1);
    EXPECT_EQ(held(), (std::vector<std::uint64_t>{keys[0], keys[1]}));
    evaluate(0);  // a hit: now the most recently used
    EXPECT_EQ(held(), (std::vector<std::uint64_t>{keys[1], keys[0]}));
    evaluate(2);
    EXPECT_EQ(held(), (std::vector<std::uint64_t>{keys[0], keys[2]}));  // the least recently used (1) went
    {
        // Table 0 in use: the next build evicts the other one; with both in use, none (the cache goes over the cap).
        const hard::Evaluator holding(odds, gs, Side::A, weights(0), &cache);
        evaluate(3);
        EXPECT_EQ(held(), (std::vector<std::uint64_t>{keys[0], keys[3]}));
        const hard::Evaluator also(odds, gs, Side::A, weights(3), &cache);
        evaluate(1);
        EXPECT_EQ(held(), (std::vector<std::uint64_t>{keys[0], keys[3], keys[1]}));
    }
    // Once nothing holds them, the next build makes room again, the least recently used first.
    evaluate(2);
    EXPECT_EQ(held(), (std::vector<std::uint64_t>{keys[1], keys[2]}));
    EXPECT_LE(cache.bytes(), 2 * one + one / 2);
    EXPECT_EQ(cache.builds(), 6u);
    // The count capacity of duels stays M44's eight.
    EXPECT_EQ(cache.capacity(), 8u);
}

TEST(M69_Cache, TrimmingDropsOnlyTheHalfTurnsAlreadyPlayed) {
    // D-078: Strike's table from battle round 1; dropping the half-turns before 4 keeps every later value.
    GameState gs = scenario("strike_44x60.json");
    start_game(gs);
    analysis::OddsCache odds;
    hard::ChainCache cache;
    std::uint64_t before = 0;
    {
        const hard::Evaluator e(odds, gs, Side::A, hard::ChainWeights{}, &cache);
        before = cache.fingerprint(4);
        cache.trim_before(4);  // in use: kept whole
        EXPECT_EQ(cache.info().front().h0, 0);
    }
    const std::size_t whole = cache.bytes();
    cache.trim_before(4);
    EXPECT_EQ(cache.info().front().h0, 4);
    EXPECT_EQ(cache.fingerprint(4), before);
    EXPECT_LT(cache.bytes(), whole * 7 / 10);  // 6 of 10 half-turns left
    // A decision of an earlier half-turn (an undo) builds its table again, with the same values.
    const std::size_t builds = cache.builds();
    { const hard::Evaluator e(odds, gs, Side::A, hard::ChainWeights{}, &cache); }
    EXPECT_EQ(cache.builds(), builds + 1);
}

TEST(M69_Cache, TheOldScenariosTablesAreTheM68Codes) {
    // D-078: the tables Hard builds along Normal's game on V1, both bot scenarios and Strike (seed 1, an Evaluator for
    // the side deciding at every 25th of the first 150 decisions, one cache) hold exactly the values the M68 code
    // (32358fa) gave for the same probe: the fingerprint of every node at every grid point (run once by hand on Linux
    // x86-64 with glibc; the game's positions go through libm, so elsewhere the test is skipped).
#if !(defined(__linux__) && defined(__x86_64__) && defined(__GLIBC__))
    GTEST_SKIP() << "the expected fingerprints were taken with glibc's libm on Linux x86-64";
#endif
    const std::vector<std::pair<const char*, std::uint64_t>> expected{
        {"v1_skirmish.json", 0x726d91bab40d921bull},
        {"bot_termagants_first.json", 0x8faf6155bb47b408ull},
        {"bot_20_termagants.json", 0x0b842f78a5c48c1bull},
        {"strike_44x60.json", 0x80da9c490ce05247ull},
    };
    for (const auto& [file, fingerprint] : expected) {
        hard::ChainCache cache;
        wh40k::client::GameSession session = wh40k::client::GameSession::new_game(scenario(file), 1);
        std::array<Bot, 2> bots{Bot(level(BotLevel::Normal)), Bot(level(BotLevel::Normal))};
        wh40k::client::AutoSettings autos;
        autos.skip_empty_steps = true;
        autos.auto_allocate = false;
        autos.bot_controlled = {true, true};
        for (int k = 0; k < 150 && session.pending(); ++k) {
            (void)wh40k::client::run_auto_actions(session, autos);
            if (!session.pending()) break;
            const Side side = session.pending()->side;
            if (k % 25 == 0 && !session.state().result) {
                analysis::OddsCache odds;
                const hard::Evaluator e(odds, session.state(), side, hard::ChainWeights{}, &cache);
            }
            const BotChoice c = bots[side_index(side)].decide(session.state(), side);
            ASSERT_FALSE(c.stopped) << file;
            ASSERT_TRUE(session.submit(c.action).ok()) << file;
        }
        EXPECT_EQ(cache.fingerprint(), fingerprint) << file;
        for (const hard::ChainTableInfo& i : cache.info()) {
            EXPECT_TRUE(i.exact[0] && i.exact[1] && !i.compact) << file;  // every wound, full precision
        }
    }
}

TEST(M69_Cache, OneWorkerAndFourBuildTheSameTablesAndHardDecidesAlike) {
    // D-078: parallel builds (each worker its own odds cache) give the serial build's tables, and so the same decisions:
    // the rosters cut to two battle rounds (the same 30 pairs, the compact form, a quicker build in Debug).
    GameState gs = rosters_at_start();
    gs.battle_rounds = 2;
    std::array<std::uint64_t, 2> fingerprints{};
    for (const std::size_t workers : {std::size_t{1}, std::size_t{4}}) {
        analysis::OddsCache odds;
        hard::ChainCache cache;
        cache.set_workers(workers);
        const hard::Evaluator e(odds, gs, Side::A, hard::ChainWeights{}, &cache);
        EXPECT_EQ(cache.size(), 30u);
        EXPECT_TRUE(cache.info().front().compact);
        fingerprints[workers == 1 ? 0 : 1] = cache.fingerprint();
    }
    EXPECT_EQ(fingerprints[0], fingerprints[1]);
    // Two Hard bots, one building with one worker and one with four, at Hard's first decisions of a game Normal plays:
    // the same action and reason each time.
    BotConfig one = level(BotLevel::Hard);
    one.hard.lookahead.table_workers = 1;
    BotConfig four = level(BotLevel::Hard);
    four.hard.lookahead.table_workers = 4;
    Bot a(one);
    Bot b(four);
    wh40k::client::GameSession session = wh40k::client::GameSession::new_game(gs, 3);
    Bot normal(level(BotLevel::Normal));
    wh40k::client::AutoSettings autos;
    autos.skip_empty_steps = true;
    autos.auto_allocate = false;
    autos.bot_controlled = {true, true};
    int compared = 0;
    for (int k = 0; k < 40 && compared < 4 && session.pending(); ++k) {
        (void)wh40k::client::run_auto_actions(session, autos);
        if (!session.pending()) break;
        const Side side = session.pending()->side;
        if (std::holds_alternative<decision::MoveUnits>(session.pending()->kind) ||
            std::holds_alternative<decision::OrderCasualties>(session.pending()->kind)) {
            const BotChoice x = a.decide(session.state(), side);
            const BotChoice y = b.decide(session.state(), side);
            EXPECT_EQ(x.action, y.action) << k;
            EXPECT_EQ(x.reason, y.reason) << k;
            a.forget();
            b.forget();
            ++compared;
        }
        const BotChoice c = normal.decide(session.state(), side);
        ASSERT_TRUE(session.submit(c.action).ok());
    }
    EXPECT_GE(compared, 2);
}

// ==== 2. The grid and the form ====

TEST(M69_Grid, OnlyASideOverTwentyWoundsIsCoarseAndOnlyTheRostersAreCompact) {
    // D-078: Strike (Intercessors 10 wounds, Termagants 10) and the 20 Termagants (20: not over 20) stay exact and at full
    // precision, V1 too; the rosters' tables are compact, and only the Chaos Terminators' side (30 wounds) is coarse:
    // 16 levels, 0, 2, ..., 30.
    for (const char* file : {"strike_44x60.json", "bot_20_termagants.json", "v1_skirmish.json"}) {
        GameState gs = scenario(file);
        start_game(gs);
        for (const hard::ChainTableInfo& i : hard::plan_chain_tables(gs, hard::ChainWeights{})) {
            EXPECT_TRUE(i.exact[0]) << file;
            EXPECT_TRUE(i.exact[1]) << file;
            EXPECT_FALSE(i.compact) << file;
            EXPECT_EQ(i.levels[0], i.max_wounds[0] + 1) << file;
            EXPECT_EQ(i.levels[1], i.max_wounds[1] + 1) << file;
        }
    }
    GameState twenty = scenario("bot_20_termagants.json");
    start_game(twenty);
    EXPECT_EQ(hard::plan_chain_tables(twenty, hard::ChainWeights{}).front().max_wounds[1], 20);
    const GameState gs = rosters_at_start();
    const std::vector<hard::ChainTableInfo> plan = hard::plan_chain_tables(gs, hard::ChainWeights{});
    ASSERT_EQ(plan.size(), 30u);
    int coarse = 0;
    for (std::size_t k = 0; k < plan.size(); ++k) {
        const hard::ChainTableInfo& i = plan[k];
        EXPECT_TRUE(i.compact) << k;
        EXPECT_TRUE(i.exact[1]) << k;  // no Tyranid unit starts with more than 20 wounds (the Tyrannofex 16)
        // Side A's unit types in unit order, 6 Tyranid types each: the third is the Chaos Terminator Squad.
        if (k / 6 == 2) {
            EXPECT_FALSE(i.exact[0]) << k;
            EXPECT_EQ(i.levels[0], 16) << k;
            EXPECT_EQ(i.max_wounds[0], 30) << k;
            ++coarse;
        } else {
            EXPECT_TRUE(i.exact[0]) << k;
        }
    }
    EXPECT_EQ(coarse, 6);
}

TEST(M69_Grid, ACoarseSideInterpolatesBetweenLevelsAndKeepsTheExpectedWoundsOfAVolley) {
    // The Chaos Terminators (30 wounds, the coarse grid) against 10 Termagants, alone, on the roster board and rules:
    // a lookup at 29 wounds is halfway between levels 28 and 30; the Termagants' volley at the Terminators, folded onto
    // the grid, keeps the exact outcome's expected wounds left and its chance of a wipe-out.
    const GameState gs = rosters_with({kTerminators, kTermagants});
    analysis::OddsCache odds;
    const hard::Evaluator e(odds, gs, Side::A, hard::ChainWeights{});
    ASSERT_TRUE(e.army());
    ASSERT_FALSE(e.tables_info().front().exact[0]);
    for (const float gap : {0.0f, in(9.0f), in(20.0f)}) {
        const std::array<float, 2> rooms{in(4.0f), in(30.0f)};
        const hard::Evaluation lo = e.at(2, {28, 10}, gap, rooms);
        const hard::Evaluation hi = e.at(2, {30, 10}, gap, rooms);
        const hard::Evaluation mid = e.at(2, {29, 10}, gap, rooms);
        EXPECT_NEAR(mid.score, 0.5 * (lo.score + hi.score), 1e-12) << gap;
        EXPECT_NEAR(mid.p_win, 0.5 * (lo.p_win + hi.p_win), 1e-12) << gap;
    }
    const Unit& gants = gs.units[1];
    const Unit& terms = gs.units[0];
    for (const int g : {1, 3, 5}) {
        const std::vector<double> chain = e.volley_outcome(gants, terms, false, g, 10, 30);
        ASSERT_EQ(chain.size(), 16u);  // levels 0, 2, ..., 30
        analysis::ShotContext ctx{ShootingType::Normal, true, std::max(static_cast<float>(g) * in(2.0f),
                                                                       kEngagementRangeMm + in(0.1f))};
        const analysis::Volley v = analysis::unit_ranged_volley(odds, gants, terms, ctx);
        const analysis::AttackOutcome exact = analysis::expected_outcome(odds, v, analysis::snapshot_of(terms));
        double mean_exact = 0.0;
        for (std::size_t w = 0; w < exact.wounds_left_pmf.size(); ++w) mean_exact += exact.wounds_left_pmf[w] * w;
        double mean_chain = 0.0;
        double total = 0.0;
        for (std::size_t l = 0; l < chain.size(); ++l) {
            mean_chain += chain[l] * static_cast<double>(2 * l);
            total += chain[l];
        }
        EXPECT_NEAR(total, 1.0, 1e-12) << g;
        EXPECT_NEAR(mean_chain, mean_exact, 1e-9) << g;
        EXPECT_NEAR(chain[0], exact.wounds_left_pmf[0], 1e-15) << g;
    }
}

// ==== 3. A unit's volley is the sum of its loadout groups ====

TEST(M69_Loadouts, TheChainsVolleyIsTheSumOfTheUnitsLoadoutGroups) {
    // D-078: the Havocs (four lascannons, a champion with a plasma gun and a chainsword) against the Tyrant Guard: the
    // chain's volley of the five is the engine-shaped sum of each model's own picks ([HAZARDOUS] priced at Normal's
    // rate, D-077), not five copies of a lascannon Havoc; with four alive, the four the defender keeps (worth most
    // against the Guard, Normal's model_worth).
    const GameState gs = rosters_with({kHavocs, kGuard});
    const Unit& havocs = gs.units[0];
    const Unit& guard = gs.units[1];
    analysis::OddsCache odds;
    const hard::Evaluator e(odds, gs, Side::A, hard::ChainWeights{});
    ASSERT_TRUE(e.army());
    const auto engine_shaped = [&](const Unit& shooters, int g) {
        analysis::ShotContext ctx{ShootingType::Normal, true, static_cast<float>(g) * in(2.0f)};
        ctx.hazard_rate = normal::hazard_rate(gs, havocs, guard);
        const analysis::Volley v = analysis::unit_ranged_volley(odds, shooters, guard, ctx);
        return analysis::expected_outcome(odds, v, analysis::snapshot_of(guard)).wounds_left_pmf;
    };
    // The keep order: each Havoc's worth against the Guard.
    const std::map<ModelId, double> worth = normal::model_worth(odds, havocs, guard);
    std::vector<Model> kept = havocs.models;
    std::stable_sort(kept.begin(), kept.end(), [&](const Model& a, const Model& b) { return worth.at(a.id) > worth.at(b.id); });
    Unit four = havocs;
    four.models.assign(kept.begin(), kept.begin() + 4);
    for (const int g : {3, 6, 12}) {  // 6", 12" (the plasma gun's half range) and 24"
        const std::vector<double> all = e.volley_outcome(havocs, guard, false, g, 15, 12);
        const std::vector<double> want = engine_shaped(havocs, g);
        ASSERT_EQ(all.size(), want.size());
        for (std::size_t w = 0; w < want.size(); ++w) EXPECT_NEAR(all[w], want[w], 1e-12) << g << " " << w;
        const std::vector<double> alive4 = e.volley_outcome(havocs, guard, false, g, 12, 12);
        const std::vector<double> want4 = engine_shaped(four, g);
        ASSERT_EQ(alive4.size(), want4.size());
        for (std::size_t w = 0; w < want4.size(); ++w) EXPECT_NEAR(alive4[w], want4[w], 1e-12) << g << " " << w;
    }
    // Five copies of the lascannon Havoc (the M68 representative) would deal more against the Guard at 24" than the
    // real squad, whose champion's plasma gun does far less against T8 than a fifth lascannon.
    Unit five_lascannons = havocs;
    for (Model& m : five_lascannons.models) {
        const ModelId id = m.id;
        m = havocs.models.back();
        m.id = id;
    }
    const std::vector<double> copies = engine_shaped(five_lascannons, 12);
    const std::vector<double> real = e.volley_outcome(havocs, guard, false, 12, 15, 12);
    EXPECT_GT(real.back(), copies.back());  // more likely to leave the Guard untouched
}

// ==== 4. 17.01 in the rollouts' translations ====

TEST(M69_Rollouts, AMonstersTranslationPassesThroughInfantryButNotInfantrys) {
    // 17.01 (D-073, D-078): the Hive Tyrant 2.04" north of the Legionaries' rank: the rollout policy's straight move 7.7"
    // south crosses the rank and ends beyond it, and the engine accepts it. The Tyrant Guard (INFANTRY) asked the same
    // never crosses an enemy model.
    const GameState gs = mf::at_movement(false, Side::B);
    analysis::OddsCache odds;
    const hard::Evaluator eval(odds, gs, Side::B, hard::ChainWeights{});
    ASSERT_TRUE(eval.army());
    const Unit& tyrant = gs.units[mf::kTyrant];
    Simulator sim(gs, 69);
    ASSERT_TRUE(sim.step(Side::B, action::StartMove{tyrant.id, MoveType::Normal, std::nullopt}).ok());
    const std::optional<std::vector<GameAction>> steps =
        hard::rollout_translation(eval, sim.state(), 0.0f, -1.0f, in(mf::kTyrantY - mf::kThroughY));
    ASSERT_TRUE(steps.has_value());
    const auto* p = std::get_if<action::ProposeModelMove>(&steps->front());
    ASSERT_NE(p, nullptr);
    EXPECT_LT(p->path.back().y, in(mf::kRankY));  // beyond the rank
    for (const GameAction& a : *steps) ASSERT_TRUE(sim.step(Side::B, a).ok());
    EXPECT_FALSE(sim.state().flow.move.has_value());  // committed, not reverted
    const Unit& guard = gs.units[mf::kGuard];
    Simulator guard_sim(gs, 69);
    ASSERT_TRUE(guard_sim.step(Side::B, action::StartMove{guard.id, MoveType::Normal, std::nullopt}).ok());
    const std::vector<PosedModel> enemies = posed_enemy_models(guard_sim.state(), Side::B);
    if (const auto g = hard::rollout_translation(eval, guard_sim.state(), 1.0f, -1.0f, in(6.0f))) {
        for (const GameAction& a : *g) {
            const auto* m = std::get_if<action::ProposeModelMove>(&a);
            if (m == nullptr) continue;
            const Model& model = *find_model(guard, m->model);
            for (const PosedModel& en : enemies) {
                EXPECT_GE(wh40k::client::point_segment_distance(en.position, model.position, m->path.back()),
                          base_radius(model.base) + base_radius(en.base) - 0.01f);
            }
        }
    }
}

// ==== 5. The points tiebreak in the army value ====

TEST(M69_Points, AnIncrementIsWhatTheEnginesTallyAddsWhenTheUnitIsDestroyed) {
    // D-078: for every roster unit, whole and after losses, points_increment is exactly what core::points_destroyed adds
    // for its destroyer when the unit is destroyed (a copy with the unit's models removed).
    GameState gs = rosters_at_start();
    gs.units[kChainswords].models.resize(7);  // 3 Legionaries lost
    gs.units[kTerminators].models[0].wounds_remaining = 1;
    gs.units[kTyrant].models[0].wounds_remaining = 4;
    gs.units[kTermagants].models.resize(2);
    for (const GameState* state : {&gs}) {
        const PointsTally now = points_destroyed(*state);
        for (std::size_t i = 0; i < state->units.size(); ++i) {
            GameState gone = *state;
            gone.units[i].models.clear();
            const PointsTally after = points_destroyed(gone);
            const std::size_t by = side_index(opponent(state->units[i].side));
            EXPECT_EQ(after.destroyed[by] - now.destroyed[by], hard::points_increment(state->units[i], now.denominator)) << i;
            EXPECT_EQ(after.destroyed[1 - by], now.destroyed[1 - by]) << i;
            EXPECT_EQ(hard::points_increment(gone.units[i], now.denominator), 0) << i;  // destroyed: nothing more to add
        }
    }
}

TEST(M69_Points, TheLeadIsTheExactSignWithoutVarianceAndErfOtherwise) {
    // D-078: T = P(A ahead) - P(B ahead) = erf(mu / (sigma sqrt 2)) (Abramowitz and Stegun 7.1.28, within 3e-7 of the
    // C library's erf), the exact sign of mu when sigma = 0 (0 for an exact tie).
    EXPECT_EQ(hard::points_lead(240.0, 0.0), 1.0);
    EXPECT_EQ(hard::points_lead(-1.0, 0.0), -1.0);
    EXPECT_EQ(hard::points_lead(0.0, 0.0), 0.0);
    double prev = -1.0;
    for (const double mu : {-500.0, -120.0, -40.0, -1.0, 0.0, 1.0, 40.0, 120.0, 500.0}) {
        const double t = hard::points_lead(mu, 100.0 * 100.0);
        EXPECT_NEAR(t, std::erf(mu / (100.0 * std::sqrt(2.0))), 3e-7) << mu;
        EXPECT_GE(t, prev) << mu;
        EXPECT_LE(std::fabs(t), 1.0) << mu;
        EXPECT_EQ(hard::points_lead(-mu, 1e4), -t) << mu;
        prev = t;
    }
    EXPECT_EQ(hard::points_lead(1e9, 1.0), 1.0);  // far ahead: certain
}

TEST(M69_Points, AFinishedGameIsValuedAsTheEngineDecidedIt) {
    // D-078: Easy against Easy on the rosters to the end (decided on points after battle round 5, D-076): the army value
    // of the final state is the engine's result for each side; and with the result taken away and every half-turn
    // played (a hypothetical position past battle round 5), core::points_destroyed decides it the same way.
    wh40k::client::GameSession session = wh40k::client::GameSession::new_game(wh40k::test::load_rosters_scenario(), 2);
    const GameRecord g = play_bot_session(session, level(BotLevel::Easy), level(BotLevel::Easy), 5000);
    ASSERT_TRUE(g.finished);
    const GameState& end = session.state();
    ASSERT_TRUE(end.result.has_value());
    ASSERT_TRUE(end.result->points.has_value());
    GameState unsettled = end;
    unsettled.result.reset();
    unsettled.flow.battle_round = end.battle_rounds + 1;
    unsettled.flow.turn_in_round = 0;
    for (const Side side : {Side::A, Side::B}) {
        analysis::OddsCache odds;
        const hard::Evaluator e(odds, end, side, hard::ChainWeights{});
        for (const GameState* s : {&end, static_cast<const GameState*>(&unsettled)}) {
            const hard::Evaluation v = e.evaluate(*s);
            if (!end.result->winner) {
                EXPECT_EQ(v.p_draw, 1.0);
            } else if (*end.result->winner == side) {
                EXPECT_EQ(v.p_win, 1.0);
                EXPECT_EQ(v.v, 1.0);
            } else {
                EXPECT_EQ(v.p_loss, 1.0);
                EXPECT_EQ(v.v, -1.0);
            }
        }
    }
}

TEST(M69_Points, ThePointsRuleSettlesTheDrawAndWithoutItTheArmyValueIsDSevens) {
    // D-078: in battle round 1 of the rosters nobody is likely to be wiped out, so under D-007 (the rule taken out of
    // the same state) the army value is nearly all draw; under the points rule P(draw) is 0 (no exact tie) and the
    // value's win and loss are P(ahead on points). One unit each keeps the army path under the rule, M44's duel without.
    const GameState gs = rosters_at_start();
    GameState d007 = gs;
    d007.result_rule = ResultRule::D007Draw;
    analysis::OddsCache odds;
    hard::ChainCache cache;
    const hard::Evaluator points(odds, gs, Side::A, hard::ChainWeights{}, &cache);
    const hard::Evaluation p = points.evaluate(gs);
    EXPECT_NEAR(p.p_draw, 0.0, 1e-12);
    EXPECT_NEAR(p.p_win + p.p_loss, 1.0, 1e-12);
    EXPECT_NEAR(p.v, p.p_win - p.p_loss, 1e-12);
    const hard::Evaluator draw(odds, d007, Side::A, hard::ChainWeights{}, &cache);
    const hard::Evaluation d = draw.evaluate(d007);
    EXPECT_GT(d.p_draw, 0.9);
    const GameState pair = rosters_with({kHavocs, kGuard});
    EXPECT_FALSE(hard::duel_play(pair).has_value());
    EXPECT_TRUE(hard::duel_of(pair).has_value());
    GameState pair_d007 = pair;
    pair_d007.result_rule = ResultRule::D007Draw;
    EXPECT_TRUE(hard::duel_play(pair_d007).has_value());
    const hard::Evaluator army(odds, pair, Side::A, hard::ChainWeights{});
    EXPECT_TRUE(army.army());
    const hard::Evaluator duel(odds, pair_d007, Side::A, hard::ChainWeights{});
    EXPECT_FALSE(duel.army());
}

TEST(M69_Points, UnderThePointsRuleHardOffersNoKeepAwayMove) {
    // D-078: D-067's extra candidate, the move ending furthest from every enemy model, played for D-007's draw; under the
    // points tiebreak there is no draw to play for, so Hard's move plans leave it out (the same origin with D-007's rule
    // keeps it, one plan more when it is not one of the others).
    GameState gs = rosters_at_start();
    const std::vector<GameAction> legal = legal_actions(gs, Side::A);
    const std::optional<UnitId> unit = normal::unit_to_move(gs, legal);
    ASSERT_TRUE(unit.has_value());
    Simulator sim(gs, 69);
    ASSERT_TRUE(sim.step(Side::A, action::StartMove{*unit, MoveType::Normal, std::nullopt}).ok());
    const GameState origin = sim.state();
    GameState d007 = origin;
    d007.result_rule = ResultRule::D007Draw;
    analysis::OddsCache cache;
    hard::HardContext hc;
    const std::optional<hard::HardMovePlan> points = hard::hard_plan_unit_move(hc, cache, origin, false);
    const std::optional<hard::HardMovePlan> draw = hard::hard_plan_unit_move(hc, cache, d007, false);
    ASSERT_TRUE(points.has_value());
    ASSERT_TRUE(draw.has_value());
    // Whether the furthest move is one of the plans already (then D-007 adds nothing).
    const std::optional<normal::PlannedMove> far = normal::furthest_standing_move(origin);
    ASSERT_TRUE(far.has_value());
    bool known = false;
    if (const std::optional<normal::PlannedMove> np = normal::plan_unit_move(cache, origin)) {
        known = np->candidate.ends == far->candidate.ends;
    }
    for (const normal::PlannedMove& pm : normal::standing_moves(cache, origin, hc.weights.move, hc.weights.static_plans, 12)) {
        known = known || pm.candidate.ends == far->candidate.ends;
    }
    EXPECT_EQ(draw->standing, points->standing + (known ? 0u : 1u));
}

// ==== 5b. Hard's move search by army size ====

namespace {

// The roster scenario played by Normal on both sides until Side B's first Move Units decision.
GameState rosters_at_side_b_movement() {
    wh40k::client::GameSession s = wh40k::client::GameSession::new_game(wh40k::test::load_rosters_scenario(), 5);
    std::array<Bot, 2> bots{Bot(level(BotLevel::Normal)), Bot(level(BotLevel::Normal))};
    wh40k::client::AutoSettings autos;
    autos.bot_controlled = {true, true};
    const auto at_b_movement = [&] {
        return s.pending() && s.pending()->side == Side::B && std::holds_alternative<decision::MoveUnits>(s.pending()->kind);
    };
    for (int guard = 0; guard < 2000 && s.pending() && !at_b_movement(); ++guard) {
        (void)wh40k::client::run_auto_actions(s, autos);
        if (!s.pending() || at_b_movement()) break;
        const Side side = s.pending()->side;
        const BotChoice c = bots[side_index(side)].decide(s.state(), side);
        EXPECT_TRUE(s.submit(c.action).ok()) << c.reason;
    }
    return s.state();
}

bool same(const PolicyResult& a, const PolicyResult& b) {
    if (!(a.action == b.action) || a.reason != b.reason || a.rest.size() != b.rest.size()) return false;
    for (std::size_t i = 0; i < a.rest.size(); ++i) {
        if (a.rest[i].expect_hash != b.rest[i].expect_hash || !(a.rest[i].action == b.rest[i].action)) return false;
    }
    return true;
}

}  // namespace

TEST(M69_Moves, ASideWithMoreThanSevenUnitsWithModelsMovesAsNormal) {
    // D-078: Hard searches its own unit moves only while its side has at most BotWeights::move_search_units (7) units
    // with models. The Chaos Space Marines (5 units) get Hard's own answer, the Tyranids (9) Normal's, plan and all.
    const hard::BotWeights defaults;
    ASSERT_EQ(defaults.move_search_units, 7u);
    {
        const GameState gs = rosters_at_start();
        const std::vector<GameAction> legal = legal_actions(gs, Side::A);
        hard::HardContext hc;
        const PolicyResult r = hard::hard_policy(gs, Side::A, legal, decision_seed(gs), hc);
        EXPECT_NE(r.reason.find("(Hard: "), std::string::npos) << r.reason;
    }
    const GameState gs = rosters_at_side_b_movement();
    ASSERT_TRUE(gs.pending && gs.pending->side == Side::B &&
                std::holds_alternative<decision::MoveUnits>(gs.pending->kind));
    const std::vector<GameAction> legal = legal_actions(gs, Side::B);
    const std::uint64_t seed = decision_seed(gs);
    hard::HardContext hc;
    const PolicyResult hard_answer = hard::hard_policy(gs, Side::B, legal, seed, hc);
    const PolicyResult normal_answer = normal_policy(gs, Side::B, legal, seed);
    EXPECT_TRUE(same(hard_answer, normal_answer)) << hard_answer.reason << " / " << normal_answer.reason;
    EXPECT_EQ(hard_answer.reason.find("(Hard: "), std::string::npos) << hard_answer.reason;
    // With the threshold at the Tyranids' 9 units, Hard searches their moves too.
    hc.weights.move_search_units = 9;
    const PolicyResult searched = hard::hard_policy(gs, Side::B, legal, seed, hc);
    EXPECT_NE(searched.reason.find("(Hard: "), std::string::npos) << searched.reason;
    // Every side of V1, the bot scenarios and Strike has at most 7 units, so there Hard always searches.
    for (const char* f : {"v1_skirmish.json", "bot_termagants_first.json", "bot_20_termagants.json", "strike_44x60.json"}) {
        const GameState s = scenario(f);
        for (const Side side : {Side::A, Side::B}) {
            std::size_t n = 0;
            for (const Unit& u : s.units) n += u.side == side ? 1 : 0;
            EXPECT_LE(n, defaults.move_search_units) << f;
        }
    }
}

// ==== 6. The rollout budget ====

TEST(M69_Rollouts, TheBudgetScalesWithTheUnitsWithModelsWhenEachMonteCarloStarts) {
    // D-078: up to 7 units with models (V1, the bot scenarios, Strike) the budget is the weights'; with 14, 3 rollouts
    // and 3000 actions a Monte Carlo; it rises again as units are destroyed.
    const hard::LookaheadWeights w;
    GameState gs = rosters_at_start();
    ASSERT_EQ(count_units_with_models(gs), 14);
    const auto budget = [&](const GameState& s) { return hard::rollout_budget(w, s); };
    EXPECT_EQ(budget(gs).rollouts, 3u);
    EXPECT_EQ(budget(gs).budget_actions, 3000u);
    EXPECT_EQ(budget(gs).rollout_actions, 400u);
    const std::vector<std::pair<int, std::array<std::size_t, 2>>> as_units_die{
        {13, {3, 3230}}, {12, {3, 3500}}, {11, {3, 3818}}, {10, {4, 4200}}, {9, {4, 4666}}, {8, {5, 5250}},
        {7, {6, 6000}}, {6, {6, 6000}}};
    for (const auto& [units, want] : as_units_die) {
        for (Unit& u : gs.units) {
            if (count_units_with_models(gs) <= units) break;
            if (!u.models.empty()) u.models.clear();
        }
        ASSERT_EQ(count_units_with_models(gs), units);
        EXPECT_EQ(budget(gs).rollouts, want[0]) << units;
        EXPECT_EQ(budget(gs).budget_actions, want[1]) << units;
    }
    GameState strike = scenario("strike_44x60.json");
    start_game(strike);
    EXPECT_EQ(budget(strike).rollouts, w.rollouts);
    EXPECT_EQ(budget(strike).budget_actions, w.budget_actions);
    hard::LookaheadWeights unscaled = w;
    unscaled.full_budget_units = 0;
    EXPECT_EQ(hard::rollout_budget(unscaled, rosters_at_start()).rollouts, w.rollouts);
    // A Monte Carlo on the rosters plays 3 rollouts, within 3000 actions.
    const GameState start = rosters_at_start();
    analysis::OddsCache odds;
    hard::ChainCache cache;
    const hard::Evaluator eval(odds, start, Side::A, hard::ChainWeights{}, &cache);
    const hard::MonteCarloResult r = hard::monte_carlo(eval, odds, start, Side::A, hard::rollout_seed(start), w);
    EXPECT_EQ(r.rollouts, 3u);
    EXPECT_LE(r.actions, 3000u);
}

// ==== 7. Legality on the new scenario ====

namespace {

void expect_clean(const GameState& scenario_state, BotLevel a, BotLevel b, std::uint64_t seed, double* seconds) {
    const auto t0 = std::chrono::steady_clock::now();
    const GameRecord g = play_bot_game(scenario_state, seed, level(a), level(b), 5000);
    if (seconds != nullptr) *seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    EXPECT_TRUE(g.finished) << "seed " << seed << (g.problems.empty() ? "" : ": " + g.problems.front());
    EXPECT_EQ(g.rejected, 0u) << "seed " << seed;
    EXPECT_EQ(g.reverted, 0u) << "seed " << seed;
    EXPECT_EQ(g.fallbacks, 0u) << "seed " << seed;
}

}  // namespace

TEST(M69_Games, HardAgainstNormalInEachSeatPlaysTheRostersClean) {
    // One game in each seat on the rosters cut to two battle rounds (decided on points after round 2, D-076): every Hard
    // decision legal, nothing reverted, no fallback. The arena plays 200 full games a seat (PLAN.md, M69).
    GameState short_game = wh40k::test::load_rosters_scenario();
    short_game.battle_rounds = 2;
    expect_clean(short_game, BotLevel::Hard, BotLevel::Normal, 7, nullptr);
    expect_clean(short_game, BotLevel::Normal, BotLevel::Hard, 8, nullptr);
}

// The bigger run (full games: four in each seat and two of Hard against Hard, about a minute in Release), by hand:
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M69_Games.DISABLED_*
TEST(M69_Games, DISABLED_FullGamesOfHardOnTheRostersAreClean) {
    const GameState gs = wh40k::test::load_rosters_scenario();
    for (std::uint64_t seed = 201; seed <= 204; ++seed) {
        double s = 0.0;
        expect_clean(gs, BotLevel::Hard, BotLevel::Normal, seed, &s);
        std::printf("Hard vs Normal, seed %llu: %.1f s\n", static_cast<unsigned long long>(seed), s);
        expect_clean(gs, BotLevel::Normal, BotLevel::Hard, seed, &s);
        std::printf("Normal vs Hard, seed %llu: %.1f s\n", static_cast<unsigned long long>(seed), s);
    }
    for (std::uint64_t seed = 205; seed <= 206; ++seed) {
        double s = 0.0;
        expect_clean(gs, BotLevel::Hard, BotLevel::Hard, seed, &s);
        std::printf("Hard vs Hard, seed %llu: %.1f s\n", static_cast<unsigned long long>(seed), s);
    }
}
