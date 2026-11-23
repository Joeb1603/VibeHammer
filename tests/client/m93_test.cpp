// M93 (D-094): the client's per-weapon new targets for 04.03.03 RetargetAttacks (RetargetPicks), and the objective
// markers' board marks (14.01.01).

#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "phase_track.hpp"
#include "retarget_picks.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::add_unit_copy;
using wh40k::test::model_ids;
using wh40k::test::select;

namespace {

// The V1 Shoot step with two more Termagant units (copies) and the first cut to one model. Intercessor 0 fires
// Saturation at it (batch 0, which destroys it); Intercessors 1 and 2 fire Focused Fire at it (batch 1, lost), so two
// weapons wait for new targets, each able to take either copy (the second copy set 7" to the west).
GameState two_lost_weapons(UnitId& second, UnitId& third) {
    GameState gs = wh40k::test::shoot_step_state();
    const UnitId gants = gs.units[1].id;
    second = add_unit_copy(gs, 1);
    third = add_unit_copy(gs, 1);
    for (Model& m : gs.find_unit(third)->models) m.position.x -= wh40k::test::in(7.0f);  // beside it, in sight
    gs.units[1].models.resize(1);
    const AttackContext shooting{AttackType::Shooting, true};
    const std::vector<WeaponSelection> sel{select(gs, 0, 0, 0, 1, gants), select(gs, 0, 1, 0, 0, gants),
                                           select(gs, 0, 2, 0, 0, gants)};
    ScriptedDice dice{3, 3, 3, 3, 3, 3, 1, 1, 1};
    begin_attacks(gs, dice, gs.units[0].id, shooting, sel);
    apply_action(gs, Side::A, action::SelectAttackBatch{0}, dice);
    apply_action(gs, Side::B, action::OrderCasualties{gants, model_ids(gs.units[1])}, dice);
    return gs;
}

}  // namespace

TEST(M93_RetargetPicks, OneRowPerLostWeaponEachTargetPickedOnItsOwn) {
    UnitId second;
    UnitId third;
    const GameState gs = two_lost_weapons(second, third);
    ASSERT_TRUE(std::holds_alternative<decision::RetargetAttacks>(gs.pending->kind));
    EXPECT_EQ(retarget_choices(gs), (std::vector<RetargetChoice>{{1, {second, third}}, {2, {second, third}}}));
    RetargetPicks picks(gs);
    ASSERT_TRUE(picks.active());
    ASSERT_EQ(picks.rows().size(), 2u);
    EXPECT_NE(picks.rows()[0].weapon.find("Focused"), std::string::npos) << picks.rows()[0].weapon;
    // Each row starts at its first option.
    EXPECT_EQ(picks.action(), (action::RetargetAttacks{{AttackRetarget{1, second}, AttackRetarget{2, second}}}));
    picks.choose(1, 1);
    const action::RetargetAttacks split{{AttackRetarget{1, second}, AttackRetarget{2, third}}};
    EXPECT_EQ(picks.action(), split);
    EXPECT_TRUE(validate_action(gs, Side::A, picks.action()).ok());
    // The engine splits them into one new batch per target.
    GameState after = gs;
    const auto events = apply_action(after, Side::A, split);
    const auto moved = wh40k::test::events_of<event::AttacksRetargeted>(events);
    ASSERT_EQ(moved.size(), 1u);
    EXPECT_EQ(moved[0].batches.size(), 2u);
    picks.choose(0, std::nullopt);  // lost
    EXPECT_EQ(picks.action(), (action::RetargetAttacks{{AttackRetarget{2, third}}}));
    picks.choose(0, 5);  // out of range: ignored
    EXPECT_EQ(picks.rows()[0].chosen, std::nullopt);
    picks.all_at(third);
    EXPECT_EQ(picks.action(), (action::RetargetAttacks{{AttackRetarget{1, third}, AttackRetarget{2, third}}}));
    EXPECT_EQ(picks.units(), (std::vector<UnitId>{second, third}));
    // Inactive for any other decision.
    EXPECT_FALSE(RetargetPicks(wh40k::test::shoot_step_state()).active());
}

TEST(M93_ObjectiveMarks, AMarkerShowsItsOwnRadiusInsideItsRange) {
    GameState gs = wh40k::test::shoot_step_state();
    Mission m;
    ObjectiveArea marker{"Centre", wh40k::test::at(12.0f, 12.0f), kObjectiveMarkerDiameterMm + 2.0f * kObjectiveMarkerRangeMm,
                         {1, 1}, std::nullopt, std::nullopt, true};
    ObjectiveArea area{"Home", wh40k::test::at(12.0f, 3.0f), wh40k::test::in(6.0f), {1, 1}, std::nullopt, std::nullopt, false};
    m.objectives = {marker, area};
    gs.mission = m;
    const std::vector<ObjectiveMark> marks = objective_marks(gs);
    ASSERT_EQ(marks.size(), 2u);
    EXPECT_FLOAT_EQ(marks[0].marker_radius_mm, 20.0f);
    EXPECT_FLOAT_EQ(marks[0].radius_mm, 20.0f + 3.0f * 25.4f);
    EXPECT_FLOAT_EQ(marks[1].marker_radius_mm, 0.0f);
}
