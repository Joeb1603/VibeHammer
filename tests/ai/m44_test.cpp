// M44: the new bot scenarios (D-054). D-085 (M85): the rest of M44's tests (level Hard: the game-aware evaluator, the
// lookahead, Hard against Normal) went with Hard; they are in git under the tag bots-three-levels.

#include <gtest/gtest.h>

#include <set>

#include "arena.hpp"
#include "bot.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/state/queries.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::in;

namespace {

GameState load(const char* file) {
    return load_scenario(wh40k::test::repo_root() / "data/scenarios" / file, wh40k::test::repo_root());
}

}  // namespace

// ==== The new scenarios ====

TEST(M44_Scenarios, TheBotScenariosLoadDeployLegallyAndPlay) {
    const GameState v1 = wh40k::test::load_v1_scenario();
    const GameState first = load("bot_termagants_first.json");
    const GameState twenty = load("bot_20_termagants.json");
    // The same armies, the Tyranids first.
    EXPECT_EQ(first.first_turn, Side::B);
    EXPECT_EQ(v1.first_turn, Side::A);
    ASSERT_EQ(first.units.size(), v1.units.size());
    for (std::size_t i = 0; i < v1.units.size(); ++i) {
        EXPECT_EQ(first.units[i].models, v1.units[i].models);
    }
    // 20 Termagants (the datasheet's 10-20) in three ranks of 7, 7 and 6, in base contact, in coherency (03.03)
    // and wholly within their deployment zone (D-008, D-009; the loader also checks both).
    const Unit& gants = wh40k::test::unit_of(twenty, Side::B);
    ASSERT_EQ(gants.models.size(), 20u);
    EXPECT_EQ(gants.starting_strength, 20);
    EXPECT_TRUE(unit_coherency(gants).ok());
    std::set<float> rows;
    for (const Model& m : gants.models) rows.insert(m.position.y);
    EXPECT_EQ(rows.size(), 3u);
    const Rect zone{0.0f, twenty.battlefield.depth_mm - in(6.0f), twenty.battlefield.width_mm,
                    twenty.battlefield.depth_mm};
    for (const Model& m : gants.models) EXPECT_TRUE(wholly_within(zone, m.position, m.base));
    EXPECT_EQ(wh40k::test::unit_of(twenty, Side::A).models.size(), 5u);
    // Each one plays a whole clean game, the bot on both sides (D-085, M85: the 20-Termagant game was Hard against
    // Normal).
    for (const GameState* sc : {&first, &twenty}) {
        const GameRecord g = play_bot_game(*sc, 1, BotConfig{}, BotConfig{});
        EXPECT_TRUE(g.finished) << (g.problems.empty() ? "" : g.problems.front());
        EXPECT_EQ(g.rejected, 0u);
        EXPECT_EQ(g.reverted, 0u);
        EXPECT_EQ(g.fallbacks, 0u);
    }
}
