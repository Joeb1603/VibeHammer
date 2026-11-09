// M72 (D-079 part 2): custom battles in the arena: a scenario per seed (--random-armies 1) and army lists (--army-a,
// --army-b, --first). A fixed scenario plays exactly as before.

#include <gtest/gtest.h>

#include <string>

#include "arena.hpp"
#include "bot.hpp"
#include "core/data/army_list.hpp"
#include "core/data/load_error.hpp"
#include "core/serialization/json.hpp"
#include "support/army_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;

namespace {

BotConfig easy() { return BotConfig{BotLevel::Easy}; }

// The per-game lines without the last one (decision times and the wall time).
std::string game_lines(const std::string& text) {
    const std::size_t last = text.rfind("decisions ");
    return text.substr(0, last);
}

const ArmySource& source() {
    static const ArmySource s = load_army_source(wh40k::test::repo_root());
    return s;
}

}  // namespace

TEST(M72_Arena, AScenarioForEverySeedPlaysAsTheFixedScenario) {
    // The arena's games now come from a scenario per seed; a fixed scenario gives the same games, line for line.
    const GameState v1 = wh40k::test::load_v1_scenario();
    const PerGameReport fixed = run_per_game(v1, 3, 1, easy(), easy());
    const PerGameReport per_seed = run_per_game([&v1](std::uint64_t) { return v1; }, 3, 1, easy(), easy());
    EXPECT_EQ(game_lines(fixed.text), game_lines(per_seed.text));
    EXPECT_FALSE(game_lines(fixed.text).empty());
}

TEST(M72_Arena, RandomArmiesAreTheTestsRandomArmies) {
    // --random-armies 1 draws the armies the core tests draw (core::random_army_seed), so a seed names one battle.
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        const GameState arena = random_armies_battle(source(), seed, Side::A);
        EXPECT_EQ(arena, wh40k::test::random_armies_battle(seed)) << "seed " << seed;
        EXPECT_EQ(arena.sides[0].name, "Chaos Space Marines");
        EXPECT_EQ(arena.sides[1].name, "Tyranids");
        EXPECT_EQ(random_armies_battle(source(), seed, Side::B).first_turn, Side::B);
    }
    EXPECT_NE(random_armies_battle(source(), 1, Side::A), random_armies_battle(source(), 2, Side::A));
}

TEST(M72_Arena, RandomArmiesGamesFinishCleanAtEasy) {
    const ArenaResult r =
        run_arena([](std::uint64_t seed) { return random_armies_battle(source(), seed, Side::A); }, 3, 1, easy(), easy());
    EXPECT_TRUE(r.clean()) << format_arena(r, r.games.empty() ? GameState{} : random_armies_battle(source(), 1, Side::A));
    EXPECT_EQ(r.fallbacks, 0u);
}

TEST(M72_Arena, ArmyListsMakeACustomBattle) {
    const GameState stock = custom_battle_state(source(), wh40k::test::stock_army(wh40k::test::csm()),
                                                wh40k::test::stock_army(wh40k::test::tyranids()), Side::B);
    EXPECT_EQ(stock.first_turn, Side::B);
    EXPECT_EQ(stock.result_rule, ResultRule::PointsDestroyed);
    EXPECT_EQ(stock.units.size(), 14u);
    EXPECT_THROW((void)custom_battle_state(source(), wh40k::test::stock_army(wh40k::test::csm()),
                                           wh40k::test::stock_army(wh40k::test::csm()), Side::A),
                 DataError);
}
