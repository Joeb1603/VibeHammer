#include <gtest/gtest.h>

#include <vector>

#include "core/geometry/closest.hpp"
#include "core/geometry/distance.hpp"
#include "core/state/queries.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::at;
using wh40k::test::in;
using wh40k::test::kIntercessorBase;
using wh40k::test::kTermagantBase;
using wh40k::test::placed;

// ---- D-009 Within / Wholly Within ----

TEST(D009_Within, ModelWithinDistanceIfAnyPartOfBaseIs) {
    // Edge to edge 3" although the centres are 3" + 30.25 mm apart.
    const Position a = at(0, 0);
    const Position b{in(3) + 16.0f + 14.25f, 0.0f};
    EXPECT_TRUE(within(base_distance(a, kIntercessorBase, b, kTermagantBase), in(3)));
    EXPECT_FALSE(within(base_distance(a, kIntercessorBase, b, kTermagantBase), in(2.9f)));
}

TEST(D009_Within, PartlyInsideAreaIsWithinButNotWhollyWithin) {
    const Rect zone{0.0f, 0.0f, in(24), in(6)};
    const Position straddling{in(12), in(6) + 10.0f};  // centre 10 mm outside, base reaches 6 mm in
    EXPECT_TRUE(within_area(zone, straddling, kIntercessorBase));
    EXPECT_FALSE(wholly_within(zone, straddling, kIntercessorBase));
}

TEST(D009_Within, BaseJustOutsideAreaIsNotWithin) {
    const Rect zone{0.0f, 0.0f, in(24), in(6)};
    EXPECT_TRUE(within_area(zone, Position{in(12), in(6) + 16.0f}, kIntercessorBase));  // touching
    EXPECT_FALSE(within_area(zone, Position{in(12), in(6) + 16.5f}, kIntercessorBase));
}

TEST(D009_Within, UnitWithinIfAnyModelIs) {
    const std::vector<PlacedModel> a{placed(1, at(0, 0)), placed(2, at(0, 10))};
    const std::vector<PlacedModel> b{placed(3, at(0, 20)), placed(4, at(0, 13))};
    // Closest pair is 2 and 4: 3" between centres minus 32 mm.
    const auto d = closest_distance(a, b);
    ASSERT_TRUE(d.has_value());
    EXPECT_NEAR(*d, in(3) - 32.0f, 1e-3f);
    EXPECT_FALSE(closest_distance(a, std::vector<PlacedModel>{}).has_value());
}

TEST(D009_Within, V1UnitsAre12InchesApart) {
    const GameState gs = wh40k::test::load_v1_scenario();
    const auto d = unit_distance(gs.units[0], gs.units[1]);
    ASSERT_TRUE(d.has_value());
    EXPECT_NEAR(*d, in(12), 1e-2f);
    EXPECT_TRUE(within(*d, in(12)));
    EXPECT_FALSE(within(*d, in(11.9f)));
}

// ---- D-010 Closest Model / Unit ----

TEST(D010_ClosestModel, SingleClosest) {
    const PlacedModel from = placed(1, at(0, 0));
    const std::vector<PlacedModel> candidates{placed(10, at(5, 0)), placed(11, at(3, 0)),
                                              placed(12, at(0, 7))};
    EXPECT_EQ(closest_models(from, candidates), (std::vector<ModelId>{ModelId{11}}));
}

TEST(D010_ClosestModel, EquallyCloseModelsAreAllReturnedForThePlayerToChoose) {
    const PlacedModel from = placed(1, at(12, 12));
    const std::vector<PlacedModel> candidates{placed(10, at(15, 12)), placed(11, at(9, 12)),
                                              placed(12, at(12, 20))};
    EXPECT_EQ(closest_models(from, candidates), (std::vector<ModelId>{ModelId{10}, ModelId{11}}));
}

TEST(D010_ClosestModel, MeasuredEdgeToEdgeSoBaseSizeMatters) {
    // Same centre distance, but the Intercessor's bigger base is 1.75 mm closer edge to edge.
    const PlacedModel from = placed(1, at(12, 12));
    const std::vector<PlacedModel> candidates{placed(10, at(15, 12), kTermagantBase),
                                              placed(11, at(9, 12), kIntercessorBase)};
    EXPECT_EQ(closest_models(from, candidates), (std::vector<ModelId>{ModelId{11}}));
}

TEST(D010_ClosestModel, DifferenceWithinEpsilonIsATie) {
    const PlacedModel from = placed(1, Position{0.0f, 0.0f});
    const std::vector<PlacedModel> candidates{placed(10, Position{100.0f, 0.0f}),
                                              placed(11, Position{-100.0f - EPS_MM * 0.5f, 0.0f}),
                                              placed(12, Position{0.0f, 100.0f + EPS_MM * 3.0f})};
    EXPECT_EQ(closest_models(from, candidates), (std::vector<ModelId>{ModelId{10}, ModelId{11}}));
}

TEST(D010_ClosestModel, NoCandidates) {
    EXPECT_TRUE(closest_models(placed(1, at(0, 0)), std::vector<PlacedModel>{}).empty());
}

TEST(D010_ClosestUnit, MeasuredFromTheUnitUsingTheRule) {
    const std::vector<PlacedModel> from{placed(1, at(12, 2)), placed(2, at(14, 2))};
    const std::vector<std::vector<PlacedModel>> candidates{
        {placed(10, at(2, 12))},                         // far
        {placed(20, at(20, 2)), placed(21, at(22, 2))},  // closest to model 2
        {},                                              // empty unit ignored
    };
    EXPECT_EQ(closest_units(from, candidates), (std::vector<std::size_t>{1}));
}

TEST(D010_ClosestUnit, EquallyCloseUnitsAreAllReturned) {
    const std::vector<PlacedModel> from{placed(1, at(12, 12))};
    const std::vector<std::vector<PlacedModel>> candidates{
        {placed(10, at(12, 16))},
        {placed(20, at(12, 30)), placed(21, at(8, 12))},
    };
    EXPECT_EQ(closest_units(from, candidates), (std::vector<std::size_t>{0, 1}));
}

TEST(D010_ClosestUnit, NoNonEmptyCandidates) {
    const std::vector<PlacedModel> from{placed(1, at(12, 12))};
    const std::vector<std::vector<PlacedModel>> candidates{{}};
    EXPECT_TRUE(closest_units(from, candidates).empty());
}
