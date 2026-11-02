// Minimal geometry needed by the M2 loader. M3 extends these with its own acceptance tests.
#include <gtest/gtest.h>

#include <vector>

#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::in;

namespace {
const Base kBase32{BaseShape::Round, 32.0f, 0.0f};
}

TEST(R01_04_MeasuringDistances, EdgeToEdgeBetweenBases) {
    // Centres 3" apart, 32 mm bases: 3" - 32 mm edge to edge.
    const float d = base_distance(Position{0, 0}, kBase32, Position{in(3), 0}, kBase32);
    EXPECT_NEAR(d, in(3) - 32.0f, 1e-3f);
}

TEST(R01_04_MeasuringDistances, OverlappingBasesClampToZero) {
    EXPECT_EQ(base_distance(Position{0, 0}, kBase32, Position{10.0f, 0}, kBase32), 0.0f);
}

TEST(R03_03_Coherency, PairWithin2InchesIsCoherent) {
    const std::vector<PlacedModel> models{
        {ModelId{1}, Position{0, 0}, kBase32},
        {ModelId{2}, Position{32.0f + in(1.9f), 0}, kBase32},
    };
    EXPECT_TRUE(check_coherency(models).ok());
}

TEST(R03_03_Coherency, PairMoreThan2InchesApartReportsBothIsolated) {
    const std::vector<PlacedModel> models{
        {ModelId{1}, Position{0, 0}, kBase32},
        {ModelId{2}, Position{32.0f + in(2.1f), 0}, kBase32},
    };
    const CoherencyResult r = check_coherency(models);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.isolated, (std::vector<ModelId>{ModelId{1}, ModelId{2}}));
    EXPECT_TRUE(r.too_far.empty());
}

TEST(R03_03_Coherency, SingleModelUnitIsAlwaysCoherent) {
    const std::vector<PlacedModel> models{{ModelId{1}, Position{0, 0}, kBase32}};
    EXPECT_TRUE(check_coherency(models).ok());
}

TEST(D009_WhollyWithin, BaseTouchingZoneEdgeFromInsideIsWhollyWithin) {
    const Rect zone{0, 0, in(24), in(6)};
    EXPECT_TRUE(wholly_within(zone, Position{in(12), in(6) - 16.0f}, kBase32));
}

TEST(D009_WhollyWithin, BasePokingOutBy0_1mmIsNot) {
    const Rect zone{0, 0, in(24), in(6)};
    EXPECT_FALSE(wholly_within(zone, Position{in(12), in(6) - 16.0f + 0.1f}, kBase32));
}

TEST(D009_WhollyWithin, CentreInsideButBaseOverEdgeIsNot) {
    const Rect zone{0, 0, in(24), in(6)};
    EXPECT_FALSE(wholly_within(zone, Position{in(12), in(6) - 1.0f}, kBase32));
}
