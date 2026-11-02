#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "core/geometry/path.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::at;
using wh40k::test::in;
using wh40k::test::kIntercessorBase;
using wh40k::test::kTermagantBase;
using wh40k::test::placed;

namespace {

const Rect kBoard{0.0f, 0.0f, in(24), in(24)};
const ModelId kMover{1};

// Intercessor (r = 16 mm) moving from (2",12") to (12",12"). An enemy Termagant (r = 14.25 mm)
// sits at x = 7" with its centre `offset_mm` from the path line: the bases just touch when
// offset = 30.25 mm.
std::vector<PathViolation> pass_enemy(float offset_mm, bool through_allowed = false) {
    const std::vector<Position> path{at(2, 12), at(12, 12)};
    const std::vector<PlacedModel> enemy{
        placed(20, Position{in(7), in(12) + offset_mm}, kTermagantBase)};
    PathContext ctx{kBoard, {}, enemy, through_allowed};
    return check_model_path(kMover, kIntercessorBase, path, ctx);
}

bool has(const std::vector<PathViolation>& v, PathViolationKind k) {
    for (const auto& p : v) {
        if (p.kind == k) return true;
    }
    return false;
}

}  // namespace

// ---- Path length (03.01 Moving a Model in a Straight Line) ----

TEST(R03_01_PathLength, SumsStraightLineSegments) {
    // Rulebook example: 3" in a straight line, then 3" in another straight line = 6".
    const std::vector<Position> path{at(5, 5), at(8, 5), at(8, 8)};
    EXPECT_NEAR(path_length(path), in(6), 1e-3f);
}

TEST(R03_01_PathLength, SinglePointIsZero) {
    const std::vector<Position> path{at(5, 5)};
    EXPECT_EQ(path_length(path), 0.0f);
}

TEST(R03_01_PathLength, DiagonalSegment) {
    const std::vector<Position> path{at(0, 0), at(3, 4)};
    EXPECT_NEAR(path_length(path), in(5), 1e-3f);
}

// ---- Enemy models ----

TEST(R03_01_MovingThroughModels, PathClippingEnemyBaseBy0_5mmIsRejected) {
    const auto v = pass_enemy(30.25f - 0.5f);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].kind, PathViolationKind::CrossesEnemyModel);
    EXPECT_EQ(v[0].other, ModelId{20});
    EXPECT_EQ(v[0].segment, std::size_t{0});
    EXPECT_NE(v[0].reason.find("03.01"), std::string::npos);
}

TEST(R03_01_MovingThroughModels, PathGrazingEnemyBaseInContactIsAllowed) {
    EXPECT_TRUE(pass_enemy(30.25f).empty());
}

TEST(R03_01_MovingThroughModels, PathStraightThroughEnemyIsRejected) {
    EXPECT_TRUE(has(pass_enemy(0.0f), PathViolationKind::CrossesEnemyModel));
}

TEST(R03_01_MovingThroughModels, MoveTypeMayAllowMovingThroughEnemyModels) {
    EXPECT_TRUE(pass_enemy(0.0f, /*through_allowed=*/true).empty());
}

TEST(R03_01_MovingThroughModels, PathThroughFriendlyBaseIsAccepted) {
    const std::vector<Position> path{at(2, 12), at(12, 12)};
    const std::vector<PlacedModel> friendly{placed(2, at(7, 12))};
    const PathContext ctx{kBoard, friendly, {}};
    EXPECT_TRUE(check_model_path(kMover, kIntercessorBase, path, ctx).empty());
}

TEST(R03_01_MovingThroughModels, SecondSegmentClippingEnemyReportsThatSegment) {
    const std::vector<Position> path{at(2, 2), at(2, 10), at(10, 10)};
    const std::vector<PlacedModel> enemy{placed(20, at(6, 10), kTermagantBase)};
    const PathContext ctx{kBoard, {}, enemy};
    const auto v = check_model_path(kMover, kIntercessorBase, path, ctx);
    ASSERT_FALSE(v.empty());
    EXPECT_EQ(v[0].kind, PathViolationKind::CrossesEnemyModel);
    EXPECT_EQ(v[0].segment, std::size_t{1});
}

// ---- Ending a move ----

TEST(R03_01_EndingAMove, EndingOnFriendlyModelIsRejected) {
    const std::vector<Position> path{at(2, 12), at(7, 12)};
    const std::vector<PlacedModel> friendly{placed(2, at(7, 12))};
    const PathContext ctx{kBoard, friendly, {}};
    const auto v = check_model_path(kMover, kIntercessorBase, path, ctx);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].kind, PathViolationKind::EndsOnModel);
    EXPECT_EQ(v[0].other, ModelId{2});
}

TEST(R03_01_EndingAMove, EndingPartlyOverlappingFriendlyIsRejected) {
    // Centres 31 mm apart: 1 mm overlap.
    const std::vector<Position> path{at(2, 12), Position{in(7) - 31.0f, in(12)}};
    const std::vector<PlacedModel> friendly{placed(2, at(7, 12))};
    const PathContext ctx{kBoard, friendly, {}};
    EXPECT_TRUE(has(check_model_path(kMover, kIntercessorBase, path, ctx),
                    PathViolationKind::EndsOnModel));
}

TEST(R03_01_EndingAMove, EndingInBaseContactWithFriendlyIsAllowed) {
    const std::vector<Position> path{at(2, 12), Position{in(7) - 32.0f, in(12)}};
    const std::vector<PlacedModel> friendly{placed(2, at(7, 12))};
    const PathContext ctx{kBoard, friendly, {}};
    EXPECT_TRUE(check_model_path(kMover, kIntercessorBase, path, ctx).empty());
}

TEST(R03_01_EndingAMove, EndingOnEnemyIsRejectedEvenWhenMovingThroughIsAllowed) {
    const std::vector<Position> path{at(2, 12), at(7, 12)};
    const std::vector<PlacedModel> enemy{placed(20, at(7, 12), kTermagantBase)};
    const PathContext ctx{kBoard, {}, enemy, /*may_move_through_enemy_models=*/true};
    const auto v = check_model_path(kMover, kIntercessorBase, path, ctx);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].kind, PathViolationKind::EndsOnModel);
}

TEST(R03_01_EndingAMove, MoversOwnStartPositionIsIgnored) {
    // The mover's own entry in the friendly list (its old position) never blocks it.
    const std::vector<Position> path{at(2, 12), at(2.5f, 12)};
    const std::vector<PlacedModel> friendly{placed(1, at(2, 12))};
    const PathContext ctx{kBoard, friendly, {}};
    EXPECT_TRUE(check_model_path(kMover, kIntercessorBase, path, ctx).empty());
}

// ---- Battlefield edge ----

TEST(R03_01_BattlefieldEdge, EndingOverTheEdgeIsRejected) {
    const std::vector<Position> path{at(2, 12), at(0.5f, 12)};  // base radius is 0.63"
    const PathContext ctx{kBoard, {}, {}};
    const auto v = check_model_path(kMover, kIntercessorBase, path, ctx);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].kind, PathViolationKind::LeavesBattlefield);
}

TEST(R03_01_BattlefieldEdge, LeavingAndComingBackIsRejected) {
    const std::vector<Position> path{at(2, 12), Position{10.0f, in(12)}, at(2, 14)};
    const PathContext ctx{kBoard, {}, {}};
    EXPECT_TRUE(has(check_model_path(kMover, kIntercessorBase, path, ctx),
                    PathViolationKind::LeavesBattlefield));
}

TEST(R03_01_BattlefieldEdge, BaseTouchingTheEdgeIsAllowed) {
    const std::vector<Position> path{at(2, 12), Position{16.0f, in(12)}};
    const PathContext ctx{kBoard, {}, {}};
    EXPECT_TRUE(check_model_path(kMover, kIntercessorBase, path, ctx).empty());
}

TEST(R03_01_Path, EmptyPathIsProgrammerError) {
    const std::vector<Position> path;
    const PathContext ctx{kBoard, {}, {}};
    EXPECT_THROW((void)check_model_path(kMover, kIntercessorBase, path, ctx), std::logic_error);
}
