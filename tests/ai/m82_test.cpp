// M82 (D-083 part 4): Hard on the army plan. The army value reads the army plan of the side it values for
// (normal::army_plan at the decision's root; hard::ArmyPlans holds both sides', the rollouts' shooting reads them): a
// group member's queue starts with its group's target, the members that
// start on it and take wounds from it in the attention timeline fight it together (each member's duel with its slice of
// it), so the target's P(wiped) comes from the group's combined pace and the members start on their later targets sooner;
// a unit without a group keeps its queue (distance alone), and so does every enemy unit. Generation::M77 keeps the
// evaluator as it was before the batch. Plus the measurements of D-083 part 4 as disabled diagnostics.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "core/engine/actions.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/state/queries.hpp"
#include "generation.hpp"
#include "hard/evaluator.hpp"
#include "hard/hard_bot.hpp"
#include "normal/army_plan.hpp"
#include "normal/shooting.hpp"
#include "normal/value.hpp"
#include "session.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"
#include "unit_style.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::ai::analysis::OddsCache;
namespace client = wh40k::client;
namespace hard = wh40k::ai::hard;
namespace mf = wh40k::test::monsters;

namespace {

// The roster scenario's units by id (D-076: Side A first): the chainsword Legionaries (1), the Chaos Lord (2), the
// Chaos Terminators (3), the Havocs (4), the boltgun Legionaries (5); Termagants (6), Genestealers (7), a Screamer-killer
// (8), the Tyrant Guard (9), the Hive Tyrant (10), the Tyrannofex (11), the second Screamer-killer (12).
constexpr UnitId kChainswords{1};
constexpr UnitId kHavocs{4};
constexpr UnitId kBoltguns{5};
constexpr UnitId kScreamer{8};
constexpr UnitId kGuard{9};

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

// Started, at the first turn's Move Units decision (the rosters: Side A's).
GameState at_move_units(GameState gs) {
    start_game(gs);
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind);
    });
    return gs;
}

// M79's fixture: the Screamer-killer at (22, 34) and both Legionaries ranks `gap_in` inches from it, edge to edge,
// south-west and south-east; the Tyrant Guard at `guard` (far north-east by default).
GameState screamer_fixture(float gap_in, Position guard = wh40k::test::at(40.0f, 58.0f)) {
    GameState gs = rosters_with({kChainswords, kBoltguns, kScreamer, kGuard});
    mf::place_rank(unit(gs, kScreamer), 22.0f, 34.0f);
    const float half = 6.3f - radius_in(gs, kChainswords);  // a rank of ten 32 mm bases: its end model's centre
    const float c = (gap_in + radius_in(gs, kScreamer) + radius_in(gs, kChainswords)) / 1.41421356f;
    mf::place_rank(unit(gs, kChainswords), 22.0f - c - half, 34.0f - c);
    mf::place_rank(unit(gs, kBoltguns), 22.0f + c + half, 34.0f - c);
    mf::place_rank(unit(gs, kGuard), mm_to_inches(guard.x), mm_to_inches(guard.y));
    return at_move_units(gs);
}

std::string name(const GameState& gs, UnitId id) { return client::unit_display_name(gs, id); }

std::string turn_text(const std::optional<int>& t) { return t ? std::to_string(*t) : std::string("-"); }

// The queue's targets in order with their start turns: "Screamer-killer 1@0 Hive Tyrant 1@2 ...".
std::string queue_text(const GameState& gs, const hard::AttentionQueue& q) {
    std::string s;
    for (const hard::AttentionTarget& t : q.targets) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s@%s(%.1f) ", name(gs, t.unit).c_str(), turn_text(t.start).c_str(), t.dealt);
        s += buf;
    }
    return s;
}

std::vector<UnitId> order_of(const hard::AttentionQueue& q) {
    std::vector<UnitId> out;
    for (const hard::AttentionTarget& t : q.targets) out.push_back(t.unit);
    return out;
}

double p_wiped(const std::vector<hard::UnitFate>& fates, UnitId unit) {
    for (const hard::UnitFate& f : fates) {
        if (f.unit == unit) return f.p_wiped;
    }
    return -1.0;
}

// Prints, at `gs`, both sides' plans and the evaluator of the side to decide with its plan (new) and without (M77, old):
// its groups' targets (wipe turn and P(wiped) old and new, the members' shares, each member alone), the queues that
// changed, and how many queues without a group are unchanged.
void probe(const GameState& gs, hard::ChainCache& chains, int& groups_seen, int& unchanged, int& changed_unassigned) {
    OddsCache cache;
    const Side side = gs.pending->side;
    const hard::ArmyPlans plans = hard::army_plans(cache, gs, Generation::Current);
    const normal::ArmyPlan& plan = plans.sides[side_index(side)];
    const normal::ArmyPlan& other = plans.sides[side_index(opponent(side))];
    const hard::Evaluator old_eval(cache, gs, side, hard::ChainWeights{}, &chains, false, Generation::M77);
    const hard::Evaluator new_eval(cache, gs, side, hard::ChainWeights{}, &chains, false, Generation::Current, &plans);
    if (!new_eval.army()) return;
    const hard::Attention o = old_eval.attention(gs);
    const hard::Attention n = new_eval.attention(gs);
    const std::vector<hard::UnitFate> fo = old_eval.fates(gs);
    const std::vector<hard::UnitFate> fn = new_eval.fates(gs);
    std::printf("== R%d %c: plan [%s] (the other side's [%s]); score old %+.3f new %+.3f\n", gs.flow.battle_round,
                side == Side::A ? 'A' : 'B', normal::describe(gs, plan).c_str(), normal::describe(gs, other).c_str(),
                old_eval.evaluate(gs).score, new_eval.evaluate(gs).score);
    for (const hard::GroupPace& g : n.groups) {
        ++groups_seen;
        std::string members;
        for (std::size_t i = 0; i < g.members.size(); ++i) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "%s %.2f, ", name(gs, g.members[i]).c_str(), g.shares[i]);
            members += buf;
        }
        std::printf("   group on %s: %swipe turn old %s new %s; P(wiped) old %.3f new %.3f (group %.3f, those members "
                    "apart %.3f)\n",
                    name(gs, g.target).c_str(), members.c_str(), turn_text(o.wiped(g.target)).c_str(),
                    turn_text(n.wiped(g.target)).c_str(), p_wiped(fo, g.target), p_wiped(fn, g.target), g.p_wiped,
                    g.p_apart);
        // Each member alone (the other members' models removed): the target's wipe turn.
        for (const UnitId keep : g.members) {
            GameState alone = gs;
            for (const UnitId other_member : g.members) {
                if (other_member != keep) alone.find_unit(other_member)->models.clear();
            }
            OddsCache c2;
            const hard::Evaluator e2(c2, alone, side, hard::ChainWeights{}, &chains, false, Generation::Current, &plans);
            if (!e2.army()) continue;
            const hard::Attention a2 = e2.attention(alone);
            const hard::AttentionQueue* q2 = a2.queue_of(keep);
            std::printf("      %s alone: wipe turn %s, P(wiped) %.3f, its queue %s\n", name(gs, keep).c_str(),
                        turn_text(a2.wiped(g.target)).c_str(), p_wiped(e2.fates(alone), g.target),
                        q2 != nullptr ? queue_text(alone, *q2).c_str() : "?");
        }
    }
    for (const hard::AttentionQueue& qn : n.queues) {
        const hard::AttentionQueue* qo = o.queue_of(qn.attacker);
        if (qo == nullptr) continue;
        if (qn.focus) {
            std::printf("   %s (focus %s): old %s\n      new %s\n", name(gs, qn.attacker).c_str(),
                        name(gs, *qn.focus).c_str(), queue_text(gs, *qo).c_str(), queue_text(gs, qn).c_str());
        } else if (order_of(qn) == order_of(*qo)) {
            ++unchanged;
        } else {
            ++changed_unassigned;
            std::printf("   UNASSIGNED QUEUE CHANGED %s: old %s new %s\n", name(gs, qn.attacker).c_str(),
                        queue_text(gs, *qo).c_str(), queue_text(gs, qn).c_str());
        }
    }
}

}  // namespace

// ---- The queues ----

TEST(M82_Evaluator, AUnitsQueueFollowsTheArmyPlan) {
    // Both Legionaries units 12" from a Screamer-killer, the Tyrant Guard close to the boltgun unit (its closest enemy
    // unit). The plan pairs them on the Screamer-killer, so the boltguns' queue starts with it (then the Guard, by
    // distance); under M77 (distance alone) the Guard comes first.
    const GameState gs = screamer_fixture(12.0f, wh40k::test::at(44.0f, 30.0f));
    OddsCache cache;
    const hard::ArmyPlans plans = hard::army_plans(cache, gs, Generation::Current);
    const normal::ArmyPlan& plan = plans.sides[0];
    EXPECT_EQ(plan, normal::army_plan(cache, gs, Side::A));
    const normal::PlanGroup* g = plan.group_of(kBoltguns);
    ASSERT_NE(g, nullptr) << normal::describe(gs, plan);
    EXPECT_EQ(g->target, kScreamer);
    ASSERT_NE(g->member(kChainswords), nullptr);
    ASSERT_EQ(normal::closest_enemy(gs, *gs.find_unit(kBoltguns))->id, kGuard);
    const hard::Evaluator now(cache, gs, Side::A, hard::ChainWeights{}, nullptr, false, Generation::Current, &plans);
    const hard::Evaluator m77(cache, gs, Side::A, hard::ChainWeights{}, nullptr, false, Generation::M77);
    EXPECT_EQ(now.plan(), plan);
    // Without a plan given, the evaluator works out the same one at its root.
    EXPECT_EQ(hard::Evaluator(cache, gs, Side::A, hard::ChainWeights{}).plan(), plan);
    const hard::Attention a = now.attention(gs);
    const hard::Attention o = m77.attention(gs);
    for (const UnitId l : {kChainswords, kBoltguns}) {
        const hard::AttentionQueue* q = a.queue_of(l);
        ASSERT_NE(q, nullptr);
        EXPECT_EQ(q->focus, kScreamer);
        EXPECT_EQ(order_of(*q), (std::vector<UnitId>{kScreamer, kGuard})) << queue_text(gs, *q);
        EXPECT_EQ(q->targets.front().start, 0);
        EXPECT_GT(q->targets.front().dealt, 0.0);
    }
    const hard::AttentionQueue* old_boltguns = o.queue_of(kBoltguns);
    ASSERT_NE(old_boltguns, nullptr);
    EXPECT_FALSE(old_boltguns->focus.has_value());
    EXPECT_EQ(order_of(*old_boltguns), (std::vector<UnitId>{kGuard, kScreamer})) << queue_text(gs, *old_boltguns);
    // Worked on by both from the start, the Screamer-killer falls in the timeline sooner than under M77.
    ASSERT_TRUE(a.wiped(kScreamer).has_value());
    ASSERT_TRUE(o.wiped(kScreamer).has_value());
    EXPECT_LT(*a.wiped(kScreamer), *o.wiped(kScreamer));
}

TEST(M82_Evaluator, AGroupWipesItsTargetSooner) {
    // Both Legionaries units 12" from the Screamer-killer, the Tyrant Guard far away: the plan's pair. Together they wipe
    // it out a turn sooner in the attention timeline than either alone (the other unit's models removed), its P(wiped)
    // by their slices is well above what their duels with the whole monster give (independent, as without the plan) and
    // above either alone, and the members start on their next target (the Guard) sooner.
    const GameState gs = screamer_fixture(12.0f);
    OddsCache cache;
    const hard::ArmyPlans plans = hard::army_plans(cache, gs, Generation::Current);
    ASSERT_EQ(plans.sides[0].groups.size(), 1u) << normal::describe(gs, plans.sides[0]);
    const hard::Evaluator eval(cache, gs, Side::A, hard::ChainWeights{}, nullptr, false, Generation::Current, &plans);
    const hard::Attention a = eval.attention(gs);
    const hard::GroupPace* g = a.group_on(kScreamer);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->members, (std::vector<UnitId>{kChainswords, kBoltguns}));
    ASSERT_EQ(g->shares.size(), 2u);
    EXPECT_NEAR(g->shares[0] + g->shares[1], 1.0, 1e-12);
    for (const double share : g->shares) EXPECT_GT(share, 0.1);
    const std::optional<int> together = a.wiped(kScreamer);
    ASSERT_TRUE(together.has_value());
    const double p_together = p_wiped(eval.fates(gs), kScreamer);
    EXPECT_NEAR(p_together, g->p_wiped, 1e-12);  // the group's slices are all its attackers' duels
    EXPECT_GT(g->p_wiped, 2.0 * g->p_apart) << g->p_wiped << " against " << g->p_apart;
    for (const UnitId keep : {kChainswords, kBoltguns}) {
        GameState alone = gs;
        alone.find_unit(keep == kChainswords ? kBoltguns : kChainswords)->models.clear();
        OddsCache c2;
        const hard::Evaluator e2(c2, alone, Side::A, hard::ChainWeights{}, nullptr, false, Generation::Current, &plans);
        const hard::Attention a2 = e2.attention(alone);
        EXPECT_TRUE(a2.groups.empty());  // one member left: no group
        const std::optional<int> by_one = a2.wiped(kScreamer);
        EXPECT_TRUE(!by_one || *together < *by_one) << name(gs, keep);
        EXPECT_GT(p_together, p_wiped(e2.fates(alone), kScreamer)) << name(gs, keep);
        // Its next target, the Tyrant Guard, starts later alone than with the group.
        const hard::AttentionQueue* q = a.queue_of(keep);
        const hard::AttentionQueue* q2 = a2.queue_of(keep);
        ASSERT_TRUE(q != nullptr && q2 != nullptr);
        ASSERT_EQ(q->targets.back().unit, kGuard);
        ASSERT_EQ(q2->targets.back().unit, kGuard);
        ASSERT_TRUE(q->targets.back().start.has_value());
        EXPECT_TRUE(!q2->targets.back().start || *q->targets.back().start < *q2->targets.back().start)
            << queue_text(gs, *q) << " / " << queue_text(alone, *q2);
    }
}

TEST(M82_Evaluator, AUnitWithoutAGroupKeepsItsQueue) {
    // The enemy's units (no plan of theirs is read) keep their queues, order and start turns, though our pair changes
    // its own. With no group (16" with the Guard near: no group pays) the whole value is M77's to the bit. And at the
    // rosters' first Move Units decision, valued for the Tyranids (whose plan has groups there), every unit without one
    // keeps its M77 queue.
    {
        const GameState gs = screamer_fixture(12.0f, wh40k::test::at(44.0f, 30.0f));
        OddsCache cache;
        const hard::ArmyPlans plans = hard::army_plans(cache, gs, Generation::Current);
        ASSERT_FALSE(plans.sides[0].groups.empty());
        const hard::Evaluator now(cache, gs, Side::A, hard::ChainWeights{}, nullptr, false, Generation::Current, &plans);
        const hard::Evaluator m77(cache, gs, Side::A, hard::ChainWeights{}, nullptr, false, Generation::M77);
        const hard::Attention a = now.attention(gs);
        const hard::Attention o = m77.attention(gs);
        for (const UnitId b : {kScreamer, kGuard}) {
            const hard::AttentionQueue* q = a.queue_of(b);
            const hard::AttentionQueue* qo = o.queue_of(b);
            ASSERT_TRUE(q != nullptr && qo != nullptr);
            EXPECT_FALSE(q->focus.has_value());
            EXPECT_EQ(order_of(*q), order_of(*qo));
            for (std::size_t k = 0; k < q->targets.size(); ++k) EXPECT_EQ(q->targets[k].start, qo->targets[k].start);
        }
    }
    {
        const GameState gs = screamer_fixture(16.0f, wh40k::test::at(44.0f, 30.0f));
        OddsCache cache;
        const hard::ArmyPlans plans = hard::army_plans(cache, gs, Generation::Current);
        ASSERT_TRUE(plans.sides[0].active);
        ASSERT_TRUE(plans.sides[0].groups.empty()) << normal::describe(gs, plans.sides[0]);
        const hard::Evaluator now(cache, gs, Side::A, hard::ChainWeights{}, nullptr, false, Generation::Current, &plans);
        const hard::Evaluator m77(cache, gs, Side::A, hard::ChainWeights{}, nullptr, false, Generation::M77);
        const hard::Evaluation e = now.evaluate(gs);
        const hard::Evaluation e77 = m77.evaluate(gs);
        EXPECT_EQ(e.score, e77.score);
        EXPECT_EQ(e.p_win, e77.p_win);
        EXPECT_EQ(e.p_loss, e77.p_loss);
        const hard::Attention a = now.attention(gs);
        const hard::Attention o = m77.attention(gs);
        ASSERT_EQ(a.queues.size(), o.queues.size());
        for (std::size_t i = 0; i < a.queues.size(); ++i) EXPECT_EQ(order_of(a.queues[i]), order_of(o.queues[i]));
    }
    const GameState start = at_move_units(wh40k::test::load_rosters_scenario());
    OddsCache cache;
    hard::ChainCache chains;
    const hard::ArmyPlans plans = hard::army_plans(cache, start, Generation::Current);
    const normal::ArmyPlan& plan = plans.sides[1];
    const hard::Evaluator now(cache, start, Side::B, hard::ChainWeights{}, &chains, false, Generation::Current, &plans);
    const hard::Evaluator m77(cache, start, Side::B, hard::ChainWeights{}, &chains, false, Generation::M77);
    const hard::Attention a = now.attention(start);
    const hard::Attention o = m77.attention(start);
    std::size_t without = 0;
    std::size_t with = 0;
    for (const hard::AttentionQueue& q : a.queues) {
        const hard::AttentionQueue* qo = o.queue_of(q.attacker);
        ASSERT_NE(qo, nullptr);
        if (q.focus) {
            ++with;
            EXPECT_EQ(start.find_unit(q.attacker)->side, Side::B);
            EXPECT_EQ(q.targets.front().unit, *q.focus);
            continue;
        }
        ++without;
        EXPECT_EQ(order_of(q), order_of(*qo)) << name(start, q.attacker);
    }
    EXPECT_GT(with, 0u) << normal::describe(start, plan);
    EXPECT_GT(without, 0u);
}

// ---- Hard's decisions and the rollouts ----

TEST(M82_Hard, TheMoveValuesReadTheArmyPlan) {
    // Hard's own values of the move types at Move Units (the chain, the lookahead off for speed) read the deciding side's
    // plan: where the Legionaries pair on the Screamer-killer (it can wipe it out together) Remain Stationary is worth more
    // than under M77; where no group pays every value is M77's to the bit.
    const auto values = [](const GameState& gs, Generation g) {
        hard::HardContext hc;
        hc.generation = g;
        hc.weights.lookahead.share = 0.0;
        OddsCache cache;
        hard::HardTypeValues v;
        const std::vector<GameAction> legal = legal_actions(gs, Side::A);
        EXPECT_TRUE(hard::hard_move_units(hc, cache, gs, Side::A, legal, 1, &v).has_value());
        return v;
    };
    {
        const GameState gs = screamer_fixture(12.0f);
        const hard::HardTypeValues now = values(gs, Generation::Current);
        const hard::HardTypeValues old = values(gs, Generation::M77);
        ASSERT_TRUE(now.remain_stationary && old.remain_stationary);
        EXPECT_GT(*now.remain_stationary, *old.remain_stationary + 0.01);  // -0.99 against -1.07 measured
    }
    {
        const GameState gs = screamer_fixture(16.0f, wh40k::test::at(44.0f, 30.0f));
        OddsCache cache;
        ASSERT_TRUE(normal::army_plan(cache, gs, Side::A).groups.empty());
        const hard::HardTypeValues now = values(gs, Generation::Current);
        const hard::HardTypeValues old = values(gs, Generation::M77);
        EXPECT_EQ(now.remain_stationary, old.remain_stationary);
        EXPECT_EQ(now.normal, old.normal);
        EXPECT_EQ(now.advance, old.advance);
    }
}

TEST(M82_Rollouts, TheShootingReadsTheGivenPlan) {
    // The rollouts' shooting reads the decision's plan instead of working one out (D-083 part 4): given the state's own
    // plan, Normal's shooting chooses exactly what it chooses working it out; another plan of that side is read (an
    // inactive one has no focus); another side's plan is ignored.
    // M81's focus-fire fixture: the chainsword Legionaries engaged with a Screamer-killer at the north edge, the boltgun
    // Legionaries 9.4" from it (the pair a group, the engaged unit its seed), the Havocs at the south edge and the Tyrant
    // Guard between; Side A's first Shoot decision, every unit having remained stationary.
    GameState gs = rosters_with({kChainswords, kHavocs, kBoltguns, kScreamer, kGuard});
    const float rs = radius_in(gs, kScreamer);
    const float rl = radius_in(gs, kChainswords);
    mf::place_rank(unit(gs, kScreamer), 22.0f, 52.0f);
    mf::place_rank(unit(gs, kChainswords), 22.0f, 52.0f - rs - rl - 1.0f);
    mf::place_rank(unit(gs, kBoltguns), 36.0f, 52.0f - rs - rl - 6.0f);
    mf::place_rank(unit(gs, kHavocs), 22.0f, 1.5f);
    mf::place_rank(unit(gs, kGuard), 10.0f, 24.0f);
    gs.first_turn = Side::A;
    start_game(gs);
    for (int guard = 0; guard < 200 && gs.pending && !std::holds_alternative<decision::Shoot>(gs.pending->kind);
         ++guard) {
        const Side d = gs.pending->side;
        const std::vector<GameAction> legal = legal_actions(gs, d);
        GameAction a = legal.front();
        for (const GameAction& l : legal) {
            const auto* m = std::get_if<action::StartMove>(&l);
            if (m != nullptr && m->move_type == MoveType::RemainStationary) a = l;
        }
        apply_action(gs, d, a);
    }
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::Shoot>(gs.pending->kind));
    const std::vector<GameAction> legal = legal_actions(gs, Side::A);
    OddsCache cache;
    const normal::ArmyPlan own = normal::army_plan(cache, gs, Side::A);
    ASSERT_FALSE(own.groups.empty()) << normal::describe(gs, own);
    const std::optional<normal::ShootingChoice> worked_out = normal::choose_shooting(cache, gs, legal);
    const std::optional<normal::ShootingChoice> given = normal::choose_shooting(cache, gs, legal, Generation::Current, &own);
    ASSERT_TRUE(worked_out && given);
    EXPECT_EQ(given->unit, worked_out->unit);
    EXPECT_EQ(given->shots, worked_out->shots);
    EXPECT_EQ(given->value, worked_out->value);
    EXPECT_EQ(given->notes, worked_out->notes);
    EXPECT_TRUE(normal::ShootingAim(cache, gs, Side::A, Generation::Current, &own).focus(kScreamer));
    normal::ArmyPlan inactive;
    inactive.side = Side::A;
    EXPECT_FALSE(normal::ShootingAim(cache, gs, Side::A, Generation::Current, &inactive).focus(kScreamer));
    normal::ArmyPlan theirs = normal::army_plan(cache, gs, Side::B);
    EXPECT_TRUE(normal::ShootingAim(cache, gs, Side::A, Generation::Current, &theirs).focus(kScreamer));
}

// ---- The regression switch ----

TEST(M82_Regression, TheM77SwitchKeepsTheOldEvaluator) {
    // Under Generation::M77 the evaluator works out no plan and reads none: its values are those of an evaluator given an
    // inactive plan, at every Move Units and Shoot decision of two battle rounds of a rosters game (Normal on both
    // sides), where the current evaluator's differ once groups form.
    hard::ChainCache chains;
    GameState gs = at_move_units(wh40k::test::load_rosters_scenario());
    const hard::ArmyPlans none;  // inactive
    int differ = 0;
    int checked = 0;
    for (int step = 0; step < 400 && gs.pending && !gs.result && gs.flow.battle_round <= 2; ++step) {
        if (std::holds_alternative<decision::MoveUnits>(gs.pending->kind) ||
            std::holds_alternative<decision::Shoot>(gs.pending->kind)) {
            OddsCache c;
            const hard::Evaluator old_eval(c, gs, Side::A, hard::ChainWeights{}, &chains, false, Generation::M77);
            const hard::Evaluator plain(c, gs, Side::A, hard::ChainWeights{}, &chains, false, Generation::Current, &none);
            const hard::Evaluator current(c, gs, Side::A, hard::ChainWeights{}, &chains, false, Generation::Current);
            EXPECT_FALSE(old_eval.plan().active);
            EXPECT_FALSE(plain.plan().active);
            const hard::Evaluation e = old_eval.evaluate(gs);
            EXPECT_EQ(e.score, plain.evaluate(gs).score);
            EXPECT_EQ(e.p_win, plain.evaluate(gs).p_win);
            if (current.evaluate(gs).score != e.score) ++differ;
            ++checked;
        }
        const Side side = gs.pending->side;
        BotConfig normal_config;
        normal_config.level = BotLevel::Normal;
        const BotChoice c = Bot(normal_config).decide(gs, side);
        ASSERT_FALSE(c.stopped);
        (void)apply_action(gs, side, c.action);
    }
    EXPECT_GT(checked, 10);
    EXPECT_GT(differ, 0);
}

// ---- Diagnostics (D-083 part 4's measurements; run by hand in a Release build) ----

TEST(M82_Diagnostics, DISABLED_ProbeQueuesAndWipeTurns) {
    // Hard (side A) against Normal (side B) on the rosters, seeds 1-3 (M82_PROBE_SEEDS=n for more): at each side's first
    // Move Units decision of battle rounds 1-3, the evaluator's queues and wipe turns with the plan and without (M77).
    const int seeds = std::getenv("M82_PROBE_SEEDS") != nullptr ? std::atoi(std::getenv("M82_PROBE_SEEDS")) : 3;
    hard::ChainCache chains;
    int groups_seen = 0;
    int unchanged = 0;
    int changed_unassigned = 0;
    for (int seed = 1; seed <= seeds; ++seed) {
        client::GameSession session = client::GameSession::new_game(wh40k::test::load_rosters_scenario(), seed);
        BotConfig a;
        a.level = BotLevel::Hard;
        BotConfig b;
        b.level = BotLevel::Normal;
        (void)play_bot_session(session, a, b, 5000);
        std::printf("##### seed %d\n", seed);
        client::GameSession replay(session.initial());
        int last_turn = -1;
        for (const ReplayStep& step : session.history()) {
            const GameState& gs = replay.state();
            const int turn = (gs.flow.battle_round - 1) * 2 + gs.flow.turn_in_round;
            if (gs.pending && std::holds_alternative<decision::MoveUnits>(gs.pending->kind) && gs.flow.battle_round <= 3 &&
                turn != last_turn) {
                last_turn = turn;
                probe(gs, chains, groups_seen, unchanged, changed_unassigned);
            }
            ASSERT_TRUE(replay.submit(step.action).ok());
        }
    }
    std::printf("groups %d; queues without a group unchanged %d, changed %d\n", groups_seen, unchanged,
                changed_unassigned);
}

TEST(M82_Diagnostics, DISABLED_ScreamerFixture) {
    // The fixture of the tests above at 12", 16" and 20", the Tyrant Guard far away, near the boltguns, or near the
    // chainswords.
    for (const float gap : {12.0f, 16.0f, 20.0f}) {
        for (const Position guard :
             {wh40k::test::at(40.0f, 58.0f), wh40k::test::at(44.0f, 30.0f), wh40k::test::at(4.0f, 30.0f)}) {
            const GameState gs = screamer_fixture(gap, guard);
            hard::ChainCache chains;
            int a = 0;
            int b = 0;
            int c = 0;
            std::printf("##### gap %.0f guard (%.0f, %.0f)\n", gap, mm_to_inches(guard.x), mm_to_inches(guard.y));
            probe(gs, chains, a, b, c);
        }
    }
}

TEST(M82_Diagnostics, DISABLED_CalibrationAgainstResults) {
    // How well the army value predicts the result, with side A's plan (new) and without (M77, old): Normal against Normal
    // (M82_CALIB_GAMES games, default 40, from seed M82_CALIB_FIRST; M82_CALIB_RANDOM=1 for random armies, else the
    // rosters), at each half-turn's first Move Units decision, side A's V = P(win) - P(loss) against the game's result
    // (+1, 0, -1): the mean squared error by battle round.
    const int games = std::getenv("M82_CALIB_GAMES") != nullptr ? std::atoi(std::getenv("M82_CALIB_GAMES")) : 40;
    const bool random = std::getenv("M82_CALIB_RANDOM") != nullptr && std::atoi(std::getenv("M82_CALIB_RANDOM")) != 0;
    const int first = std::getenv("M82_CALIB_FIRST") != nullptr ? std::atoi(std::getenv("M82_CALIB_FIRST")) : 1;
    const ArmySource source = load_army_source(wh40k::test::repo_root());
    hard::ChainCache chains;
    struct Acc {
        double n = 0.0, se_old = 0.0, se_new = 0.0, v_old = 0.0, v_new = 0.0, r = 0.0;
    };
    std::array<Acc, 6> by_round{};
    for (int seed = first; seed < first + games; ++seed) {
        const GameState scenario = random ? random_armies_battle(source, static_cast<std::uint64_t>(seed), Side::A)
                                          : wh40k::test::load_rosters_scenario();
        client::GameSession session = client::GameSession::new_game(scenario, seed);
        BotConfig a;
        a.level = BotLevel::Normal;
        const BotConfig b = a;
        const GameRecord rec = play_bot_session(session, a, b, 5000);
        if (!rec.finished) continue;
        const double result = !rec.winner ? 0.0 : (*rec.winner == Side::A ? 1.0 : -1.0);
        client::GameSession replay(session.initial());
        int last_turn = -1;
        for (const ReplayStep& step : session.history()) {
            const GameState& gs = replay.state();
            const int turn = (gs.flow.battle_round - 1) * 2 + gs.flow.turn_in_round;
            if (gs.pending && std::holds_alternative<decision::MoveUnits>(gs.pending->kind) && turn != last_turn &&
                gs.flow.battle_round >= 1 && gs.flow.battle_round <= 5) {
                last_turn = turn;
                OddsCache cache;
                const hard::Evaluator old_eval(cache, gs, Side::A, hard::ChainWeights{}, &chains, false, Generation::M77);
                const hard::Evaluator new_eval(cache, gs, Side::A, hard::ChainWeights{}, &chains, false,
                                               Generation::Current);
                if (new_eval.army()) {
                    const hard::Evaluation eo = old_eval.evaluate(gs);
                    const hard::Evaluation en = new_eval.evaluate(gs);
                    Acc& acc = by_round[static_cast<std::size_t>(gs.flow.battle_round)];
                    acc.n += 1.0;
                    acc.se_old += (eo.v - result) * (eo.v - result);
                    acc.se_new += (en.v - result) * (en.v - result);
                    acc.v_old += eo.v;
                    acc.v_new += en.v;
                    acc.r += result;
                }
            }
            ASSERT_TRUE(replay.submit(step.action).ok());
        }
    }
    double n = 0.0;
    double so = 0.0;
    double sn = 0.0;
    for (std::size_t r = 1; r < by_round.size(); ++r) {
        const Acc& acc = by_round[r];
        if (acc.n == 0.0) continue;
        std::printf("round %zu: %3.0f positions; MSE old %.3f new %.3f; mean V old %+.3f new %+.3f; mean result %+.3f\n",
                    r, acc.n, acc.se_old / acc.n, acc.se_new / acc.n, acc.v_old / acc.n, acc.v_new / acc.n,
                    acc.r / acc.n);
        n += acc.n;
        so += acc.se_old;
        sn += acc.se_new;
    }
    std::printf("all: %.0f positions; MSE old %.4f new %.4f\n", n, so / n, sn / n);
}

TEST(M82_Diagnostics, DISABLED_TimeTheDecisions) {
    // Hard's decision times (each bot decision of side A at Move Units, Charge and casualty decisions of rounds 1-3 of a
    // Hard vs Normal rosters game, seed M82_TIME_SEED, default 1), the pre-batch evaluator (M77's plan: none, the M81
    // rollouts) against the current one.
    const int seed = std::getenv("M82_TIME_SEED") != nullptr ? std::atoi(std::getenv("M82_TIME_SEED")) : 1;
    client::GameSession session = client::GameSession::new_game(wh40k::test::load_rosters_scenario(), seed);
    BotConfig a;
    a.level = BotLevel::Hard;
    BotConfig b;
    b.level = BotLevel::Normal;
    (void)play_bot_session(session, a, b, 5000);
    std::vector<GameState> states;
    client::GameSession replay(session.initial());
    for (const ReplayStep& step : session.history()) {
        const GameState& gs = replay.state();
        if (gs.pending && gs.pending->side == Side::A && gs.flow.battle_round <= 3 &&
            (std::holds_alternative<decision::MoveUnits>(gs.pending->kind) ||
             std::holds_alternative<decision::Charge>(gs.pending->kind) ||
             std::holds_alternative<decision::OrderCasualties>(gs.pending->kind))) {
            states.push_back(gs);
        }
        ASSERT_TRUE(replay.submit(step.action).ok());
    }
    for (const Generation g : {Generation::M77, Generation::Current}) {
        BotConfig h;
        h.level = BotLevel::Hard;
        h.generation = g;
        Bot bot(h);
        (void)bot.decide(states.front(), Side::A);  // the chain tables built once
        std::vector<double> ms;
        for (const GameState& gs : states) {
            bot.forget();
            const auto t0 = std::chrono::steady_clock::now();
            (void)bot.decide(gs, Side::A);
            ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        std::vector<double> sorted = ms;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0.0;
        for (const double m : ms) sum += m;
        std::printf("%-8s %zu decisions: mean %.1f ms, p95 %.1f ms, max %.1f ms\n",
                    g == Generation::M77 ? "hard-m77" : "hard", ms.size(), sum / static_cast<double>(ms.size()),
                    sorted[sorted.size() * 95 / 100], sorted.back());
    }
}
