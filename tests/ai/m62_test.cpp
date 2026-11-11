// M62 (D-069): the bots with random characteristics. profile_supported lets a random A or D through (a random-A melee
// weapon stays refused); the exact odds mix over the A and D distributions (checked against 20,000 engine batches); the
// bots' samples roll them with their own dice; bot games with the lascannons, the bio-plasmic scream and the heavy
// venom cannon finish cleanly.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "default_choice.hpp"
#include "core/data/datasheet.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/rules/attacks.hpp"
#include "state_hash.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using namespace wh40k::ai::analysis;
using wh40k::test::repo_root;
using wh40k::test::select;

namespace {

WeaponProfile weapon_profile(const char* unit, const char* weapon) {
    return load_datasheet(repo_root() / (std::string("data/units/") + unit + ".json")).find_weapon(weapon)->profiles.at(0);
}

GameState duel() { return load_scenario(repo_root() / wh40k::test::kHavocsVsTyrantFixture, repo_root()); }

// The duel with the Hive Tyrant's bonesword swapped for a heavy venom cannon (A D3, D3, [BLAST]; as replay m60).
GameState duel_with_cannon() {
    GameState gs = duel();
    gs.units[1].models[0].weapons[0] =
        *load_datasheet(repo_root() / "data/units/hive_tyrant.json").find_weapon("Heavy venom cannon");
    return gs;
}

void expect_matches_samples(OddsCache& cache, const Volley& v, const Unit& target, const std::string& label) {
    const AttackOutcome exact = expected_outcome(cache, v, snapshot_of(target));
    const SampledOutcome s = sample_outcome(v, target, 6262, 20000);
    EXPECT_NEAR(s.damage, exact.damage, 0.02 * exact.damage) << label;
    EXPECT_NEAR(s.models_destroyed, exact.models_destroyed, std::max(0.02 * exact.models_destroyed, 0.005)) << label;
    for (std::size_t k = 0; k < exact.models_destroyed_pmf.size(); ++k) {
        EXPECT_NEAR(s.models_destroyed_pmf[k], exact.models_destroyed_pmf[k], 0.015) << label << " k=" << k;
    }
}

}  // namespace

TEST(M62_Odds, ProfileSupportedLetsARandomAOrDThroughButNotARandomAMeleeWeapon) {
    EXPECT_TRUE(profile_supported(weapon_profile("havocs", "Havoc lascannon")));            // D D6+1
    EXPECT_TRUE(profile_supported(weapon_profile("screamer_killer", "Bio-plasmic scream")));  // A D6+3
    EXPECT_TRUE(profile_supported(weapon_profile("hive_tyrant", "Heavy venom cannon")));     // A D3
    EXPECT_FALSE(profile_supported(weapon_profile("havocs", "Meltagun")));                    // [MELTA 2], D-071
    WeaponProfile melee = weapon_profile("havocs", "Astartes chainsword");
    EXPECT_TRUE(profile_supported(melee));
    melee.damage = DiceExpr{1, 3, 0};
    EXPECT_TRUE(profile_supported(melee));  // a random D is rolled in melee too
    melee.attacks = DiceExpr{1, 3, 0};
    EXPECT_FALSE(profile_supported(melee));  // D-069: a random A on a melee weapon is refused
}

TEST(M62_Odds, TheDistributionsComeFromCoresRoll) {
    OddsCache cache;
    const std::vector<double>& d3 = cache.value_pmf(DiceExpr{1, 3, 0});
    ASSERT_EQ(d3.size(), 4u);
    EXPECT_EQ(d3[0], 0.0);
    for (int v = 1; v <= 3; ++v) EXPECT_NEAR(d3[static_cast<std::size_t>(v)], 1.0 / 3.0, 1e-12) << v;
    const std::vector<double>& d6p1 = cache.value_pmf(DiceExpr{1, 6, 1});
    ASSERT_EQ(d6p1.size(), 8u);
    EXPECT_EQ(d6p1[1], 0.0);
    for (int v = 2; v <= 7; ++v) EXPECT_NEAR(d6p1[static_cast<std::size_t>(v)], 1.0 / 6.0, 1e-12) << v;
    const std::vector<double>& two = cache.value_pmf(DiceExpr{2, 6, 0});
    EXPECT_NEAR(two[7], 6.0 / 36.0, 1e-12);
    EXPECT_NEAR(pmf_mean(two), 7.0, 1e-12);
    EXPECT_EQ(cache.value_pmf(DiceExpr::fixed(3)), (std::vector<double>{0.0, 0.0, 0.0, 1.0}));
    const DiceExpr screams[] = {DiceExpr{1, 6, 3}, DiceExpr{1, 6, 3}};
    const std::vector<double>& sum = cache.sum_pmf(screams);
    EXPECT_EQ(sum.size(), 19u);  // 8..18
    EXPECT_NEAR(pmf_mean(sum), 13.0, 1e-12);
    EXPECT_NEAR(sum[13], 6.0 / 36.0, 1e-12);
}

TEST(M62_Odds, ARandomAMixesExactlyOverItsCounts) {
    // The outcome of a D6 batch is the average of the outcomes of 1..6 fixed dice (its counts' mixture), with the
    // bonus dice added to every count.
    const GameState gs = wh40k::test::load_v1_scenario();
    OddsCache cache;
    const AttackProfile scream =
        make_attack_profile(weapon_profile("screamer_killer", "Bio-plasmic scream"), AttackContext{AttackType::Shooting, false});
    for (const Unit* target : {&gs.units[1], &gs.units[0]}) {
        const TargetSnapshot snap = snapshot_of(*target);
        const AttackOutcome random = expected_outcome(cache, Volley{VolleyBatch{scream, 2, {DiceExpr{1, 6, 0}}}}, snap);
        AttackOutcome mean;
        mean.models_destroyed_pmf.assign(random.models_destroyed_pmf.size(), 0.0);
        for (int k = 1; k <= 6; ++k) {
            const AttackOutcome fixed = expected_outcome(cache, Volley{VolleyBatch{scream, 2 + k, {}}}, snap);
            mean.damage += fixed.damage / 6.0;
            mean.models_destroyed += fixed.models_destroyed / 6.0;
            mean.attack_dice += fixed.attack_dice / 6.0;
            mean.unsaved += fixed.unsaved / 6.0;
            for (std::size_t i = 0; i < mean.models_destroyed_pmf.size(); ++i) {
                mean.models_destroyed_pmf[i] += fixed.models_destroyed_pmf[i] / 6.0;
            }
        }
        EXPECT_NEAR(random.damage, mean.damage, 1e-9) << target->name;
        EXPECT_NEAR(random.models_destroyed, mean.models_destroyed, 1e-9) << target->name;
        EXPECT_NEAR(random.attack_dice, mean.attack_dice, 1e-9) << target->name;
        EXPECT_NEAR(random.unsaved, mean.unsaved, 1e-9) << target->name;
        for (std::size_t i = 0; i < mean.models_destroyed_pmf.size(); ++i) {
            EXPECT_NEAR(random.models_destroyed_pmf[i], mean.models_destroyed_pmf[i], 1e-9) << target->name << " " << i;
        }
    }
}

TEST(M62_Odds, ARandomABlastWeaponMatchesTwentyThousandEngineBatches) {
    // The bio-plasmic scream (A D6+3, [BLAST]) against the ten Termagants (+2 dice after the roll) and the five
    // Intercessors (+1), one and two of them: within 2% of core::resolve_attack_batch with the bots' own dice.
    const GameState gs = wh40k::test::load_v1_scenario();
    OddsCache cache;
    const AttackProfile scream =
        make_attack_profile(weapon_profile("screamer_killer", "Bio-plasmic scream"), AttackContext{AttackType::Shooting, false});
    const DiceExpr a{1, 6, 3};
    expect_matches_samples(cache, Volley{VolleyBatch{scream, 2, {a}}}, gs.units[1], "1 scream vs Termagants");
    expect_matches_samples(cache, Volley{VolleyBatch{scream, 4, {a, a}}}, gs.units[1], "2 screams vs Termagants");
    expect_matches_samples(cache, Volley{VolleyBatch{scream, 2, {a, a}}}, gs.units[0], "2 screams vs Intercessors");
    // The heavy venom cannon (A D3, D3) against the Intercessors (W2): a random A and a fixed D above W.
    const AttackProfile cannon =
        make_attack_profile(weapon_profile("hive_tyrant", "Heavy venom cannon"), AttackContext{AttackType::Shooting, false});
    expect_matches_samples(cache, Volley{VolleyBatch{cannon, 3, {DiceExpr{1, 3, 0}, DiceExpr{1, 3, 0}, DiceExpr{1, 3, 0}}}},
                           gs.units[0], "3 cannons vs Intercessors");
}

TEST(M62_Odds, ARandomDWeaponMatchesTwentyThousandEngineBatches) {
    // The Havoc lascannon (D D6+1) against the Hive Tyrant (W10, the excess lost), a Screamer-killer (W10), the Tyrant
    // Guard (three W4 models) and the V1 Intercessors (W2: most of each roll is lost).
    const GameState rosters = wh40k::test::load_rosters_fixture();
    const GameState v1 = wh40k::test::load_v1_scenario();
    OddsCache cache;
    const AttackProfile lascannon =
        make_attack_profile(weapon_profile("havocs", "Havoc lascannon"), AttackContext{AttackType::Shooting, false});
    for (const int n : {4, 12}) {
        for (const std::uint32_t id : {6u, 11u, 12u}) {
            const Unit& target = *rosters.find_unit(UnitId{id});
            expect_matches_samples(cache, Volley{VolleyBatch{lascannon, n, {}}}, target,
                                   std::to_string(n) + " lascannons vs " + target.name);
        }
        expect_matches_samples(cache, Volley{VolleyBatch{lascannon, n, {}}}, v1.units[0],
                               std::to_string(n) + " lascannons vs Intercessors");
    }
}

TEST(M62_Odds, TheSamplesRollWithTheBotsOwnDice) {
    // D-054: the same seed gives the same sample, another seed another; no game dice are involved.
    const GameState gs = wh40k::test::load_v1_scenario();
    const AttackProfile scream =
        make_attack_profile(weapon_profile("screamer_killer", "Bio-plasmic scream"), AttackContext{AttackType::Shooting, false});
    const Volley v{VolleyBatch{scream, 2, {DiceExpr{1, 6, 3}}}};
    const SampledOutcome a = sample_outcome(v, gs.units[1], 7, 200);
    const SampledOutcome b = sample_outcome(v, gs.units[1], 7, 200);
    const SampledOutcome c = sample_outcome(v, gs.units[1], 8, 200);
    EXPECT_EQ(a.damage, b.damage);
    EXPECT_EQ(a.models_destroyed_pmf, b.models_destroyed_pmf);
    EXPECT_NE(a.models_destroyed_pmf, c.models_destroyed_pmf);
}

TEST(M62_Odds, AnEngineBatchWithAnUnrolledRandomAGivesItsVolley) {
    AttackBatch b;
    b.attack_dice = 3;
    b.random_attacks = {RandomAttackDice{1, ModelId{4}, DiceExpr{1, 6, 3}, 2}};
    const VolleyBatch v = volley_batch(b);
    EXPECT_EQ(v.attack_dice, 5);  // the bonus dice are counted with the fixed ones
    EXPECT_EQ(v.random_attacks, (std::vector<DiceExpr>{DiceExpr{1, 6, 3}}));
}

TEST(M62_Bots, TheStateHashSeesAnUnrolledRandomAOnlyWhileItWaits) {
    // The V1 Shoot step with two batches, the second a stand-in random A: the hash includes it while it is unrolled.
    GameState gs = wh40k::test::shoot_step_state();
    gs.units[0].models[1].weapons[0].profiles[1].attacks = DiceExpr{1, 6, 3};
    ScriptedDice none{};
    begin_attacks(gs, none, gs.units[0].id, AttackContext{AttackType::Shooting, false},
                  {select(gs, 0, 0, 0, 0, gs.units[1].id), select(gs, 0, 1, 0, 1, gs.units[1].id)});
    ASSERT_TRUE(std::holds_alternative<decision::SelectAttackBatch>(gs.pending->kind));
    GameState rolled = gs;
    rolled.flow.attack->batches[1].random_attacks.clear();
    EXPECT_NE(state_hash(gs), state_hash(rolled));
    GameState other = gs;
    other.flow.attack->batches[1].random_attacks[0].attacks = DiceExpr{1, 6, 2};
    EXPECT_NE(state_hash(gs), state_hash(other));
}

TEST(M62_Bots, TheBotAndItsDefaultsFireTheHavocLascannons) {
    // D-069 makes them usable: the Havocs' Shoot step on the duel, the bot's declaration and its defaults' (once level
    // Easy's) include them.
    GameState gs = duel();
    start_game(gs);
    apply_action(gs, Side::A, action::StartMove{gs.units[0].id, MoveType::RemainStationary, std::nullopt});
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
    for (const bool defaults : {false, true}) {
        const BotChoice c = defaults ? wh40k::test::default_choice(gs, Side::A) : Bot(BotConfig{}).decide(gs, Side::A);
        ASSERT_FALSE(c.stopped);
        EXPECT_TRUE(validate_action(gs, Side::A, c.action).ok());
        const auto* s = std::get_if<action::Shoot>(&c.action);
        ASSERT_NE(s, nullptr) << defaults;
        int lascannons = 0;
        for (const ShotDeclaration& d : s->shots) {
            lascannons += find_model(gs, d.model)->weapons.at(d.weapon_index).name == "Havoc lascannon" ? 1 : 0;
        }
        EXPECT_EQ(lascannons, 4) << defaults;
    }
}

TEST(M62_Bots, GamesWithRandomCharacteristicsAreClean) {
    // The duel (lascannons), the duel with the Tyrant's heavy venom cannon (a random A with [BLAST]) and the roster
    // fixture (lascannons and bio-plasmic screams): no rejected action, no reverted move, no fallback. (D-085, M85: the
    // seeds of the old level pairings, the bot on both sides.)
    const GameState plain = duel();
    const GameState cannon = duel_with_cannon();
    const GameState rosters = wh40k::test::load_rosters_fixture_closer(26.0f);
    struct Pairing {
        const GameState* scenario;
        std::uint64_t seed;
    };
    for (const Pairing& p : {Pairing{&plain, 21}, Pairing{&plain, 22}, Pairing{&plain, 23}, Pairing{&cannon, 24},
                             Pairing{&cannon, 25}, Pairing{&cannon, 26}, Pairing{&rosters, 27}, Pairing{&rosters, 28}}) {
        const GameRecord g = play_bot_game(*p.scenario, p.seed, BotConfig{}, BotConfig{}, 8000);
        EXPECT_TRUE(g.finished) << "seed " << p.seed << (g.problems.empty() ? "" : ": " + g.problems.front());
        EXPECT_EQ(g.rejected, 0u) << "seed " << p.seed;
        EXPECT_EQ(g.reverted, 0u) << "seed " << p.seed;
        EXPECT_EQ(g.fallbacks, 0u) << "seed " << p.seed;
    }
}

// Normal's decision times on the two fixtures with the random weapons, for the M62 report (Release numbers come from the
// arena: `wh40k_bot_arena --scenario tests/data/scenarios/...`):
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M62_Timings.*
TEST(M62_Timings, DISABLED_NormalDecisionsOnTheFixtures) {
    for (const bool rosters : {false, true}) {
        const GameState scenario = rosters ? wh40k::test::load_rosters_fixture() : duel_with_cannon();
        std::vector<double> ms;
        const GameRecord g = play_bot_game(scenario, 1, BotConfig{}, BotConfig{}, 8000, &ms);
        std::sort(ms.begin(), ms.end());
        std::printf("%s: finished %d, %zu decisions, p95 %.2f ms, max %.2f ms\n", rosters ? "rosters" : "duel",
                    g.finished ? 1 : 0, ms.size(), ms.empty() ? 0.0 : ms[ms.size() * 95 / 100], ms.empty() ? 0.0 : ms.back());
    }
}
