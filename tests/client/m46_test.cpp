// M46 (D-057): the fight-move shape the bot uses to tell a tidy pile-in or consolidation from not moving
// (auto_fight_move.hpp): engaged models, base contact, total gap and coherency links, and the tidier order.

#include <gtest/gtest.h>

#include "auto_fight_move.hpp"
#include "core/geometry/units.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::in;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

float radius(const Model& m) { return m.base.diameter_mm / 2.0f; }

// Two Intercessors west of one Termagant: the first in base contact with it, the second 1" behind the first
// (coherent with it, not engaged).
GameState two_against_one() {
    GameState gs = wh40k::test::load_v1_scenario();
    gs.units[kMarines].models.resize(2);
    gs.units[kGants].models.resize(1);
    Model& gant = gs.units[kGants].models[0];
    Model& front = gs.units[kMarines].models[0];
    Model& back = gs.units[kMarines].models[1];
    gant.position = Position{in(12.0f), in(12.0f), 0.0f};
    front.position = Position{gant.position.x - radius(gant) - radius(front), in(12.0f), 0.0f};
    back.position = Position{front.position.x - radius(front) - radius(back) - in(1.0f), in(12.0f), 0.0f};
    return gs;
}

}  // namespace

TEST(M46_FightMoveShape, CountsEngagedContactGapAndLinks) {
    const GameState gs = two_against_one();
    const FightMoveShape s = fight_move_shape(gs, gs.units[kMarines].id, {gs.units[kGants].id});
    EXPECT_EQ(s.engaged, 1);     // the back one is 1" + a base behind: more than 2" away (03.04)
    EXPECT_EQ(s.in_contact, 1);  // 01.04
    const Model& front = gs.units[kMarines].models[0];
    EXPECT_NEAR(s.total_gap, 2.0f * radius(front) + in(1.0f), 0.05f);  // 0 + the back one's gap
    EXPECT_EQ(s.links, 2);  // each has one friend within 2" (03.03)
}

TEST(M46_FightMoveShape, NoTargetsOrUnknownUnitGiveZero) {
    const GameState gs = two_against_one();
    const FightMoveShape none = fight_move_shape(gs, gs.units[kMarines].id, {});
    EXPECT_EQ(none.engaged, 0);
    EXPECT_EQ(none.links, 0);
    EXPECT_FLOAT_EQ(none.total_gap, 0.0f);
    EXPECT_EQ(fight_move_shape(gs, UnitId{999}, {gs.units[kGants].id}).in_contact, 0);
}

TEST(M46_FightMoveShape, ChangeIsAfterMinusBefore) {
    const FightMoveShape before{1, 0, 50.0f, 2};
    const FightMoveShape after{3, 2, 10.0f, 4};
    const FightMoveShape c = shape_change(before, after);
    EXPECT_EQ(c.engaged, 2);
    EXPECT_EQ(c.in_contact, 2);
    EXPECT_FLOAT_EQ(c.total_gap, -40.0f);
    EXPECT_EQ(c.links, 2);
}

TEST(M46_FightMoveShape, TidierPutsClosenessBeforeCoherency) {
    const FightMoveShape still{};
    EXPECT_TRUE(tidier(FightMoveShape{1, 0, 0.0f, -2}, still));  // a model engaged outweighs looser coherency
    EXPECT_TRUE(tidier(FightMoveShape{0, 1, 5.0f, 0}, still));   // base contact outweighs the gap
    EXPECT_TRUE(tidier(FightMoveShape{0, 0, -5.0f, -1}, still));  // closer outweighs coherency
    EXPECT_TRUE(tidier(FightMoveShape{0, 0, 0.0f, 1}, still));    // tighter coherency, nothing else
    // Less than kAutoMoveGapBandMm (1 mm) closer is no closer.
    EXPECT_FALSE(tidier(FightMoveShape{0, 0, -0.5f, 0}, still));
    EXPECT_FALSE(tidier(still, still));
    EXPECT_FALSE(tidier(FightMoveShape{0, 0, 3.0f, 0}, still));  // further away
}

TEST(M46_FightMoveShape, DescribesTheChange) {
    EXPECT_EQ(describe_shape_change(FightMoveShape{0, 3, -in(1.2f), 0}), "3 more in base contact, 1.2\" closer in total");
    EXPECT_EQ(describe_shape_change(FightMoveShape{2, 0, 0.0f, 1}), "2 more engaged, tighter coherency");
    EXPECT_EQ(describe_shape_change(FightMoveShape{}), "no closer");
}
