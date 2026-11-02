#include <gtest/gtest.h>

#include <vector>

#include "core/geometry/engagement.hpp"
#include "core/state/queries.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::at;
using wh40k::test::in;
using wh40k::test::kTermagantBase;
using wh40k::test::placed;
using wh40k::test::unit_of;

namespace {
// Intercessor at the origin; a Termagant `gap_mm` edge to edge from it along x.
PlacedModel termagant_at_gap(std::uint32_t id, float gap_mm) {
    return placed(id, Position{16.0f + 14.25f + gap_mm, 0.0f}, kTermagantBase);
}
const PlacedModel kMarine = placed(1, Position{0.0f, 0.0f});
}  // namespace

TEST(R03_04_Engagement, Exactly2InchesIsEngaged) {
    EXPECT_TRUE(models_engaged(kMarine, termagant_at_gap(2, in(2))));
}

TEST(R03_04_Engagement, TwoInchesPlusEpsilonIsNotEngaged) {
    EXPECT_FALSE(models_engaged(kMarine, termagant_at_gap(2, in(2) + 2 * EPS_MM)));
}

TEST(R03_04_Engagement, BaseContactIsEngaged) {
    EXPECT_TRUE(models_engaged(kMarine, termagant_at_gap(2, 0.0f)));
}

TEST(R03_04_Engagement, UnitEngagedIfAnyModelIs) {
    const std::vector<PlacedModel> marines{kMarine, placed(2, at(-3, 0))};
    const std::vector<PlacedModel> gants{termagant_at_gap(10, in(1.5f)), termagant_at_gap(11, in(8))};
    EXPECT_TRUE(units_engaged(marines, gants));
    // Only the marine at the origin is engaged.
    EXPECT_EQ(engaged_models(marines, gants), (std::vector<ModelId>{ModelId{1}}));
    EXPECT_EQ(engaged_models(gants, marines), (std::vector<ModelId>{ModelId{10}}));
}

TEST(R03_04_Engagement, UnitUnengagedWhenNoModelIs) {
    const std::vector<PlacedModel> marines{kMarine};
    const std::vector<PlacedModel> gants{termagant_at_gap(10, in(2.5f))};
    EXPECT_FALSE(units_engaged(marines, gants));
    EXPECT_TRUE(engaged_models(marines, gants).empty());
}

TEST(R03_04_Engagement, V1ScenarioStartsUnengaged) {
    const GameState gs = wh40k::test::load_v1_scenario();
    for (const Unit& u : gs.units) {
        EXPECT_FALSE(is_engaged(gs, u)) << u.name;
    }
}

TEST(R03_04_Engagement, GameStateQueriesFindEngagedEnemyUnit) {
    GameState gs = wh40k::test::load_v1_scenario();
    Unit& marines = gs.units[0];
    Unit& gants = gs.units[1];
    // Put one Termagant 1.5" in front of the first Intercessor.
    const Model& m = marines.models[0];
    gants.models[0].position = Position{m.position.x, m.position.y + 16.0f + 14.25f + in(1.5f)};
    EXPECT_TRUE(is_engaged(gs, marines));
    EXPECT_TRUE(is_engaged(gs, gants));
    EXPECT_EQ(engaged_enemy_units(gs, marines), (std::vector<UnitId>{gants.id}));
    EXPECT_EQ(engaged_enemy_units(gs, gants), (std::vector<UnitId>{marines.id}));
}
