// M98 (D-100): the bot's charge planning prices 15.06 Crushing Impact, behind its crushing_impact key (0 by default):
// crushing_impact_value gives what the stratagem is expected to gain at a planned charge move's end (the window's
// valuation less the CP's worth, where the engine's forecast says it would be offered), and charge_prospect and
// decide_charge_targets add it. At 0 nothing is computed, so the default bot plays as at M97.

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "bot_configs.hpp"
#include "core/engine/engine.hpp"
#include "core/state/queries.hpp"
#include "normal/charge.hpp"
#include "normal/move_score.hpp"
#include "normal/stratagems.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::ai::BotConfig;
using wh40k::ai::PolicyResult;
using wh40k::ai::analysis::OddsCache;
using wh40k::ai::normal::ChargeProspect;
using wh40k::ai::normal::MoveWeights;
using wh40k::test::in;
namespace mf = wh40k::test::monsters;
namespace normal = wh40k::ai::normal;

namespace {

constexpr std::uint64_t kSeed = 1;

UnitId tyrant(const GameState& gs) { return gs.units[mf::kTyrant].id; }
UnitId legionaries(const GameState& gs) { return gs.units[mf::kLegionaries].id; }

// The monsters fixture at Side B's Charge decision (round 1), Side B with `cp_b` CP. `points`: every unit's points
// (Side A's first), so shares are weighted (D-077); with the Legionaries worth four times the Tyrant, Crushing Impact
// gains clearly (M97_CrushingImpact).
GameState b_charge_step(int cp_b, std::optional<std::pair<int, int>> points) {
    GameState gs = mf::at_movement();
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::Charge>(g.pending->kind) && g.pending->side == Side::B;
    });
    gs.command_points[1] = cp_b;
    if (points) {
        for (Unit& u : gs.units) {
            u.points = u.side == Side::A ? points->first : points->second;
            u.starting_wounds = 0;
            for (const Model& m : u.models) u.starting_wounds += m.characteristics.wounds;
        }
    }
    gs.pending = compute_decision(gs);
    return gs;
}

// The Tyrant's charge declared (a 6, inline): its targets wait.
GameState charge_targets(int cp_b, std::optional<std::pair<int, int>> points) {
    GameState gs = b_charge_step(cp_b, points);
    ScriptedDice six{3, 3};
    apply_action(gs, Side::B, action::DeclareCharge{tyrant(gs)}, six);
    EXPECT_TRUE(gs.pending && std::holds_alternative<decision::SelectChargeTargets>(gs.pending->kind));
    return gs;
}

// The Tyrant's charge move into the Legionaries, ending 0.5" from their rank and committed. `paced`: Crushing Impact's
// window waits; otherwise it is declined inline, as the planner's simulator plays a charge.
GameState after_charge(int cp_b, std::optional<std::pair<int, int>> points, bool paced) {
    GameState gs = charge_targets(cp_b, points);
    apply_action(gs, Side::B, action::StartChargeMove{tyrant(gs), {legionaries(gs)}});
    const Model& t = gs.units[mf::kTyrant].models[0];
    apply_action(gs, Side::B,
                 action::ProposeModelMove{t.id, {Position{t.position.x, in(mf::kRankY + mf::tyrant_centre_gap(0.5f)), 0.0f}}});
    ScriptedDice none{std::vector<int>{}};
    if (paced) {
        apply_action_paced(gs, Side::B, action::CommitUnitMove{}, none);
    } else {
        apply_action(gs, Side::B, action::CommitUnitMove{}, none);
    }
    return gs;
}

const std::pair<int, int> kRichEnemy{400, 100};

MoveWeights crush_on() {
    MoveWeights w;
    w.crushing_impact = 1.0;
    return w;
}

void expect_same(const ChargeProspect& a, const ChargeProspect& b) {
    EXPECT_EQ(a.targets, b.targets);
    EXPECT_EQ(a.p_success, b.p_success);
    EXPECT_EQ(a.hold, b.hold);
    EXPECT_EQ(a.expected, b.expected);
    EXPECT_EQ(a.gain, b.gain);
    EXPECT_EQ(a.success_mean, b.success_mean);
    EXPECT_EQ(a.success_by_roll, b.success_by_roll);
    EXPECT_EQ(a.value_by_roll, b.value_by_roll);
    EXPECT_EQ(a.engaged_by_roll, b.engaged_by_roll);
}

}  // namespace

// ==== The value at a charge move's end ====

TEST(M98_Value, CrushingImpactsValueIsTheWindowsGainLessTheCpsWorth) {
    // The same charge, once with the window waiting (the bot's M97 valuation there), once declined inline (the planner's
    // view): the value is the window's best gain less kCrushMinGain.
    const GameState window = after_charge(1, kRichEnemy, true);
    ASSERT_TRUE(window_pending(window));
    const auto pick = normal::best_crushing_impact(window);
    ASSERT_TRUE(pick.has_value());
    ASSERT_GT(pick->value, normal::kCrushMinGain);
    const GameState planned = after_charge(1, kRichEnemy, false);
    ASSERT_FALSE(window_pending(planned));
    EXPECT_DOUBLE_EQ(normal::crushing_impact_value(planned, tyrant(planned)), pick->value - normal::kCrushMinGain);
    // Read-only.
    const GameState before = planned;
    (void)normal::crushing_impact_value(planned, tyrant(planned));
    EXPECT_EQ(planned, before);
}

TEST(M98_Value, ItIsZeroWhereTheBotWouldNotUseIt) {
    const GameState planned = after_charge(1, kRichEnemy, false);
    ASSERT_GT(normal::crushing_impact_value(planned, tyrant(planned)), 0.0);
    // Without points the Tyrant's own 1s cost as much as the 5+s take (M97): no gain above the CP's worth.
    const GameState even = after_charge(1, std::nullopt, false);
    EXPECT_EQ(normal::crushing_impact_value(even, tyrant(even)), 0.0);
    // Where the engine would not offer it (forecast_stratagem_problem): no CP, used this phase, already targeted (a
    // Command Re-roll of the charge roll), battle-shocked, not a MONSTER/VEHICLE, not engaged.
    {
        GameState poor = planned;
        poor.command_points[1] = 0;
        EXPECT_EQ(normal::crushing_impact_value(poor, tyrant(poor)), 0.0);
    }
    {
        GameState used = planned;
        used.stratagems[1].used_this_phase.push_back(Stratagem::CrushingImpact);
        EXPECT_EQ(normal::crushing_impact_value(used, tyrant(used)), 0.0);
    }
    {
        GameState targeted = planned;
        targeted.stratagems[1].targeted_this_phase.push_back(tyrant(targeted));
        EXPECT_EQ(normal::crushing_impact_value(targeted, tyrant(targeted)), 0.0);
    }
    {
        GameState shocked = planned;
        shocked.units[mf::kTyrant].battle_shocked = true;
        EXPECT_EQ(normal::crushing_impact_value(shocked, tyrant(shocked)), 0.0);
    }
    EXPECT_EQ(normal::crushing_impact_value(planned, planned.units[mf::kGuard].id), 0.0);
    {
        GameState apart = planned;
        for (Model& m : apart.units[mf::kTyrant].models) m.position.y += in(3.0f);
        EXPECT_EQ(normal::crushing_impact_value(apart, tyrant(apart)), 0.0);
    }
    // In the opponent's turn (a heroic charge's): never.
    EXPECT_EQ(normal::crushing_impact_value(mf::at_movement(false, Side::A), tyrant(planned)), 0.0);
}

// ==== The charge planning ====

TEST(M98_Charge, TheProspectCountsItOnlyWithTheFlag) {
    const GameState gs = b_charge_step(1, kRichEnemy);
    OddsCache cache;
    const std::optional<ChargeProspect> off = normal::charge_prospect(cache, gs, tyrant(gs), kSeed);
    const std::optional<ChargeProspect> on =
        normal::charge_prospect(cache, gs, tyrant(gs), kSeed, 0.0, std::nullopt, nullptr, nullptr, true);
    ASSERT_TRUE(off && on);
    // The flag off is the call without it (M97's prospect).
    const std::optional<ChargeProspect> off2 =
        normal::charge_prospect(cache, gs, tyrant(gs), kSeed, 0.0, std::nullopt, nullptr, nullptr, false);
    ASSERT_TRUE(off2);
    expect_same(*off, *off2);
    // On: every roll whose charge move stands is worth more, by the same odds; failed rolls hold as before.
    EXPECT_EQ(on->success_by_roll, off->success_by_roll);
    EXPECT_EQ(on->p_success, off->p_success);
    EXPECT_EQ(on->hold, off->hold);
    int more = 0;
    for (std::size_t r = 0; r <= 12; ++r) {
        if (!off->success_by_roll[r]) {
            EXPECT_EQ(on->value_by_roll[r], off->value_by_roll[r]) << r;
            continue;
        }
        EXPECT_GE(on->value_by_roll[r], off->value_by_roll[r]) << r;
        more += on->value_by_roll[r] > off->value_by_roll[r] ? 1 : 0;
    }
    EXPECT_GT(more, 0);
    EXPECT_GT(on->expected, off->expected);
    EXPECT_GE(on->gain, off->gain);
    // Without the CP the forecast refuses it: the same prospect as off.
    const GameState poor = b_charge_step(0, kRichEnemy);
    const std::optional<ChargeProspect> poor_off = normal::charge_prospect(cache, poor, tyrant(poor), kSeed);
    const std::optional<ChargeProspect> poor_on =
        normal::charge_prospect(cache, poor, tyrant(poor), kSeed, 0.0, std::nullopt, nullptr, nullptr, true);
    ASSERT_TRUE(poor_off && poor_on);
    expect_same(*poor_on, *poor_off);
}

TEST(M98_Charge, TheChargeMoveNamesCrushingImpactWithTheKeyOn) {
    const GameState gs = charge_targets(1, kRichEnemy);
    const std::vector<GameAction> legal = legal_actions(gs, Side::B);
    OddsCache cache;
    const std::optional<PolicyResult> on = normal::decide_charge_targets(cache, gs, Side::B, legal, kSeed, crush_on());
    ASSERT_TRUE(on.has_value());
    EXPECT_TRUE(std::holds_alternative<action::StartChargeMove>(on->action)) << on->reason;
    EXPECT_NE(on->reason.find("; Crushing Impact +"), std::string::npos) << on->reason;
    // Off (the default): the same move, without the term.
    const std::optional<PolicyResult> off = normal::decide_charge_targets(cache, gs, Side::B, legal, kSeed);
    ASSERT_TRUE(off.has_value());
    EXPECT_EQ(off->action, on->action);
    EXPECT_EQ(off->reason.find("Crushing Impact"), std::string::npos) << off->reason;
    // On without the CP: as off.
    const GameState poor = charge_targets(0, kRichEnemy);
    const std::vector<GameAction> poor_legal = legal_actions(poor, Side::B);
    const std::optional<PolicyResult> poor_on =
        normal::decide_charge_targets(cache, poor, Side::B, poor_legal, kSeed, crush_on());
    const std::optional<PolicyResult> poor_off = normal::decide_charge_targets(cache, poor, Side::B, poor_legal, kSeed);
    ASSERT_TRUE(poor_on && poor_off);
    EXPECT_EQ(poor_on->action, poor_off->action);
    EXPECT_EQ(poor_on->reason, poor_off->reason);
}

TEST(M98_Charge, TheDeclarationWithTheKeyOffIsAsWithoutIt) {
    // The Charge decision itself: the default weights and the key at 0 explicitly give the same answer and reason (no
    // term computed); with the key on the declaration still stands.
    const GameState gs = b_charge_step(1, kRichEnemy);
    const std::vector<GameAction> legal = legal_actions(gs, Side::B);
    OddsCache cache;
    MoveWeights zero;
    zero.crushing_impact = 0.0;
    const std::optional<PolicyResult> d = normal::decide_charge(cache, gs, Side::B, legal, kSeed);
    const std::optional<PolicyResult> z = normal::decide_charge(cache, gs, Side::B, legal, kSeed, zero);
    const std::optional<PolicyResult> on = normal::decide_charge(cache, gs, Side::B, legal, kSeed, crush_on());
    ASSERT_TRUE(d && z && on);
    EXPECT_EQ(d->action, z->action);
    EXPECT_EQ(d->reason, z->reason);
    EXPECT_TRUE(validate_action(gs, Side::B, on->action).ok());
}

// ==== Games ====

TEST(M98_Bots, GamesWithTheKeyOnStayClean) {
    // The monsters fixture (MONSTER charges) with Crushing Impact on for both sides: every answer legal (no rejection,
    // revert or fallback) and every game finished.
    BotConfig crush;
    crush.move.crushing_impact = 1.0;
    int used = 0;
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        GameSession s = GameSession::new_game(mf::fixture(), seed);
        const wh40k::ai::GameRecord g = wh40k::ai::play_bot_session(s, crush, crush, 5000, nullptr, nullptr);
        EXPECT_TRUE(g.finished);
        EXPECT_EQ(g.fallbacks, 0u);
        EXPECT_EQ(g.rejected, 0u);
        EXPECT_EQ(g.reverted, 0u);
        for (const ReplayStep& step : s.history()) {
            if (const auto* u = std::get_if<action::UseStratagem>(&step.action)) {
                used += u->stratagem == Stratagem::CrushingImpact ? 1 : 0;
            }
        }
    }
    std::printf("Crushing Impact used %d times in 3 games\n", used);
}
