// M60 (D-071): the bots and the new weapon abilities. [SUSTAINED HITS X] in the exact odds (checked against the
// engine), abilities the engine refuses never supported or offered, a model with [EXTRA ATTACKS] weapons
// declaring them with its main weapon at every level, and bot games on the M59 fixtures finishing cleanly.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using namespace wh40k::ai::analysis;
using wh40k::test::decider;
using wh40k::test::repo_root;

namespace {

BotConfig level(BotLevel l) {
    BotConfig c;
    c.level = l;
    return c;
}

GameState havocs_fixture() {
    return load_scenario(repo_root() / wh40k::test::kHavocsVsTyrantFixture, repo_root());
}

// The Intercessors' Bolt Rifle on Focused Fire, as an attack profile, with [SUSTAINED HITS x] added (0: none).
AttackProfile bolt_rifle(const GameState& gs, int x) {
    WeaponProfile p = gs.units[0].models[0].weapons[0].profiles[0];
    if (x > 0) p.abilities.push_back(ability::SustainedHits{x});
    return make_attack_profile(p, AttackContext{AttackType::Shooting, false});
}

// The fixture with the Hive Tyrant in base contact with the middle Havoc, Side B first, at its DeclareMeleeAttacks
// decision. `usable_bonesword`: a stand-in bonesword without [TWIN-LINKED] (until M61).
GameState tyrant_declaring(bool usable_bonesword) {
    GameState gs = havocs_fixture();
    gs.first_turn = Side::B;
    if (usable_bonesword) gs.units[1].models[0].weapons[0].profiles[0].abilities.clear();
    const Model& havoc = gs.units[0].models[2];
    Model& tyrant = gs.units[1].models[0];
    tyrant.position = Position{havoc.position.x, havoc.position.y + base_radius(havoc.base) + base_radius(tyrant.base), 0};
    start_game(gs);
    for (int guard = 0; guard < 30 && gs.pending; ++guard) {
        const DecisionKind& k = gs.pending->kind;
        if (std::holds_alternative<decision::SelectUnitToFight>(k)) break;
        if (std::holds_alternative<decision::MoveUnits>(k)) {
            apply_action(gs, decider(gs), action::StartMove{gs.units[1].id, MoveType::RemainStationary, std::nullopt});
        } else if (std::holds_alternative<decision::Shoot>(k)) {
            apply_action(gs, decider(gs), action::EndShootStep{});
        } else if (std::holds_alternative<decision::Charge>(k)) {
            apply_action(gs, decider(gs), action::EndChargeStep{});
        } else if (std::holds_alternative<decision::PileIn>(k)) {
            apply_action(gs, decider(gs), action::EndPileIns{});
        } else {
            apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
        }
    }
    apply_action(gs, Side::B, action::SelectToFight{gs.units[1].id, FightType::Normal});
    EXPECT_TRUE(gs.pending && std::holds_alternative<decision::DeclareMeleeAttacks>(gs.pending->kind));
    return gs;
}

std::vector<std::string> weapons_declared(const GameState& gs, const action::Fight& f) {
    std::vector<std::string> out;
    for (const MeleeDeclaration& d : f.declarations) out.push_back(find_model(gs, d.model)->weapons.at(d.weapon_index).name);
    return out;
}

}  // namespace

TEST(M60_Odds, SustainedHitsMatchTwentyThousandEngineBatches) {
    // 24.36: a die scores 0, 1 or 1 + X hits; expected_outcome mixes over that exactly.
    const GameState gs = wh40k::test::load_v1_scenario();
    OddsCache cache;
    for (const int x : {1, 2}) {
        for (const Unit* target : {&gs.units[1], &gs.units[0]}) {
            const Volley v{VolleyBatch{bolt_rifle(gs, x), 10}};
            const AttackOutcome exact = expected_outcome(cache, v, snapshot_of(*target));
            const SampledOutcome s = sample_outcome(v, *target, 6060, 20000);
            EXPECT_NEAR(s.damage, exact.damage, 0.02 * exact.damage) << "x=" << x << " vs " << target->name;
            EXPECT_NEAR(s.models_destroyed, exact.models_destroyed, 0.02 * exact.models_destroyed) << x;
            double total = 0.0;
            for (std::size_t k = 0; k < exact.models_destroyed_pmf.size(); ++k) {
                EXPECT_NEAR(s.models_destroyed_pmf[k], exact.models_destroyed_pmf[k], 0.015) << x << " k=" << k;
                total += exact.models_destroyed_pmf[k];
            }
            EXPECT_NEAR(total, 1.0, 1e-9);
            const AttackOutcome plain = expected_outcome(cache, Volley{VolleyBatch{bolt_rifle(gs, 0), 10}}, snapshot_of(*target));
            EXPECT_GT(exact.damage, plain.damage);
            EXPECT_NEAR(exact.hits, plain.hits + 10.0 * x / 6.0, 1e-9);  // X more hits per critical hit (1 in 6)
        }
    }
    // The cache keeps the profiles with and without the ability apart.
    EXPECT_EQ(cache.hit_wound(bolt_rifle(gs, 0), 3).sustained, 0);
    EXPECT_EQ(cache.hit_wound(bolt_rifle(gs, 2), 3).sustained, 2);
    EXPECT_EQ(cache.hit_wound(bolt_rifle(gs, 2), 3).hit, cache.hit_wound(bolt_rifle(gs, 0), 3).hit);
}

TEST(M60_Odds, AbilitiesTheEngineRefusesAreNotSupported) {
    WeaponProfile p = wh40k::test::load_v1_scenario().units[0].models[0].weapons[0].profiles[0];
    EXPECT_TRUE(profile_supported(p));
    for (const WeaponAbility& ok : {WeaponAbility{ability::SustainedHits{1}}, WeaponAbility{ability::ExtraAttacks{}},
                                    WeaponAbility{ability::Hazardous{}}}) {
        WeaponProfile q = p;
        q.abilities.push_back(ok);
        EXPECT_TRUE(profile_supported(q));
    }
    p.abilities.push_back(ability::Unknown{"Lethal Hits"});  // D-071
    EXPECT_FALSE(profile_supported(p));
}

TEST(M60_Odds, TheMeleeVolleyAddsTheExtraAttacksWeapon) {
    // 24.11: the Hive Tyrant fights with its bonesword and its scything talons (A6 and A4); with the bonesword refused
    // (D-026, until M61), with the talons alone.
    OddsCache cache;
    for (const bool usable : {true, false}) {
        GameState gs = havocs_fixture();
        if (usable) gs.units[1].models[0].weapons[0].profiles[0].abilities.clear();
        const Volley v = unit_melee_volley(cache, gs.units[1], gs.units[0]);
        int dice = 0;
        for (const VolleyBatch& b : v) dice += b.attack_dice;
        EXPECT_EQ(v.size(), usable ? 2u : 1u);
        EXPECT_EQ(dice, usable ? 10 : 4);
    }
}

TEST(M60_Odds, BestMeleeDeclaresTheExtraAttacksWeaponToo) {
    const GameState gs = tyrant_declaring(true);
    OddsCache cache;
    const auto plan = best_melee(cache, gs, gs.units[1].id, gs.units[0].id);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(weapons_declared(gs, action::Fight{gs.units[1].id, plan->declarations}),
              (std::vector<std::string>{"Monstrous bonesword and lash whip", "Monstrous scything talons"}));
    EXPECT_TRUE(melee_problems(gs, gs.units[1], plan->declarations).empty());
}

TEST(M60_Bots, EveryLevelDeclaresTheExtraAttacksWeaponWithItsMainWeapon) {
    for (const bool usable : {true, false}) {
        const GameState gs = tyrant_declaring(usable);
        for (const BotLevel l : {BotLevel::Easy, BotLevel::Normal, BotLevel::Hard}) {
            Bot bot(level(l));
            const BotChoice c = bot.decide(gs, Side::B);
            ASSERT_FALSE(c.stopped) << static_cast<int>(l);
            const auto* f = std::get_if<action::Fight>(&c.action);
            ASSERT_NE(f, nullptr) << static_cast<int>(l);
            EXPECT_TRUE(validate_action(gs, Side::B, c.action).ok()) << static_cast<int>(l);
            const std::vector<std::string> expected =
                usable ? std::vector<std::string>{"Monstrous bonesword and lash whip", "Monstrous scything talons"}
                       : std::vector<std::string>{"Monstrous scything talons"};
            EXPECT_EQ(weapons_declared(gs, *f), expected) << static_cast<int>(l);
            EXPECT_EQ(bot.fallbacks(), 0u);
        }
    }
}

TEST(M60_Bots, NoLevelFiresAWeaponWithAnAbilityTheEngineRefuses) {
    // D-071: the Bolt Rifles get an ability the engine doesn't know; only the bolt pistols may fire.
    GameState gs = wh40k::test::shoot_step_state();
    for (Model& m : gs.units[0].models) {
        for (WeaponProfile& p : m.weapons[0].profiles) p.abilities.push_back(ability::Unknown{"Lethal Hits"});
    }
    for (const BotLevel l : {BotLevel::Easy, BotLevel::Normal, BotLevel::Hard}) {
        Bot bot(level(l));
        const BotChoice c = bot.decide(gs, Side::A);
        ASSERT_FALSE(c.stopped);
        EXPECT_TRUE(validate_action(gs, Side::A, c.action).ok());
        if (const auto* s = std::get_if<action::Shoot>(&c.action)) {
            for (const ShotDeclaration& d : s->shots) EXPECT_NE(d.weapon_index, 0u) << static_cast<int>(l);
        }
    }
}

TEST(M60_Bots, GamesOnTheFixturesAreClean) {
    // Whole games with Hazardous supercharge profiles, the Tyrant's talons and the mixed loadouts: no rejected action,
    // no reverted move, no fallback. (Hard on the thirteen-unit fixture is M69's.)
    const GameState duel = havocs_fixture();
    const GameState rosters = wh40k::test::load_rosters_fixture_closer(26.0f);
    struct Pairing {
        const GameState* scenario;
        BotLevel a;
        BotLevel b;
        std::uint64_t seed;
    };
    for (const Pairing& p : {Pairing{&duel, BotLevel::Easy, BotLevel::Easy, 1}, Pairing{&duel, BotLevel::Normal, BotLevel::Normal, 2},
                             Pairing{&duel, BotLevel::Hard, BotLevel::Normal, 3}, Pairing{&rosters, BotLevel::Easy, BotLevel::Easy, 4},
                             Pairing{&rosters, BotLevel::Normal, BotLevel::Easy, 5}}) {
        const GameRecord g = play_bot_game(*p.scenario, p.seed, level(p.a), level(p.b), 8000);
        EXPECT_TRUE(g.finished) << "seed " << p.seed << (g.problems.empty() ? "" : ": " + g.problems.front());
        EXPECT_EQ(g.rejected, 0u) << "seed " << p.seed;
        EXPECT_EQ(g.reverted, 0u) << "seed " << p.seed;
        EXPECT_EQ(g.fallbacks, 0u) << "seed " << p.seed;
    }
}
