// M44: level Hard (D-054): the game-aware evaluator (a Markov chain over the wounds each unit has left, the
// half-turns left and the range band; V = P(win) - P(loss), a draw worth 0, D-007), the engine-backed lookahead
// (a Monte Carlo of the rest of our turn and the enemy's reply with the bot's own dice and a fixed action budget),
// the new bot scenarios, and Hard against Normal.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "auto_actions.hpp"
#include "bot.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/state/queries.hpp"
#include "hard/evaluator.hpp"
#include "hard/hard_bot.hpp"
#include "hard/lookahead.hpp"
#include "session.hpp"
#include "simulator.hpp"
#include "state_hash.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::in;

namespace {

BotConfig hard_level() { return BotConfig{BotLevel::Hard}; }
BotConfig normal_level() { return BotConfig{BotLevel::Normal}; }

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

GameState load(const char* file) {
    return load_scenario(wh40k::test::repo_root() / "data/scenarios" / file, wh40k::test::repo_root());
}

// The V1 scenario played (first legal action each time) to side A's first MoveUnits decision; made once.
const GameState& first_move_step() {
    static const GameState base = [] {
        GameState gs = wh40k::test::load_v1_scenario();
        start_game(gs);
        while (!(gs.pending && gs.pending->side == Side::A &&
                 std::holds_alternative<decision::MoveUnits>(gs.pending->kind))) {
            const Side side = gs.pending.value().side;
            apply_action(gs, side, legal_actions(gs, side).front());
        }
        return gs;
    }();
    return base;
}

int wounds_of(const Unit& u) {
    int w = 0;
    for (const Model& m : u.models) w += m.wounds_remaining;
    return w;
}

// Side A's MoveUnits decision with the Termagants (`gants_models` of them) on side A, south, `gap_in` from the
// Intercessors (side B, north) in battle round `round`, side A's turn first in the round. Flags cleared.
GameState termagants_fixture(float gap_in, int round, int gants_models, float advance_in = 4.0f) {
    GameState gs = first_move_step();
    Unit& us = gs.units[kGants];
    Unit& them = gs.units[kMarines];
    us.side = Side::A;
    them.side = Side::B;
    for (Unit* u : {&us, &them}) {
        u->turn_flags = {};
        u->phase_flags = {};
        u->battle_shocked = false;
    }
    // Mirror so the Termagants are in the south.
    for (Unit* u : {&us, &them}) {
        for (Model& m : u->models) m.position.y = gs.battlefield.depth_mm - m.position.y;
    }
    for (Model& m : us.models) m.position.y += in(advance_in);
    us.models.resize(static_cast<std::size_t>(gants_models));
    for (int i = 0; i < 60; ++i) {
        const float d = *unit_distance(us, them);
        if (std::fabs(d - in(gap_in)) < 1e-4f) break;
        for (Model& m : them.models) m.position.y += in(gap_in) - d;
    }
    gs.flow.battle_round = round;
    gs.flow.turn_in_round = 0;
    gs.first_turn = Side::A;
    gs.pending = compute_decision(gs);
    return gs;
}

// Plays side A's decisions with `bot` (the engine applies them with the state's own dice) until the turn passes
// to side B or the game ends; returns the actions chosen.
std::vector<GameAction> play_turn(GameState& gs, Bot& bot, std::vector<std::string>* reasons = nullptr) {
    std::vector<GameAction> out;
    for (int guard = 0; guard < 200 && gs.pending && turn_side(gs) == Side::A && !gs.result; ++guard) {
        const Side who = gs.pending->side;
        if (who != Side::A) {
            // The Intercessors' reactive answers (allocation, casualties): the engine's first legal choice.
            const std::vector<GameAction> legal = legal_actions(gs, who);
            if (legal.empty() || is_template(legal.front())) break;
            apply_action(gs, who, legal.front());
            continue;
        }
        const BotChoice c = bot.decide(gs, Side::A);
        EXPECT_FALSE(c.stopped) << c.reason;
        if (c.stopped) break;
        EXPECT_TRUE(validate_action(gs, Side::A, c.action).ok()) << c.reason;
        out.push_back(c.action);
        if (reasons != nullptr) reasons->push_back(c.reason);
        for (const GameEvent& e : apply_action(gs, Side::A, c.action)) {
            EXPECT_FALSE(std::holds_alternative<event::UnitMoveReverted>(e)) << c.reason;
        }
    }
    return out;
}

}  // namespace

// ==== The evaluator ====

TEST(M44_Evaluator, ValueStaysWithinMinusOneAndOneAndTheTwoSidesSeeTheSameGame) {
    const GameState& gs = first_move_step();
    analysis::OddsCache cache;
    const hard::Evaluator a(cache, gs, Side::A, hard::ChainWeights{});
    const hard::Evaluator b(cache, gs, Side::B, hard::ChainWeights{});
    ASSERT_TRUE(a.ok());
    ASSERT_TRUE(b.ok());
    int checked = 0;
    for (const int h : {0, 3, 6, 9}) {
        for (const int wa : {1, 4, 10}) {
            for (const int wb : {1, 5, 10}) {
                for (const float gap_in : {0.0f, 3.0f, 8.0f, 12.0f, 16.0f}) {
                    const hard::Evaluation ea = a.at(h, {wa, wb}, in(gap_in), {in(3.0f), in(3.0f)});
                    const hard::Evaluation eb = b.at(h, {wa, wb}, in(gap_in), {in(3.0f), in(3.0f)});
                    EXPECT_GE(ea.v, -1.0);
                    EXPECT_LE(ea.v, 1.0);
                    EXPECT_NEAR(ea.p_win + ea.p_draw + ea.p_loss, 1.0, 1e-9);
                    EXPECT_NEAR(ea.v, ea.p_win - ea.p_loss, 1e-12);
                    // Zero-sum: what one side wins the other loses.
                    EXPECT_NEAR(ea.v, -eb.v, 1e-12);
                    EXPECT_NEAR(ea.score, -eb.score, 1e-12);
                    ++checked;
                }
            }
        }
    }
    EXPECT_EQ(checked, 180);
}

TEST(M44_Evaluator, ObviousPositionsOrderCorrectly) {
    const GameState& gs = first_move_step();  // side A: 5 Intercessors (10 wounds); side B: 10 Termagants
    analysis::OddsCache cache;
    const hard::Evaluator marines(cache, gs, Side::A, hard::ChainWeights{});
    const std::array<float, 2> rooms{in(5.0f), in(4.0f)};
    // Five Intercessors against one Termagant, in their own turn 6" away: a win.
    EXPECT_GT(marines.at(0, {10, 1}, in(6.0f), rooms).v, 0.95);
    // Against the whole unit they are still the favourites (the V1 matchup: Normal vs Normal wins 200 of 200).
    EXPECT_GT(marines.at(0, {10, 10}, in(12.0f), rooms).v, 0.5);
    // One wounded Intercessor engaged with ten Termagants in the Termagants' turn is losing.
    EXPECT_LT(marines.at(1, {1, 10}, 0.0f, rooms).v, 0.0);
    // The last battle round favours the side that is behind: fewer half-turns to be caught in (D-007).
    const hard::Evaluator gants(cache, gs, Side::B, hard::ChainWeights{});
    EXPECT_GT(gants.at(8, {10, 3}, in(16.0f), rooms).p_draw, gants.at(0, {10, 3}, in(16.0f), rooms).p_draw);
    // Far away the Termagants survive more often than in base contact.
    EXPECT_GT(gants.at(8, {10, 3}, in(16.0f), rooms).p_draw, gants.at(8, {10, 3}, 0.0f, rooms).p_draw);
}

TEST(M44_Evaluator, ADrawIsWorthZero) {
    const GameState& gs = first_move_step();
    analysis::OddsCache cache;
    hard::ChainWeights no_material;
    no_material.material = 0.0;
    const hard::Evaluator a(cache, gs, Side::A, no_material);
    // After the last half-turn (D-007: the game ends after battle round 5) every position is a draw.
    const int h = 2 * gs.battle_rounds;
    for (const float gap_in : {0.0f, 10.0f}) {
        const hard::Evaluation e = a.at(h, {6, 4}, in(gap_in), {in(3.0f), in(3.0f)});
        EXPECT_EQ(e.p_draw, 1.0);
        EXPECT_EQ(e.v, 0.0);
        EXPECT_EQ(e.score, 0.0);
    }
    // A game that ended in a draw.
    GameState drawn = gs;
    drawn.result = GameResult{std::nullopt, "D-007: battle round 5 ended"};
    const hard::Evaluation d = a.evaluate(drawn);
    EXPECT_EQ(d.p_draw, 1.0);
    EXPECT_EQ(d.v, 0.0);
    EXPECT_EQ(d.score, 0.0);
}

TEST(M44_Evaluator, TerminalStatesAreExact) {
    const GameState& gs = first_move_step();
    analysis::OddsCache cache;
    const hard::Evaluator a(cache, gs, Side::A, hard::ChainWeights{});
    const hard::Evaluator b(cache, gs, Side::B, hard::ChainWeights{});
    GameState won = gs;
    won.result = GameResult{Side::A, "D-007: Tyranids have no models left"};
    EXPECT_EQ(a.evaluate(won).v, 1.0);
    EXPECT_EQ(a.evaluate(won).p_win, 1.0);
    EXPECT_EQ(b.evaluate(won).v, -1.0);
    EXPECT_EQ(b.evaluate(won).p_loss, 1.0);
    // A hypothetical position with the Termagants gone (a casualty order's preview) is a win, now: the time
    // preference makes it worth more than the same win later.
    GameState wiped = gs;
    wiped.units[kGants].models.clear();
    const hard::Evaluation now = a.evaluate(wiped);
    EXPECT_EQ(now.v, 1.0);
    GameState later = wiped;
    later.flow.battle_round = 4;
    EXPECT_GT(now.score, a.evaluate(later).score);
    EXPECT_GT(now.score, 1.0);
}

TEST(M44_Evaluator, MoreModelsGiveAHigherValue) {
    const GameState& gs = first_move_step();
    analysis::OddsCache cache;
    const std::array<float, 2> rooms{in(4.0f), in(4.0f)};
    for (const Side side : {Side::A, Side::B}) {
        const hard::Evaluator e(cache, gs, side, hard::ChainWeights{});
        for (const float gap_in : {0.0f, 6.0f, 14.0f}) {
            for (const int h : {0, 5}) {
                // Our wounds up: never worse; theirs up: never better.
                for (int w = 1; w < 10; ++w) {
                    const std::array<int, 2> less = side == Side::A ? std::array<int, 2>{w, 6} : std::array<int, 2>{5, w};
                    const std::array<int, 2> more =
                        side == Side::A ? std::array<int, 2>{w + 1, 6} : std::array<int, 2>{5, w + 1};
                    EXPECT_GE(e.at(h, more, in(gap_in), rooms).v + 1e-9, e.at(h, less, in(gap_in), rooms).v)
                        << "side " << side_index(side) << " gap " << gap_in << " h " << h << " w " << w;
                    EXPECT_GE(e.at(h, more, in(gap_in), rooms).score + 1e-9, e.at(h, less, in(gap_in), rooms).score);
                    const std::array<int, 2> theirs_less =
                        side == Side::A ? std::array<int, 2>{6, w} : std::array<int, 2>{w, 6};
                    const std::array<int, 2> theirs_more =
                        side == Side::A ? std::array<int, 2>{6, w + 1} : std::array<int, 2>{w + 1, 6};
                    EXPECT_LE(e.at(h, theirs_more, in(gap_in), rooms).v, e.at(h, theirs_less, in(gap_in), rooms).v + 1e-9);
                }
            }
        }
        // Strictly: a full unit is worth more than a single model.
        EXPECT_GT(e.at(0, {10, 10}, in(12.0f), rooms).score,
                  e.at(0, side == Side::A ? std::array<int, 2>{1, 10} : std::array<int, 2>{10, 1}, in(12.0f), rooms).score);
    }
}

TEST(M44_Evaluator, ACachedTableGivesTheSameValuesAsAFreshOne) {
    // D-054: the chain tables are a cache of a pure function. A table built from an earlier position (more
    // wounds, an earlier half-turn) answers a later one with exactly the values a fresh table gives.
    const GameState& early = first_move_step();
    GameState late = early;
    late.flow.battle_round = 3;
    late.units[kGants].models.resize(6);
    late.units[kMarines].models[0].wounds_remaining = 1;
    late.pending = compute_decision(late);
    analysis::OddsCache cache;
    hard::ChainCache tables;
    { const hard::Evaluator warm(cache, early, Side::A, hard::ChainWeights{}, &tables); }
    ASSERT_EQ(tables.builds(), 1u);
    const hard::Evaluator reused(cache, late, Side::A, hard::ChainWeights{}, &tables);
    EXPECT_EQ(tables.builds(), 1u);  // covered by the first table
    const hard::Evaluator fresh(cache, late, Side::A, hard::ChainWeights{});
    for (const float gap_in : {0.0f, 5.0f, 11.0f}) {
        for (const int wb : {1, 3, 6}) {
            const hard::Evaluation x = reused.at(5, {9, wb}, in(gap_in), {in(4.0f), in(4.0f)});
            const hard::Evaluation y = fresh.at(5, {9, wb}, in(gap_in), {in(4.0f), in(4.0f)});
            EXPECT_EQ(x.score, y.score);
            EXPECT_EQ(x.p_win, y.p_win);
            EXPECT_EQ(x.p_loss, y.p_loss);
        }
    }
    EXPECT_EQ(reused.evaluate(late).score, fresh.evaluate(late).score);
}

// ==== The lookahead ====

TEST(M44_MonteCarlo, TheSameStateGivesTheSameResultTheDiceAreTheBotsOwnAndTheBudgetIsRespected) {
    // The position just after the Intercessors' first Normal move (Hard's own plan), before they shoot.
    const GameState& gs = first_move_step();
    hard::HardContext hc;
    analysis::OddsCache cache;
    GameState started = gs;
    apply_action(started, Side::A, action::StartMove{gs.units[kMarines].id, MoveType::Normal, std::nullopt});
    const std::optional<hard::HardMovePlan> plan = hard::hard_plan_unit_move(hc, cache, started, false);
    ASSERT_TRUE(plan.has_value());
    const GameState& after = plan->move.after;
    const hard::Evaluator eval(cache, after, Side::A, hc.weights.chain, &hc.chains);
    hard::LookaheadWeights w;
    w.rollouts = 4;
    const std::uint64_t seed = hard::rollout_seed(after);
    const hard::MonteCarloResult r1 = hard::monte_carlo(eval, cache, after, Side::A, seed, w);
    const hard::MonteCarloResult r2 = hard::monte_carlo(eval, cache, after, Side::A, seed, w);
    EXPECT_EQ(r1.mean, r2.mean);
    EXPECT_EQ(r1.actions, r2.actions);
    EXPECT_EQ(r1.rollouts, 4u);
    EXPECT_GT(r1.actions, 4u * 10u);  // each rollout plays the rest of the turn and the enemy's
    EXPECT_LE(r1.actions, w.budget_actions);
    // Only the bot's own dice: changing the game's dice changes nothing, and the seed is dice-free.
    GameState other = after;
    other.dice.seed ^= 0x9E3779B97F4A7C15ull;
    other.dice.position += 11;
    EXPECT_EQ(hard::rollout_seed(other), seed);
    const hard::MonteCarloResult r3 = hard::monte_carlo(eval, cache, other, Side::A, hard::rollout_seed(other), w);
    EXPECT_EQ(r3.mean, r1.mean);
    EXPECT_EQ(r3.actions, r1.actions);
    // A different seed is different luck.
    const hard::MonteCarloResult r4 = hard::monte_carlo(eval, cache, after, Side::A, seed + 1, w);
    EXPECT_EQ(r4.rollouts, 4u);
    // The budget counts simulated actions (D-054), never time: a tight one stops the rollouts early.
    hard::LookaheadWeights tight = w;
    tight.budget_actions = 50;
    const hard::MonteCarloResult r5 = hard::monte_carlo(eval, cache, after, Side::A, seed, tight);
    EXPECT_LE(r5.actions, 50u);
    EXPECT_GE(r5.cut, 1u);
    hard::LookaheadWeights short_rollouts = w;
    short_rollouts.rollout_actions = 5;
    const hard::MonteCarloResult r6 = hard::monte_carlo(eval, cache, after, Side::A, seed, short_rollouts);
    EXPECT_LE(r6.actions, 4u * 5u);
    EXPECT_EQ(r6.cut, 4u);
}

// ==== Hard is its own level, never peeks at dice, plans from any state ====

TEST(M44_NeverPeeksAtDice, HardDecisionsAreTheSameWhateverTheDiceAndAFreshBotGivesTheSamePlan) {
    std::size_t checked = 0;
    std::set<std::size_t> kinds;
    // Hard as the Tyranids in V1 and in the Termagants-first scenario (where it plays most differently from Normal;
    // M44_Scenarios plays a whole 20-Termagant game with Hard as the Space Marines, and M44_Arena both seats).
    const GameState v1 = wh40k::test::load_v1_scenario();
    const GameState first = load("bot_termagants_first.json");
    struct Game {
        const GameState* scenario;
        std::uint64_t seed;
        bool hard_is_a;
    };
    for (const Game game : {Game{&v1, 2, false}, Game{&first, 3, false}}) {
        const std::uint64_t seed = game.seed;
        {
            const bool hard_is_a = game.hard_is_a;
            client::GameSession s = client::GameSession::new_game(*game.scenario, seed);
            std::array<Bot, 2> bots{Bot(hard_is_a ? hard_level() : normal_level()),
                                    Bot(hard_is_a ? normal_level() : hard_level())};
            client::AutoSettings autos;
            autos.bot_controlled = {true, true};
            const Side hard_side = hard_is_a ? Side::A : Side::B;
            std::size_t moves_checked = 0;
            while (s.pending() && s.history().size() < 3000) {
                (void)client::run_auto_actions(s, autos);
                if (!s.pending()) break;
                const Side side = s.pending()->side;
                const GameState gs = s.state();
                const BotChoice played = bots[side_index(side)].decide(gs, side);
                if (side == hard_side) {
                    // Every decision but the model moves of a move (a fresh bot replans each from the move's start,
                    // which costs a whole Monte Carlo in Debug): the first two of each move are checked.
                    const bool model_move = std::holds_alternative<decision::MoveModels>(gs.pending->kind);
                    moves_checked = model_move ? moves_checked + 1 : 0;
                    if (!model_move || moves_checked <= 2) {
                        GameState other = gs;
                        other.dice.seed ^= 0x9E3779B97F4A7C15ull;
                        other.dice.position += 37;
                        const BotChoice fresh = Bot(hard_level()).decide(gs, side);
                        const BotChoice dice = Bot(hard_level()).decide(other, side);
                        EXPECT_EQ(fresh.action, played.action) << "seed " << seed << ": " << played.reason;
                        EXPECT_EQ(dice.action, played.action) << "seed " << seed << ": " << played.reason;
                        kinds.insert(gs.pending->kind.index());
                        ++checked;
                    }
                }
                if (played.stopped || !s.submit(played.action).ok()) break;
            }
            EXPECT_TRUE(s.state().result.has_value());
        }
    }
    EXPECT_GE(checked, 15u);  // 16 Hard decisions in these two games
    EXPECT_GE(kinds.size(), 6u);
}

TEST(M44_Levels, HardIsItsOwnLevelAndDecidesWithTheEvaluatorAndTheLookahead) {
    // M39_Levels.EveryLevelIsBuiltAndHardIsItsOwnLevel checks the dispatch; here, what Hard says it did.
    const GameState& gs = first_move_step();
    Bot hard(hard_level());
    const BotChoice h = hard.decide(gs, Side::A);
    EXPECT_NE(h.reason.find("Hard: win"), std::string::npos) << h.reason;
    EXPECT_NE(h.reason.find("lookahead"), std::string::npos) << h.reason;
    EXPECT_GE(hard.chain_tables_built(), 1u);
    const BotChoice n = Bot(normal_level()).decide(gs, Side::A);
    EXPECT_EQ(n.reason.find("Hard:"), std::string::npos) << n.reason;
}

// ==== Play for the draw (D-007): the losing side keeps away in a late round ====

TEST(M44_PlayForTheDraw, InTheLastRoundLosingTermagantsKiteRatherThanCharge) {
    // Battle round 5, the Termagants' turn first: four of them 9" from a whole Intercessor Squad. A charge or a
    // stand-up fight loses; surviving the Intercessors' last turn draws. Hard backs away out of half range and
    // the likely charge, shoots, and declares no charge.
    GameState gs = termagants_fixture(9.0f, 5, 4);
    const float before = *unit_distance(gs.units[kGants], gs.units[kMarines]);
    Bot hard(hard_level());
    std::vector<std::string> reasons;
    const std::vector<GameAction> played = play_turn(gs, hard, &reasons);
    ASSERT_FALSE(played.empty());
    const auto* start = std::get_if<action::StartMove>(&played.front());
    ASSERT_NE(start, nullptr);
    EXPECT_TRUE(start->move_type == MoveType::Normal || start->move_type == MoveType::Advance) << reasons.front();
    for (const GameAction& a : played) {
        EXPECT_FALSE(std::holds_alternative<action::DeclareCharge>(a)) << reasons.back();
        EXPECT_FALSE(std::holds_alternative<action::StartChargeMove>(a));
    }
    ASSERT_FALSE(gs.result.has_value());
    const float after = *unit_distance(gs.units[kGants], gs.units[kMarines]);
    EXPECT_GT(after, before + in(3.0f)) << reasons.front();
    EXPECT_GT(after, in(12.0f)) << reasons.front();  // outside the Bolt Rifles' half range
    EXPECT_NE(reasons.front().find("draw"), std::string::npos);
}

// ==== The new scenarios ====

TEST(M44_Scenarios, TheBotScenariosLoadDeployLegallyAndPlay) {
    const GameState v1 = wh40k::test::load_v1_scenario();
    const GameState first = load("bot_termagants_first.json");
    const GameState twenty = load("bot_20_termagants.json");
    // The same armies, the Tyranids first.
    EXPECT_EQ(first.first_turn, Side::B);
    EXPECT_EQ(v1.first_turn, Side::A);
    ASSERT_EQ(first.units.size(), v1.units.size());
    for (std::size_t i = 0; i < v1.units.size(); ++i) {
        EXPECT_EQ(first.units[i].models, v1.units[i].models);
    }
    // 20 Termagants (the datasheet's 10-20) in three ranks of 7, 7 and 6, in base contact, in coherency (03.03)
    // and wholly within their deployment zone (D-008, D-009; the loader also checks both).
    const Unit& gants = wh40k::test::unit_of(twenty, Side::B);
    ASSERT_EQ(gants.models.size(), 20u);
    EXPECT_EQ(gants.starting_strength, 20);
    EXPECT_TRUE(unit_coherency(gants).ok());
    std::set<float> rows;
    for (const Model& m : gants.models) rows.insert(m.position.y);
    EXPECT_EQ(rows.size(), 3u);
    const Rect zone{0.0f, twenty.battlefield.depth_mm - in(6.0f), twenty.battlefield.width_mm,
                    twenty.battlefield.depth_mm};
    for (const Model& m : gants.models) EXPECT_TRUE(wholly_within(zone, m.position, m.base));
    EXPECT_EQ(wh40k::test::unit_of(twenty, Side::A).models.size(), 5u);
    // Each one plays a whole clean game: Normal against Normal on the Termagants-first scenario (Hard plays it in
    // M44_NeverPeeksAtDice), Hard against Normal with the 20 Termagants.
    for (const auto& [sc, a] : {std::pair{&first, normal_level()}, std::pair{&twenty, hard_level()}}) {
        const GameRecord g = play_bot_game(*sc, 1, a, normal_level());
        EXPECT_TRUE(g.finished) << (g.problems.empty() ? "" : g.problems.front());
        EXPECT_EQ(g.rejected, 0u);
        EXPECT_EQ(g.reverted, 0u);
        EXPECT_EQ(g.fallbacks, 0u);
    }
}

// ==== A small seeded Hard-vs-Normal arena ====

TEST(M44_Arena, HardAgainstNormalInBothSeatsIsCleanAndHoldsItsGround) {
    const GameState scenario = wh40k::test::load_v1_scenario();
    const ArenaResult base = run_arena(scenario, 2, 1, normal_level(), normal_level());
    const ArenaResult as_a = run_arena(scenario, 2, 1, hard_level(), normal_level());
    const ArenaResult as_b = run_arena(scenario, 2, 1, normal_level(), hard_level());
    for (const ArenaResult* r : {&base, &as_a, &as_b}) {
        EXPECT_TRUE(r->clean()) << format_arena(*r, scenario);
        EXPECT_EQ(r->fallbacks, 0u) << format_arena(*r, scenario);
    }
    // As the Space Marines it wins every game, as Normal does; as the Tyranids it wins or draws no fewer.
    EXPECT_EQ(as_a.wins[0], 2) << format_arena(as_a, scenario);
    EXPECT_GE(as_b.wins[1] + as_b.draws, base.wins[1] + base.draws) << format_arena(as_b, scenario);
    // The robust direction of the Release arena (1.67 rounds against 1.51): as the Tyranids it lasts no shorter.
    EXPECT_GE(as_b.mean_end_round, base.mean_end_round) << format_arena(as_b, scenario);
}

// ==== Diagnostics (run on purpose) ====

TEST(M44_Diagnostics, DISABLED_TimeHardDecisions) {
    for (const char* file : {"v1_skirmish.json", "bot_20_termagants.json"}) {
        const GameState scenario = load(file);
        std::vector<double> ms;
        std::vector<std::string> trace;
        const auto t0 = std::chrono::steady_clock::now();
        const GameRecord g = play_bot_game(scenario, 1, hard_level(), hard_level(), 5000, &ms, &trace);
        const auto t1 = std::chrono::steady_clock::now();
        std::sort(ms.begin(), ms.end());
        std::printf("%s: finished %d, %zu decisions, game %.0f ms, max decision %.1f ms, p95 %.1f ms\n", file,
                    g.finished ? 1 : 0, ms.size(), std::chrono::duration<double, std::milli>(t1 - t0).count(),
                    ms.empty() ? 0.0 : ms.back(), ms.empty() ? 0.0 : ms[ms.size() * 95 / 100]);
    }
}

TEST(M44_Diagnostics, DISABLED_PrintChainValues) {
    const GameState& gs = first_move_step();
    analysis::OddsCache cache;
    const hard::Evaluator gants(cache, gs, Side::B, hard::ChainWeights{});
    std::printf("line %d buckets\n", gants.line_buckets());
    for (const int h : {0, 1, 4, 8, 9}) {
        for (const int wb : {10, 5, 2}) {
            std::printf("h %d, Termagants %2d:", h, wb);
            for (const float gap : {0.0f, 4.0f, 8.0f, 12.0f, 16.0f}) {
                const hard::Evaluation e = gants.at(h, {10, wb}, in(gap), {in(4.0f), in(4.0f)});
                std::printf("  %4.0f\" v %+.3f d %.3f s %+.3f", gap, e.v, e.p_draw, e.score);
            }
            std::printf("\n");
        }
    }
}

TEST(M44_Diagnostics, DISABLED_TimeTheChainTable) {
    for (const char* file : {"v1_skirmish.json", "bot_20_termagants.json"}) {
        GameState gs = load(file);
        start_game(gs);
        analysis::OddsCache warm;
        { const hard::Evaluator e(warm, gs, Side::A, hard::ChainWeights{}); }
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 5; ++i) {
            analysis::OddsCache cache;
            const hard::Evaluator e(cache, gs, Side::A, hard::ChainWeights{});
        }
        const auto t1 = std::chrono::steady_clock::now();
        std::printf("%s: chain table %.1f ms\n", file, std::chrono::duration<double, std::milli>(t1 - t0).count() / 5);
    }
}
