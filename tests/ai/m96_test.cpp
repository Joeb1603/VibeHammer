// M96 (D-097): the bot and the opponent-turn stratagems. Three keys, 0 by default: at 0 the session declines every
// window that offers only stratagems the bot doesn't play, so it never sees one; at 1 normal/stratagems.hpp answers
// them (Fire Overwatch and its snap shot, Heroic Intervention, Counteroffensive).

#include <gtest/gtest.h>

#include <algorithm>
#include <variant>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "bot_configs.hpp"
#include "bot_log.hpp"
#include "core/engine/engine.hpp"
#include "fallback.hpp"
#include "normal/shooting.hpp"
#include "normal/stratagems.hpp"
#include "session.hpp"
#include "state_hash.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::ai::BotConfig;
using wh40k::ai::PolicyResult;
using wh40k::test::in;

namespace {

BotConfig all_three() {
    BotConfig c;
    c.move.fire_overwatch = 1.0;
    c.move.heroic_intervention = 1.0;
    c.move.counteroffensive = 1.0;
    return c;
}

PolicyResult answer(const GameState& gs, const BotConfig& config) {
    wh40k::ai::analysis::OddsCache cache;
    return wh40k::ai::normal::decide_stratagems(cache, gs, gs.pending->side, config.move, 1);
}

// V1, paced: the Intercessors remain stationary, so Side B's Fire Overwatch window waits (round 1: Side B has 1 CP).
GameState fire_overwatch_window() {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game_paced(gs);
    ScriptedDice none{std::vector<int>{}};
    apply_action_paced(gs, Side::A, action::StartMove{gs.units[0].id, MoveType::RemainStationary, std::nullopt}, none);
    EXPECT_TRUE(window_pending(gs));
    return gs;
}

// Side A's Charge step after the Intercessors' charge with a lone Termagant `gap_in` west of them; Side A ends the step,
// paced: Side B's Heroic Intervention window (3 CP).
GameState heroic_window(float gap_in) {
    GameState gs = wh40k::test::after_successful_charge();
    Position w = gs.units[0].models[0].position;
    for (const Model& m : gs.units[0].models) {
        if (m.position.x < w.x) w = m.position;
    }
    const UnitId lone = wh40k::test::add_unit_copy(gs, 1);
    Unit& u = *gs.find_unit(lone);
    u.models.resize(1);
    u.models[0].position = Position{w.x - 16.0f - 14.25f - in(gap_in), w.y, 0.0f};
    u.turn_flags = UnitTurnFlags{};
    u.phase_flags = UnitPhaseFlags{};
    gs.command_points[1] = 3;
    gs.pending = compute_decision(gs);
    ScriptedDice none{std::vector<int>{}};
    apply_action_paced(gs, Side::A, action::EndChargeStep{}, none);
    EXPECT_TRUE(window_pending(gs));
    return gs;
}

}  // namespace

// ==== The keys and the session's declines ====

TEST(M96_Bots, FireOverwatchIsOnByDefaultAndPlaysFollowsEachKey) {
    // D-097's head-to-head: Fire Overwatch +26 games in 400 (on), Heroic Intervention +2 and Counteroffensive +0 (off).
    const BotConfig d;
    EXPECT_EQ(d.move.fire_overwatch, 1.0);
    EXPECT_EQ(d.move.heroic_intervention, 0.0);
    EXPECT_EQ(d.move.counteroffensive, 0.0);
    EXPECT_TRUE(d.plays(Stratagem::CommandReroll));
    EXPECT_TRUE(d.plays(Stratagem::FireOverwatch));
    for (const Stratagem s : {Stratagem::InsaneBravery, Stratagem::HeroicIntervention, Stratagem::Counteroffensive}) {
        EXPECT_FALSE(d.plays(s)) << stratagem_name(s);
    }
    EXPECT_EQ(d.declined_stratagems(), (std::vector<Stratagem>{Stratagem::InsaneBravery, Stratagem::HeroicIntervention,
                                                               Stratagem::Counteroffensive}));
    EXPECT_EQ(wh40k::test::without_stratagems().declined_stratagems().size(), kAllStratagems.size());
    const BotConfig on = all_three();
    EXPECT_TRUE(on.plays(Stratagem::FireOverwatch));
    EXPECT_TRUE(on.plays(Stratagem::HeroicIntervention));
    EXPECT_TRUE(on.plays(Stratagem::Counteroffensive));
    EXPECT_EQ(on.declined_stratagems(), std::vector<Stratagem>{Stratagem::InsaneBravery});
    BotConfig only_fo = wh40k::test::without_stratagems();
    only_fo.move.fire_overwatch = 1.0;
    EXPECT_TRUE(only_fo.plays_stratagems());
}

TEST(M96_Bots, ABotWithOnlyCommandRerollNeverSeesTheNewWindows) {
    // M95's default bot (Command Re-roll only) in a whole V1 game: every window that follows no roll is declined by its
    // session.
    BotConfig only_reroll = wh40k::test::without_stratagems();
    only_reroll.move.command_reroll = 1.0;
    GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario(), 3);
    std::vector<std::string> trace;
    const wh40k::ai::GameRecord g = wh40k::ai::play_bot_session(s, only_reroll, only_reroll, 5000, nullptr, &trace);
    EXPECT_TRUE(g.finished);
    EXPECT_EQ(g.fallbacks, 0u);
    for (const std::string& line : trace) {
        EXPECT_EQ(line.find("Fire Overwatch"), std::string::npos) << line;
        EXPECT_EQ(line.find("Heroic Intervention"), std::string::npos) << line;
        EXPECT_EQ(line.find("Counteroffensive"), std::string::npos) << line;
        EXPECT_EQ(line.find("Insane Bravery"), std::string::npos) << line;
    }
    // The windows were there, each declined as a step (V1 has a Fire Overwatch window at the end of most Movement
    // phases).
    std::size_t declines = 0;
    for (const ReplayStep& step : s.history()) declines += std::holds_alternative<action::DeclineStratagems>(step.action);
    EXPECT_GT(declines, 0u);
}

// ==== Fire Overwatch and the snap shot ====

TEST(M96_FireOverwatch, TheBotUsesItWhenTheSnapShotIsWorthTheCpAndDeclinesAtZero) {
    const GameState gs = fire_overwatch_window();
    wh40k::ai::analysis::OddsCache cache;
    const auto pick = wh40k::ai::normal::best_fire_overwatch(cache, gs);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->action, (GameAction{action::UseStratagem{Stratagem::FireOverwatch, gs.units[1].id, std::nullopt}}));
    EXPECT_GT(pick->value, 0.0);
    const PolicyResult used = answer(gs, all_three());
    EXPECT_EQ(used.action, pick->value >= wh40k::ai::normal::kOverwatchMinShare ? pick->action
                                                                                : GameAction{action::DeclineStratagems{}});
    EXPECT_TRUE(validate_action(gs, Side::B, used.action).ok());
    EXPECT_EQ(answer(gs, BotConfig{}).action, used.action);  // on by default (D-097)
    EXPECT_EQ(answer(gs, wh40k::test::without_stratagems()).action, (GameAction{action::DeclineStratagems{}}));
}

TEST(M96_FireOverwatch, TheSnapShotIsAtOneTargetAndValuedWithOnlySixesHitting) {
    // Two Intercessor units in reach: the snap shot picks one, every shot at it.
    GameState gs = wh40k::test::load_v1_scenario();
    const UnitId second = wh40k::test::add_unit_copy(gs, 0);
    for (Model& m : gs.find_unit(second)->models) m.position.x += in(7);
    gs.units[0].models.resize(2);
    gs.find_unit(second)->models.resize(2);
    gs.units[0].starting_strength = 2;  // test-only: two-model units, not below half-strength (no battle-shock step)
    gs.find_unit(second)->starting_strength = 2;
    start_game_paced(gs);
    ScriptedDice none{std::vector<int>{}};
    for (const UnitId u : {gs.units[0].id, second}) {
        apply_action_paced(gs, Side::A, action::StartMove{u, MoveType::RemainStationary, std::nullopt}, none);
    }
    ASSERT_TRUE(window_pending(gs));
    apply_action_paced(gs, Side::B, action::UseStratagem{Stratagem::FireOverwatch, gs.units[1].id, std::nullopt}, none);
    ASSERT_TRUE(std::holds_alternative<decision::SnapShoot>(gs.pending->kind));
    wh40k::ai::analysis::OddsCache cache;
    const auto c = wh40k::ai::normal::plan_snap_shooting(cache, gs, gs.units[1].id);
    ASSERT_TRUE(c.has_value());
    ASSERT_FALSE(c->shots.empty());
    for (const ShotDeclaration& d : c->shots) EXPECT_EQ(d.target, c->shots.front().target);
    EXPECT_TRUE(validate_action(gs, Side::B, action::Shoot{c->unit, c->type, c->shots}).ok());
    // The same declaration as a normal shot (BS4+) would be worth more: only 6s hit (15.09).
    const auto normal = wh40k::ai::normal::plan_shooting(cache, gs, gs.units[1].id, ShootingType::Normal);
    ASSERT_TRUE(normal.has_value());
    EXPECT_LT(c->value, normal->value);
    // The bot's answer and the fallback's are valid snap shots.
    wh40k::ai::Bot bot(all_three());
    const auto choice = bot.decide(gs, Side::B);
    EXPECT_FALSE(choice.stopped);
    EXPECT_TRUE(validate_action(gs, Side::B, choice.action).ok()) << choice.reason;
    const wh40k::ai::FallbackResult fb = wh40k::ai::fallback_action(gs, Side::B);
    ASSERT_TRUE(fb.action.has_value());
    EXPECT_TRUE(validate_action(gs, Side::B, *fb.action).ok());
}

TEST(M96_FireOverwatch, TheReasonsHaveAShortForm) {
    // M45's log: each reason of the new stratagems has a short form (scores and extra clauses dropped).
    const GameState gs = fire_overwatch_window();
    wh40k::ai::analysis::OddsCache cache;
    const auto pick = wh40k::ai::normal::best_fire_overwatch(cache, gs);
    ASSERT_TRUE(pick.has_value());
    const PolicyResult declined = answer(gs, wh40k::test::without_stratagems());
    for (const std::string& reason : {pick->why, declined.reason}) {
        const auto sum = wh40k::ai::summarize_reason(reason);
        EXPECT_FALSE(sum.text.empty()) << reason;
        EXPECT_LE(sum.text.size(), reason.size()) << reason;
    }
}

// ==== Heroic Intervention ====

TEST(M96_HeroicIntervention, TheBotChargesWhenTheGainIsWorthItsCp) {
    const GameState gs = heroic_window(5.0f);
    wh40k::ai::analysis::OddsCache cache;
    const auto pick = wh40k::ai::normal::best_heroic_intervention(cache, gs, 1);
    ASSERT_TRUE(pick.has_value());
    const auto* use = std::get_if<action::UseStratagem>(&pick->action);
    ASSERT_NE(use, nullptr);
    EXPECT_EQ(use->stratagem, Stratagem::HeroicIntervention);
    EXPECT_TRUE(use->mode.has_value());
    EXPECT_TRUE(validate_action(gs, Side::B, pick->action).ok());
    const PolicyResult r = answer(gs, all_three());
    EXPECT_TRUE(validate_action(gs, Side::B, r.action).ok());
    EXPECT_EQ(answer(gs, BotConfig{}).action, (GameAction{action::DeclineStratagems{}}));
}

// ==== Counteroffensive ====

TEST(M96_Counteroffensive, OnlyWhenItChangesWhoFightsNext) {
    // After the Intercessors' fight (Fights First) the Termagants are next anyway (D-020: the sequence alternates, and
    // the enemy has no Fights First unit left): Counteroffensive changes nothing, so the bot declines.
    GameState gs = wh40k::test::after_successful_charge();
    gs.command_points[1] = 2;
    gs.pending = compute_decision(gs);
    ScriptedDice none{std::vector<int>{}};
    apply_action_paced(gs, Side::A, action::EndChargeStep{}, none);
    ScriptedDice misses(std::vector<int>(200, 1));
    for (int guard = 0; guard < 60 && gs.pending; ++guard) {
        if (window_pending(gs) && std::get<decision::UseStratagems>(gs.pending->kind).timing == WindowTiming::AfterAttacks) {
            break;
        }
        GameAction a = window_pending(gs) ? GameAction{action::DeclineStratagems{}} : wh40k::test::neutral_action(gs);
        if (std::holds_alternative<decision::SelectUnitToFight>(gs.pending->kind)) {
            a = action::SelectToFight{gs.units[0].id, FightType::Normal};
        }
        apply_action_paced(gs, gs.pending->side, a, misses);
    }
    ASSERT_TRUE(window_pending(gs));
    wh40k::ai::analysis::OddsCache cache;
    EXPECT_FALSE(wh40k::ai::normal::best_counteroffensive(cache, gs).has_value());
    EXPECT_EQ(answer(gs, all_three()).action, (GameAction{action::DeclineStratagems{}}));
}

// ==== The bot's hash of the new state ====

TEST(M96_StateHash, EachNewFieldCountsOnlyWhenSet) {
    GameState gs = wh40k::test::after_successful_charge();
    const std::uint64_t base = wh40k::ai::state_hash(gs);
    GameState a = gs;
    a.units[1].phase_flags.fights_first = true;
    EXPECT_NE(wh40k::ai::state_hash(a), base);
    GameState b = gs;
    b.flow.snap_shooter = gs.units[1].id;
    EXPECT_NE(wh40k::ai::state_hash(b), base);
    // The windows that follow no roll hash apart from each other and from a roll window.
    const GameState fo = fire_overwatch_window();
    GameState fo_other = fo;
    fo_other.flow.window->timing = WindowTiming::AfterAttacks;
    EXPECT_NE(wh40k::ai::state_hash(fo), wh40k::ai::state_hash(fo_other));
}
