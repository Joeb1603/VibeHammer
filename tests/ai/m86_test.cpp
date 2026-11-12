// M86 (D-084): the bot on the objective mission. It ignores the objectives here (M87 plays for them) and plays a
// mission like the points rule (kills count, and settle equal VP), so it plays the objective scenario exactly as the
// rosters. The arena counts the VP, and its custom and random battles can play the mission. M87 (D-086): with both
// objective weights 0 the bot still plays so.

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "arena.hpp"
#include "bot.hpp"
#include "core/data/army_list.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/serialization/json.hpp"
#include "core/state/objectives.hpp"
#include "session.hpp"
#include "state_hash.hpp"
#include "support/army_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;

namespace {

constexpr const char* kObjectiveScenario = "data/scenarios/incursion_objectives_44x60.json";

GameState objectives() { return load_scenario(wh40k::test::repo_root() / kObjectiveScenario, wh40k::test::repo_root()); }

// The field after `key` in a --per-game line ("actions 0123..." -> "0123...").
std::string field(const std::string& line, const std::string& key) {
    const std::size_t at = line.find(key + " ");
    if (at == std::string::npos) return "";
    const std::size_t start = at + key.size() + 1;
    return line.substr(start, line.find(' ', start) - start);
}

}  // namespace

TEST(M86_Bot, WithoutObjectiveWeightsTheBotPlaysTheObjectiveScenarioExactlyAsTheRosters) {
    // The same armies and dice: the same actions (the bot ignores the objectives and scores kills as under the points
    // rule), so only the final state differs (the mission, its control and VP, and the result).
    // M87 (D-086): the bot now plays for the objectives; with both objective weights 0 it plays as at M86, which this
    // test keeps (M87_Regression checks that the default weights don't).
    BotConfig m86;
    m86.move.objective = 0.0;
    m86.move.objective_approach = 0.0;
    const PerGameReport rosters = run_per_game(wh40k::test::load_rosters_scenario(), 1, 1, BotConfig{}, BotConfig{});
    const PerGameReport mission = run_per_game(objectives(), 1, 1, m86, m86);
    EXPECT_TRUE(rosters.clean);
    EXPECT_TRUE(mission.clean);
    EXPECT_FALSE(field(mission.text, "actions").empty());
    EXPECT_EQ(field(mission.text, "actions"), field(rosters.text, "actions"));
    EXPECT_EQ(field(mission.text, "trace"), field(rosters.text, "trace"));
    EXPECT_NE(field(mission.text, "state"), field(rosters.text, "state"));
}

TEST(M86_Arena, AMissionGameIsCountedOnVpAndItsVpAreReported) {
    const GameState scenario = objectives();
    const ArenaResult r = run_arena(scenario, 1, 1, BotConfig{}, BotConfig{});
    ASSERT_EQ(r.games.size(), 1u);
    const GameRecord& g = r.games[0];
    EXPECT_TRUE(r.clean()) << format_arena(r, scenario);
    EXPECT_EQ(r.fallbacks, 0u);
    // The record follows the engine's result: on VP, or on points with equal VP; a wipe-out is neither.
    EXPECT_FALSE(g.on_vp && g.on_points);
    if (g.on_vp) EXPECT_EQ(r.vp_wins[0] + r.vp_wins[1], 1);
    EXPECT_EQ(r.mean_objective_vp[0], g.objective_vp[0]);
    EXPECT_EQ(r.mean_kill_vp[1], g.kill_vp[1]);
    const std::string text = format_arena(r, scenario);
    EXPECT_NE(text.find("decided after the last battle round (D-084)"), std::string::npos);
    EXPECT_NE(text.find("mean VP by the end (D-084)"), std::string::npos);
    EXPECT_EQ(format_arena(r, wh40k::test::load_rosters_scenario()).find("D-084"), std::string::npos);
    std::printf("Bot vs bot on the objective scenario, seed 1: winner %s, on VP %d, objective VP %d/%d, kill VP %d/%d\n",
                g.winner ? (*g.winner == Side::A ? "A" : "B") : "none", g.on_vp ? 1 : 0, g.objective_vp[0],
                g.objective_vp[1], g.kill_vp[0], g.kill_vp[1]);
}

TEST(M86_Arena, CustomAndRandomBattlesPlayTheMissionTheArmySourceCarries) {
    ArmySource source = load_army_source(wh40k::test::repo_root());
    EXPECT_FALSE(source.mission.has_value());  // the tests' battles stay on points destroyed
    EXPECT_EQ(random_armies_battle(source, 1, Side::A).result_rule, ResultRule::PointsDestroyed);
    source.mission = load_mission_block(wh40k::test::repo_root() / kObjectiveScenario);
    const GameState battle = random_armies_battle(source, 1, Side::A);
    EXPECT_EQ(battle.result_rule, ResultRule::Mission);
    ASSERT_TRUE(battle.mission.has_value());
    EXPECT_EQ(battle.mission->objectives.size(), 3u);
    EXPECT_EQ(custom_battle_state(source, wh40k::test::stock_army(wh40k::test::csm()),
                                  wh40k::test::stock_army(wh40k::test::tyranids()), Side::B, DeploymentMode::Hand)
                  .result_rule,
              ResultRule::Mission);
    const ArenaResult r = run_arena([&source](std::uint64_t seed) { return random_armies_battle(source, seed, Side::A); },
                                    1, 1, BotConfig{}, BotConfig{});
    EXPECT_TRUE(r.clean()) << format_arena(r, battle);
    EXPECT_EQ(r.fallbacks, 0u);
}

TEST(M86_StateHash, AStateWithoutAMissionHashesAsBeforeAndTheMissionChangesTheHash) {
    // The objective scenario without its mission (and with the rosters' rule) is the rosters scenario, hash for hash:
    // the mission is hashed only when present, behind a tag.
    const GameState rosters = wh40k::test::load_rosters_scenario();
    GameState gs = objectives();
    GameState stripped = gs;
    stripped.mission.reset();
    stripped.result_rule = ResultRule::PointsDestroyed;
    EXPECT_EQ(state_hash(stripped), state_hash(rosters));
    EXPECT_NE(state_hash(gs), state_hash(stripped));
    GameState held = gs;
    held.mission->objectives[0].controller = Side::A;
    EXPECT_NE(state_hash(held), state_hash(gs));
    GameState secured = held;
    secured.mission->objectives[0].secured_by = Side::A;
    EXPECT_NE(state_hash(secured), state_hash(held));
    GameState scored = gs;
    scored.mission->objective_vp[1] = 2;
    EXPECT_NE(state_hash(scored), state_hash(gs));
}
