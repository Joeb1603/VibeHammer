// M42: "Auto move" for charge moves (D-056): 11.04, 03.01, 03.03, 03.04.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

#include "auto_charge_move.hpp"
#include "auto_fight_move.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/state/queries.hpp"
#include "fight_move_skip.hpp"
#include "session.hpp"
#include "support/charge_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "undo_groups.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::Charger;
using wh40k::test::ChargeLayout;
using wh40k::test::in;

namespace {

// The M39 baseline (level Easy's charge move, since M85 the bot's default charge move, D-085): the unit translated towards the target's centre, 1" short of it;
// models furthest along first (D-027), a refused proposal leaves that model where it is. Its score if the move
// could end, else nullopt.
std::optional<ChargeArrangementScore> baseline_translation(GameState gs) {
    const Unit& u = *gs.find_unit(gs.flow.move->unit);
    const Unit& t = *gs.find_unit(gs.flow.move->targets.front());
    float ax = 0, ay = 0, bx = 0, by = 0;
    for (const Model& m : u.models) ax += m.position.x, ay += m.position.y;
    for (const Model& m : t.models) bx += m.position.x, by += m.position.y;
    ax /= static_cast<float>(u.models.size());
    ay /= static_cast<float>(u.models.size());
    bx /= static_cast<float>(t.models.size());
    by /= static_cast<float>(t.models.size());
    const float len = std::hypot(bx - ax, by - ay);
    const float dist = std::max(0.0f, *unit_distance(u, t) - in(1.0f));
    const float dx = (bx - ax) / len * dist;
    const float dy = (by - ay) / len * dist;
    std::vector<const Model*> order;
    for (const Model& m : u.models) order.push_back(&m);
    std::stable_sort(order.begin(), order.end(), [&](const Model* a, const Model* b) {
        return a->position.x * dx + a->position.y * dy > b->position.x * dx + b->position.y * dy;
    });
    std::vector<GameAction> moves;
    for (const Model* m : order) {
        moves.push_back(action::ProposeModelMove{m->id, {Position{m->position.x + dx, m->position.y + dy, 0}}});
    }
    for (const GameAction& a : moves) {
        if (validate_action(gs, Side::A, a).ok()) apply_action(gs, Side::A, a);
    }
    if (!move_end_problems(gs).empty()) return std::nullopt;
    return charge_arrangement_score(gs);
}

// Applies the plan on a copy: every proposal is valid, the move can end, every moved model ends closer to a
// charge target, and the commit is not reverted. Returns the score after the moves.
ChargeArrangementScore apply_checked(const GameState& gs, const AutoChargeMove& plan, const std::string& where) {
    EXPECT_TRUE(plan.ok()) << where << ": " << (plan.problems.empty() ? "" : plan.problems.front());
    GameState work = gs;
    for (const GameAction& a : plan.moves) {
        const auto v = validate_action(work, Side::A, a);
        EXPECT_TRUE(v.ok()) << where << ": " << (v.reasons.empty() ? "" : v.reasons.front());
        apply_action(work, Side::A, a);
    }
    EXPECT_TRUE(move_end_problems(work).empty()) << where;
    const ChargeArrangementScore score = charge_arrangement_score(work);
    EXPECT_EQ(score, plan.score) << where;
    const auto events = apply_action(work, Side::A, action::CommitUnitMove{});
    for (const GameEvent& e : events) EXPECT_FALSE(std::holds_alternative<event::UnitMoveReverted>(e)) << where;
    EXPECT_TRUE(work.units[0].turn_flags.made_charge_move || work.units[1].turn_flags.made_charge_move) << where;
    return score;
}

std::string label(Charger c, const ChargeLayout& l, int roll) {
    char buf[128];
    std::snprintf(buf, sizeof buf, "%s gap %.1f\" offset %.1f\" ranks %d roll %d",
                  c == Charger::Intercessors ? "Intercessors" : "Termagants", l.gap_in, l.offset_in, l.ranks, roll);
    return buf;
}

struct GridTotals {
    int cases = 0;
    int planned = 0;
    int baseline_legal = 0;
    int more_engaged = 0;
};

void print_totals(const GridTotals& t) {
    std::printf("%d cases, %d planned, the baseline stands in %d, more models engaged than it in %d\n", t.cases,
                t.planned, t.baseline_legal, t.more_engaged);
}

// Every gap, offset, formation and roll in the grid for which the target is an eligible charge target.
void run_grid(Charger c, const std::vector<int>& ranks, GridTotals& totals) {
    for (const float gap : {2.5f, 4.5f, 6.5f, 8.5f, 10.5f, 11.5f}) {
        for (const float offset : {0.0f, 3.0f, 7.0f}) {
            for (const int r : ranks) {
                const ChargeLayout layout{gap, offset, r};
                for (int roll = 2; roll <= 12; ++roll) {
                    const std::optional<GameState> gs = wh40k::test::charge_move_state(c, layout, roll);
                    if (!gs) continue;
                    ++totals.cases;
                    const std::string where = label(c, layout, roll);
                    ASSERT_TRUE(auto_charge_move_available(*gs)) << where;
                    const AutoChargeMove plan = auto_charge_move(*gs);
                    const std::optional<ChargeArrangementScore> base = baseline_translation(*gs);
                    if (base) {
                        ++totals.baseline_legal;
                        ASSERT_TRUE(plan.ok()) << where << ": the baseline stands but auto move found nothing ("
                                               << (plan.problems.empty() ? "" : plan.problems.front()) << ")";
                    }
                    if (!plan.ok()) continue;
                    ++totals.planned;
                    const ChargeArrangementScore score = apply_checked(*gs, plan, where);
                    if (base) {
                        EXPECT_GE(score.engaged, base->engaged) << where;
                        if (score.engaged > base->engaged) ++totals.more_engaged;
                    }
                }
            }
        }
    }
}

}  // namespace

// ==== Acceptance: a grid of positions and rolls, both units charging ====

TEST(M42_AutoChargeMove, IntercessorsChargingTermagantsAcrossAGridOfGapsOffsetsFormationsAndRolls) {
    GridTotals t;
    run_grid(Charger::Intercessors, {0, 2}, t);
    print_totals(t);
    EXPECT_GT(t.cases, 100);
    EXPECT_EQ(t.planned, t.cases);  // every eligible roll has a legal charge move here
    EXPECT_GT(t.more_engaged, 0);
}

TEST(M42_AutoChargeMove, TermagantsChargingIntercessorsAcrossAGridOfGapsOffsetsFormationsAndRolls) {
    GridTotals t;
    run_grid(Charger::Termagants, {0, 1}, t);
    print_totals(t);
    EXPECT_GT(t.cases, 100);
    EXPECT_EQ(t.planned, t.cases);
    EXPECT_GT(t.more_engaged, 0);  // the back rank is brought in, not left 2.1" behind
}

// ==== 11.04 priorities ====

TEST(M42_AutoChargeMove, AHeadOnChargeWithRollToSpareEngagesEveryModelAndPutsMostInBaseContact) {
    const auto gs = wh40k::test::charge_move_state(Charger::Intercessors, ChargeLayout{4.0f, 0.0f, 0}, 9);
    ASSERT_TRUE(gs.has_value());
    const AutoChargeMove plan = auto_charge_move(*gs);
    const ChargeArrangementScore s = apply_checked(*gs, plan, "head on");
    EXPECT_EQ(s.engaged, 5);
    EXPECT_EQ(s.within_1, 5);
    EXPECT_GE(s.contacts, 3);
}

TEST(M42_AutoChargeMove, TheTermagantsBackRankEndsEngagedToo) {
    const auto gs = wh40k::test::charge_move_state(Charger::Termagants, ChargeLayout{5.0f, 0.0f, 0}, 8);
    ASSERT_TRUE(gs.has_value());
    const AutoChargeMove plan = auto_charge_move(*gs);
    const ChargeArrangementScore s = apply_checked(*gs, plan, "termagants");
    EXPECT_EQ(s.engaged, 10);
    EXPECT_GE(s.within_1, 5);
}

TEST(M42_AutoChargeMove, NeverEndsEngagedWithAUnitThatIsNotATarget) {
    // A second Termagant unit 3" east of the target, also within reach: the charge targets only the first, so
    // no Intercessor may end within 2" of the second (11.04 After Moving).
    GameState gs = wh40k::test::charge_fixture(Charger::Intercessors, ChargeLayout{4.0f, 0.0f, 0});
    const UnitId other = wh40k::test::add_unit_copy(gs, wh40k::test::kM42Gants);
    for (Model& m : gs.find_unit(other)->models) m.position.x += in(8.0f);
    for (Model& m : gs.units[wh40k::test::kM42Gants].models) m.position.x -= in(3.0f);
    gs.pending = compute_decision(gs);
    ASSERT_TRUE(wh40k::test::declare_with_roll(gs, Charger::Intercessors, 10));
    apply_action(gs, Side::A, action::StartChargeMove{gs.units[0].id, {gs.units[1].id}});
    const AutoChargeMove plan = auto_charge_move(gs);
    ASSERT_TRUE(plan.ok()) << plan.problems.front();
    GameState work = gs;
    for (const GameAction& a : plan.moves) apply_action(work, Side::A, a);
    EXPECT_TRUE(move_end_problems(work).empty());
    EXPECT_EQ(engaged_enemy_units(work, work.units[0]), (std::vector<UnitId>{gs.units[1].id}));
}

TEST(M42_AutoChargeMove, TwoTargetsAreBothEngaged) {
    GameState gs = wh40k::test::charge_fixture(Charger::Intercessors, ChargeLayout{4.0f, 0.0f, 0});
    const UnitId other = wh40k::test::add_unit_copy(gs, wh40k::test::kM42Gants);
    for (Model& m : gs.units[wh40k::test::kM42Gants].models) m.position.x -= in(5.0f);
    for (Model& m : gs.find_unit(other)->models) m.position.x += in(5.0f);
    // Keep only the front rank of each, so the gap is the same 4".
    gs.units[wh40k::test::kM42Gants].models.resize(5);
    gs.find_unit(other)->models.resize(5);
    gs.pending = compute_decision(gs);
    ASSERT_TRUE(wh40k::test::declare_with_roll(gs, Charger::Intercessors, 9));
    const GameAction start = action::StartChargeMove{gs.units[0].id, {gs.units[1].id, other}};
    ASSERT_TRUE(validate_action(gs, Side::A, start).ok());
    apply_action(gs, Side::A, start);
    const AutoChargeMove plan = auto_charge_move(gs);
    ASSERT_TRUE(plan.ok()) << plan.problems.front();
    GameState work = gs;
    for (const GameAction& a : plan.moves) apply_action(work, Side::A, a);
    EXPECT_TRUE(move_end_problems(work).empty());
    EXPECT_EQ(engaged_enemy_units(work, work.units[0]).size(), 2u);
}

TEST(M42_AutoChargeMove, SaysWhyWhenNoArrangementExists) {
    // A lone Termagant ringed by six Termagants of another unit (in base contact round it): every spot within 2"
    // of it is within 2" of the ring, which isn't a charge target (11.04 After Moving), so no charge move can end.
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
    ASSERT_TRUE(wh40k::test::declare_with_roll(gs, Charger::Intercessors, 12));
    apply_action(gs, Side::A, action::StartChargeMove{gs.units[0].id, {target.id}});
    const AutoChargeMove plan = auto_charge_move(gs);
    EXPECT_FALSE(plan.ok());
    EXPECT_TRUE(plan.moves.empty());
    ASSERT_FALSE(plan.problems.empty());
    EXPECT_NE(plan.problems.front().find("11.04"), std::string::npos) << plan.problems.front();
}

// ==== The client's Auto move: when it is offered, Undo, determinism ====

TEST(M42_AutoChargeMove, OfferedOnlyForAChargeMoveBeforeAnyModelIsDragged) {
    auto gs = wh40k::test::charge_move_state(Charger::Intercessors, ChargeLayout{4.0f, 0.0f, 0}, 8);
    ASSERT_TRUE(gs.has_value());
    EXPECT_TRUE(auto_charge_move_available(*gs));
    EXPECT_FALSE(auto_fight_move_available(*gs));  // the D-050 auto move is for pile-ins and consolidations
    const Model& m = gs->units[0].models[2];
    apply_action(*gs, Side::A, action::ProposeModelMove{m.id, {Position{m.position.x, m.position.y + in(1.0f), 0}}});
    EXPECT_FALSE(auto_charge_move_available(*gs));
    EXPECT_FALSE(auto_charge_move(*gs).ok());
    // Not before the targets are chosen, nor in other moves.
    GameState select = wh40k::test::charge_fixture(Charger::Intercessors, ChargeLayout{4.0f, 0.0f, 0});
    EXPECT_FALSE(auto_charge_move_available(select));
    ASSERT_TRUE(wh40k::test::declare_with_roll(select, Charger::Intercessors, 8));
    EXPECT_FALSE(auto_charge_move_available(select));
    EXPECT_FALSE(auto_charge_move_available(wh40k::test::shoot_step_state()));
    GameState pile_in = wh40k::test::after_successful_charge();
    apply_action(pile_in, Side::A, action::EndChargeStep{});
    apply_action(pile_in, Side::A, action::StartPileIn{pile_in.units[0].id, {pile_in.units[1].id}});
    EXPECT_FALSE(auto_charge_move_available(pile_in));
}

TEST(M42_AutoChargeMove, SubmittedAsOneUndoGroupReplaysAndIsDeterministic) {
    GameState rolled = wh40k::test::charge_fixture(Charger::Termagants, ChargeLayout{6.0f, 2.0f, 0});
    ASSERT_TRUE(wh40k::test::declare_with_roll(rolled, Charger::Termagants, 9));
    GameSession session(rolled);
    const UnitId gants = session.state().units[wh40k::test::kM42Gants].id;
    const UnitId marines = session.state().units[wh40k::test::kM42Marines].id;
    ASSERT_TRUE(session.submit(action::StartChargeMove{gants, {marines}}).ok());
    const GameState before = session.state();
    const AutoChargeMove plan = auto_charge_move(before);
    ASSERT_TRUE(plan.ok()) << plan.problems.front();
    EXPECT_EQ(auto_charge_move(before).moves, plan.moves);  // same input, same output

    UndoGroups groups;
    groups.begin(session.history().size());
    for (const GameAction& a : plan.moves) ASSERT_TRUE(session.submit(a).ok());
    groups.end(session.history().size());
    EXPECT_TRUE(move_end_problems(session.state()).empty());
    const GameSession rebuilt = GameSession::from_steps(session.initial(), session.history());
    EXPECT_EQ(rebuilt.state(), session.state());
    EXPECT_TRUE(groups.undo(session));
    EXPECT_EQ(session.state(), before);
    // Committed, the charge stands (11.04 After Moving): no revert, Fights First.
    for (const GameAction& a : plan.moves) ASSERT_TRUE(session.submit(a).ok());
    ASSERT_TRUE(session.submit(action::CommitUnitMove{}).ok());
    EXPECT_TRUE(session.state().find_unit(gants)->turn_flags.fights_first);
}
