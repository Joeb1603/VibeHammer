// M80 (D-083 part 2): Normal moves as a group. A unit the army plan gives partners follows its focus: the line is the
// focus's, the partners' expected volleys on it are credited (ScoreLine::allies_hit), its reply is shared among the
// group's members it can reach, a unit with no partner able to reach the focus by our next turn keeps its own line and
// only approaches its focus, and the move reason names the focus and the partners. A unit without partners moves as
// it does with no plan at all (normal::no_plan, what Generation::M77 read; D-085, M85: the switch itself went, and with
// it the comparisons with the whole pre-batch Movement phase). Hand-built roster positions (the M79 fixtures' units).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "analysis/threat.hpp"
#include "arena.hpp"
#include "auto_actions.hpp"
#include "bot.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/actions.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/state/queries.hpp"
#include "state_fixture.hpp"
#include "normal/army_plan.hpp"
#include "normal/move_score.hpp"
#include "normal/movement.hpp"
#include "normal/value.hpp"
#include "session.hpp"
#include "simulator.hpp"
#include "state_hash.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using namespace wh40k::ai::normal;
using wh40k::ai::analysis::OddsCache;
using wh40k::test::in;
namespace mf = wh40k::test::monsters;

namespace {

// The roster scenario's units by id (D-076: Side A first): the chainsword Legionaries (1), the Chaos Lord (2), the
// boltgun Legionaries (5), the Genestealers (7), a Screamer-killer (8), the Tyrant Guard (9), the Hive Tyrant (10).
constexpr UnitId kChainswords{1};
constexpr UnitId kLord{2};
constexpr UnitId kBoltguns{5};
constexpr UnitId kGenestealers{7};
constexpr UnitId kScreamer{8};
constexpr UnitId kGuard{9};
constexpr UnitId kTyrant{10};

GameState rosters_with(std::initializer_list<UnitId> ids) {
    const GameState all = wh40k::test::load_rosters_scenario();
    GameState gs = all;
    gs.units.clear();
    for (const Unit& u : all.units) {
        if (std::find(ids.begin(), ids.end(), u.id) != ids.end()) gs.units.push_back(u);
    }
    return gs;
}

Unit& unit(GameState& gs, UnitId id) {
    Unit* u = gs.find_unit(id);
    if (u == nullptr) throw std::logic_error("no such unit");
    return *u;
}

float radius_in(const GameState& gs, UnitId id) { return mm_to_inches(base_radius(gs.find_unit(id)->models.front().base)); }

// Started with `first` taking the first turn, at its first Move Units decision.
GameState at_move_units(GameState gs, Side first = Side::A) {
    gs.first_turn = first;
    start_game(gs);
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind);
    });
    return gs;
}

// Edge to edge, in inches.
float gap_in(const GameState& gs, UnitId a, UnitId b) {
    return mm_to_inches(unit_distance(*gs.find_unit(a), *gs.find_unit(b)).value_or(-1.0f));
}

// The side to move's whole Movement phase, played by the bot (each StartMove's reason in `reasons`).
GameState play_movement(GameState gs, std::vector<std::string>* reasons = nullptr) {
    Bot bot(BotConfig{});
    const Side side = gs.pending.value().side;
    for (int i = 0; i < 400 && gs.pending && gs.pending->side == side &&
                    (std::holds_alternative<decision::MoveUnits>(gs.pending->kind) ||
                     std::holds_alternative<decision::MoveModels>(gs.pending->kind));
         ++i) {
        const BotChoice c = bot.decide(gs, side);
        if (reasons != nullptr && std::holds_alternative<action::StartMove>(c.action)) reasons->push_back(c.reason);
        EXPECT_TRUE(validate_action(gs, side, c.action).ok()) << c.reason;
        (void)apply_action(gs, side, c.action);
    }
    return gs;
}

// The same with the move score's weights given (Normal's own functions, as normal_policy calls them, the plans replayed
// as the bot replays them).
GameState play_movement_with(GameState gs, const MoveWeights& weights) {
    const Side side = gs.pending.value().side;
    std::vector<PlannedStep> rest;
    for (int i = 0; i < 400 && gs.pending && gs.pending->side == side &&
                    (std::holds_alternative<decision::MoveUnits>(gs.pending->kind) ||
                     std::holds_alternative<decision::MoveModels>(gs.pending->kind));
         ++i) {
        GameAction a;
        if (!rest.empty() && rest.front().expect_hash == state_hash(gs)) {
            a = rest.front().action;
            rest.erase(rest.begin());
        } else {
            OddsCache cache;
            std::optional<PolicyResult> r =
                std::holds_alternative<decision::MoveUnits>(gs.pending->kind)
                    ? decide_move_units(cache, gs, side, legal_actions(gs, side), decision_seed(gs), nullptr, nullptr,
                                        weights)
                    : continue_unit_move(cache, gs, side, weights);
            if (!r) {
                ADD_FAILURE() << "no move";
                return gs;
            }
            a = r->action;
            rest = std::move(r->rest);
        }
        EXPECT_TRUE(validate_action(gs, side, a).ok());
        (void)apply_action(gs, side, a);
    }
    return gs;
}

// Both Legionaries ranks `gap` inches south of a Screamer-killer at (22, 40), side by side (1.4" apart, edge to edge);
// the Tyrant Guard far north-east (Side B's other unit). Side A to move.
GameState side_by_side(float gap, bool with_lord = false) {
    GameState gs = with_lord ? rosters_with({kChainswords, kLord, kBoltguns, kScreamer, kGuard})
                             : rosters_with({kChainswords, kBoltguns, kScreamer, kGuard});
    mf::place_rank(unit(gs, kScreamer), 22.0f, 40.0f);
    const float y = 40.0f - gap - radius_in(gs, kScreamer) - radius_in(gs, kChainswords);
    mf::place_rank(unit(gs, kChainswords), 15.0f, y);
    mf::place_rank(unit(gs, kBoltguns), 29.0f, y);
    mf::place_rank(unit(gs, kGuard), 40.0f, 58.0f);
    if (with_lord) mf::place_rank(unit(gs, kLord), 40.0f, 3.0f);
    return at_move_units(gs);
}

// A Hive Tyrant at 3 wounds and the Genestealers north of a lone chainsword Legionaries unit at (22, 10), `gap` and
// `tyrant_gap` inches away (the Chaos Lord far south-east). Side B to move. The plan pairs them (M79:
// AWoundedHiveTyrantPairsWithGenestealers).
GameState tyrant_pair(float gap, float tyrant_gap = -1.0f) {
    if (tyrant_gap < 0.0f) tyrant_gap = gap;
    GameState gs = rosters_with({kChainswords, kLord, kGenestealers, kTyrant});
    mf::place_rank(unit(gs, kChainswords), 22.0f, 10.0f);
    mf::place_rank(unit(gs, kLord), 40.0f, 2.0f);
    const float r = radius_in(gs, kChainswords);
    mf::place_rank(unit(gs, kGenestealers), 15.0f, 10.0f + gap + r + radius_in(gs, kGenestealers));
    mf::place_rank(unit(gs, kTyrant), 29.0f, 10.0f + tyrant_gap + r + radius_in(gs, kTyrant));
    unit(gs, kTyrant).models.front().wounds_remaining = 3;
    return at_move_units(gs, Side::B);
}

}  // namespace

// ---- The group's moves ----

TEST(M80_Normal, TwoLegionariesUnitsAdvanceTogetherOnAScreamerKiller) {
    // Both Legionaries units 22" from a Screamer-killer: alone each loses (M79), together they win, so the plan pairs
    // them. Following the group, both close in and end within 6" of each other; before the batch (M77) the chainswords
    // fell back, and with the shipped weights they still do (below).
    const GameState gs = side_by_side(22.0f);
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::A);
    ASSERT_EQ(plan.groups.size(), 1u) << describe(gs, plan);
    const PlanGroup& group = plan.groups.front();
    EXPECT_EQ(group.target, kScreamer);
    EXPECT_EQ(plan.find(kChainswords)->partners, std::vector<UnitId>{kBoltguns});

    MoveWeights follow_any;
    follow_any.follow = 0.0;  // every paying group followed
    const GameState together = play_movement_with(gs, follow_any);
    EXPECT_LT(gap_in(together, kChainswords, kScreamer), 22.0f);
    EXPECT_LT(gap_in(together, kBoltguns, kScreamer), 22.0f);
    EXPECT_LE(gap_in(together, kChainswords, kBoltguns), 6.0f);

    // D-083 part 2: Normal follows a group only when its gain is at least a fifth of its risk (MoveWeights::follow,
    // measured). Two 180-point Legionaries units put 2.55 at risk for this +0.13, so today's Normal keeps their
    // pre-batch moves here (an open item for M81's focus fire and joint charges): the chainswords keep away alone.
    // (D-085, M85: this compared the moves with the pre-batch Normal's, Generation::M77, which played them the same.)
    EXPECT_LT(group.gain, MoveWeights{}.follow * group.risk);
    const GameState now = play_movement(gs);
    EXPECT_GT(gap_in(now, kChainswords, kScreamer), 22.0f);
    EXPECT_GT(gap_in(now, kChainswords, kBoltguns), 6.0f);
}

TEST(M80_Normal, ALoneUnitWaitsOutsideTheMonstersChargeReach) {
    // The chainsword Legionaries 16" from the Screamer-killer, the boltgun Legionaries 20" behind them. The plan pairs
    // nothing (the partner can't arrive with them), so they keep away alone: outside the Screamer-killer's likely charge
    // reach after its Normal move.
    GameState gs = rosters_with({kChainswords, kBoltguns, kScreamer, kGuard});
    mf::place_rank(unit(gs, kScreamer), 22.0f, 46.0f);
    const float y = 46.0f - 16.0f - radius_in(gs, kScreamer) - radius_in(gs, kChainswords);
    mf::place_rank(unit(gs, kChainswords), 22.0f, y);
    mf::place_rank(unit(gs, kBoltguns), 22.0f, y - 20.0f - 2.0f * radius_in(gs, kChainswords));
    mf::place_rank(unit(gs, kGuard), 40.0f, 58.0f);
    gs = at_move_units(gs);
    ASSERT_NEAR(gap_in(gs, kChainswords, kBoltguns), 20.0f, 0.05f);
    OddsCache cache;
    EXPECT_TRUE(army_plan(cache, gs, Side::A).groups.empty());
    const analysis::ThreatRanges screamer = analysis::threat_ranges(*gs.find_unit(kScreamer));
    const GameState after = play_movement(gs);
    EXPECT_LT(screamer.p_charge_after_move(in(gap_in(after, kChainswords, kScreamer))), 0.05);

    // Even handed the pair's group (as the plan forms it side by side), the partner 20" back is out of the Screamer-killer's
    // reach, so it can't take the reply: the chainswords' move is the one they make alone.
    const GameState pair = side_by_side(16.0f);
    ArmyPlan group = army_plan(cache, pair, Side::A);
    ASSERT_EQ(group.groups.size(), 1u);
    group.groups.front().gain = group.groups.front().risk;  // as a group Normal follows (MoveWeights::follow)
    for (const int arrival : {0, 2}) {
        for (PlanMember& m : group.groups.front().members) {
            if (m.unit == kBoltguns) m.arrival = arrival;
        }
        Simulator sim(gs, 0);
        ASSERT_TRUE(sim.step(Side::A, action::StartMove{kChainswords, MoveType::Normal, std::nullopt}).ok());
        const std::optional<PlannedMove> together = plan_unit_move(cache, sim.state(), &group);
        const ArmyPlan none = no_plan(sim.state(), Side::A);
        const std::optional<PlannedMove> alone = plan_unit_move(cache, sim.state(), &none);
        ASSERT_TRUE(together && alone);
        EXPECT_EQ(together->candidate.ends, alone->candidate.ends) << together->candidate.label;
        EXPECT_LT(screamer.p_charge_after_move(in(gap_in(together->after, kChainswords, kScreamer))), 0.1);
    }
}

TEST(M80_Normal, FarUnitsCloseInRatherThanDrift) {
    // A Hive Tyrant at 3 wounds and the Genestealers 38" from a lone Legionaries unit: the plan pairs them (M79), each
    // beyond the reach of its next turn's charge. Every move scores about 0 on the line there; the approach term brings
    // both closer. Before the batch (M77) the Genestealers drifted back (D-085, M85: that comparison went with M77).
    const GameState gs = tyrant_pair(38.0f);
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::B);
    ASSERT_EQ(plan.groups.size(), 1u) << describe(gs, plan);
    for (const PlanMember& m : plan.groups.front().members) EXPECT_GE(m.arrival, 2);  // beyond the horizon

    const GameState now = play_movement(gs);
    EXPECT_LT(gap_in(now, kGenestealers, kChainswords), 38.0f - 6.0f);
    EXPECT_LT(gap_in(now, kTyrant, kChainswords), 38.0f - 6.0f);
}

TEST(M80_Normal, AUnitWithoutPartnersMovesAsBefore) {
    // The Legionaries pair on the Screamer-killer, and the Chaos Lord far south-east without partners: its move is the
    // move it makes with no plan at all (the plan is active, but nothing in it is the Lord's).
    const GameState gs = side_by_side(16.0f, /*with_lord=*/true);
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::A);
    ASSERT_TRUE(plan.active);
    ASSERT_FALSE(plan.groups.empty());
    ASSERT_TRUE(plan.find(kLord)->partners.empty());
    for (const MoveType type : {MoveType::Normal, MoveType::Advance}) {
        Simulator sim(gs, 0);
        wh40k::core::ScriptedDice dice{4};
        ASSERT_TRUE(sim.step(Side::A, action::StartMove{kLord, type, std::nullopt}, dice).ok());
        const MoveScoreContext ctx(cache, sim.state(), kLord, MoveWeights{}, &plan);
        EXPECT_EQ(ctx.team(), nullptr);
        const std::optional<PlannedMove> now = plan_unit_move(cache, sim.state());
        const ArmyPlan none = no_plan(sim.state(), Side::A);
        const std::optional<PlannedMove> before = plan_unit_move(cache, sim.state(), &none);
        ASSERT_TRUE(now && before);
        EXPECT_EQ(now->candidate.ends, before->candidate.ends);
        EXPECT_EQ(now->candidate.label, before->candidate.label);
        EXPECT_DOUBLE_EQ(now->score.total, before->score.total);
        EXPECT_EQ(now->steps.size(), before->steps.size());
    }
}

TEST(M80_Normal, TheReasonNamesTheFocusAndPartners) {
    // The Tyrant 14" and the Genestealers 22" from the Legionaries: the Tyrant (the closer) moves first, and its reason
    // names the group's focus and its partner ("Hive Tyrant 2: Normal move 8\" towards Legionaries 1 with Genestealers 1
    // - 6.6\" from Legionaries 1, ...").
    std::vector<std::string> reasons;
    (void)play_movement(tyrant_pair(22.0f, 14.0f), &reasons);
    ASSERT_EQ(reasons.size(), 2u);
    EXPECT_EQ(reasons.front().rfind("Hive Tyrant 2: ", 0), 0u) << reasons.front();
    EXPECT_NE(reasons.front().find(" Legionaries 1 with Genestealers 1"), std::string::npos) << reasons.front();
    EXPECT_NE(reasons.front().find("from Legionaries 1"), std::string::npos) << reasons.front();
    for (const std::string& r : reasons) {
        if (r.find(" with ") != std::string::npos) EXPECT_NE(r.find(" Legionaries 1 with "), std::string::npos) << r;
    }
    // Without partners (no plan) the reasons name no partner. (D-085, M85: this played the pre-batch Normal's moves.)
    const GameState gs = tyrant_pair(22.0f, 14.0f);
    OddsCache cache;
    const ArmyPlan none = no_plan(gs, Side::B);
    const std::optional<PolicyResult> alone =
        decide_move_units(cache, gs, Side::B, legal_actions(gs, Side::B), decision_seed(gs), nullptr, &none);
    ASSERT_TRUE(alone.has_value());
    EXPECT_EQ(alone->reason.find(" with "), std::string::npos) << alone->reason;
}

TEST(M80_Normal, TheDesperateEscapeGuardStillHolds) {
    // M68_Movement.ADesperateEscapeIsMadeOnlyWhenTheLikelyLossesLeaveAMoveThatStands's position (rosters, Normal vs
    // Normal, seed 193, battle round 5: the battle-shocked Chaos Terminators engaged with the Hive Tyrant against the
    // south edge), reached by the pre-batch Normal (the current one plays the game differently; D-085, M85: kept as the
    // fixture tests/data/states/m68_desperate_escape.json): the current Normal doesn't make the escape either (D-077's
    // guard).
    const GameState gs = wh40k::test::load_state_fixture("m68_desperate_escape.json");
    const UnitId terminators{3};
    const GameAction escape = action::StartMove{terminators, MoveType::FallBack, FallBackMode::DesperateEscape};
    ASSERT_TRUE(gs.pending.has_value());
    const Side side = gs.pending->side;
    const std::vector<GameAction> legal = legal_actions(gs, side);
    ASSERT_NE(std::find(legal.begin(), legal.end(), escape), legal.end());
    Bot bot(BotConfig{});
    const BotChoice c = bot.decide(gs, side);
    EXPECT_NE(c.action, escape) << c.reason;
    EXPECT_TRUE(validate_action(gs, side, c.action).ok());
}

// ---- The score's pieces ----

TEST(M80_MoveScore, PartnersVolleysWeakenTheFocusButAreNotThisUnitsValue) {
    ScoreLine line(1.0, 1.0, 1.0, 1.0);
    line.we_hit(0.2);
    const double v = line.value();
    line.allies_hit(0.3);
    EXPECT_DOUBLE_EQ(line.value(), v);
    EXPECT_NEAR(line.them(), 0.5, 1e-12);
    line.allies_hit(2.0);  // capped at what is left
    EXPECT_DOUBLE_EQ(line.them(), 0.0);
    EXPECT_DOUBLE_EQ(line.value(), v);
}

TEST(M80_MoveScore, TheGroupsLineFollowsTheFocus) {
    // The Genestealers 22" and the Tyrant 14" from the Legionaries: the Genestealers' line is the Legionaries' (their
    // focus), the Tyrant synced (it can charge the focus by our next turn), its expected volleys credited, and the
    // Legionaries' reply shared with the Tyrant in their reach (one half each).
    const GameState gs = tyrant_pair(22.0f, 14.0f);
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::B);
    ASSERT_EQ(plan.groups.size(), 1u) << describe(gs, plan);
    Simulator sim(gs, 0);
    ASSERT_TRUE(sim.step(Side::B, action::StartMove{kGenestealers, MoveType::Normal, std::nullopt}).ok());
    const MoveScoreContext ctx(cache, sim.state(), kGenestealers, MoveWeights{}, &plan);
    ASSERT_NE(ctx.team(), nullptr);
    ASSERT_NE(ctx.enemy(), nullptr);
    EXPECT_EQ(ctx.enemy()->id, kChainswords);
    EXPECT_EQ(ctx.team()->focus, kChainswords);
    EXPECT_TRUE(ctx.team()->line);
    EXPECT_TRUE(ctx.supported());
    ASSERT_EQ(ctx.team()->partners.size(), 1u);
    EXPECT_EQ(ctx.team()->partners.front().unit, kTyrant);
    EXPECT_TRUE(ctx.team()->partners.front().synced);
    EXPECT_GT(ctx.team()->support_now + ctx.team()->support_next, 0.0);  // the Tyrant's charge
    EXPECT_NEAR(ctx.reply_share(in(12.0f)), 0.5, 1e-12);
    // Out of the Legionaries' reach the reply isn't shared (they can't reach us anyway).
    EXPECT_DOUBLE_EQ(ctx.reply_share(in(40.0f)), 1.0);
    // The same context computes the same plan itself.
    const MoveScoreContext own(cache, sim.state(), kGenestealers);
    ASSERT_NE(own.team(), nullptr);
    EXPECT_EQ(own.team()->support_next, ctx.team()->support_next);
    // Without the plan (no_plan, as M77 read) the line is the closest enemy unit's and there is no team.
    const ArmyPlan none = no_plan(sim.state(), Side::B);
    const MoveScoreContext old(cache, sim.state(), kGenestealers, MoveWeights{}, &none);
    EXPECT_EQ(old.team(), nullptr);
}

TEST(M80_MoveScore, AUnitWhosePartnersCannotArriveKeepsItsOwnLine) {
    // The Tyrant and the Genestealers 38" from the Legionaries: neither reaches the focus by our next turn, so each keeps
    // its closest enemy unit's line (no support, no shared reply) and only approaches the focus.
    const GameState gs = tyrant_pair(38.0f);
    OddsCache cache;
    Simulator sim(gs, 0);
    ASSERT_TRUE(sim.step(Side::B, action::StartMove{kGenestealers, MoveType::Normal, std::nullopt}).ok());
    const MoveScoreContext ctx(cache, sim.state(), kGenestealers);
    ASSERT_NE(ctx.team(), nullptr);
    EXPECT_FALSE(ctx.team()->line);
    EXPECT_FALSE(ctx.supported());
    EXPECT_EQ(ctx.team()->focus, kChainswords);
    // Closer to the focus scores more beyond the horizon: the approach term.
    const Unit& g = *sim.state().find_unit(kGenestealers);
    std::vector<Position> start;
    std::vector<Position> closer;
    for (const Model& m : g.models) {
        start.push_back(m.position);
        closer.push_back(Position{m.position.x, m.position.y - in(8.0f), 0.0f});
    }
    EXPECT_DOUBLE_EQ(ctx.approach_term(start), 0.0);
    EXPECT_GT(ctx.approach_term(closer), 0.0);
}
