// D-100 (M98): forecast_stratagem_problem, the refusal a window that follows no roll would give were it opened in the
// state as it stands (15.01, 01.07, the stratagem's WHEN and TARGET), for the bots' forecasts: Crushing Impact (15.06)
// at the end of a charge move still being planned.

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/state/queries.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::in;
namespace mf = wh40k::test::monsters;

namespace {

std::string label(UnitId id) { return "unit " + std::to_string(id.v); }

UnitId tyrant(const GameState& gs) { return gs.units[mf::kTyrant].id; }
UnitId legionaries(const GameState& gs) { return gs.units[mf::kLegionaries].id; }

// Side B's Charge decision in round 1 (every unit remained stationary, no shooting), Side B with `cp_b` CP.
GameState b_charge_step(int cp_b) {
    GameState gs = mf::at_movement();
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::Charge>(g.pending->kind) && g.pending->side == Side::B;
    });
    gs.command_points[1] = cp_b;
    gs.pending = compute_decision(gs);
    return gs;
}

// The Tyrant charges the Legionaries (a 6, inline) and ends `gap_in` from their rank. `paced`: the commit is paced, so
// Crushing Impact's window waits; otherwise it is declined inline, as the bot's planner simulates a charge.
GameState after_tyrant_charge(int cp_b, bool paced, float gap_in = 0.5f) {
    GameState gs = b_charge_step(cp_b);
    ScriptedDice six{3, 3};
    apply_action(gs, Side::B, action::DeclareCharge{tyrant(gs)}, six);
    EXPECT_TRUE(gs.pending && std::holds_alternative<decision::SelectChargeTargets>(gs.pending->kind));
    apply_action(gs, Side::B, action::StartChargeMove{tyrant(gs), {legionaries(gs)}});
    const Model& t = gs.units[mf::kTyrant].models[0];
    const Position end{t.position.x, in(mf::kRankY + mf::tyrant_centre_gap(gap_in)), 0.0f};
    apply_action(gs, Side::B, action::ProposeModelMove{t.id, {end}});
    ScriptedDice none{std::vector<int>{}};
    if (paced) {
        apply_action_paced(gs, Side::B, action::CommitUnitMove{}, none);
    } else {
        apply_action(gs, Side::B, action::CommitUnitMove{}, none);
    }
    return gs;
}

std::optional<std::string> crush_forecast(const GameState& gs, UnitId unit, Side side = Side::B) {
    return forecast_stratagem_problem(gs, side, Stratagem::CrushingImpact, WindowTiming::AfterChargeMove, unit, unit);
}

}  // namespace

TEST(D100_Forecast, AgreesWithTheWindowThatOpensAfterTheChargeMove) {
    const GameState open = after_tyrant_charge(1, true);
    ASSERT_TRUE(window_pending(open));
    const std::optional<WindowInfo> w = window_info(open);
    ASSERT_TRUE(w);
    ASSERT_EQ(w->options.size(), 1u);
    EXPECT_EQ(w->options[0].stratagem, Stratagem::CrushingImpact);
    EXPECT_EQ(w->options[0].targets, std::vector<UnitId>{tyrant(open)});
    // For every unit of the side: no refusal exactly for the targets the window offers.
    for (const Unit& u : open.units) {
        if (u.side != Side::B) continue;
        const std::optional<std::string> p =
            forecast_stratagem_problem(open, Side::B, Stratagem::CrushingImpact, WindowTiming::AfterChargeMove,
                                       tyrant(open), u.id);
        EXPECT_EQ(!p.has_value(), u.id == tyrant(open)) << label(u.id) << ": " << p.value_or("none");
    }
    // The same charge with the window declined inline (as the bot's simulator plays it): the Charge step goes on, and
    // the forecast there is the window's answer.
    const GameState planned = after_tyrant_charge(1, false);
    ASSERT_FALSE(window_pending(planned));
    EXPECT_EQ(planned.pending, (PendingDecision{Side::B, decision::Charge{}}));
    EXPECT_EQ(crush_forecast(planned, tyrant(planned)), std::nullopt);
    // Read-only.
    const GameState before = planned;
    (void)crush_forecast(planned, tyrant(planned));
    EXPECT_EQ(planned, before);
}

TEST(D100_Forecast, GivesTheWindowsRefusalsFor15_01And01_07) {
    const GameState gs = after_tyrant_charge(1, false);
    {
        GameState poor = gs;
        poor.command_points[1] = 0;
        EXPECT_EQ(crush_forecast(poor, tyrant(poor)), "15.01: Side B has 0 CP; Crushing Impact costs 1CP");
    }
    {
        GameState used = gs;
        used.stratagems[1].used_this_phase.push_back(Stratagem::CrushingImpact);
        EXPECT_EQ(crush_forecast(used, tyrant(used)), "15.01: Side B has already used Crushing Impact this phase");
    }
    {
        // A Command Re-roll of its charge roll targeted the Tyrant this phase.
        GameState targeted = gs;
        targeted.stratagems[1].targeted_this_phase.push_back(tyrant(targeted));
        EXPECT_EQ(crush_forecast(targeted, tyrant(targeted)),
                  "15.01: " + label(tyrant(targeted)) + " has already been targeted by a stratagem of Side B this phase");
    }
    {
        GameState shocked = gs;
        shocked.units[mf::kTyrant].battle_shocked = true;
        EXPECT_EQ(crush_forecast(shocked, tyrant(shocked)),
                  "01.07: " + label(tyrant(shocked)) + " is battle-shocked, so its player can't target it with stratagems");
    }
}

TEST(D100_Forecast, GivesTheTargetsRefusalsFor15_06) {
    const GameState gs = after_tyrant_charge(1, false);
    // A unit without MONSTER/VEHICLE.
    const UnitId guard = gs.units[mf::kGuard].id;
    EXPECT_EQ(crush_forecast(gs, guard), "15.06: Crushing Impact targets a MONSTER/VEHICLE unit (" + label(guard) + " is not)");
    // Another unit than the window's.
    const UnitId screamer = gs.units[mf::kScreamer].id;
    EXPECT_EQ(forecast_stratagem_problem(gs, Side::B, Stratagem::CrushingImpact, WindowTiming::AfterChargeMove,
                                         tyrant(gs), screamer),
              "15.06: Crushing Impact targets the unit that has just ended its charge move (" + label(tyrant(gs)) +
                  "), not " + label(screamer));
    // A charge move that ends unengaged (more than 2" away): no pair.
    GameState apart = gs;
    for (Model& m : apart.units[mf::kTyrant].models) m.position.y += in(3.0f);
    ASSERT_TRUE(engaged_enemy_units(apart, apart.units[mf::kTyrant]).empty());
    EXPECT_EQ(crush_forecast(apart, tyrant(apart)),
              "15.06: no model of " + label(tyrant(apart)) + " is engaged with an enemy unit (D-099)");
}

TEST(D100_Forecast, GivesTheWhenOutsideItsWindow) {
    const GameState gs = after_tyrant_charge(1, false);
    const std::string when = "15.06: Crushing Impact is used in your Charge phase, just after a friendly MONSTER/VEHICLE "
                             "unit ends a charge move";
    // The opponent's Charge phase (Side A's forecast for Side B's unit, and for its own).
    EXPECT_EQ(crush_forecast(gs, tyrant(gs), Side::A), when);
    // Another timing.
    EXPECT_EQ(forecast_stratagem_problem(gs, Side::B, Stratagem::CrushingImpact, WindowTiming::EndOfPhase, tyrant(gs),
                                         tyrant(gs)),
              when);
    // A roll window, and a roll's stratagem: their WHEN.
    EXPECT_EQ(forecast_stratagem_problem(gs, Side::B, Stratagem::CrushingImpact, WindowTiming::AfterRoll, tyrant(gs),
                                         tyrant(gs)),
              when);
    EXPECT_EQ(forecast_stratagem_problem(gs, Side::B, Stratagem::CommandReroll, WindowTiming::AfterChargeMove,
                                         tyrant(gs), tyrant(gs))
                  .value_or("")
                  .substr(0, 6),
              "15.02:");
    // Explosives has no window (it is answered at the Shoot step).
    EXPECT_EQ(forecast_stratagem_problem(gs, Side::B, Stratagem::Explosives, WindowTiming::AfterChargeMove, tyrant(gs),
                                         tyrant(gs))
                  .value_or("")
                  .substr(0, 6),
              "15.05:");
    // Outside the Charge step (the Movement phase).
    const GameState moving = mf::at_movement();
    EXPECT_EQ(crush_forecast(moving, tyrant(moving)), when);
    // D-007 first: once the battle is over, no window opens.
    GameState over = gs;
    over.result = GameResult{Side::B, "D-007"};
    EXPECT_EQ(crush_forecast(over, tyrant(over)), "D-007: the battle is over, so no window opens");
}
