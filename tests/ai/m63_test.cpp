// M63 (D-073): the bots with 17.01 and 17.03. The shooting odds take the -1 at an engaged MONSTER from the engine's
// selections (make_weapon_selections through shooting_batches), with the D-011 cap; every level stays legal on the
// monster fixture (an engaged Hive Tyrant to shoot, monsters next to infantry to move past).

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "default_choice.hpp"
#include "core/engine/engine.hpp"
#include "core/rules/shooting.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using namespace wh40k::ai::analysis;
namespace mf = wh40k::test::monsters;

namespace {

std::vector<ShotDeclaration> lascannons_at(const GameState& gs, std::size_t target) {
    std::vector<ShotDeclaration> out;
    for (std::size_t i = 1; i < gs.units[mf::kHavocs].models.size(); ++i) {
        out.push_back(ShotDeclaration{gs.units[mf::kHavocs].models[i].id, 0, 0, gs.units[target].id});
    }
    return out;
}

Volley volley_of(const std::vector<AttackBatch>& batches, std::optional<int> hit_modifier = std::nullopt) {
    Volley v;
    for (const AttackBatch& b : batches) {
        VolleyBatch vb = volley_batch(b);
        if (hit_modifier) vb.profile.hit_modifier = *hit_modifier;
        v.push_back(vb);
    }
    return v;
}

}  // namespace

TEST(M63_Odds, TheShootingOddsTakeTheMinusOneFromTheEngine) {
    const GameState gs = mf::at_side_a_shooting();
    const UnitId havocs = gs.units[mf::kHavocs].id;
    const auto shots = lascannons_at(gs, mf::kTyrant);
    const auto batches = shooting_batches(gs, havocs, ShootingType::Normal, shots);
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].profile.hit_modifier, -1);  // 17.03, from make_weapon_selections
    OddsCache cache;
    const TargetSnapshot tyrant = snapshot_of(gs.units[mf::kTyrant]);
    const AttackOutcome with = expected_outcome(cache, volley_of(batches), tyrant);
    const AttackOutcome without = expected_outcome(cache, volley_of(batches, 0), tyrant);
    EXPECT_LT(with.damage, without.damage);
    // BS3+ at -1: 3 hits in 6 instead of 4.
    EXPECT_NEAR(cache.hit_wound(volley_of(batches)[0].profile, 10).hit, 3.0 / 6.0, 1e-12);
    EXPECT_NEAR(cache.hit_wound(volley_of(batches, 0)[0].profile, 10).hit, 4.0 / 6.0, 1e-12);
    const auto evaluated = evaluate_shots(cache, gs, havocs, ShootingType::Normal, shots);
    ASSERT_EQ(evaluated.size(), 1u);
    EXPECT_DOUBLE_EQ(evaluated[0].outcome.damage, with.damage);
    // best_shooting (Normal and Hard's live choice) finds the Tyrant a target, with the same odds.
    const auto plan = best_shooting(cache, gs, havocs, gs.units[mf::kTyrant].id);
    ASSERT_TRUE(plan);
    const auto plan_batches = shooting_batches(gs, havocs, plan->type, plan->shots);
    ASSERT_FALSE(plan_batches.empty());
    for (const AttackBatch& b : plan_batches) EXPECT_EQ(b.profile.hit_modifier, -1);
}

TEST(M63_Odds, TheCapIsInTheOddsToo) {
    // The Havocs as a stand-in VEHICLE engaged with the engaged Tyrant, in close-quarters shooting: 10.06 and 17.03 make
    // -2, which the odds (core's make_hit_rolls) cap to -1 as the engine does (D-011).
    GameState gs = mf::fixture(true, Side::A);
    gs.units[mf::kHavocs].keywords.push_back("Vehicle");
    const float tyrant_y = mf::kRankY + mf::tyrant_centre_gap(1.0f);
    mf::place_rank(gs.units[mf::kHavocs], 22.0f + mm_to_inches(30.0f) + 1.0f + 5.0f * mm_to_inches(20.0f), tyrant_y);
    start_game(gs);
    mf::play_until(gs, [](const GameState& g) { return g.pending && std::holds_alternative<decision::Shoot>(g.pending->kind); });
    const auto batches =
        shooting_batches(gs, gs.units[mf::kHavocs].id, ShootingType::CloseQuarters, lascannons_at(gs, mf::kTyrant));
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].profile.hit_modifier, -2);
    OddsCache cache;
    const TargetSnapshot tyrant = snapshot_of(gs.units[mf::kTyrant]);
    EXPECT_DOUBLE_EQ(expected_outcome(cache, volley_of(batches), tyrant).damage,
                     expected_outcome(cache, volley_of(batches, -1), tyrant).damage);
}

TEST(M63_Bots, TheBotAndItsDefaultsShootLegallyAtTheEngagedTyrant) {
    const GameState gs = mf::at_side_a_shooting();
    for (const bool defaults : {false, true}) {
        const BotChoice c = defaults ? wh40k::test::default_choice(gs, Side::A) : Bot(BotConfig{}).decide(gs, Side::A);
        ASSERT_FALSE(c.stopped) << defaults;
        EXPECT_TRUE(validate_action(gs, Side::A, c.action).ok()) << defaults;
    }
}

namespace {

// Plays each game on the monster fixture, the bot on both sides: no rejected action, no reverted move, no fallback.
struct Pairing {
    bool engaged;
    Side first;
    std::uint64_t seed;
};

void expect_clean_games(std::initializer_list<Pairing> pairings) {
    for (const Pairing& p : pairings) {
        const GameState scenario = mf::fixture(p.engaged, p.first);
        const GameRecord g = play_bot_game(scenario, p.seed, BotConfig{}, BotConfig{}, 8000);
        EXPECT_TRUE(g.finished) << "seed " << p.seed << (g.problems.empty() ? "" : ": " + g.problems.front());
        EXPECT_EQ(g.rejected, 0u) << "seed " << p.seed;
        EXPECT_EQ(g.reverted, 0u) << "seed " << p.seed;
        EXPECT_EQ(g.fallbacks, 0u) << "seed " << p.seed;
    }
}

}  // namespace

TEST(M63_Bots, GamesOnTheMonsterFixtureAreClean) {
    // Apart and engaged, either side first (D-085, M85: the seeds of the old Easy and Normal pairings).
    expect_clean_games({Pairing{false, Side::B, 31}, Pairing{true, Side::A, 32}, Pairing{false, Side::B, 33},
                        Pairing{true, Side::A, 34}, Pairing{true, Side::B, 38}, Pairing{false, Side::A, 39}});
}
