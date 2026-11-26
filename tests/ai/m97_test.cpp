// M97 (D-099): the bot and the own-turn stratagems that end in mortal wounds. Two keys, 0 by default: at 0 the session
// hides Explosives' answers (GameSession::legal_offered; the bot's own list too) and declines Crushing Impact's window,
// so the bot never sees either; at 1 normal/stratagems.hpp answers them, valued by a mortal-wound expectation.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <variant>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "bot_configs.hpp"
#include "bot_log.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/rules/stratagem_effects.hpp"
#include "core/state/queries.hpp"
#include "fallback.hpp"
#include "normal/stratagems.hpp"
#include "normal/value.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::ai::BotConfig;
using wh40k::ai::PolicyResult;
using wh40k::test::in;
namespace mf = wh40k::test::monsters;

namespace {

BotConfig both_keys() {
    BotConfig c;
    c.move.explosives = 1.0;
    c.move.crushing_impact = 1.0;
    return c;
}

// V1 at Side A's Shoot step (round 1), the Termagants `gap_in` from the Intercessors, Side A with 1 CP.
GameState explosives_state(float gap_in = 6.0f) {
    GameState gs = wh40k::test::shoot_step_state(Side::A);
    const float gap = mm_to_inches(*unit_distance(gs.units[0], gs.units[1]));
    for (Model& m : gs.units[1].models) m.position.y -= in(gap - gap_in);
    gs.command_points[0] = 1;
    gs.pending = compute_decision(gs);
    return gs;
}

// The monsters fixture, paced: the Hive Tyrant's charge at the Legionaries committed, Crushing Impact's window waiting
// (Side B with 1 CP). `points`: every unit's points (the enemy's first), so shares are weighted (D-077).
GameState crushing_window(std::optional<std::pair<int, int>> points = std::nullopt) {
    GameState gs = mf::at_movement();
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::Charge>(g.pending->kind) && g.pending->side == Side::B;
    });
    gs.command_points[1] = 1;
    if (points) {
        for (Unit& u : gs.units) {
            u.points = u.side == Side::A ? points->first : points->second;
            u.starting_wounds = 0;
            for (const Model& m : u.models) u.starting_wounds += m.characteristics.wounds;
        }
    }
    gs.pending = compute_decision(gs);
    const UnitId tyrant = gs.units[mf::kTyrant].id;
    ScriptedDice six{3, 3};
    apply_action(gs, Side::B, action::DeclareCharge{tyrant}, six);
    apply_action(gs, Side::B, action::StartChargeMove{tyrant, {gs.units[mf::kLegionaries].id}});
    const Model& t = gs.units[mf::kTyrant].models[0];
    apply_action(gs, Side::B,
                 action::ProposeModelMove{t.id, {Position{t.position.x, in(mf::kRankY + mf::tyrant_centre_gap(0.5f)), 0.0f}}});
    ScriptedDice none{std::vector<int>{}};
    apply_action_paced(gs, Side::B, action::CommitUnitMove{}, none);
    EXPECT_TRUE(window_pending(gs));
    return gs;
}

PolicyResult window_answer(const GameState& gs, const BotConfig& config) {
    wh40k::ai::analysis::OddsCache cache;
    return wh40k::ai::normal::decide_stratagems(cache, gs, gs.pending->side, config.move, 1);
}

}  // namespace

// ==== The keys ====

TEST(M97_Bots, TheKeysAreOffByDefaultAndPlaysFollowsEachKey) {
    const BotConfig d;
    EXPECT_EQ(d.move.explosives, 0.0);
    EXPECT_EQ(d.move.crushing_impact, 0.0);
    EXPECT_FALSE(d.plays(Stratagem::Explosives));
    EXPECT_FALSE(d.plays(Stratagem::CrushingImpact));
    const BotConfig on = both_keys();
    EXPECT_TRUE(on.plays(Stratagem::Explosives));
    EXPECT_TRUE(on.plays(Stratagem::CrushingImpact));
    EXPECT_FALSE(wh40k::test::without_stratagems().plays(Stratagem::Explosives));
    EXPECT_EQ(wh40k::test::without_stratagems().declined_stratagems().size(), kAllStratagems.size());
    // The session declines them for the default bot.
    const std::vector<Stratagem> declined = d.declined_stratagems();
    EXPECT_NE(std::find(declined.begin(), declined.end(), Stratagem::Explosives), declined.end());
    EXPECT_NE(std::find(declined.begin(), declined.end(), Stratagem::CrushingImpact), declined.end());
}

// ==== The valuation ====

TEST(M97_Value, MortalWoundsCostWhatTheEngineWouldInflict) {
    using wh40k::ai::analysis::capped_binomial;
    using wh40k::ai::analysis::mortal_wound_model_equivalents;
    // Binomial(6, 1/2): sums to 1; capped at 6, a T12's 5+s fold their 7+ into 6.
    const std::vector<double> six = capped_binomial(6, 0.5, 6);
    ASSERT_EQ(six.size(), 7u);
    double sum = 0.0;
    double mean = 0.0;
    for (std::size_t k = 0; k < six.size(); ++k) {
        sum += six[k];
        mean += static_cast<double>(k) * six[k];
    }
    EXPECT_NEAR(sum, 1.0, 1e-12);
    EXPECT_NEAR(mean, 3.0, 1e-12);
    const std::vector<double> capped = capped_binomial(12, 1.0 / 3.0, 6);
    ASSERT_EQ(capped.size(), 7u);
    double tail = 0.0;
    for (int k = 6; k <= 12; ++k) {
        double c = 1.0;
        for (int i = 1; i <= k; ++i) c = c * static_cast<double>(12 - k + i) / static_cast<double>(i);
        tail += c * std::pow(1.0 / 3.0, k) * std::pow(2.0 / 3.0, 12 - k);
    }
    EXPECT_NEAR(capped[6], tail, 1e-12);
    // W1 Termagants: each mortal wound a model; a W10 Hive Tyrant: a tenth of one.
    const GameState gs = explosives_state();
    EXPECT_NEAR(mortal_wound_model_equivalents(gs.units[1], six), 3.0, 1e-12);
    const GameState monsters = mf::fixture();
    EXPECT_NEAR(mortal_wound_model_equivalents(monsters.units[mf::kTyrant], six), 0.3, 1e-12);
    // Mortal wounds left when a unit is destroyed are lost: two Termagants lose at most two models.
    Unit two = gs.units[1];
    two.models.resize(2);
    EXPECT_LE(mortal_wound_model_equivalents(two, six), 2.0);
}

// ==== Explosives ====

TEST(M97_Explosives, TheBotUsesItBeforeShootingWhenItIsWorthTheCpAndNeverAtZero) {
    GameState gs = explosives_state();
    const UnitId marines = gs.units[0].id;
    const UnitId gants = gs.units[1].id;
    const std::vector<GameAction> legal = legal_actions(gs, Side::A);
    const auto pick = wh40k::ai::normal::best_explosives(gs, legal);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->action, (GameAction{action::UseStratagem{Stratagem::Explosives, marines, std::nullopt}}));
    EXPECT_NEAR(pick->value, 0.3, 1e-9);  // three Termagants of ten, expected
    EXPECT_GE(pick->value, wh40k::ai::normal::kExplosivesMinShare);
    // At 1 it takes it first; the selection names the Termagants and their first legal model.
    wh40k::ai::Bot bot(both_keys());
    const auto choice = bot.decide(gs, Side::A);
    EXPECT_EQ(choice.action, pick->action) << choice.reason;
    apply_action(gs, Side::A, choice.action);
    ASSERT_TRUE(std::holds_alternative<decision::SelectModelAndEnemy>(gs.pending->kind));
    const auto select = bot.decide(gs, Side::A);
    EXPECT_EQ(select.action, legal_actions(gs, Side::A).front()) << select.reason;
    EXPECT_EQ(std::get<action::SelectModelAndEnemy>(select.action).enemy, gants);
    // At 0 (the default): never, as it shot before.
    const GameState again = explosives_state();
    for (const BotConfig& c : {BotConfig{}, wh40k::test::without_stratagems()}) {
        wh40k::ai::Bot off(c);
        const auto d = off.decide(again, Side::A);
        EXPECT_FALSE(std::holds_alternative<action::UseStratagem>(d.action)) << d.reason;
    }
    // The reasons have a short form (M45's log).
    const auto sum = wh40k::ai::summarize_reason(pick->why);
    EXPECT_FALSE(sum.text.empty()) << pick->why;
}

TEST(M97_Explosives, TheFallbackTakesTheFirstLegalPairAndEndsTheShootStep) {
    GameState gs = explosives_state();
    const wh40k::ai::FallbackResult at_shoot = wh40k::ai::fallback_action(gs, Side::A);
    ASSERT_TRUE(at_shoot.action.has_value());
    EXPECT_EQ(*at_shoot.action, (GameAction{action::EndShootStep{}}));
    apply_action(gs, Side::A, action::UseStratagem{Stratagem::Explosives, gs.units[0].id, std::nullopt});
    const wh40k::ai::FallbackResult at_selection = wh40k::ai::fallback_action(gs, Side::A);
    ASSERT_TRUE(at_selection.action.has_value());
    EXPECT_EQ(*at_selection.action, legal_actions(gs, Side::A).front());
}

// ==== Crushing Impact ====

TEST(M97_CrushingImpact, TheBotWeighsTheEnemysLossAgainstItsOwn) {
    // Without points, a T10 W10 Hive Tyrant's 1s cost it a share (1.67 of 10 wounds) as large as the 5+s take from ten W2
    // Legionaries (3.33 of 20 wounds): no gain, so it declines.
    {
        const GameState gs = crushing_window();
        const auto pick = wh40k::ai::normal::best_crushing_impact(gs);
        ASSERT_TRUE(pick.has_value());
        EXPECT_NEAR(pick->value, 0.0, 0.01);
        EXPECT_LT(pick->value, wh40k::ai::normal::kCrushMinGain);
        EXPECT_EQ(window_answer(gs, both_keys()).action, (GameAction{action::DeclineStratagems{}}));
    }
    // Legionaries worth four times the Tyrant's points (D-077): a clear gain, so it uses it, and the selection names the
    // Tyrant and the Legionaries.
    {
        GameState gs = crushing_window(std::make_pair(400, 100));
        const auto pick = wh40k::ai::normal::best_crushing_impact(gs);
        ASSERT_TRUE(pick.has_value());
        EXPECT_GT(pick->value, wh40k::ai::normal::kCrushMinGain);
        const PolicyResult used = window_answer(gs, both_keys());
        EXPECT_EQ(used.action,
                  (GameAction{action::UseStratagem{Stratagem::CrushingImpact, gs.units[mf::kTyrant].id, std::nullopt}}));
        EXPECT_EQ(window_answer(gs, BotConfig{}).action, (GameAction{action::DeclineStratagems{}}));  // off by default
        apply_action(gs, Side::B, used.action);
        wh40k::ai::Bot bot(both_keys());
        const auto select = bot.decide(gs, Side::B);
        EXPECT_EQ(select.action, (GameAction{action::SelectModelAndEnemy{gs.units[mf::kTyrant].models[0].id,
                                                                         gs.units[mf::kLegionaries].id}}))
            << select.reason;
        EXPECT_TRUE(validate_action(gs, Side::B, select.action).ok());
    }
}

TEST(M97_Bots, AtTheDefaultsTheSessionHidesBothFromTheBot) {
    // A whole game on the monsters fixture's roster (MONSTERs, GRENADES) with the default bot: Explosives never offered to
    // it, Crushing Impact's windows declined by its session; the bot never names either.
    GameSession s = GameSession::new_game(wh40k::test::load_rosters_fixture(), 2);
    std::vector<std::string> trace;
    const wh40k::ai::GameRecord g = wh40k::ai::play_bot_session(s, BotConfig{}, BotConfig{}, 5000, nullptr, &trace);
    EXPECT_TRUE(g.finished);
    EXPECT_EQ(g.fallbacks, 0u);
    for (const std::string& line : trace) {
        EXPECT_EQ(line.find("Explosives"), std::string::npos) << line;
        EXPECT_EQ(line.find("Crushing Impact"), std::string::npos) << line;
    }
    for (const ReplayStep& step : s.history()) {
        if (const auto* u = std::get_if<action::UseStratagem>(&step.action)) {
            EXPECT_NE(u->stratagem, Stratagem::Explosives);
            EXPECT_NE(u->stratagem, Stratagem::CrushingImpact);
        }
    }
}

TEST(M97_Bots, WithBothKeysOnGamesUseThemAndStayClean) {
    // The monsters fixture with Side A first (the Legionaries, GRENADES, 2.04" from the Hive Tyrant) and both keys on:
    // every answer legal (no rejection, revert or fallback), and Explosives is used.
    int explosives = 0;
    int crushing = 0;
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        GameSession s = GameSession::new_game(mf::fixture(false, Side::A), seed);
        const wh40k::ai::GameRecord g = wh40k::ai::play_bot_session(s, both_keys(), both_keys(), 5000, nullptr, nullptr);
        EXPECT_TRUE(g.finished);
        EXPECT_EQ(g.fallbacks, 0u);
        EXPECT_EQ(g.rejected, 0u);
        EXPECT_EQ(g.reverted, 0u);
        for (const ReplayStep& step : s.history()) {
            if (const auto* u = std::get_if<action::UseStratagem>(&step.action)) {
                explosives += u->stratagem == Stratagem::Explosives ? 1 : 0;
                crushing += u->stratagem == Stratagem::CrushingImpact ? 1 : 0;
            }
        }
    }
    std::printf("Explosives used %d times, Crushing Impact %d times in 4 games\n", explosives, crushing);
    EXPECT_GT(explosives, 0);
}

