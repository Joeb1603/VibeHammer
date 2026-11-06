// M31: pile-ins and consolidations too small to matter are skipped under "Skip steps with nothing to do"
// (D-047, extending D-036): 12.02, 12.03, 12.07, 12.08. M32: near base contact counts for skipping (D-048).
// M33: so does a model that can only get up to 0.1" closer (D-049).

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <variant>
#include <vector>

#include "auto_actions.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
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
constexpr int kRing = 7;

// One Intercessor in a pocket: seven Termagants round it, each `gap_mm` from its base, too close together
// for it to slip between. It can move about gap_mm at most (closer to them, not onto them); every
// Termagant is in the unit it is already engaged with, so nothing new can be reached.
void make_pocket(GameState& gs, float gap_mm) {
    gs.units[kMarines].models.resize(1);
    gs.units[kGants].models.resize(kRing);
    Model& marine = gs.units[kMarines].models[0];
    marine.position = Position{gs.battlefield.width_mm / 2, gs.battlefield.depth_mm / 2, 0.0f};
    const float d = base_radius(marine.base) + base_radius(gs.units[kGants].models[0].base) + gap_mm;
    for (int i = 0; i < kRing; ++i) {
        const float a = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / kRing;
        gs.units[kGants].models[static_cast<std::size_t>(i)].position =
            Position{marine.position.x + d * std::cos(a), marine.position.y + d * std::sin(a), 0.0f};
    }
}

// Side A's Charge step after the charge with the pocket made: the player ends the step to reach Pile In.
GameState pocket_before_pile_in(float gap_mm) {
    GameState gs = wh40k::test::after_successful_charge();
    make_pocket(gs, gap_mm);
    return gs;
}

GameState pocket_pile_in(float gap_mm) {
    GameState gs = pocket_before_pile_in(gap_mm);
    apply_action(gs, Side::A, action::EndChargeStep{});
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::PileIn{}}));
    return gs;
}

std::optional<GameAction> side_a_skip(const GameState& gs) {
    return pointless_fight_step_skip(gs, legal_actions(gs, Side::A));
}

}  // namespace

TEST(M31_FightMoveSkip, APileInOfAModelInBaseContactIsSkipped) {
    const GameState gs = pocket_pile_in(0.0f);  // 12.03 While Moving: base contact, it cannot be moved
    EXPECT_EQ(side_a_skip(gs), GameAction{action::EndPileIns{}});
}

TEST(M31_FightMoveSkip, APileInThatCanMoveOnly2MmOrLessIsSkipped) {
    const GameState gs = pocket_pile_in(1.5f);
    const action::StartPileIn start{gs.units[kMarines].id, {gs.units[kGants].id}};
    EXPECT_FALSE(fight_move_worthwhile(gs, Side::A, start));
    EXPECT_EQ(side_a_skip(gs), GameAction{action::EndPileIns{}});
}

TEST(M31_FightMoveSkip, APileInWithRoomFor3MmIsNotSkipped) {
    const GameState gs = pocket_pile_in(3.0f);
    const action::StartPileIn start{gs.units[kMarines].id, {gs.units[kGants].id}};
    EXPECT_TRUE(fight_move_worthwhile(gs, Side::A, start));
    EXPECT_FALSE(side_a_skip(gs).has_value());
}

TEST(M31_FightMoveSkip, TheUsualPileInAfterAChargeIsNotSkipped) {
    GameState gs = wh40k::test::after_successful_charge();
    apply_action(gs, Side::A, action::EndChargeStep{});
    EXPECT_FALSE(side_a_skip(gs).has_value());
}

TEST(M31_FightMoveSkip, ASmallMoveThatCouldReachANewEnemyUnitIsNotSkipped) {
    GameState gs = pocket_before_pile_in(1.5f);
    const UnitId other = wh40k::test::add_unit_copy(gs, kGants);
    Unit& extra = gs.units.back();
    extra.models.resize(1);
    const Model& marine = gs.units[kMarines].models[0];
    // Its engagement range is 1" from the Intercessor's base: not engaged now, but within its 3" move.
    const float d = base_radius(marine.base) + base_radius(extra.models[0].base) + in(2) + in(1);
    extra.models[0].position = Position{marine.position.x, marine.position.y + d, 0.0f};
    apply_action(gs, Side::A, action::EndChargeStep{});
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::PileIn{}}));
    const action::StartPileIn start{gs.units[kMarines].id, {gs.units[kGants].id}};
    ASSERT_TRUE(validate_action(gs, Side::A, start).ok());
    EXPECT_TRUE(fight_move_worthwhile(gs, Side::A, start));  // D-047: new contact always counts
    EXPECT_FALSE(side_a_skip(gs).has_value());
    EXPECT_NE(other, gs.units[kGants].id);
}

namespace {

// One Intercessor `gap_mm` from a Termagant to its west, and a second Termagant of the same unit 0.5" to
// its east: it could pile in about 12 mm east (ending closer to the Termagants than it is now).
GameState near_contact_pile_in(float gap_mm) {
    GameState gs = wh40k::test::after_successful_charge();
    gs.units[kMarines].models.resize(1);
    gs.units[kGants].models.resize(2);
    Model& marine = gs.units[kMarines].models[0];
    marine.position = Position{gs.battlefield.width_mm / 2, gs.battlefield.depth_mm / 2, 0.0f};
    const float touching = base_radius(marine.base) + base_radius(gs.units[kGants].models[0].base);
    gs.units[kGants].models[0].position = Position{marine.position.x - touching - gap_mm, marine.position.y, 0.0f};
    gs.units[kGants].models[1].position = Position{marine.position.x + touching + in(0.5f), marine.position.y, 0.0f};
    apply_action(gs, Side::A, action::EndChargeStep{});
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::PileIn{}}));
    return gs;
}

}  // namespace

TEST(M32_FightMoveSkip, AModelWithin005InchOfAnEnemyCountsAsInBaseContactForSkipping) {
    const GameState gs = near_contact_pile_in(1.0f);
    // D-048: the engine still lets it pile in closer...
    const auto reach_gs = preview_fight_move(gs, Side::A, action::StartPileIn{gs.units[kMarines].id,
                                                                              {gs.units[kGants].id}});
    ASSERT_TRUE(reach_gs.has_value());
    const auto reach = move_reach(*reach_gs, gs.units[kMarines].models[0].id);
    ASSERT_TRUE(reach.has_value());
    EXPECT_FALSE(reach->immobile);
    // ...but the step is skipped as if it were in base contact.
    EXPECT_EQ(side_a_skip(gs), GameAction{action::EndPileIns{}});
}

// (Was 1.5 mm in M32; D-049 now skips anything within 0.1", so a model 4 mm away shows the contrast.)
TEST(M32_FightMoveSkip, AModelMoreThan01InchFromAnEnemyWithRoomIsNotSkipped) {
    const GameState gs = near_contact_pile_in(4.0f);
    EXPECT_FALSE(side_a_skip(gs).has_value());
}

TEST(M33_FightMoveSkip, AModelThatCanOnlyGetMarginallyCloserIsSkippedEvenWithRoomToMove) {
    // pile-in.png: 1.8 mm from one Termagant, with room to slide about 12 mm towards the other.
    const GameState gs = near_contact_pile_in(1.8f);
    const action::StartPileIn start{gs.units[kMarines].id, {gs.units[kGants].id}};
    const auto preview = preview_fight_move(gs, Side::A, start);
    ASSERT_TRUE(preview.has_value());
    const auto reach = move_reach(*preview, gs.units[kMarines].models[0].id);
    ASSERT_TRUE(reach.has_value());
    EXPECT_FALSE(reach->immobile);  // the engine lets it move, and it has room for more than 2 mm
    EXPECT_FALSE(fight_move_worthwhile(gs, Side::A, start));
    EXPECT_EQ(side_a_skip(gs), GameAction{action::EndPileIns{}});
}

TEST(M33_FightMoveSkip, JustOver01InchFromTheUnitIsNotSkipped) {
    const GameState gs = near_contact_pile_in(in(0.1f) + 0.2f);
    EXPECT_TRUE(fight_move_worthwhile(gs, Side::A, action::StartPileIn{gs.units[kMarines].id,
                                                                         {gs.units[kGants].id}}));
}

TEST(M31_FightMoveSkip, AnOngoingConsolidationWithNoRoomIsSkipped) {
    GameState gs = wh40k::test::consolidate_step_state();
    make_pocket(gs, 1.5f);
    gs.pending = compute_decision(gs);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Consolidate{}}));
    EXPECT_EQ(side_a_skip(gs), GameAction{action::EndConsolidations{}});
}

TEST(M31_FightMoveSkip, AConsolidationWithRoomIsNotSkipped) {
    const GameState gs = wh40k::test::consolidate_step_state();
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Consolidate{}}));
    EXPECT_FALSE(side_a_skip(gs).has_value());
}

TEST(M31_FightMoveSkip, OnlyPileInAndConsolidateDecisionsAreJudged) {
    const GameState gs = wh40k::test::shoot_step_state();
    EXPECT_FALSE(pointless_fight_step_skip(gs, legal_actions(gs, Side::A)).has_value());
    // Not a pile-in or consolidation: nothing to judge, so it counts as worth doing.
    EXPECT_TRUE(fight_move_worthwhile(gs, Side::A, action::EndShootStep{}));
}

TEST(M31_AutoSkip, ThePlayersEndChargeStepIsFollowedByTheSkippedPileInAndUndoesWithIt) {
    GameSession session(pocket_before_pile_in(1.5f));
    const GameState start = session.state();
    UndoGroups groups;
    groups.begin(session.history().size());
    ASSERT_TRUE(session.submit(action::EndChargeStep{}).ok());
    const std::vector<GameAction> done = run_auto_actions(session, AutoSettings{true, true});
    groups.end(session.history().size());
    ASSERT_FALSE(done.empty());
    EXPECT_EQ(done.front(), GameAction{action::EndPileIns{}});
    // The skip is an ordinary step: the history replays to the same state.
    const GameSession rebuilt = GameSession::from_steps(session.initial(), session.history());
    EXPECT_EQ(rebuilt.state(), session.state());
    // D-039: one Undo removes the player's action and the skips after it.
    EXPECT_TRUE(groups.undo(session));
    EXPECT_EQ(session.state(), start);
}

TEST(M31_AutoSkip, NothingIsSkippedWithTheSettingOff) {
    GameSession session(pocket_before_pile_in(1.5f));
    ASSERT_TRUE(session.submit(action::EndChargeStep{}).ok());
    EXPECT_TRUE(run_auto_actions(session, AutoSettings{false, false}).empty());
    EXPECT_EQ(session.state().flow.stage, Stage::PileIn);
}
