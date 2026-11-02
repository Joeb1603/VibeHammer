// 06.01 Visibility: line of sight is a 1 mm wide straight line between any parts of two bases (D-017).

#include <gtest/gtest.h>

#include <vector>

#include "core/geometry/visibility.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::at;
using wh40k::test::in;
using wh40k::test::kIntercessorBase;
using wh40k::test::kTermagantBase;
using wh40k::test::placed;

namespace {

const Base kBigBase{BaseShape::Round, 100.0f, 0.0f};
constexpr float kGantRadius = 14.25f;

// A wall of Termagant bases along y = `y_in`, touching each other, with one gap of `gap_mm` (edge to
// edge) centred on x = `gap_x_in`. Eight bases each side of the gap.
std::vector<PlacedModel> wall_with_gap(float y_in, float gap_x_in, float gap_mm) {
    std::vector<PlacedModel> wall;
    std::uint32_t id = 100;
    for (int k = 0; k < 8; ++k) {
        const float offset = kGantRadius + gap_mm / 2.0f + static_cast<float>(k) * 2.0f * kGantRadius;
        wall.push_back(placed(id++, Position{in(gap_x_in) - offset, in(y_in), 0.0f}, kTermagantBase));
        wall.push_back(placed(id++, Position{in(gap_x_in) + offset, in(y_in), 0.0f}, kTermagantBase));
    }
    return wall;
}

}  // namespace

TEST(R06_01_Visibility, NothingInTheWayMeansLineOfSight) {
    EXPECT_TRUE(has_line_of_sight(placed(1, at(0, 0)), placed(2, at(10, 0)), {}));
}

TEST(R06_01_Visibility, ABaseWiderThanBothModelsBlocksEveryLine) {
    const std::vector<PlacedModel> blockers{placed(3, at(5, 0), kBigBase)};
    EXPECT_FALSE(has_line_of_sight(placed(1, at(0, 0)), placed(2, at(10, 0)), blockers));
}

TEST(R06_01_Visibility, ASmallerBaseInBetweenLeavesALineFromAnyPartOfTheBase) {
    // The centre line is blocked, but a line from the edge of one base to the edge of the other passes
    // 16 mm from the Termagant's centre, more than its 14.25 mm radius plus half the line's width.
    const std::vector<PlacedModel> blockers{placed(3, at(5, 0), kTermagantBase)};
    EXPECT_TRUE(has_line_of_sight(placed(1, at(0, 0)), placed(2, at(10, 0)), blockers));
}

TEST(R06_01_Visibility, AGapWiderThan1mmLetsTheLineThrough) {
    const auto wall = wall_with_gap(10, 0, 1.2f);
    EXPECT_TRUE(has_line_of_sight(placed(1, at(0, 0)), placed(2, at(0, 20)), wall));
}

TEST(R06_01_Visibility, AGapNarrowerThan1mmBlocksTheLine) {
    const auto wall = wall_with_gap(10, 0, 0.8f);
    EXPECT_FALSE(has_line_of_sight(placed(1, at(0, 0)), placed(2, at(0, 20)), wall));
}

TEST(R06_01_Visibility, ADiagonalLineThroughAGapIsFound) {
    // The only lines through the gap at x = 2" run from the east edge of the observer's base to the
    // west edge of the observed base, so neither the centre line nor a vertical line works. The line
    // crosses the wall about 8 degrees off square, so the gap must be wider than 1 mm: about 1.26 mm
    // here. At 1.28 mm only a line tangent to the bases finds the gap (D-017); sampled lines miss it.
    EXPECT_TRUE(has_line_of_sight(placed(1, at(0, 0)), placed(2, at(3, 20)), wall_with_gap(10, 2, 1.28f)));
    EXPECT_FALSE(has_line_of_sight(placed(1, at(0, 0)), placed(2, at(3, 20)), wall_with_gap(10, 2, 1.24f)));
    const auto wall = wall_with_gap(10, 2, 2.0f);
    EXPECT_TRUE(has_line_of_sight(placed(1, at(0, 0)), placed(2, at(3, 20)), wall));
    // Move the observed model to x = 5.5": a line through the gap would have to leave the observer's
    // base west of x = -0.87", but that base only reaches x = -0.63".
    EXPECT_FALSE(has_line_of_sight(placed(1, at(0, 0)), placed(2, at(5.5f, 20)), wall));
}

TEST(R06_01_Visibility, TouchingBasesStillBlockA1mmLine) {
    const auto wall = wall_with_gap(10, 0, 0.0f);
    EXPECT_FALSE(has_line_of_sight(placed(1, at(0, 0)), placed(2, at(0, 20)), wall));
}

TEST(R06_01_Visibility, ModelsInBaseContactCanSeeEachOther) {
    const Position a = at(5, 5);
    const Position b{a.x + 32.0f, a.y, 0.0f};  // Intercessor bases touching
    const std::vector<PlacedModel> blockers{placed(3, Position{a.x + 16.0f, a.y + 16.0f + 14.25f, 0.0f},
                                                   kTermagantBase)};
    EXPECT_TRUE(has_line_of_sight(placed(1, a), placed(2, b, kIntercessorBase), blockers));
}
