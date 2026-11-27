// M101 (D-103): the bot with [INDIRECT FIRE] / Indirect Shooting (24.19, 10.07) and [PRECISION] (24.28): its answer to
// the Precision decision, the hit rule of indirect shooting in its odds, and whole games with stand-in abilities on the
// V1 weapons (no roster weapon has them).

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/terrain_shape.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/shooting.hpp"
#include "normal/defaults.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using namespace wh40k::ai::analysis;
using wh40k::test::at;
using wh40k::test::in;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

GameRecord play(const GameState& scenario, std::uint64_t seed, wh40k::client::GameSession* out = nullptr) {
    const BotConfig bot{};
    wh40k::client::GameSession session = wh40k::client::GameSession::new_game(
        scenario, seed, {true, true}, {!bot.plays_stratagems(), !bot.plays_stratagems()});
    const GameRecord record = play_bot_session(session, bot, bot);
    if (out != nullptr) *out = session;
    return record;
}

}  // namespace

TEST(M101_Bot, SelectsTheVisibleCharacterWithTheFewestWoundsLeft) {
    GameState gs = wh40k::test::shoot_step_state(Side::B);
    gs.units[kMarines].keywords.push_back("Character");
    gs.units[kMarines].models[3].wounds_remaining = 1;
    for (Model& m : gs.units[kGants].models) m.weapons[0].profiles[0].abilities.push_back(ability::Precision{});
    ScriptedDice dice{4, 4};
    apply_action(gs, Side::B,
                 action::Shoot{gs.units[kGants].id, ShootingType::Normal,
                               {ShotDeclaration{gs.units[kGants].models[0].id, 0, 0, gs.units[kMarines].id}}},
                 dice);
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::SelectPrecisionGroup>(gs.pending->kind));
    const std::vector<GameAction> legal = legal_actions(gs, Side::B);
    const auto answer = normal::select_precision_group(gs, legal);
    ASSERT_TRUE(answer.has_value());
    EXPECT_EQ(answer->action, GameAction{action::SelectPrecisionGroup{gs.units[kMarines].models[3].id}});
    // The bot gives that answer, and it is legal.
    Bot bot(BotConfig{});
    const BotChoice choice = bot.decide(gs, Side::B);
    EXPECT_FALSE(choice.stopped);
    EXPECT_EQ(choice.action, answer->action);
    EXPECT_TRUE(validate_action(gs, Side::B, choice.action).ok());
    // For any other decision: no answer.
    GameState other = wh40k::test::shoot_step_state(Side::B);
    EXPECT_FALSE(normal::select_precision_group(other, legal_actions(other, Side::B)).has_value());
}

TEST(M101_Odds, TheHitRuleOfIndirectShootingIsInThePerDieChances) {
    OddsCache cache;
    AttackProfile p;
    p.skill = 3;
    p.strength = 4;
    p.damage = DiceExpr::fixed(1);
    EXPECT_NEAR(cache.hit_wound(p, 4).hit, 4.0 / 6.0, 1e-12);
    p.unmodified_hit_fails_to = 5;  // 10.07: only a 6 hits
    EXPECT_NEAR(cache.hit_wound(p, 4).hit, 1.0 / 6.0, 1e-12);
    EXPECT_NEAR(cache.hit_wound(p, 4).critical_hit, 1.0 / 6.0, 1e-12);
    p.unmodified_hit_fails_to = 3;  // 4, 5 and 6 at BS 3+
    EXPECT_NEAR(cache.hit_wound(p, 4).hit, 3.0 / 6.0, 1e-12);
    p.skill = 5;  // a 4 no longer reaches the BS
    EXPECT_NEAR(cache.hit_wound(p, 4).hit, 2.0 / 6.0, 1e-12);
}

TEST(M101_Games, GamesWithIndirectFireAndPrecisionWeaponsPlayClean) {
    // The Termagants-first bot scenario with [INDIRECT FIRE] Bolt Rifles and [PRECISION] Fleshborers against CHARACTER
    // Intercessors, in half the games with M88's barricade area (an obscuring area) between the units: no rejected
    // action, no reverted move, no fallback, and the bot answers the Precision decision when it comes.
    GameState gs = load_scenario(wh40k::test::repo_root() / "data/scenarios/bot_termagants_first.json",
                                 wh40k::test::repo_root());
    ASSERT_EQ(gs.units.at(kMarines).side, Side::A);
    ASSERT_EQ(gs.units.at(kGants).side, Side::B);
    gs.units[kMarines].keywords.push_back("Character");
    for (Model& m : gs.units[kMarines].models) m.weapons[0].profiles[0].abilities.push_back(ability::IndirectFire{});
    for (Model& m : gs.units[kGants].models) m.weapons[0].profiles[0].abilities.push_back(ability::Precision{});
    int indirect_shoots = 0;
    int precision_answers = 0;
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        GameState scenario = gs;
        if (seed % 2 == 0) {
            TerrainFeature f;
            f.name = "Barricade";
            f.category = TerrainCategory::Light;
            f.shape = polygon_shape({at(1, 11.4f), at(23, 11.4f), at(23, 11.6f), at(1, 11.6f)});
            TerrainArea a;
            a.name = "Barricades";
            a.shape = polygon_shape({at(0.5f, 10), at(23.5f, 10), at(23.5f, 13), at(0.5f, 13)});
            a.features = {f};
            scenario.terrain = {a};
        }
        wh40k::client::GameSession session(scenario);
        const GameRecord record = play(scenario, seed, &session);
        EXPECT_TRUE(record.finished) << "seed " << seed;
        EXPECT_EQ(record.rejected, 0u) << "seed " << seed;
        EXPECT_EQ(record.reverted, 0u) << "seed " << seed;
        EXPECT_EQ(record.fallbacks, 0u) << "seed " << seed;
        for (const auto& step : session.log()) {
            for (const GameEvent& e : step.events) {
                if (const auto* s = std::get_if<event::ShootingStarted>(&e)) {
                    indirect_shoots += s->type == ShootingType::Indirect ? 1 : 0;
                }
                if (std::holds_alternative<event::PrecisionGroupSelected>(e)) ++precision_answers;
            }
        }
    }
    std::printf("%d indirect Shoots, %d Precision answers\n", indirect_shoots, precision_answers);
    EXPECT_GT(precision_answers, 0);
}
