// M34: "Auto move" for pile-ins and consolidations (D-050): 12.03, 12.08, 03.03.

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <variant>
#include <vector>

#include "auto_fight_move.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
#include "core/state/queries.hpp"
#include "fight_move_skip.hpp"
#include "session.hpp"
#include "undo_groups.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::in;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

Position centre_of(const GameState& gs) { return Position{gs.battlefield.width_mm / 2, gs.battlefield.depth_mm / 2, 0}; }

float touching(const GameState& gs) {
    return base_radius(gs.units[kMarines].models[0].base) + base_radius(gs.units[kGants].models[0].base);
}

// Ends the Charge step and starts Side A's pile-in towards the Termagants.
GameState start_pile_in(GameState gs) {
    apply_action(gs, Side::A, action::EndChargeStep{});
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::PileIn{}}));
    const GameAction start = action::StartPileIn{gs.units[kMarines].id, {gs.units[kGants].id}};
    EXPECT_TRUE(validate_action(gs, Side::A, start).ok());
    apply_action(gs, Side::A, start);
    return gs;
}

PlacedModel placed_model(const Model& m) { return PlacedModel{m.id, m.position, m.base}; }

bool coherent(const Unit& u) {
    std::vector<PlacedModel> p;
    for (const Model& m : u.models) p.push_back(placed_model(m));
    return check_coherency(p).ok();
}

// Applies the auto move on a copy: every proposal is valid, the unit stays in coherency after each one, the
// move can end, and every moved model ends closer to the target than it started.
GameState apply_checked(const GameState& gs, const AutoFightMove& plan) {
    EXPECT_TRUE(plan.ok()) << (plan.problems.empty() ? "" : plan.problems.front());
    GameState work = gs;
    const Side side = gs.pending->side;
    for (const GameAction& a : plan.moves) {
        const auto v = validate_action(work, side, a);
        EXPECT_TRUE(v.ok()) << (v.reasons.empty() ? "" : v.reasons.front());
        apply_action(work, side, a);
        EXPECT_TRUE(coherent(*work.find_unit(work.flow.move->unit)));
    }
    EXPECT_TRUE(move_end_problems(work).empty());
    for (const GameAction& a : plan.moves) {
        const ModelId id = std::get<action::ProposeModelMove>(a).model;
        const Model& before = *find_model(gs, id);
        const Model& after = *find_model(work, id);
        EXPECT_LT(*gap_to_closest_target(work, after, after.position),
                  *gap_to_closest_target(gs, before, before.position));
    }
    return work;
}

int models_in_base_contact(const GameState& gs) {
    int n = 0;
    for (const Model& m : gs.units[kMarines].models) {
        if (in_base_contact(*gap_to_closest_target(gs, m, m.position))) ++n;
    }
    return n;
}

// One Intercessor in a ring of seven Termagants, each `gap_mm` from its base (as M31's pocket).
GameState pocket_pile_in(float gap_mm) {
    GameState gs = wh40k::test::after_successful_charge();
    gs.units[kMarines].models.resize(1);
    gs.units[kGants].models.resize(7);
    gs.units[kMarines].models[0].position = centre_of(gs);
    const float d = touching(gs) + gap_mm;
    for (int i = 0; i < 7; ++i) {
        const float a = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / 7.0f;
        gs.units[kGants].models[static_cast<std::size_t>(i)].position =
            Position{centre_of(gs).x + d * std::cos(a), centre_of(gs).y + d * std::sin(a), 0.0f};
    }
    return start_pile_in(gs);
}

}  // namespace

TEST(M34_AutoFightMove, ThePileInAfterAChargeIsLegalAndBringsModelsIntoBaseContact) {
    const GameState gs = start_pile_in(wh40k::test::after_successful_charge());
    ASSERT_TRUE(auto_fight_move_available(gs));
    const AutoFightMove plan = auto_fight_move(gs);
    ASSERT_TRUE(plan.ok());
    const GameState after = apply_checked(gs, plan);
    EXPECT_GT(models_in_base_contact(after), models_in_base_contact(gs));
    EXPECT_GE(models_in_base_contact(after), 3);
}

TEST(M34_AutoFightMove, AModelThisCloseGoesStraightIntoContactNotRoundToAnotherTermagant) {
    // pile-in.png: 1.8 mm from one Termagant, with room to slide towards another 0.5" away (D-049).
    GameState gs = wh40k::test::after_successful_charge();
    gs.units[kMarines].models.resize(1);
    gs.units[kGants].models.resize(2);
    const Position c = centre_of(gs);
    gs.units[kMarines].models[0].position = c;
    gs.units[kGants].models[0].position = Position{c.x - touching(gs) - 1.8f, c.y, 0};
    gs.units[kGants].models[1].position = Position{c.x + touching(gs) + in(0.5f), c.y, 0};
    gs = start_pile_in(gs);
    const AutoFightMove plan = auto_fight_move(gs);
    ASSERT_EQ(plan.moves.size(), 1u);
    const GameState after = apply_checked(gs, plan);
    const Model& m = after.units[kMarines].models[0];
    EXPECT_TRUE(in_base_contact(base_distance(m.position, m.base, gs.units[kGants].models[0].position,
                                              gs.units[kGants].models[0].base)));
    EXPECT_NEAR(centre_distance(c, m.position), 1.8f, 0.05f);
}

TEST(M34_AutoFightMove, APocketedModelWithA3MmGapEndsInBaseContact) {
    const GameState gs = pocket_pile_in(3.0f);
    const GameState after = apply_checked(gs, auto_fight_move(gs));
    EXPECT_EQ(models_in_base_contact(after), 1);
}

TEST(M34_AutoFightMove, NothingToDoWhenEveryModelIsInBaseContact) {
    const GameState gs = pocket_pile_in(0.0f);
    ASSERT_TRUE(auto_fight_move_available(gs));
    const AutoFightMove plan = auto_fight_move(gs);
    EXPECT_FALSE(plan.ok());
    EXPECT_TRUE(plan.moves.empty());
    EXPECT_EQ(plan.problems, (std::vector<std::string>{"no model can get closer"}));
}

TEST(M34_AutoFightMove, AnOngoingConsolidationIsLegalAndStaysEngaged) {
    GameState gs = wh40k::test::consolidate_step_state();
    apply_action(gs, Side::A, action::StartConsolidation{gs.units[kMarines].id, {gs.units[kGants].id}});
    ASSERT_EQ(gs.flow.move->consolidation_mode, ConsolidationMode::Ongoing);
    const GameState after = apply_checked(gs, auto_fight_move(gs));
    EXPECT_GT(models_in_base_contact(after), models_in_base_contact(gs));
}

TEST(M34_AutoFightMove, AnEngagingConsolidationEndsEngagedWithItsTarget) {
    GameState gs = wh40k::test::consolidate_step_state();
    for (Model& m : gs.units[kGants].models) m.position.y += in(1.5f);  // unengaged, but within 3"
    gs.pending = compute_decision(gs);
    ASSERT_TRUE(engaged_enemy_units(gs, gs.units[kMarines]).empty());
    const GameAction start = action::StartConsolidation{gs.units[kMarines].id, {gs.units[kGants].id}};
    ASSERT_TRUE(validate_action(gs, Side::A, start).ok());
    apply_action(gs, Side::A, start);
    ASSERT_EQ(gs.flow.move->consolidation_mode, ConsolidationMode::Engaging);
    const GameState after = apply_checked(gs, auto_fight_move(gs));
    EXPECT_EQ(engaged_enemy_units(after, after.units[kMarines]), (std::vector<UnitId>{gs.units[kGants].id}));
}

TEST(M34_AutoFightMove, CoherencyIsKeptAndTheRearModelMovingFirstLetsTheFrontOneReachContact) {
    // Two Intercessors 1.9" apart in a line; a Termagant 1.5" beyond the front one. The front model can't
    // reach it alone without leaving the rear one more than 2" behind (03.03); with the rear one moved first
    // it can.
    GameState gs = wh40k::test::after_successful_charge();
    gs.units[kMarines].models.resize(2);
    gs.units[kGants].models.resize(1);
    const Position c = centre_of(gs);
    const float marine_d = 2.0f * base_radius(gs.units[kMarines].models[0].base);
    gs.units[kMarines].models[0].position = c;                                                // rear
    gs.units[kMarines].models[1].position = Position{c.x + marine_d + in(1.9f), c.y, 0};     // front
    gs.units[kGants].models[0].position =
        Position{gs.units[kMarines].models[1].position.x + touching(gs) + in(1.5f), c.y, 0};
    gs = start_pile_in(gs);
    const GameState after = apply_checked(gs, auto_fight_move(gs));  // coherent after every proposal
    EXPECT_TRUE(in_base_contact(*gap_to_closest_target(after, after.units[kMarines].models[1],
                                                       after.units[kMarines].models[1].position)));
}

TEST(M34_AutoFightMove, AmongEquallyCloseSpotsTheOneNearTwoFriendsWins) {
    // The mover is 0.2" south of Termagant G. Friend F1 (in base contact with another Termagant, so it
    // can't move) is near every spot round G's south side; friend F2 (likewise held) is within 2" only of
    // spots a little west of due south. Those spots are as close to G (all touching), so the mover goes
    // there rather than straight north.
    GameState gs = wh40k::test::after_successful_charge();
    gs.units[kMarines].models.resize(3);
    gs.units[kGants].models.resize(3);
    const Position c = centre_of(gs);
    const float t = touching(gs);
    auto at = [&](float x, float y) { return Position{c.x + x, c.y + y, 0}; };
    gs.units[kGants].models[0].position = at(0, 0);                          // G
    gs.units[kMarines].models[0].position = at(0, -(t + in(0.2f)));          // the mover
    gs.units[kMarines].models[1].position = at(-40, -50);                    // F1
    gs.units[kGants].models[1].position = at(-40, -50 - t);                  // holds F1
    gs.units[kMarines].models[2].position = at(-90.33f, -29.22f);            // F2
    gs.units[kGants].models[2].position = at(-90.33f - t, -29.22f);          // holds F2
    gs = start_pile_in(gs);
    const AutoFightMove plan = auto_fight_move(gs);
    ASSERT_EQ(plan.moves.size(), 1u);
    const GameState after = apply_checked(gs, plan);
    const Model& mover = after.units[kMarines].models[0];
    const Model& f2 = after.units[kMarines].models[2];
    EXPECT_LT(*gap_to_closest_target(after, mover, mover.position), kAutoMoveGapBandMm);
    EXPECT_LT(mover.position.x, c.x - 5.0f);  // west of the straight line
    EXPECT_TRUE(within(base_distance(mover.position, mover.base, f2.position, f2.base), kCoherencyNearMm));
}

TEST(M34_AutoFightMove, OfferedOnlyBeforeAnyModelIsDragged) {
    GameState gs = start_pile_in(wh40k::test::after_successful_charge());
    ASSERT_TRUE(auto_fight_move_available(gs));
    const Model& west = gs.units[kMarines].models[0];
    apply_action(gs, Side::A,
                 action::ProposeModelMove{west.id, {Position{west.position.x, west.position.y + in(0.5f), 0}}});
    EXPECT_FALSE(auto_fight_move_available(gs));
    EXPECT_FALSE(auto_fight_move(gs).ok());
}

TEST(M34_AutoFightMove, NotOfferedOutsidePileInsAndConsolidations) {
    EXPECT_FALSE(auto_fight_move_available(wh40k::test::shoot_step_state()));
    EXPECT_FALSE(auto_fight_move_available(wh40k::test::after_successful_charge()));
}

TEST(M34_AutoFightMove, SubmittedAsOneUndoGroupReplaysAndIsDeterministic) {
    GameSession session(wh40k::test::after_successful_charge());
    ASSERT_TRUE(session.submit(action::EndChargeStep{}).ok());
    const GameState gs = session.state();
    ASSERT_TRUE(session.submit(action::StartPileIn{gs.units[kMarines].id, {gs.units[kGants].id}}).ok());
    const GameState before = session.state();
    const AutoFightMove plan = auto_fight_move(before);
    ASSERT_TRUE(plan.ok());
    EXPECT_EQ(auto_fight_move(before).moves, plan.moves);  // same input, same output

    UndoGroups groups;
    groups.begin(session.history().size());
    for (const GameAction& a : plan.moves) ASSERT_TRUE(session.submit(a).ok());
    groups.end(session.history().size());
    EXPECT_TRUE(move_end_problems(session.state()).empty());
    const GameSession rebuilt = GameSession::from_steps(session.initial(), session.history());
    EXPECT_EQ(rebuilt.state(), session.state());
    EXPECT_TRUE(groups.undo(session));
    EXPECT_EQ(session.state(), before);
}
