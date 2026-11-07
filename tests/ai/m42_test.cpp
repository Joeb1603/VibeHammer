// M42: the charge planner (D-054, D-056): 11.02, 11.04, 03.01, 03.03, 03.04. Placements across a grid of positions
// and rolls, the charge declaration, the charge targets after the roll, the charge move from any state of its
// plan, the prospect M43 uses, dice independence and clean arena games.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "analysis/charge_odds.hpp"
#include "analysis/odds.hpp"
#include "arena.hpp"
#include "auto_actions.hpp"
#include "auto_charge_move.hpp"
#include "bot.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/engagement.hpp"
#include "core/state/queries.hpp"
#include "normal/charge.hpp"
#include "session.hpp"
#include "state_hash.hpp"
#include "support/charge_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::Charger;
using wh40k::test::ChargeLayout;
using wh40k::test::in;

namespace {

BotConfig normal_level() { return BotConfig{BotLevel::Normal}; }
BotConfig easy_level() { return BotConfig{BotLevel::Easy}; }

UnitId charger_id(const GameState& gs, Charger c) { return gs.units[wh40k::test::charger_index(c)].id; }
UnitId target_id(const GameState& gs, Charger c) { return gs.units[wh40k::test::target_index(c)].id; }

// Side A's SelectChargeTargets after a scripted roll; nullopt if the target isn't an eligible charge target.
std::optional<GameState> rolled(Charger c, const ChargeLayout& layout, int roll) {
    GameState gs = wh40k::test::charge_fixture(c, layout);
    if (!wh40k::test::declare_with_roll(gs, c, roll)) return std::nullopt;
    return gs;
}

int engaged_models(const GameState& gs, UnitId id) {
    const Unit& u = *gs.find_unit(id);
    int n = 0;
    for (const Model& m : u.models) n += enemy_units_engaged_with_model(gs, u, m, m.position).empty() ? 0 : 1;
    return n;
}

struct Played {
    bool charged = false;  // the unit made a charge move (committed, not reverted)
    int reverted = 0;
    int rejected = 0;
    GameState after;
};

// Answers side A's decisions with `bot` (applying each with the engine) while they are the charge's
// SelectChargeTargets or MoveModels: from the roll to the charge's resolution.
Played play_charge(GameState gs, Bot& bot, UnitId unit) {
    Played out;
    for (int guard = 0; guard < 64 && gs.pending && gs.pending->side == Side::A; ++guard) {
        const auto& kind = gs.pending->kind;
        const bool charge_step = std::holds_alternative<decision::SelectChargeTargets>(kind) ||
                                 (std::holds_alternative<decision::MoveModels>(kind) && gs.flow.move &&
                                  gs.flow.move->type == MoveType::Charge);
        if (!charge_step) break;
        const BotChoice c = bot.decide(gs, Side::A);
        if (!validate_action(gs, Side::A, c.action).ok()) {
            ++out.rejected;
            break;
        }
        for (const GameEvent& e : apply_action(gs, Side::A, c.action)) {
            if (std::holds_alternative<event::UnitMoveReverted>(e)) ++out.reverted;
        }
    }
    out.charged = gs.find_unit(unit)->turn_flags.made_charge_move;
    out.after = std::move(gs);
    return out;
}

// The Termagants 10" from the Intercessors, which are locked in combat with a second Termagant unit (in base
// contact with their far side): the Intercessors can't shoot or charge next turn, and can't be shot (04.02).
GameState intercessors_locked_in_combat() {
    GameState gs = wh40k::test::charge_fixture(Charger::Termagants, ChargeLayout{10.0f, 0.0f, 0});
    const UnitId other = wh40k::test::add_unit_copy(gs, wh40k::test::kM42Gants);
    Unit& lock = *gs.find_unit(other);
    lock.models.resize(5);
    const Unit& marines = gs.units[wh40k::test::kM42Marines];
    for (std::size_t i = 0; i < lock.models.size(); ++i) {
        const Model& m = marines.models[i];
        lock.models[i].position = Position{m.position.x, m.position.y + m.base.diameter_mm / 2 +
                                                             lock.models[i].base.diameter_mm / 2 + 0.001f, 0.0f};
    }
    gs.pending = compute_decision(gs);
    return gs;
}

// One Termagant ringed by six Termagants of another unit: no charge move can end engaged with it alone.
GameState ringed_target(int roll) {
    GameState gs = wh40k::test::charge_fixture(Charger::Intercessors, ChargeLayout{4.0f, 0.0f, 0});
    const UnitId ring = wh40k::test::add_unit_copy(gs, wh40k::test::kM42Gants);
    Unit& target = gs.units[wh40k::test::kM42Gants];
    target.models.resize(1);
    const Position c{in(12.0f), gs.units[0].models.front().position.y + in(7.0f), 0.0f};
    target.models.front().position = c;
    Unit& others = *gs.find_unit(ring);
    others.models.resize(6);
    const float d = others.models.front().base.diameter_mm;
    for (std::size_t i = 0; i < 6; ++i) {
        const float a = 2.0f * 3.14159265f * static_cast<float>(i) / 6.0f;
        others.models[i].position = Position{c.x + d * std::cos(a), c.y + d * std::sin(a), 0.0f};
    }
    gs.pending = compute_decision(gs);
    if (roll > 0) EXPECT_TRUE(wh40k::test::declare_with_roll(gs, Charger::Intercessors, roll));
    return gs;
}

}  // namespace

// ==== Acceptance: planned charge moves across a grid of positions and rolls ====

TEST(M42_ChargeMove, AcrossAGridEveryPlannedChargeCommitsWithoutARevertAndEngagesAtLeastAsManyAsTheBaseline) {
    int cases = 0;
    int baseline_charges = 0;
    int more_engaged = 0;
    for (const Charger c : {Charger::Intercessors, Charger::Termagants}) {
        for (const float gap : {2.5f, 5.5f, 8.5f, 11.5f}) {
            for (const float offset : {0.0f, 4.0f}) {
                for (int roll = 2; roll <= 12; ++roll) {
                    const ChargeLayout layout{gap, offset, 0};
                    const std::optional<GameState> gs = rolled(c, layout, roll);
                    if (!gs) continue;
                    ++cases;
                    char where[96];
                    std::snprintf(where, sizeof where, "%s gap %.1f offset %.1f roll %d",
                                  c == Charger::Intercessors ? "Intercessors" : "Termagants", gap, offset, roll);
                    const UnitId unit = charger_id(*gs, c);
                    // The bot's placement: simulated in full (start, placements, move_end_problems, commit).
                    std::vector<std::string> problems;
                    const auto plan = normal::plan_charge_move(*gs, Side::A, unit, {target_id(*gs, c)},
                                                               decision_seed(*gs), "test", &problems);
                    // The M39 baseline: level Easy's charge move, played out.
                    Bot easy(easy_level());
                    const Played base = play_charge(*gs, easy, unit);
                    EXPECT_EQ(base.reverted, 0) << where;
                    if (base.charged) {
                        ++baseline_charges;
                        ASSERT_TRUE(plan.has_value()) << where << ": " << (problems.empty() ? "" : problems.front());
                    }
                    ASSERT_TRUE(plan.has_value()) << where << ": " << (problems.empty() ? "" : problems.front());
                    // Replayed through the engine: every step valid, the move can end, the commit stands.
                    GameState work = *gs;
                    for (const PlannedStep& step : plan->steps) {
                        EXPECT_EQ(state_hash(work), step.expect_hash) << where;
                        ASSERT_TRUE(validate_action(work, Side::A, step.action).ok()) << where;
                        if (std::holds_alternative<action::CommitUnitMove>(step.action)) {
                            EXPECT_TRUE(move_end_problems(work).empty()) << where;
                        }
                        for (const GameEvent& e : apply_action(work, Side::A, step.action)) {
                            EXPECT_FALSE(std::holds_alternative<event::UnitMoveReverted>(e)) << where;
                        }
                    }
                    EXPECT_TRUE(work.find_unit(unit)->turn_flags.made_charge_move) << where;
                    EXPECT_EQ(work, plan->after) << where;
                    const int engaged = engaged_models(work, unit);
                    EXPECT_EQ(engaged, plan->score.engaged) << where;
                    if (base.charged) {
                        const int base_engaged = engaged_models(base.after, unit);
                        EXPECT_GE(engaged, base_engaged) << where;
                        if (engaged > base_engaged) ++more_engaged;
                    }
                }
            }
        }
    }
    std::printf("%d cases; the baseline charged in %d, the planner engaged more models in %d of those\n", cases,
                baseline_charges, more_engaged);
    EXPECT_GT(cases, 60);
    EXPECT_GT(more_engaged, 0);
}

TEST(M42_ChargeMove, TheBotsChargeIsItsPlanAndAFreshBotContinuesItFromAnyStep) {
    const std::optional<GameState> gs = rolled(Charger::Intercessors, ChargeLayout{5.0f, 2.0f, 0}, 8);
    ASSERT_TRUE(gs.has_value());
    const UnitId unit = charger_id(*gs, Charger::Intercessors);
    Bot bot(normal_level());
    GameState work = *gs;
    std::vector<GameAction> played;
    for (int guard = 0; guard < 32 && work.pending && !work.find_unit(unit)->turn_flags.made_charge_move; ++guard) {
        const BotChoice c = bot.decide(work, Side::A);
        // A fresh bot (no memo) at this state gives the same action (D-054: a plan is a function of the state).
        EXPECT_EQ(Bot(normal_level()).decide(work, Side::A).action, c.action) << c.reason;
        ASSERT_TRUE(validate_action(work, Side::A, c.action).ok()) << c.reason;
        played.push_back(c.action);
        for (const GameEvent& e : apply_action(work, Side::A, c.action)) {
            EXPECT_FALSE(std::holds_alternative<event::UnitMoveReverted>(e));
        }
    }
    ASSERT_FALSE(played.empty());
    EXPECT_TRUE(std::holds_alternative<action::StartChargeMove>(played.front()));
    EXPECT_TRUE(std::holds_alternative<action::CommitUnitMove>(played.back()));
    EXPECT_TRUE(work.find_unit(unit)->turn_flags.made_charge_move);
    EXPECT_TRUE(work.find_unit(unit)->turn_flags.fights_first);
    EXPECT_GT(bot.memo_hits(), 0u);
    EXPECT_EQ(bot.fallbacks(), 0u);
    EXPECT_EQ(engaged_models(work, unit), 5);
}

// ==== Acceptance: the charge declaration ====

TEST(M42_Declare, IntercessorsFourInchesFromTermagantsCharge) {
    const GameState gs = wh40k::test::charge_fixture(Charger::Intercessors, ChargeLayout{4.0f, 0.0f, 0});
    ASSERT_TRUE(std::holds_alternative<decision::Charge>(gs.pending->kind));
    const BotChoice c = Bot(normal_level()).decide(gs, Side::A);
    EXPECT_EQ(c.action, GameAction{action::DeclareCharge{charger_id(gs, Charger::Intercessors)}}) << c.reason;
    EXPECT_EQ(c.reason.rfind("Intercessor Squad: declare a charge (92% charge odds against Termagants", 0), 0u)
        << c.reason;
    analysis::OddsCache cache;
    const auto p = normal::charge_prospect(cache, gs, charger_id(gs, Charger::Intercessors), decision_seed(gs));
    ASSERT_TRUE(p.has_value());
    EXPECT_NEAR(p->p_success, analysis::p_charge_roll_covers(in(4.0f)), 1e-12);  // every eligible roll places
    EXPECT_GT(p->success_mean, p->hold);  // Fights First into Termagants beats shooting them
    EXPECT_GT(p->gain, 0.1);
    EXPECT_EQ(p->engaged_by_roll[6], 5);
}

TEST(M42_Declare, TermagantsWithPoorOddsHoldAndKeepShooting) {
    // 11.5" away only a 12 reaches (1/36). The Intercessors win any fight, and holding keeps a chance that they
    // don't catch the Termagants next turn, so their Fleshborers keep shooting.
    const GameState gs = wh40k::test::charge_fixture(Charger::Termagants, ChargeLayout{11.5f, 0.0f, 0});
    const BotChoice c = Bot(normal_level()).decide(gs, Side::A);
    EXPECT_EQ(c.action, GameAction{action::EndChargeStep{}}) << c.reason;
    EXPECT_EQ(c.reason.rfind("Termagants: no charge (3% charge odds; the Intercessor Squad would win the fight", 0),
              0u)
        << c.reason;
    analysis::OddsCache cache;
    const auto p = normal::charge_prospect(cache, gs, charger_id(gs, Charger::Termagants), decision_seed(gs));
    ASSERT_TRUE(p.has_value());
    EXPECT_NEAR(p->p_success, 1.0 / 36.0, 1e-12);
    EXPECT_LT(p->success_mean, p->hold);
    EXPECT_EQ(p->gain, 0.0);
}

TEST(M42_Declare, TermagantsHoldWhenTheIntercessorsAreLockedInCombatElsewhere) {
    // Nothing threatens the Termagants next turn, and charging only joins a fight the Intercessors win.
    const GameState gs = intercessors_locked_in_combat();
    ASSERT_TRUE(is_engaged(gs, gs.units[wh40k::test::kM42Marines]));
    const BotChoice c = Bot(normal_level()).decide(gs, Side::A);
    EXPECT_EQ(c.action, GameAction{action::EndChargeStep{}}) << c.reason;
    EXPECT_NE(c.reason.find("17% charge odds"), std::string::npos) << c.reason;
    analysis::OddsCache cache;
    const double hold = normal::hold_value(cache, gs, charger_id(gs, Charger::Termagants));
    EXPECT_NEAR(hold, 0.0, 1e-12);  // neither side can shoot or charge the other next turn
}

TEST(M42_Declare, TheExpectedGainFollowsTheOdds) {
    analysis::OddsCache cache;
    double last = 1e9;
    for (const float gap : {2.5f, 6.0f, 9.0f, 11.5f}) {
        const GameState gs = wh40k::test::charge_fixture(Charger::Intercessors, ChargeLayout{gap, 0.0f, 0});
        const auto p = normal::charge_prospect(cache, gs, charger_id(gs, Charger::Intercessors), decision_seed(gs));
        ASSERT_TRUE(p.has_value());
        EXPECT_LT(p->gain, last) << gap;
        EXPECT_GT(p->gain, 0.0) << gap;  // a declaration costs nothing, and a successful charge pays here
        last = p->gain;
    }
}

TEST(M42_Declare, NoRollWithAStandingMoveMeansNoCharge) {
    // Intercessors with M '-' (02.02: they can't be moved): eligible to declare, and the targets eligible by the
    // quick bound, but no charge move can be made, so no roll succeeds.
    GameState gs = wh40k::test::charge_fixture(Charger::Intercessors, ChargeLayout{4.0f, 0.0f, 0});
    for (Model& m : gs.units[wh40k::test::kM42Marines].models) m.characteristics.move_mm.reset();
    analysis::OddsCache cache;
    const auto p = normal::charge_prospect(cache, gs, charger_id(gs, Charger::Intercessors), decision_seed(gs));
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->p_success, 0.0);
    const BotChoice c = Bot(normal_level()).decide(gs, Side::A);
    EXPECT_EQ(c.action, GameAction{action::EndChargeStep{}}) << c.reason;
    EXPECT_NE(c.reason.find("no charge roll gives a charge move that stands"), std::string::npos) << c.reason;
    // After a roll (declared anyway): no charge move (11.02 Attempt Charge), never a move that would be reverted.
    ASSERT_TRUE(wh40k::test::declare_with_roll(gs, Charger::Intercessors, 10));
    const BotChoice skip = Bot(normal_level()).decide(gs, Side::A);
    EXPECT_EQ(skip.action, GameAction{action::SkipChargeMove{charger_id(gs, Charger::Intercessors)}}) << skip.reason;
    EXPECT_NE(skip.reason.find("no charge move stands for a roll of 10"), std::string::npos) << skip.reason;
}

// ==== The charge targets after the roll ====

TEST(M42_ChargeTargets, ChargesBothUnitsWhenTheTargetAloneCannotBeEngaged) {
    // The ringed Termagant can't be charged alone (every spot near it is within 2" of the ring, 11.04 After
    // Moving), but both units together can: the bot picks that target set, and the move stands.
    // D-066 (M56): M42's value summed the targets, each from the charger's full strength, which made the two-target
    // set worth the most here; that is the M55 generation. With the strength shared (the current Normal) the ring
    // alone is worth slightly more: M56_Charge.TheRingedTermagantIsChargedForTheMostValue.
    const GameState gs = ringed_target(12);
    const UnitId unit = charger_id(gs, Charger::Intercessors);
    Bot bot(BotConfig{BotLevel::Normal, {}, Generation::M55});
    const BotChoice c = bot.decide(gs, Side::A);
    const auto* start = std::get_if<action::StartChargeMove>(&c.action);
    ASSERT_NE(start, nullptr) << c.reason;
    EXPECT_EQ(start->targets.size(), 2u);
    const Played p = play_charge(gs, bot, unit);
    EXPECT_TRUE(p.charged);
    EXPECT_EQ(p.reverted, 0);
    EXPECT_EQ(p.rejected, 0);
    EXPECT_EQ(engaged_enemy_units(p.after, *p.after.find_unit(unit)).size(), 2u);
}

TEST(M42_ChargeTargets, NoChargeMoveWhenHoldingIsWorthMore) {
    GameState gs = intercessors_locked_in_combat();
    ASSERT_TRUE(wh40k::test::declare_with_roll(gs, Charger::Termagants, 12));
    const BotChoice c = Bot(normal_level()).decide(gs, Side::A);
    EXPECT_EQ(c.action, GameAction{action::SkipChargeMove{charger_id(gs, Charger::Termagants)}}) << c.reason;
    EXPECT_NE(c.reason.find("worth less than holding"), std::string::npos) << c.reason;
}

// ==== M43's seam: charge_prospect ====

TEST(M42_ChargeProspect, ExactOddsInTheOpenEqualTheQuickBound) {
    analysis::OddsCache cache;
    for (const Charger c : {Charger::Intercessors, Charger::Termagants}) {
        for (const float gap : {3.0f, 7.0f, 10.5f}) {
            const GameState gs = wh40k::test::charge_fixture(c, ChargeLayout{gap, 3.0f, 0});
            const auto p = normal::charge_prospect(cache, gs, charger_id(gs, c), decision_seed(gs));
            ASSERT_TRUE(p.has_value());
            EXPECT_NEAR(p->p_success, analysis::p_charge_roll_covers(*unit_distance(gs.units[0], gs.units[1])), 1e-12)
                << gap;
            double sum = 0.0;
            for (std::size_t r = 0; r <= 12; ++r) sum += p->success_by_roll[r] ? analysis::two_d6_pmf()[r] : 0.0;
            EXPECT_NEAR(sum, p->p_success, 1e-12);
            EXPECT_NEAR(p->gain, p->expected - p->hold, 1e-12);
        }
    }
}

TEST(M42_ChargeProspect, FromTheShootStepItIsTheChargeStepsAndCasualtiesLengthenTheCharge) {
    const GameState shoot = wh40k::test::shoot_step_state(Side::A);
    ASSERT_TRUE(std::holds_alternative<decision::Shoot>(shoot.pending->kind));
    GameState charge = shoot;
    apply_action(charge, Side::A, action::EndShootStep{});
    ASSERT_TRUE(std::holds_alternative<decision::Charge>(charge.pending->kind));
    analysis::OddsCache cache;
    const UnitId marines = shoot.units[wh40k::test::kM42Marines].id;
    const auto from_shoot = normal::charge_prospect(cache, shoot, marines, 7);
    const auto from_charge = normal::charge_prospect(cache, charge, marines, 7);
    ASSERT_TRUE(from_shoot.has_value());
    ASSERT_TRUE(from_charge.has_value());
    EXPECT_EQ(from_shoot->p_success, from_charge->p_success);
    EXPECT_EQ(from_shoot->hold, from_charge->hold);
    EXPECT_EQ(from_shoot->expected, from_charge->expected);
    // Our shooting's expected kills come off the front: the charge gets longer, never shorter.
    for (const double losses : {2.0, 5.0}) {
        const auto after_losses = normal::charge_prospect(cache, charge, marines, 7, losses);
        // With the whole front rank gone the rest may be beyond 12" (no charge can be declared at all).
        EXPECT_LE(after_losses ? after_losses->p_success : 0.0, from_charge->p_success) << losses;
    }
    EXPECT_TRUE(normal::charge_prospect(cache, charge, marines, 7, 2.0).has_value());
    // Not at all from the movement phase of the other side's turn, or for a unit that can't declare.
    EXPECT_FALSE(normal::charge_prospect(cache, charge, shoot.units[wh40k::test::kM42Gants].id, 7).has_value());
}

// ==== Acceptance: never peeks at the dice; clean games ====

TEST(M42_NeverPeeksAtDice, ChargeDecisionsAreTheSameWhateverTheDiceAndFromAnyStateOfThePlan) {
    std::size_t checked = 0;
    std::set<std::size_t> kinds;
    for (const auto& [a, b] : {std::pair{normal_level(), normal_level()}, std::pair{easy_level(), normal_level()}}) {
        for (std::uint64_t seed = 1; seed <= 4; ++seed) {
            client::GameSession s = client::GameSession::new_game(wh40k::test::load_v1_scenario(), seed);
            std::array<Bot, 2> bots{Bot(a), Bot(b)};
            client::AutoSettings autos;
            autos.bot_controlled = {true, true};
            while (s.pending() && s.history().size() < 5000) {
                (void)client::run_auto_actions(s, autos);
                if (!s.pending()) break;
                const Side side = s.pending()->side;
                const GameState gs = s.state();
                const BotChoice c = bots[side_index(side)].decide(gs, side);
                const auto& kind = gs.pending->kind;
                const bool charge_decision = std::holds_alternative<decision::Charge>(kind) ||
                                             std::holds_alternative<decision::SelectChargeTargets>(kind) ||
                                             (std::holds_alternative<decision::MoveModels>(kind) && gs.flow.move &&
                                              gs.flow.move->type == MoveType::Charge);
                const BotLevel level = side == Side::A ? a.level : b.level;
                if (charge_decision && level == BotLevel::Normal) {
                    GameState other = gs;
                    other.dice.seed ^= 0x9E3779B97F4A7C15ull;
                    other.dice.position += 11;
                    const BotChoice fresh = Bot(normal_level()).decide(gs, side);
                    const BotChoice dice = Bot(normal_level()).decide(other, side);
                    EXPECT_EQ(fresh.action, c.action) << "seed " << seed << ": " << c.reason << " / " << fresh.reason;
                    EXPECT_EQ(dice.action, c.action) << "seed " << seed << ": " << c.reason;
                    kinds.insert(kind.index());
                    ++checked;
                }
                ASSERT_FALSE(c.stopped);
                ASSERT_TRUE(s.submit(c.action).ok()) << action_name(c.action) << ": " << c.reason;
            }
        }
    }
    EXPECT_GT(checked, 10u);
    EXPECT_GE(kinds.size(), 2u);  // Charge, and SelectChargeTargets / charge moves where a roll reached
}

TEST(M42_Arena, NormalChargesWithNoRejectionsRevertsOrFallbacks) {
    const GameState scenario = wh40k::test::load_v1_scenario();
    std::size_t charge_moves = 0;
    for (const auto& [a, b] : {std::pair{normal_level(), easy_level()}, std::pair{easy_level(), normal_level()},
                               std::pair{normal_level(), normal_level()}}) {
        for (std::uint64_t seed = 1; seed <= 8; ++seed) {
            std::vector<std::string> trace;
            const GameRecord g = play_bot_game(scenario, seed, a, b, 5000, nullptr, &trace);
            EXPECT_TRUE(g.finished) << "seed " << seed;
            EXPECT_EQ(g.rejected, 0u) << "seed " << seed;
            EXPECT_EQ(g.reverted, 0u) << "seed " << seed;
            EXPECT_EQ(g.fallbacks, 0u) << "seed " << seed;
            for (const std::string& line : trace) {
                if (line.find("StartChargeMove") != std::string::npos &&
                    line.find("charge move into") != std::string::npos) {
                    ++charge_moves;
                }
            }
        }
    }
    EXPECT_GT(charge_moves, 0u);  // Normal's own charge moves were made (and none reverted)
}
