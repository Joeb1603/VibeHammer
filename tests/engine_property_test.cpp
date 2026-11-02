// D-004 property tests over random play: every fully specified legal action validates; template fills
// get a deterministic, rule-cited result; the other side can never act; `pending` always equals the
// recomputed decision.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::decider;
using wh40k::test::fill_template;
using wh40k::test::is_rule_cited;
using wh40k::test::select;
using wh40k::test::shoot_step_state;
using wh40k::test::TestRng;

namespace {

struct PlayStats {
    int actions = 0;
    int template_fills = 0;
    int rejected_fills = 0;
};

void check_rejection(const ValidationResult& r) {
    ASSERT_FALSE(r.ok());
    for (const auto& reason : r.reasons) {
        EXPECT_TRUE(is_rule_cited(reason)) << reason;
    }
}

// Random play until GameOver or `max_actions`, checking the D-004 properties at every decision.
PlayStats play_and_check(GameState& gs, TestRng& rng, int max_actions) {
    PlayStats stats;
    while (gs.pending && stats.actions < max_actions) {
        const Side side = decider(gs);
        const Side other = opponent(side);
        EXPECT_TRUE(legal_actions(gs, other).empty());
        const auto legal = legal_actions(gs, side);
        EXPECT_FALSE(legal.empty()) << describe(*gs.pending);

        for (const GameAction& a : legal) {
            check_rejection(validate_action(gs, other, a));  // never the other side's turn to act
            if (!is_template(a)) {
                const auto r = validate_action(gs, side, a);
                EXPECT_TRUE(r.ok()) << action_name(a) << ": " << (r.ok() ? "" : r.reasons[0]);
                continue;
            }
            check_rejection(validate_action(gs, side, a));  // an unfilled template is never valid
            for (int k = 0; k < 4; ++k) {
                const bool corrupt = rng.below(2) == 0;
                const GameAction filled = fill_template(gs, a, rng, corrupt);
                const auto first = validate_action(gs, side, filled);
                const auto second = validate_action(gs, side, filled);
                EXPECT_EQ(first, second);  // deterministic
                ++stats.template_fills;
                if (corrupt) {
                    check_rejection(first);
                    ++stats.rejected_fills;
                } else if (wh40k::test::clean_fill_always_valid(a)) {
                    EXPECT_TRUE(first.ok()) << (first.ok() ? "" : first.reasons[0]);
                } else if (!first.ok()) {
                    check_rejection(first);  // e.g. a random path through an enemy model: still rule-cited
                }
            }
        }

        const GameAction pick = wh40k::test::random_action(gs, rng);
        apply_action(gs, side, pick);
        ++stats.actions;
        EXPECT_EQ(gs.pending, compute_decision(gs));
    }
    return stats;
}

}  // namespace

TEST(D004_LegalActions, RandomPlayFromTheScenarioStart) {
    int moves_proposed = 0;
    for (std::uint64_t seed = 1; seed <= 50; ++seed) {
        GameState gs = wh40k::test::load_v1_scenario();
        gs.dice.seed = seed;
        start_game(gs);
        TestRng rng(seed);
        const PlayStats stats = play_and_check(gs, rng, 200);
        EXPECT_TRUE(gs.flow.stage == Stage::GameOver || stats.actions == 200) << "seed " << seed;
        moves_proposed += stats.template_fills;
    }
    EXPECT_GT(moves_proposed, 0);  // path templates (D-004) were filled and checked
}

TEST(D004_LegalActions, RandomPlayFromTheScenarioStartMovesModels) {
    // Separate assertion so a regression is easy to read: random play does use the path templates.
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    TestRng rng(99);
    const PlayStats stats = play_and_check(gs, rng, 200);
    EXPECT_GT(stats.template_fills, 0);
}

TEST(D004_LegalActions, RandomPlayThroughAttackFlows) {
    int fills = 0;
    int rejected = 0;
    for (std::uint64_t seed = 1; seed <= 50; ++seed) {
        GameState gs = shoot_step_state(Side::A, seed);
        const UnitId gants = gs.units[1].id;
        std::vector<WeaponSelection> sel;
        for (std::size_t i = 0; i < 5; ++i) {
            const std::size_t profile = i % 2;  // mix Focused Fire (0) and Saturation (1): two batches
            sel.push_back(select(gs, 0, i, 0, profile, gants));
        }
        begin_attacks(gs, gs.units[0].id, AttackContext{AttackType::Shooting, true}, sel);
        EXPECT_EQ(gs.pending, compute_decision(gs));
        TestRng rng(seed * 7919);
        const PlayStats stats = play_and_check(gs, rng, 200);
        EXPECT_FALSE(gs.pending.has_value()) << "seed " << seed;  // ran to GameOver
        fills += stats.template_fills;
        rejected += stats.rejected_fills;
    }
    EXPECT_GT(fills, 0);  // the casualty-order template was exercised
    EXPECT_GT(rejected, 0);
}
