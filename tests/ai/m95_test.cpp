// M95 (D-096): the bot and reaction windows. With its stratagem keys off (the defaults) its session declines every
// window for it, so it plays as at M94; with them on it answers windows by normal/stratagems.hpp.

#include <gtest/gtest.h>

#include <variant>

#include "analysis/charge_odds.hpp"
#include "analysis/odds.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "core/engine/engine.hpp"
#include "fallback.hpp"
#include "normal/stratagems.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::ai::BotConfig;
using wh40k::ai::GameRecord;
using wh40k::ai::PolicyResult;
using wh40k::test::in;

namespace {

BotConfig stratagems_on() {
    BotConfig c;
    c.move.command_reroll = 1.0;
    c.move.insane_bravery = 1.0;
    return c;
}

std::size_t count_steps(const GameSession& s, auto is) {
    std::size_t n = 0;
    for (const ReplayStep& step : s.history()) n += is(step.action) ? 1 : 0;
    return n;
}

bool is_use(const GameAction& a) { return std::holds_alternative<action::UseStratagem>(a); }
bool is_decline(const GameAction& a) { return std::holds_alternative<action::DeclineStratagems>(a); }

PolicyResult answer(const GameState& gs, const BotConfig& config) {
    wh40k::ai::analysis::OddsCache cache;
    return wh40k::ai::normal::decide_stratagems(cache, gs, gs.pending->side, config.move);
}

// Side A's Charge step in V1, the Termagants `gap_in` from the Intercessors, the charge declared and rolled (paced) with
// `dice`: Side A's window after its charge roll.
GameState after_charge_roll(float gap_in, std::vector<int> dice) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (!(gs.flow.stage == Stage::Charge && turn_side(gs) == Side::A)) {
        apply_action(gs, gs.pending->side, legal_actions(gs, gs.pending->side).front());
    }
    const float gap = mm_to_inches(*unit_distance(gs.units[0], gs.units[1]));
    for (Model& m : gs.units[1].models) m.position.y -= in(gap - gap_in);
    ScriptedDice d{std::move(dice)};
    apply_action_paced(gs, Side::A, action::DeclareCharge{gs.units[0].id}, d);
    apply_action_paced(gs, Side::A, action::RollDice{RollKind::Charge, gs.units[0].id}, d);
    return gs;
}

}  // namespace

TEST(M95_Bots, WithTheKeysOffTheBotPlaysNoStratagem) {
    EXPECT_FALSE(BotConfig{}.plays_stratagems());
    EXPECT_TRUE(stratagems_on().plays_stratagems());
    GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario(), 3);
    const GameRecord g = wh40k::ai::play_bot_session(s, BotConfig{}, BotConfig{});
    EXPECT_TRUE(g.finished);
    EXPECT_EQ(count_steps(s, is_use), 0u);
    EXPECT_GT(count_steps(s, is_decline), 0u);  // the session's
    EXPECT_TRUE(s.auto_decline(Side::A));
    EXPECT_TRUE(s.auto_decline(Side::B));
}

TEST(M95_Bots, WithTheKeysOnTheBotsUseStratagemsAndPlayCleanGames) {
    const BotConfig on = stratagems_on();
    std::size_t used = 0;
    for (const GameState& scenario : {wh40k::test::load_v1_scenario(), wh40k::test::load_rosters_fixture()}) {
        for (std::uint64_t seed = 1; seed <= 2; ++seed) {
            SCOPED_TRACE("seed " + std::to_string(seed));
            GameSession s = GameSession::new_game(scenario, seed, {true, true}, {false, false});
            const GameRecord g = wh40k::ai::play_bot_session(s, on, on);
            EXPECT_TRUE(g.finished);
            EXPECT_EQ(g.rejected, 0u);
            EXPECT_EQ(g.fallbacks, 0u);
            EXPECT_TRUE(g.problems.empty());
            EXPECT_FALSE(s.auto_decline(Side::A));
            used += count_steps(s, is_use);
            EXPECT_GE(s.state().command_points[0], 0);
            EXPECT_GE(s.state().command_points[1], 0);
        }
    }
    EXPECT_GT(used, 0u);
}

TEST(M95_Bots, TheFallbackDeclinesAWindow) {
    GameSession s(wh40k::test::shoot_step_state(), {true, true}, {false, false});
    action::Shoot a{s.state().units[0].id, ShootingType::Normal, {}};
    for (const Model& m : s.state().units[0].models) a.shots.push_back(ShotDeclaration{m.id, 0, 0, s.state().units[1].id});
    ASSERT_TRUE(s.submit(a).ok());
    ASSERT_TRUE(window_pending(s.state()));
    const wh40k::ai::FallbackResult f = wh40k::ai::fallback_action(s.state(), Side::A);
    ASSERT_TRUE(f.action.has_value());
    EXPECT_EQ(*f.action, (GameAction{action::DeclineStratagems{}}));
    // With the keys off the bot declines a window it is shown (after a load, say) too.
    EXPECT_EQ(answer(s.state(), BotConfig{}).action, (GameAction{action::DeclineStratagems{}}));
}

TEST(M95_Stratagems, CommandRerollPicksTheLowestFailedHitDieWhenItIsWorthTheCp) {
    // The Intercessors' volley at the Termagants (Focused Fire, D2 at W1: each hit that wounds and gets through kills).
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        GameSession s(wh40k::test::shoot_step_state(Side::A, seed), {true, true}, {false, false});
        action::Shoot a{s.state().units[0].id, ShootingType::Normal, {}};
        for (const Model& m : s.state().units[0].models) a.shots.push_back(ShotDeclaration{m.id, 0, 0, s.state().units[1].id});
        ASSERT_TRUE(s.submit(a).ok());
        const std::optional<WindowInfo> w = window_info(s.state());
        ASSERT_TRUE(w.has_value());
        ASSERT_EQ(w->roll, RollKind::Hit);
        const auto& hits = std::get<event::HitRollsMade>(*w->result).rolls.rolls;
        std::optional<std::size_t> lowest_miss;
        for (std::size_t i = 0; i < hits.size(); ++i) {
            if (hits[i].outcome == HitOutcome::Fail && (!lowest_miss || w->faces[i] < w->faces[*lowest_miss])) lowest_miss = i;
        }
        if (!lowest_miss) continue;  // every die hit
        SCOPED_TRACE("seed " + std::to_string(seed));
        wh40k::ai::analysis::OddsCache cache;
        const auto pick = wh40k::ai::normal::best_command_reroll(cache, s.state());
        ASSERT_TRUE(pick.has_value());
        EXPECT_EQ(pick->die, lowest_miss);
        EXPECT_GT(pick->gain, wh40k::ai::normal::kRerollMinGain);  // a fresh die at a Termagant: ~0.6 of a model
        EXPECT_EQ(answer(s.state(), stratagems_on()).action,
                  (GameAction{action::UseStratagem{Stratagem::CommandReroll, s.state().units[0].id, lowest_miss}}));
        EXPECT_EQ(answer(s.state(), BotConfig{}).action, (GameAction{action::DeclineStratagems{}}));
        return;
    }
    FAIL() << "no seed missed";
}

TEST(M95_Stratagems, AChargeReachingNoTargetIsReRolledOnlyWhenANewRollOftenReaches) {
    const BotConfig on = stratagems_on();
    // 6.9" away: a new 2D6 reaches on a 7+ (21 in 36).
    const GameState near = after_charge_roll(6.9f, {1, 1});
    ASSERT_TRUE(window_pending(near));
    EXPECT_EQ(answer(near, on).action,
              (GameAction{action::UseStratagem{Stratagem::CommandReroll, near.units[0].id, std::nullopt}}));
    // 11.5" away: only a 12 does (1 in 36).
    const GameState far = after_charge_roll(11.5f, {1, 1});
    ASSERT_TRUE(window_pending(far));
    EXPECT_EQ(answer(far, on).action, (GameAction{action::DeclineStratagems{}}));
    // A roll that reaches its target is kept.
    const GameState made = after_charge_roll(6.9f, {4, 4});
    ASSERT_TRUE(window_pending(made));
    EXPECT_EQ(answer(made, on).action, (GameAction{action::DeclineStratagems{}}));
}

TEST(M95_Stratagems, InsaneBraveryForARiskyBattleShockTest) {
    // V1 with 2 of the 5 Intercessors: Side A's first Battle-shock step tests them (Ld 6+: fails 10 times in 36).
    GameState gs = wh40k::test::load_v1_scenario();
    gs.units[0].models.resize(2);
    GameSession s = GameSession::new_game(std::move(gs), 1, {true, true}, {false, false});
    ASSERT_TRUE(window_pending(s.state()));
    const std::optional<WindowInfo> w = window_info(s.state());
    ASSERT_TRUE(w && w->next_roll && w->next_roll->need);
    const double risk = 1.0 - wh40k::ai::analysis::p_2d6_at_least(*w->next_roll->need);
    const GameAction used = answer(s.state(), stratagems_on()).action;
    if (risk >= wh40k::ai::normal::kBraveryRisk) {
        EXPECT_EQ(used, (GameAction{action::UseStratagem{Stratagem::InsaneBravery, s.state().units[0].id, std::nullopt}}));
    } else {
        EXPECT_EQ(used, (GameAction{action::DeclineStratagems{}}));
    }
    BotConfig reroll_only = stratagems_on();
    reroll_only.move.insane_bravery = 0.0;
    EXPECT_EQ(answer(s.state(), reroll_only).action, (GameAction{action::DeclineStratagems{}}));
}
