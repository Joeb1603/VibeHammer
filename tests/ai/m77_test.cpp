// M77 (D-081, bot deployment): Normal and Hard set their units up by a simple heuristic (src/ai/deploy_policy.*); Easy
// keeps M74's policy (the lowest-id unit at Auto-place's spot, tested in m74_test.cpp).
// - Order: long-range shooters first, melee-oriented units last, the rest between; ties by id.
// - Spot: core's proposal, nudged within a small bounded set of translations toward the centre of the enemy units set up
//   (melee nearer it, shooters further back while it stays in range), the first one core accepts; the proposal itself
//   with no enemy set up yet.
// - Hard deploys as Normal and builds no chain table; CannotSetUp only when the engine finds no legal spot.
// - The replay: Normal vs Normal on two custom lists with hand deployment, to the result.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

#include "arena.hpp"
#include "bot.hpp"
#include "core/data/army_list.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/units.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/deployment.hpp"
#include "deploy_policy.hpp"
#include "session.hpp"
#include "support/army_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::decider;
using wh40k::test::repo_root;

namespace {

BotConfig level(BotLevel l) {
    BotConfig c;
    c.level = l;
    return c;
}

ArmyList fixture_army(const std::string& file) { return load_army(repo_root() / "tests/data/armies" / file); }

GameState stock_battle(Side first) {
    return wh40k::test::custom_battle(wh40k::test::stock_army(wh40k::test::csm()),
                                      wh40k::test::stock_army(wh40k::test::tyranids()), first, DeploymentMode::Hand);
}

Position centroid_of(const action::DeployUnit& a) {
    double x = 0.0;
    double y = 0.0;
    for (const ModelPlacement& p : a.models) {
        x += p.position.x;
        y += p.position.y;
    }
    const double n = static_cast<double>(a.models.size());
    return Position{static_cast<float>(x / n), static_cast<float>(y / n), 0.0f};
}

// How far `a` stands behind `b` (positive: further back from the enemy), for side `side`'s zone.
float back_of(const GameState& gs, Side side, const action::DeployUnit& a, const action::DeployUnit& b) {
    const float dy = centroid_of(a).y - centroid_of(b).y;
    return gs.side(side).deployment_zone.edge == BattlefieldEdge::South ? -dy : dy;
}

// One set-up decision as the bot answered it.
struct BotSetUp {
    GameState before;
    Side side = Side::A;
    std::vector<GameAction> legal;
    GameAction action;
    std::string reason;
    UnitId unit{0};
    std::optional<action::DeployUnit> proposal;  // core's, for the chosen unit
    DeployAssessment assessment;
    std::optional<Position> enemy;  // enemy_deployed_centre before the set-up
};

struct Deployment {
    std::vector<BotSetUp> set_ups;
    GameState after;
    std::size_t fallbacks = 0;
    std::size_t tables = 0;
};

UnitId unit_of(const GameAction& a) {
    if (const auto* d = std::get_if<action::DeployUnit>(&a)) return d->unit;
    if (const auto* c = std::get_if<action::CannotSetUp>(&a)) return c->unit;
    return UnitId{0};
}

// Both sides' deployment by bots of these levels (the state is not started).
Deployment deploy(GameState gs, BotLevel a, BotLevel b) {
    start_game(gs);
    Bot ba(level(a));
    Bot bb(level(b));
    Deployment out;
    while (gs.flow.stage == Stage::Deploy) {
        const Side side = decider(gs);
        BotSetUp s;
        s.before = gs;
        s.side = side;
        s.legal = legal_actions(gs, side);
        const BotChoice c = (side == Side::A ? ba : bb).decide(gs, side);
        EXPECT_FALSE(c.stopped);
        s.action = c.action;
        s.reason = c.reason;
        s.unit = unit_of(c.action);
        s.proposal = propose_set_up(gs, s.unit);
        analysis::OddsCache cache;
        s.assessment = deploy_assessment(cache, gs, *find_undeployed(gs, s.unit));
        s.enemy = enemy_deployed_centre(gs, side);
        apply_action(gs, side, c.action);
        out.set_ups.push_back(std::move(s));
    }
    out.after = gs;
    out.fallbacks = ba.fallbacks() + bb.fallbacks();
    out.tables = ba.chain_tables_built() + bb.chain_tables_built();
    return out;
}

// The battles the property tests deploy: the stock armies both ways round, and the arena's random armies.
std::vector<GameState> battles() {
    std::vector<GameState> out{stock_battle(Side::A), stock_battle(Side::B)};
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        out.push_back(wh40k::test::random_armies_battle(seed, DeploymentMode::Hand, seed % 2 == 0 ? Side::B : Side::A));
    }
    return out;
}

const std::vector<Deployment>& normal_deployments() {
    static const std::vector<Deployment> d = [] {
        std::vector<Deployment> out;
        for (const GameState& gs : battles()) out.push_back(deploy(gs, BotLevel::Normal, BotLevel::Normal));
        return out;
    }();
    return d;
}

const Unit* unit_named(const GameState& gs, const std::string& prefix, Side side) {
    for (const Unit& u : gs.undeployed) {
        if (u.side == side && wh40k::test::starts_with(u.name, prefix)) return &u;
    }
    return nullptr;
}

}  // namespace

// ---- The roles and the order ----

TEST(M77_BotDeploy, RolesComeFromTheWeapons) {
    // The stock armies: the lascannon Havocs and the Tyrannofex (its 24" fleshborer hive) are long-range shooters; the
    // Genestealers, the Tyrant Guard and the Screamer-killers are melee-oriented; the Chaos Terminators (combi-bolters
    // and accursed weapons) and the Termagants (18" fleshborers) are neither. Printed for the record.
    GameState gs = stock_battle(Side::A);
    analysis::OddsCache cache;
    for (const Unit& u : gs.undeployed) {
        const DeployAssessment a = deploy_assessment(cache, gs, u);
        std::printf("%-28s side %s  range %5.1f\"  ranged share %.3f  role %d\n", u.name.c_str(),
                    u.side == Side::A ? "A" : "B", static_cast<double>(mm_to_inches(a.max_range_mm)), a.ranged_share,
                    static_cast<int>(a.role));
        EXPECT_GE(a.ranged_share, 0.0);
        EXPECT_LE(a.ranged_share, 1.0);
    }
    const auto role = [&](const std::string& name, Side side) {
        const Unit* u = unit_named(gs, name, side);
        EXPECT_NE(u, nullptr) << name;
        return u == nullptr ? DeployRole::Other : deploy_assessment(cache, gs, *u).role;
    };
    EXPECT_EQ(role("Havocs", Side::A), DeployRole::LongRangeShooter);
    EXPECT_EQ(role("Tyrannofex", Side::B), DeployRole::LongRangeShooter);
    EXPECT_EQ(role("Genestealers", Side::B), DeployRole::Melee);
    EXPECT_EQ(role("Tyrant Guard", Side::B), DeployRole::Melee);
    EXPECT_EQ(role("Screamer-killer", Side::B), DeployRole::Melee);
    EXPECT_EQ(role("Chaos Terminator Squad", Side::A), DeployRole::Other);
    EXPECT_EQ(role("Termagants", Side::B), DeployRole::Other);  // ranged-leaning, but 18" is not long range
    // A unit with no ranged weapon at all is never a shooter; one whose longest gun is short never a long-range one.
    for (const Unit& u : gs.undeployed) {
        const DeployAssessment a = deploy_assessment(cache, gs, u);
        if (a.role == DeployRole::LongRangeShooter) EXPECT_GE(a.max_range_mm, inches_to_mm(24.0f) - EPS_MM) << u.name;
        if (a.max_range_mm == 0.0f) EXPECT_EQ(a.role, DeployRole::Melee) << u.name;
    }
}

TEST(M77_BotDeploy, LongRangeShootersFirstMeleeLastTiesById) {
    for (const Deployment& d : normal_deployments()) {
        std::array<std::vector<DeployRole>, 2> roles;
        for (const BotSetUp& s : d.set_ups) {
            // The unit chosen is the first of deploy_order, which is sorted by role, then id.
            const std::vector<UnitId> order = deploy_order(s.before, s.side);
            ASSERT_FALSE(order.empty());
            EXPECT_EQ(s.unit, order.front());
            analysis::OddsCache cache;
            for (std::size_t i = 1; i < order.size(); ++i) {
                const DeployRole r0 = deploy_assessment(cache, s.before, *find_undeployed(s.before, order[i - 1])).role;
                const DeployRole r1 = deploy_assessment(cache, s.before, *find_undeployed(s.before, order[i])).role;
                EXPECT_TRUE(r0 < r1 || (r0 == r1 && order[i - 1].v < order[i].v));
            }
            roles[side_index(s.side)].push_back(s.assessment.role);
        }
        for (const auto& r : roles) EXPECT_TRUE(std::is_sorted(r.begin(), r.end()));
    }
}

// ---- The spots ----

TEST(M77_BotDeploy, EverySpotIsLegalAndNothingFallsBack) {
    int set_ups = 0;
    int nudged = 0;
    for (const Deployment& d : normal_deployments()) {
        EXPECT_EQ(d.fallbacks, 0u);
        for (const BotSetUp& s : d.set_ups) {
            const auto* a = std::get_if<action::DeployUnit>(&s.action);
            ASSERT_NE(a, nullptr) << "no CannotSetUp in these battles";
            EXPECT_TRUE(set_up_problems(s.before, s.side, *a).empty());
            EXPECT_TRUE(validate_action(s.before, s.side, s.action).ok());
            EXPECT_TRUE(wh40k::test::starts_with(s.reason, "D-081 (M77): set up ")) << s.reason;
            ++set_ups;
            if (s.proposal && *a != *s.proposal) ++nudged;
        }
        EXPECT_TRUE(d.after.undeployed.empty());
        EXPECT_EQ(d.after.flow.battle_round, 1);
    }
    std::printf("%d set-ups, %d nudged from the proposal\n", set_ups, nudged);
    EXPECT_GT(nudged, set_ups / 4);  // the heuristic does move units
}

TEST(M77_BotDeploy, WithNoEnemyUnitSetUpItIsTheProposal) {
    int seen = 0;
    for (const Deployment& d : normal_deployments()) {
        for (const BotSetUp& s : d.set_ups) {
            if (s.enemy) continue;
            ASSERT_TRUE(s.proposal.has_value());
            EXPECT_EQ(s.action, GameAction{*s.proposal});
            ++seen;
        }
    }
    EXPECT_GE(seen, 14);  // at least the first set-up of every battle
}

TEST(M77_BotDeploy, MeleeUnitsStandNearerTheEnemyThanTheirProposal) {
    int seen = 0;
    int nearer = 0;
    for (const Deployment& d : normal_deployments()) {
        for (const BotSetUp& s : d.set_ups) {
            if (s.assessment.role != DeployRole::Melee || !s.enemy) continue;
            const auto& a = std::get<action::DeployUnit>(s.action);
            const float chosen = centre_distance(centroid_of(a), *s.enemy);
            const float proposed = centre_distance(centroid_of(*s.proposal), *s.enemy);
            EXPECT_LE(chosen, proposed + EPS_MM);
            EXPECT_GE(back_of(s.before, s.side, *s.proposal, a), -EPS_MM);  // never further back than the proposal
            ++seen;
            if (chosen < proposed - inches_to_mm(0.5f)) ++nearer;
        }
    }
    std::printf("%d melee set-ups, %d nearer the enemy\n", seen, nearer);
    EXPECT_GT(seen, 20);
    EXPECT_GT(nearer, seen / 3);
}

TEST(M77_BotDeploy, LongRangeShootersStandBackWhileInRange) {
    int seen = 0;
    int back = 0;
    for (const Deployment& d : normal_deployments()) {
        for (const BotSetUp& s : d.set_ups) {
            if (s.assessment.role != DeployRole::LongRangeShooter || !s.enemy) continue;
            const auto& a = std::get<action::DeployUnit>(s.action);
            const float behind = back_of(s.before, s.side, a, *s.proposal);
            EXPECT_GE(behind, -EPS_MM);  // never forward of the proposal
            const bool proposal_in_range =
                centre_distance(centroid_of(*s.proposal), *s.enemy) <= s.assessment.max_range_mm + EPS_MM;
            const bool chosen_in_range = centre_distance(centroid_of(a), *s.enemy) <= s.assessment.max_range_mm + EPS_MM;
            if (proposal_in_range) EXPECT_TRUE(chosen_in_range);  // going back never costs the range
            if (!proposal_in_range) EXPECT_LE(behind, EPS_MM);    // out of range: no further back than the proposal
            ++seen;
            if (behind > inches_to_mm(0.5f)) ++back;
        }
    }
    std::printf("%d long-range shooter set-ups with an enemy set up, %d further back\n", seen, back);
    EXPECT_GT(seen, 5);
    EXPECT_GT(back, 0);
}

TEST(M77_BotDeploy, CandidatesAreBoundedAndStartWithTheBestFacing) {
    // The stock battle after B's first set-up: every candidate of each of A's units is a translation of its proposal (x
    // within 6", 13 shifts; at most 7 depths for a shooter, 14 for a melee unit in a 12" zone), the proposal among them.
    GameState gs = stock_battle(Side::A);
    start_game(gs);
    apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());  // B sets up a unit (at its proposal)
    const Side side = decider(gs);
    ASSERT_EQ(side, Side::A);
    const std::optional<Position> enemy = enemy_deployed_centre(gs, side);
    ASSERT_TRUE(enemy.has_value());
    analysis::OddsCache cache;
    for (const Unit& u : gs.undeployed) {
        if (u.side != side) continue;
        const action::DeployUnit p = *propose_set_up(gs, u.id);
        const DeployAssessment as = deploy_assessment(cache, gs, u);
        const std::vector<action::DeployUnit> c = deploy_candidates(gs, p, as.role, as.max_range_mm, *enemy);
        EXPECT_LE(c.size(), 13u * (as.role == DeployRole::Melee ? 14u : 7u)) << u.name;
        EXPECT_EQ(std::count(c.begin(), c.end(), p), 1) << u.name;
        for (const action::DeployUnit& a : c) {
            ASSERT_EQ(a.models.size(), p.models.size());
            const float dx = a.models.front().position.x - p.models.front().position.x;
            const float dy = a.models.front().position.y - p.models.front().position.y;
            for (std::size_t i = 0; i < a.models.size(); ++i) {
                EXPECT_EQ(a.models[i].model, p.models[i].model);
                EXPECT_EQ(a.models[i].facing_deg, p.models[i].facing_deg);
                EXPECT_NEAR(a.models[i].position.x - p.models[i].position.x, dx, 0.01f);
                EXPECT_NEAR(a.models[i].position.y - p.models[i].position.y, dy, 0.01f);
            }
            EXPECT_LE(std::abs(dx), inches_to_mm(6.0f) + EPS_MM);
        }
        if (as.role == DeployRole::Melee) {
            // Nearest the enemy's centre first.
            for (std::size_t i = 1; i < c.size(); ++i) {
                EXPECT_LE(centre_distance(centroid_of(c[i - 1]), *enemy), centre_distance(centroid_of(c[i]), *enemy));
            }
        }
    }
}

// ---- Determinism, Hard, CannotSetUp ----

TEST(M77_BotDeploy, DeterministicForFreshAndUsedBots) {
    const GameState gs = wh40k::test::random_armies_battle(5, DeploymentMode::Hand);
    const Deployment first = deploy(gs, BotLevel::Normal, BotLevel::Normal);
    const Deployment second = deploy(gs, BotLevel::Normal, BotLevel::Normal);
    EXPECT_EQ(first.after, second.after);
    // A bot that has already played answers every decision of the deployment the same.
    Bot used(level(BotLevel::Normal));
    for (const BotSetUp& s : first.set_ups) {
        EXPECT_EQ(used.decide(s.before, s.side).action, s.action);
    }
    for (auto it = first.set_ups.rbegin(); it != first.set_ups.rend(); ++it) {  // in another order too
        EXPECT_EQ(used.decide(it->before, it->side).action, it->action);
    }
    EXPECT_EQ(heuristic_deploy_policy(first.set_ups[3].before, first.set_ups[3].side, first.set_ups[3].legal)->action,
              first.set_ups[3].action);
}

TEST(M77_BotDeploy, HardDeploysAsNormalAndBuildsNoTables) {
    for (const GameState& gs : {stock_battle(Side::A), wh40k::test::random_armies_battle(8, DeploymentMode::Hand)}) {
        const Deployment normal = deploy(gs, BotLevel::Normal, BotLevel::Normal);
        const Deployment hard = deploy(gs, BotLevel::Hard, BotLevel::Hard);
        EXPECT_EQ(hard.tables, 0u);  // no chain table from a half-deployed army
        EXPECT_EQ(hard.fallbacks, 0u);
        ASSERT_EQ(hard.set_ups.size(), normal.set_ups.size());
        for (std::size_t i = 0; i < hard.set_ups.size(); ++i) {
            EXPECT_EQ(hard.set_ups[i].action, normal.set_ups[i].action) << "set-up " << i;
            EXPECT_EQ(hard.set_ups[i].reason, normal.set_ups[i].reason);
        }
        EXPECT_EQ(hard.after, normal.after);
    }
    // Normal as at M55 (D-066, arena only) keeps M74's policy, as Easy does.
    BotConfig m55 = level(BotLevel::Normal);
    m55.generation = Generation::M55;
    EXPECT_FALSE(uses_deploy_heuristic(m55));
    EXPECT_FALSE(uses_deploy_heuristic(level(BotLevel::Easy)));
    EXPECT_TRUE(uses_deploy_heuristic(level(BotLevel::Normal)));
    EXPECT_TRUE(uses_deploy_heuristic(level(BotLevel::Hard)));
}

TEST(M77_BotDeploy, NoCannotSetUpWhileTheEngineFindsASpot) {
    // In every deployment above a unit is declared not set up only when core lists CannotSetUp for it, and the nudges
    // never leave one of the side's own units without a spot: no battle has a unit not set up (Easy has none either).
    for (const Deployment& d : normal_deployments()) {
        for (const BotSetUp& s : d.set_ups) {
            const bool cannot = std::holds_alternative<action::CannotSetUp>(s.action);
            EXPECT_EQ(cannot, !s.proposal.has_value());
            EXPECT_FALSE(cannot);
        }
    }
    // At each decision of the stock deployment, CannotSetUp is answered exactly when core lists it for the unit chosen.
    // (Where nothing fits, M74's narrow zone, m74_test.cpp checks every level: the third line of Termagants is declared
    // not set up.)
    GameState gs = stock_battle(Side::A);
    start_game(gs);
    Bot bot(level(BotLevel::Normal));
    while (gs.flow.stage == Stage::Deploy) {
        const Side side = decider(gs);
        const std::vector<GameAction> legal = legal_actions(gs, side);
        const BotChoice c = bot.decide(gs, side);
        const UnitId u = unit_of(c.action);
        const bool listed_cannot = std::any_of(legal.begin(), legal.end(), [&](const GameAction& a) {
            const auto* cs = std::get_if<action::CannotSetUp>(&a);
            return cs != nullptr && cs->unit == u;
        });
        EXPECT_EQ(std::holds_alternative<action::CannotSetUp>(c.action), listed_cannot);
        apply_action(gs, side, c.action);
    }
}

TEST(M77_BotDeploy, HandDeployedGamesAtNormalAndHardFinishClean) {
    // The arena's random armies with --deploy hand (the acceptance's 200 seeds per pairing are run in Release): Normal vs
    // Normal, and Hard vs Normal on seed 6 (small armies: Hard's tables are cheap in Debug).
    const ArmySource source = load_army_source(repo_root());
    const auto hand = [&](std::uint64_t seed) { return random_armies_battle(source, seed, Side::A, DeploymentMode::Hand); };
    struct Run {
        BotLevel a;
        BotLevel b;
        std::uint64_t first;
    };
    for (const Run& run : {Run{BotLevel::Normal, BotLevel::Normal, 2}, Run{BotLevel::Hard, BotLevel::Normal, 6}}) {
        const ArenaResult r = run_arena(hand, 1, run.first, level(run.a), level(run.b));
        EXPECT_TRUE(r.clean()) << format_arena(r, hand(run.first));
        EXPECT_EQ(r.fallbacks, 0u);
        for (const GameRecord& g : r.games) EXPECT_TRUE(g.finished) << "seed " << g.seed;
    }
}

// ---- The replay: Normal vs Normal on two custom lists, set up by hand, to the result ----

namespace {

constexpr const char* kArmyA = "m73_chaos_space_marines.json";
constexpr const char* kArmyB = "m77_tyranids.json";
// The custom battle of the two lists in Hand mode as a scenario file (custom_battle_scenario's JSON), so the replay names
// its scenario as M73's and M75's do.
constexpr const char* kBattleScenario = "tests/data/scenarios/m77_custom_hand_deploy_44x60.json";
constexpr const char* kReplayFile = "m77_custom_hand_deploy_bot_game.json";
constexpr std::uint64_t kReplaySeed = 1;
constexpr Side kFirst = Side::B;  // the Chaos Space Marines (side A) take the second turn, so they set up first

std::filesystem::path saved_path() { return std::filesystem::path(WH40K_REPLAY_DIR) / kReplayFile; }

Json battle_scenario() {
    return custom_battle_scenario(fixture_army(kArmyA), fixture_army(kArmyB), kFirst, wh40k::test::factions(),
                                  wh40k::test::army_resolver(), DeploymentMode::Hand);
}

GameState load(const std::string& file) { return load_scenario(repo_root() / file, repo_root()); }

struct BotGame {
    Replay replay;
    GameRecord record;
};

// Played through GameSession as the arena plays; its history a scenario-form replay. Replaying it needs only core.
BotGame play_hand_deploy_bot_game(std::uint64_t seed = kReplaySeed) {
    BotGame g;
    g.replay.description =
        "M77: a whole custom battle played by the bot on both sides, level Normal vs Normal, with dice seed " +
        std::to_string(seed) +
        ", the units set up by hand (D-081: alternating, the side taking the second turn first) by Normal's deployment "
        "heuristic (long-range shooters first and back, melee units last and forward, each spot nudged toward the "
        "enemy's set-up units). Two custom lists (tests/data/armies) on the 44\"x60\" portrait battlefield (Incursion, "
        "decided on points destroyed, D-076), the Tyranids (side B) taking the first turn: side A, Chaos Space Marines "
        "(595 points): a Chaos Lord, 10 Legionaries, Havocs with four missile launchers, 5 Chaos Terminators. Side B, "
        "Tyranids: a Hive Tyrant with a heavy venom cannon, 10 Termagants, 5 Genestealers, 3 Tyrant Guard and a "
        "Tyrannofex. Replaying it needs only the engine.";
    g.replay.initial.scenario = kBattleScenario;
    g.replay.initial.seed = seed;
    wh40k::client::GameSession session(replay_initial_state(g.replay, repo_root()));
    g.record = play_bot_session(session, level(BotLevel::Normal), level(BotLevel::Normal), 5000);
    g.replay.steps = session.history();
    g.replay.expect = replay_expectation(session.state());
    return g;
}

}  // namespace

TEST(M77_Replay, TheScenarioIsTheTwoArmyFilesSetUpByHand) {
    EXPECT_EQ(read_json_file(repo_root() / kBattleScenario), battle_scenario());
    const GameState gs = load(kBattleScenario);
    EXPECT_EQ(gs, wh40k::test::custom_battle(fixture_army(kArmyA), fixture_army(kArmyB), kFirst, DeploymentMode::Hand));
    EXPECT_TRUE(gs.units.empty());
    EXPECT_EQ(gs.undeployed.size(), 9u);
    EXPECT_EQ(gs.result_rule, ResultRule::PointsDestroyed);
    // Both lists are legal custom armies.
    EXPECT_TRUE(army_problems(fixture_army(kArmyA), wh40k::test::csm(), wh40k::test::army_resolver()).empty());
    EXPECT_TRUE(army_problems(fixture_army(kArmyB), wh40k::test::tyranids(), wh40k::test::army_resolver()).empty());
}

TEST(M77_Replay, TheGameReplaysToItsResultWithCoreOnly) {
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
    // The first nine steps set the units up (A first), at least one of them away from Auto-place's spot.
    ASSERT_GE(saved.steps.size(), 9u);
    for (std::size_t i = 0; i < 9; ++i) {
        EXPECT_TRUE(std::holds_alternative<action::DeployUnit>(saved.steps[i].action)) << "step " << i;
    }
    EXPECT_EQ(saved.steps[0].side, Side::A);
    GameState gs = load(kBattleScenario);
    gs.dice.seed = kReplaySeed;
    start_game(gs);
    int nudged = 0;
    for (std::size_t i = 0; i < 9; ++i) {
        const auto& a = std::get<action::DeployUnit>(saved.steps[i].action);
        if (GameAction{a} != GameAction{*propose_set_up(gs, a.unit)}) ++nudged;
        apply_action(gs, saved.steps[i].side, saved.steps[i].action);
    }
    EXPECT_GT(nudged, 0);
    EXPECT_EQ(gs.flow.battle_round, 1);
}

TEST(M77_Replay, TheSavedFileMatchesTheGame) {
    // Normal vs Normal with the replay's seed plays exactly the saved game now (D-054: the bots are deterministic),
    // with nothing rejected, reverted or answered by the fallback.
    const Replay saved = load_replay(saved_path());
    const BotGame g = play_hand_deploy_bot_game();
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

// Writes tests/data/scenarios/m77_custom_hand_deploy_44x60.json and tests/replays/m77_custom_hand_deploy_bot_game.json.
// Run on purpose only:
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M77_Replay.DISABLED_RegenerateCustomHandDeployBotGameReplay
TEST(M77_Replay, DISABLED_RegenerateCustomHandDeployBotGameReplay) {
    {
        std::ofstream out(repo_root() / kBattleScenario, std::ios::binary);
        out << battle_scenario().dump(2) << "\n";
    }
    const BotGame g = play_hand_deploy_bot_game();
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
