// M61 (D-072): the bots and [TWIN-LINKED] (24.38). Every level, the fallback and the lookahead's rollouts re-roll
// every failed wound roll; the exact odds use p + (1 - p)p (checked against 20,000 engine batches); bot games with
// the Hive Tyrant's bonesword finish cleanly.

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "bot_driver.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/attacks.hpp"
#include "fallback.hpp"
#include "hard/evaluator.hpp"
#include "hard/hard_bot.hpp"
#include "hard/lookahead.hpp"
#include "state_hash.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using namespace wh40k::ai::analysis;
using wh40k::test::repo_root;
using wh40k::test::select;

namespace {

BotConfig level(BotLevel l) {
    BotConfig c;
    c.level = l;
    return c;
}

// The V1 Shoot step with the Bolt Rifles [TWIN-LINKED] (a stand-in: no V1 weapon has it), one Intercessor firing on
// Saturation (3 dice, 3+ to wound against the Termagants), the second wound roll failed: the re-roll decision.
GameState at_the_decision() {
    GameState gs = wh40k::test::shoot_step_state();
    for (Model& m : gs.units[0].models) {
        for (WeaponProfile& p : m.weapons[0].profiles) p.abilities.push_back(ability::TwinLinked{});
    }
    ScriptedDice dice{3, 3, 3, 4, 1, 2};
    begin_attacks(gs, dice, gs.units[0].id, AttackContext{AttackType::Shooting, true},
                  {select(gs, 0, 0, 0, 1, gs.units[1].id)});
    EXPECT_TRUE(gs.pending && std::holds_alternative<decision::RerollWoundRolls>(gs.pending->kind));
    return gs;
}

// The Intercessors' Bolt Rifle on Saturation as an attack profile, with [TWIN-LINKED] (and [SUSTAINED HITS x]).
AttackProfile saturation(const GameState& gs, bool twin_linked, int sustained = 0) {
    WeaponProfile p = gs.units[0].models[0].weapons[0].profiles[1];
    if (twin_linked) p.abilities.push_back(ability::TwinLinked{});
    if (sustained > 0) p.abilities.push_back(ability::SustainedHits{sustained});
    return make_attack_profile(p, AttackContext{AttackType::Shooting, false});
}

}  // namespace

TEST(M61_Bots, EveryLevelReRollsEveryFailedWoundRoll) {
    const GameState gs = at_the_decision();
    const GameAction expected = action::RerollWoundRolls{{1, 2}};
    for (const BotLevel l : {BotLevel::Easy, BotLevel::Normal, BotLevel::Hard}) {
        Bot bot(level(l));
        const BotChoice c = bot.decide(gs, Side::A);
        ASSERT_FALSE(c.stopped) << static_cast<int>(l);
        EXPECT_EQ(c.action, expected) << static_cast<int>(l);
        EXPECT_TRUE(validate_action(gs, Side::A, c.action).ok());
        EXPECT_EQ(bot.fallbacks(), 0u) << static_cast<int>(l);
        EXPECT_NE(c.reason.find("24.38"), std::string::npos) << c.reason;
    }
    // The always-legal fallback answers the same, and the driver paces it like the attacker's other choices.
    const FallbackResult fb = fallback_action(gs, Side::A);
    ASSERT_TRUE(fb.action.has_value());
    EXPECT_EQ(*fb.action, expected);
    EXPECT_EQ(base_pause_seconds(expected), base_pause_seconds(GameAction{action::SelectAttackBatch{0}}));
}

TEST(M61_Bots, TheStateHashSeesThePendingReRollOnlyWhileItWaits) {
    GameState gs = at_the_decision();
    const std::uint64_t waiting = state_hash(gs);
    GameState other = gs;
    other.flow.attack->wound_rerolls->rolls.rolls[0].unmodified = 5;  // different dice to choose from
    EXPECT_NE(state_hash(other), waiting);
    // Without it (as in every state before M61) the hash ignores the field entirely.
    GameState cleared = gs;
    cleared.flow.attack->wound_rerolls.reset();
    GameState also_cleared = other;
    also_cleared.flow.attack->wound_rerolls.reset();
    EXPECT_EQ(state_hash(cleared), state_hash(also_cleared));
}

TEST(M61_Rollouts, TheLookaheadAnswersTheDecisionOnItsScratchCopies) {
    // Hard's Monte Carlo rollouts start from a state at the decision: their first action is the same answer.
    const GameState gs = at_the_decision();
    hard::HardContext hc;
    OddsCache cache;
    const hard::Evaluator eval(cache, gs, Side::A, hc.weights.chain, &hc.chains);
    ASSERT_TRUE(eval.ok());
    hard::LookaheadWeights w;
    w.rollouts = 3;
    std::vector<hard::RolloutTrace> traces;
    const hard::MonteCarloResult r = hard::monte_carlo(eval, cache, gs, Side::A, hard::rollout_seed(gs), w, {}, &traces);
    EXPECT_EQ(r.rollouts, 3u);
    ASSERT_EQ(traces.size(), 3u);
    for (const hard::RolloutTrace& t : traces) {
        ASSERT_FALSE(t.actions.empty());
        EXPECT_EQ(t.actions.front(), GameAction{(action::RerollWoundRolls{{1, 2}})});
    }
}

TEST(M61_Odds, TheTwinLinkedWoundChanceIsPPlusOneMinusPTimesP) {
    const GameState gs = wh40k::test::load_v1_scenario();
    OddsCache cache;
    // S5 against T3: 3+ to wound (p = 4/6, c = 1/6); against T5: 4+ (p = 3/6).
    for (const int toughness : {3, 5}) {
        const HitWoundOdds& plain = cache.hit_wound(saturation(gs, false), toughness);
        const HitWoundOdds& twin = cache.hit_wound(saturation(gs, true), toughness);
        const double p = plain.wound;
        const double c = plain.critical_wound;
        EXPECT_NEAR(twin.wound, p + (1.0 - p) * p, 1e-12) << toughness;
        EXPECT_NEAR(twin.critical_wound, c + (1.0 - p) * c, 1e-12) << toughness;  // a failed roll re-rolled to a 6
        EXPECT_EQ(twin.hit, plain.hit);
        EXPECT_TRUE(twin.twin_linked);
        EXPECT_FALSE(plain.twin_linked);
    }
    EXPECT_NEAR(cache.hit_wound(saturation(gs, false), 3).wound, 4.0 / 6.0, 1e-12);
    // A wound modifier applies after the re-roll (D-011): -1 makes it 4+ (p = 3/6 with the critical 6).
    AttackProfile minus = saturation(gs, true);
    minus.wound_modifier = -1;
    EXPECT_NEAR(cache.hit_wound(minus, 3).wound, 0.5 + 0.5 * 0.5, 1e-12);
    // The cache keeps the two apart, and the profile without the ability gets exactly the old numbers.
    OddsCache fresh;
    EXPECT_EQ(fresh.hit_wound(saturation(gs, false), 3).wound, cache.hit_wound(saturation(gs, false), 3).wound);
    EXPECT_NE(cache.hit_wound(saturation(gs, true), 3).wound, cache.hit_wound(saturation(gs, false), 3).wound);
}

TEST(M61_Odds, TwinLinkedMatchesTwentyThousandEngineBatches) {
    // expected_outcome against core::resolve_attack_batch with every failed wound roll re-rolled (24.38), with and
    // without [SUSTAINED HITS 1], against both V1 units: within 2%.
    const GameState gs = wh40k::test::load_v1_scenario();
    OddsCache cache;
    for (const int sustained : {0, 1}) {
        for (const Unit* target : {&gs.units[1], &gs.units[0]}) {
            const Volley v{VolleyBatch{saturation(gs, true, sustained), 12}};
            const AttackOutcome exact = expected_outcome(cache, v, snapshot_of(*target));
            const SampledOutcome s = sample_outcome(v, *target, 6161, 20000);
            EXPECT_NEAR(s.damage, exact.damage, 0.02 * exact.damage) << sustained << " vs " << target->name;
            EXPECT_NEAR(s.models_destroyed, exact.models_destroyed, 0.02 * exact.models_destroyed) << sustained;
            for (std::size_t k = 0; k < exact.models_destroyed_pmf.size(); ++k) {
                EXPECT_NEAR(s.models_destroyed_pmf[k], exact.models_destroyed_pmf[k], 0.015) << sustained << " k=" << k;
            }
            const AttackOutcome plain =
                expected_outcome(cache, Volley{VolleyBatch{saturation(gs, false, sustained), 12}}, snapshot_of(*target));
            EXPECT_GT(exact.damage, plain.damage);
            EXPECT_NEAR(exact.hits, plain.hits, 1e-9);  // the hit rolls are not re-rolled
        }
    }
}

TEST(M61_Odds, ProfileSupportedLetsTwinLinkedThrough) {
    WeaponProfile p = wh40k::test::load_v1_scenario().units[0].models[0].weapons[0].profiles[0];
    p.abilities.push_back(ability::TwinLinked{});
    EXPECT_TRUE(profile_supported(p));
    p.abilities.push_back(ability::DevastatingWounds{});  // still refused (D-026)
    EXPECT_FALSE(profile_supported(p));
}

TEST(M61_Bots, GamesWithTheTwinLinkedBoneswordAreClean) {
    // The Havocs vs Hive Tyrant fixture as it is (the Tyrant fights with its real bonesword, 24.11) and the roster
    // fixture: no rejected action, no reverted move, no fallback, at every level. (Hard on the thirteen-unit fixture is
    // M69's.)
    const GameState duel = load_scenario(repo_root() / wh40k::test::kHavocsVsTyrantFixture, repo_root());
    const GameState rosters = wh40k::test::load_rosters_fixture_closer(26.0f);
    struct Pairing {
        const GameState* scenario;
        BotLevel a;
        BotLevel b;
        std::uint64_t seed;
    };
    int rerolls = 0;
    for (const Pairing& p : {Pairing{&duel, BotLevel::Easy, BotLevel::Easy, 11}, Pairing{&duel, BotLevel::Normal, BotLevel::Normal, 12},
                             Pairing{&duel, BotLevel::Hard, BotLevel::Hard, 13}, Pairing{&rosters, BotLevel::Easy, BotLevel::Easy, 14},
                             Pairing{&rosters, BotLevel::Normal, BotLevel::Normal, 15}}) {
        std::vector<std::string> trace;
        const GameRecord g = play_bot_game(*p.scenario, p.seed, level(p.a), level(p.b), 8000, nullptr, &trace);
        EXPECT_TRUE(g.finished) << "seed " << p.seed << (g.problems.empty() ? "" : ": " + g.problems.front());
        EXPECT_EQ(g.rejected, 0u) << "seed " << p.seed;
        EXPECT_EQ(g.reverted, 0u) << "seed " << p.seed;
        EXPECT_EQ(g.fallbacks, 0u) << "seed " << p.seed;
        for (const std::string& line : trace) rerolls += line.find("24.38") != std::string::npos ? 1 : 0;
    }
    EXPECT_GT(rerolls, 0);  // the bots met the decision and answered it
}
