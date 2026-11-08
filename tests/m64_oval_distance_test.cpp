// M64 (D-074 part 1): the oval geometry kernel. 01.04 distances between bases of any shape with a facing, the
// EPS/2 overlap rule, the facing convention, and the round-round results kept bit-identical.

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "core/geometry/closest.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/oval.hpp"
#include "core/geometry/pose.hpp"
#include "support/oval_brute_force.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::at;
using wh40k::test::in;
namespace brute = wh40k::test::oval;

namespace {

const Base kTyrannofex{BaseShape::Oval, 92.0f, 120.0f};
const Base kRound32{BaseShape::Round, 32.0f, 0.0f};
const Base kRound25{BaseShape::Round, 25.0f, 0.0f};

BasePose pose(Position p, const Base& b, float facing = 0.0f) { return BasePose{p, b, facing}; }

std::uint32_t bits(float f) { return std::bit_cast<std::uint32_t>(f); }

Base random_oval(brute::Rng& r) {
    if (r.chance(0.15)) return kTyrannofex;
    const float w = r.uniform_f(25.0, 110.0);
    const float l = r.chance(0.05) ? w : w * r.uniform_f(1.0, 1.6);
    return Base{BaseShape::Oval, w, l};
}

Base random_round(brute::Rng& r) { return Base{BaseShape::Round, r.uniform_f(25.0, 60.0), 0.0f}; }

// `b` placed from `a`'s centre at a random direction: mostly near (overlapping, touching, a few mm apart), some far.
Position random_near(brute::Rng& r, const Position& a, double reach) {
    const double ang = r.uniform(0.0, 2.0 * brute::kPi);
    const double d = r.chance(0.1) ? r.uniform(reach, 1500.0) : r.uniform(0.0, reach + 50.0);
    return Position{a.x + static_cast<float>(d * std::cos(ang)), a.y + static_cast<float>(d * std::sin(ang)), 0.0f};
}

// ---- copies of the round-only formulas as they were before M64 (distance.hpp, distance.cpp, path.cpp) ----
float old_centre_distance(const Position& a, const Position& b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}
float old_radius(const Base& b) { return b.diameter_mm * 0.5f; }
float old_base_distance(const Position& a, const Base& ba, const Position& b, const Base& bb) {
    return std::max(0.0f, old_centre_distance(a, b) - old_radius(ba) - old_radius(bb));
}
bool old_bases_overlap(const Position& a, const Base& ba, const Position& b, const Base& bb) {
    return old_centre_distance(a, b) < old_radius(ba) + old_radius(bb) - EPS_MM;
}
bool old_within_area(const Rect& area, const Position& pos, const Base& base) {
    const float nx = std::clamp(pos.x, area.min_x, area.max_x);
    const float ny = std::clamp(pos.y, area.min_y, area.max_y);
    return old_centre_distance(pos, Position{nx, ny, pos.z}) <= old_radius(base) + EPS_MM;
}
bool old_wholly_within(const Rect& area, const Position& pos, const Base& base) {
    const float r = old_radius(base);
    return pos.x - r >= area.min_x - EPS_MM && pos.x + r <= area.max_x + EPS_MM && pos.y - r >= area.min_y - EPS_MM &&
           pos.y + r <= area.max_y + EPS_MM;
}

}  // namespace

// ============================== R01_04: distances ==============================

TEST(R01_04_OvalDistance, RoundToOvalWithinAHundredthOfAMillimetreOfBoundarySamplingOver10000Poses) {
    brute::Rng r(64001);
    double worst = 0.0;
    int overlapping = 0;
    int apart = 0;
    for (int i = 0; i < 10000; ++i) {
        const BasePose oval = pose(Position{r.uniform_f(-200, 200), r.uniform_f(-200, 200)}, random_oval(r),
                                   r.uniform_f(-180, 180));
        const Base rb = random_round(r);
        const BasePose round = pose(random_near(r, oval.position, 0.5 * oval.base.length_mm + 0.5 * rb.diameter_mm), rb,
                                    r.uniform_f(-180, 180));
        const brute::Ell e = brute::ell_of(oval);
        const double sd = brute::point_ellipse_sd(brute::P2{round.position.x, round.position.y}, e) -
                          0.5 * round.base.diameter_mm;
        ASSERT_NEAR(base_distance(oval, round), std::max(0.0, sd), 0.01) << "pose " << i;
        ASSERT_NEAR(base_distance(round, oval), std::max(0.0, sd), 0.01) << "pose " << i;
        ASSERT_NEAR(signed_base_distance(oval, round), sd, 0.01) << "pose " << i;
        worst = std::max(worst, std::abs(static_cast<double>(signed_base_distance(round, oval)) - sd));
        (sd < 0.0 ? overlapping : apart) += 1;
    }
    EXPECT_GT(overlapping, 1500);  // both kinds are exercised
    EXPECT_GT(apart, 1500);
    RecordProperty("worst_error_mm", std::to_string(worst));
}

TEST(R01_04_OvalDistance, OvalToOvalWithinAHundredthOfAMillimetreOfBruteForceOver10000Poses) {
    brute::Rng r(64002);
    double worst_distance = 0.0;
    double worst_signed = 0.0;
    int overlapping = 0;
    int apart = 0;
    for (int i = 0; i < 10000; ++i) {
        const BasePose a = pose(Position{r.uniform_f(-200, 200), r.uniform_f(-200, 200)}, random_oval(r),
                                r.uniform_f(-180, 180));
        const Base bb = random_oval(r);
        const BasePose b =
            pose(random_near(r, a.position, 0.5 * (a.base.length_mm + bb.length_mm)), bb, r.uniform_f(-180, 180));
        const brute::Ell ea = brute::ell_of(a);
        const brute::Ell eb = brute::ell_of(b);
        // 01.04 against boundary sampling; the signed distance against the support-function duality.
        const double d = brute::ellipse_ellipse_distance(ea, eb);
        const double sd = brute::support_signed_distance(ea, eb);
        ASSERT_NEAR(base_distance(a, b), d, 0.01) << "pose " << i;
        ASSERT_NEAR(base_distance(b, a), d, 0.01) << "pose " << i;
        ASSERT_NEAR(signed_base_distance(a, b), sd, 0.01) << "pose " << i;
        worst_distance = std::max(worst_distance, std::abs(static_cast<double>(base_distance(a, b)) - d));
        worst_signed = std::max(worst_signed, std::abs(static_cast<double>(signed_base_distance(a, b)) - sd));
        (sd < 0.0 ? overlapping : apart) += 1;
    }
    EXPECT_GT(overlapping, 1500);
    EXPECT_GT(apart, 1500);
    RecordProperty("worst_distance_error_mm", std::to_string(worst_distance));
    RecordProperty("worst_signed_error_mm", std::to_string(worst_signed));
}

TEST(R01_04_OvalDistance, TouchingOverlappingNestedAndFarCases) {
    // Two Tyrannofexes at facing 0 side by side: 92 mm between centres is touching (their widths meet).
    const BasePose t0 = pose(Position{0, 0}, kTyrannofex);
    EXPECT_NEAR(base_distance(t0, pose(Position{92.0f, 0}, kTyrannofex)), 0.0f, 1e-4f);
    EXPECT_NEAR(signed_base_distance(t0, pose(Position{92.5f, 0}, kTyrannofex)), 0.5f, 1e-4f);
    EXPECT_NEAR(signed_base_distance(t0, pose(Position{90.0f, 0}, kTyrannofex)), -2.0f, 1e-4f);
    // Tip to tip along y: 120 mm.
    EXPECT_NEAR(base_distance(t0, pose(Position{0, 121.0f}, kTyrannofex)), 1.0f, 1e-4f);
    // One turned across the other's tip: 60 + 46 between centres.
    EXPECT_NEAR(signed_base_distance(t0, pose(Position{0, 107.0f}, kTyrannofex, 90.0f)), 1.0f, 1e-4f);
    // A 32 mm base 1" off the Tyrannofex's tip and 1" off its side.
    EXPECT_NEAR(base_distance(t0, pose(Position{0, 60.0f + 16.0f + in(1)}, kRound32)), in(1), 1e-3f);
    EXPECT_NEAR(base_distance(t0, pose(Position{46.0f + 16.0f + in(1), 0}, kRound32)), in(1), 1e-3f);
    // Touching at the tip.
    EXPECT_NEAR(base_distance(t0, pose(Position{0, -76.0f}, kRound32)), 0.0f, 1e-4f);
    // Nested: a 25 mm base on the Tyrannofex's centre has to move 46 + 12.5 mm (across the width) to come out.
    EXPECT_EQ(base_distance(t0, pose(Position{0, 0}, kRound25)), 0.0f);
    EXPECT_NEAR(signed_base_distance(t0, pose(Position{0, 0}, kRound25)), -58.5f, 1e-4f);
    // Nested ovals: a small one inside, sharing the centre and facing: depth = 46 + 20 across the width.
    const Base small{BaseShape::Oval, 40.0f, 50.0f};
    EXPECT_EQ(base_distance(t0, pose(Position{0, 0}, small)), 0.0f);
    EXPECT_NEAR(signed_base_distance(t0, pose(Position{0, 0}, small)), -66.0f, 1e-3f);
    EXPECT_TRUE(bases_overlap(t0, pose(Position{0, 0}, small)));
    // Far: across the battlefield.
    EXPECT_NEAR(base_distance(pose(Position{0, 0}, kTyrannofex, 90.0f), pose(Position{1000.0f, 0}, kRound32)),
                1000.0f - 60.0f - 16.0f, 1e-3f);
    EXPECT_NEAR(base_distance(pose(Position{0, 0}, kTyrannofex, 90.0f), pose(Position{0, 1200.0f}, kTyrannofex)),
                1200.0f - 46.0f - 60.0f, 1e-3f);
}

TEST(R01_04_OvalDistance, ADegenerateOvalIsTheCircleWithinAThousandthOfAMillimetre) {
    // length == width: an oval that is a circle, at any facing.
    brute::Rng r(64003);
    for (int i = 0; i < 2000; ++i) {
        const float d = r.uniform_f(25.0, 100.0);
        const Base as_oval{BaseShape::Oval, d, d};
        const Base as_round{BaseShape::Round, d, 0.0f};
        const Position p{r.uniform_f(-100, 100), r.uniform_f(-100, 100)};
        const float facing = r.uniform_f(-180, 180);
        const Base other = r.chance(0.5) ? random_round(r) : random_oval(r);
        const BasePose q = pose(random_near(r, p, d), other, r.uniform_f(-180, 180));
        ASSERT_NEAR(base_distance(pose(p, as_oval, facing), q), base_distance(pose(p, as_round), q), 0.001f) << i;
        ASSERT_NEAR(signed_base_distance(pose(p, as_oval, facing), q), signed_base_distance(pose(p, as_round), q), 0.001f)
            << i;
    }
}

// ============================== D-074 ==============================

TEST(D074_OvalBases, RoundToRoundIsBitIdenticalToTheOldFormulas) {
    brute::Rng r(64004);
    const Base bases[] = {Base{BaseShape::Round, 32.0f, 0.0f}, Base{BaseShape::Round, 25.0f, 0.0f},
                          Base{BaseShape::Round, 28.5f, 0.0f}, Base{BaseShape::Round, 40.0f, 0.0f},
                          Base{BaseShape::Round, 50.0f, 0.0f}};
    const Rect area{in(2), in(3), in(20), in(9)};
    int overlaps = 0;
    for (int i = 0; i < 200000; ++i) {
        const Base& ba = bases[r.next() % 5];
        const Base& bb = bases[r.next() % 5];
        const Position pa{r.uniform_f(0, 600), r.uniform_f(0, 600)};
        // Mostly near, including centre distances at exactly the touching and overlap thresholds.
        Position pb{pa.x + r.uniform_f(-80, 80), pa.y + r.uniform_f(-80, 80)};
        if (i % 7 == 0) pb = Position{pa.x + old_radius(ba) + old_radius(bb) - EPS_MM, pa.y};
        if (i % 11 == 0) pb = Position{pa.x, pa.y + old_radius(ba) + old_radius(bb)};
        const BasePose a = pose(pa, ba, r.uniform_f(-180, 180));  // a round base ignores facing
        const BasePose b = pose(pb, bb, r.uniform_f(-180, 180));
        ASSERT_EQ(bits(base_distance(a, b)), bits(old_base_distance(pa, ba, pb, bb))) << i;
        ASSERT_EQ(bits(base_distance(a, b)), bits(base_distance(pa, ba, pb, bb))) << i;
        ASSERT_EQ(bits(signed_base_distance(a, b)), bits(old_centre_distance(pa, pb) - old_radius(ba) - old_radius(bb)))
            << i;
        ASSERT_EQ(bases_overlap(a, b), old_bases_overlap(pa, ba, pb, bb)) << i;
        ASSERT_EQ(within_area(area, a), old_within_area(area, pa, ba)) << i;
        ASSERT_EQ(wholly_within(area, a), old_wholly_within(area, pa, ba)) << i;
        const BaseExtents ext = base_extents(ba, a.facing_deg);
        ASSERT_EQ(bits(ext.half_x_mm), bits(old_radius(ba)));
        ASSERT_EQ(bits(ext.half_y_mm), bits(old_radius(ba)));
        overlaps += bases_overlap(a, b) ? 1 : 0;
    }
    EXPECT_GT(overlaps, 10000);
}

TEST(D074_OvalBases, PosedModelFunctionsMatchTheRoundOnesExactly) {
    // models_engaged, units_engaged, engaged_models, check_coherency, PosedCoherencyWithOneMoved, closest_distance,
    // closest_models and closest_units on round bases give the round-only functions' answers.
    brute::Rng r(64005);
    const Base bases[] = {Base{BaseShape::Round, 32.0f, 0.0f}, Base{BaseShape::Round, 25.0f, 0.0f},
                          Base{BaseShape::Round, 28.5f, 0.0f}, Base{BaseShape::Round, 40.0f, 0.0f}};
    const auto make_unit = [&](std::uint32_t first_id, std::size_t n, float x0, float y0, float spread) {
        std::vector<PlacedModel> placed;
        std::vector<PosedModel> posed;
        for (std::size_t k = 0; k < n; ++k) {
            const Position p{x0 + r.uniform_f(0, spread), y0 + r.uniform_f(0, spread)};
            const Base& b = bases[r.next() % 4];
            placed.push_back(PlacedModel{ModelId{first_id + static_cast<std::uint32_t>(k)}, p, b});
            posed.push_back(PosedModel{ModelId{first_id + static_cast<std::uint32_t>(k)}, p, b, r.uniform_f(-180, 180)});
        }
        return std::pair{placed, posed};
    };
    for (int trial = 0; trial < 400; ++trial) {
        const std::size_t n = 1 + r.next() % 10;
        const float spread = r.uniform_f(30, 260);
        const auto [ua, pa] = make_unit(1, n, 0, 0, spread);
        const auto [ub, pb] = make_unit(100, 1 + r.next() % 10, r.uniform_f(-100, 200), r.uniform_f(-100, 200), spread);
        const auto [uc, pc] = make_unit(200, 1 + r.next() % 5, r.uniform_f(-100, 300), r.uniform_f(-100, 300), spread);
        ASSERT_EQ(units_engaged(pa, pb), units_engaged(ua, ub));
        ASSERT_EQ(engaged_models(pa, pb), engaged_models(ua, ub));
        for (std::size_t k = 0; k < n; ++k) ASSERT_EQ(models_engaged(pa[k], pb[0]), models_engaged(ua[k], ub[0]));
        const CoherencyResult cr = check_coherency(std::span<const PlacedModel>(ua));
        const CoherencyResult cp = check_coherency(std::span<const PosedModel>(pa));
        ASSERT_EQ(cp.isolated, cr.isolated);
        ASSERT_EQ(cp.too_far, cr.too_far);
        const std::optional<float> dr = closest_distance(std::span<const PlacedModel>(ua), std::span<const PlacedModel>(ub));
        const std::optional<float> dp = closest_distance(std::span<const PosedModel>(pa), std::span<const PosedModel>(pb));
        ASSERT_TRUE(dr && dp);
        ASSERT_EQ(bits(*dp), bits(*dr));
        ASSERT_EQ(closest_models(pa[0], pb), closest_models(ua[0], ub));
        const std::vector<std::vector<PlacedModel>> round_units{ub, uc};
        const std::vector<std::vector<PosedModel>> posed_units{pb, pc};
        ASSERT_EQ(closest_units(std::span<const PosedModel>(pa), posed_units),
                  closest_units(std::span<const PlacedModel>(ua), round_units));
        // One model tried at random spots.
        const std::size_t moved = r.next() % n;
        const CoherencyWithOneMoved fast_round(ua, moved);
        const PosedCoherencyWithOneMoved fast_posed(pa, moved);
        for (int spot = 0; spot < 20; ++spot) {
            const Position p{r.uniform_f(-20, spread + 20), r.uniform_f(-20, spread + 20)};
            const float facing = r.uniform_f(-180, 180);
            ASSERT_EQ(fast_posed.ok_at(p, facing), fast_round.ok_at(p));
        }
    }
}

TEST(D074_OvalBases, PosedFunctionsHandleOvals) {
    // A Tyrannofex 1.9" from a Termagant across its width is engaged (03.04, within 2"); at 2.1" it is not.
    const PosedModel tyrannofex{ModelId{1}, at(10, 10), kTyrannofex, 0.0f};
    const Base gant{BaseShape::Round, 28.5f, 0.0f};
    const auto gant_at = [&](float gap_in) {
        return PosedModel{ModelId{2}, Position{in(10) + 46.0f + 14.25f + in(gap_in), in(10)}, gant, 0.0f};
    };
    EXPECT_TRUE(models_engaged(tyrannofex, gant_at(1.9f)));
    EXPECT_FALSE(models_engaged(tyrannofex, gant_at(2.1f)));
    // Turned to face east (90), its length points at the Termagant: 14 mm closer, so 2.1" across the width is engaged.
    PosedModel turned = tyrannofex;
    turned.facing_deg = 90.0f;
    EXPECT_TRUE(models_engaged(turned, gant_at(2.1f)));
    // Closest model: the one off the tip is closer than the one off the side at the same centre distance.
    const std::vector<PosedModel> candidates{
        PosedModel{ModelId{3}, Position{in(10) + 100.0f, in(10)}, kRound32, 0.0f},
        PosedModel{ModelId{4}, Position{in(10), in(10) + 100.0f}, kRound32, 0.0f}};
    EXPECT_EQ(closest_models(tyrannofex, candidates), std::vector<ModelId>{ModelId{4}});
    EXPECT_NEAR(*closest_distance(std::span<const PosedModel>(&tyrannofex, 1), candidates), 100.0f - 60.0f - 16.0f,
                1e-3f);
    // 03.03 coherency between ovals: two Tyrannofexes side by side 1.9" apart are coherent; at 2.1" they are not.
    const std::vector<PosedModel> pair_close{tyrannofex,
                                             PosedModel{ModelId{5}, Position{in(10) + 92.0f + in(1.9f), in(10)}, kTyrannofex}};
    const std::vector<PosedModel> pair_far{tyrannofex,
                                           PosedModel{ModelId{5}, Position{in(10) + 92.0f + in(2.1f), in(10)}, kTyrannofex}};
    EXPECT_TRUE(check_coherency(std::span<const PosedModel>(pair_close)).ok());
    EXPECT_FALSE(check_coherency(std::span<const PosedModel>(pair_far)).ok());
    EXPECT_FALSE(PosedCoherencyWithOneMoved(pair_close, 1).ok_at(pair_far[1].position, 0.0f));
    EXPECT_TRUE(PosedCoherencyWithOneMoved(pair_far, 1).ok_at(pair_close[1].position, 0.0f));
}

TEST(D074_OvalBases, OverlapMeansTheyStillIntersectAfterShrinkingEachByHalfEps) {
    // Round (unchanged): shrinking radii r1 and r2 by EPS/2 each leaves discs whose interiors meet iff the centres are
    // less than r1 + r2 - EPS apart, today's rule. A 0.009 mm overlap is allowed; 0.011 mm is not.
    const Base r16 = kRound32;
    EXPECT_FALSE(bases_overlap(pose(Position{0, 0}, r16), pose(Position{32.0f - 0.009f, 0}, r16)));
    EXPECT_TRUE(bases_overlap(pose(Position{0, 0}, r16), pose(Position{32.0f - 0.011f, 0}, r16)));
    // Round to oval, at the Tyrannofex's side and its tip.
    const BasePose t0 = pose(Position{0, 0}, kTyrannofex);
    EXPECT_FALSE(bases_overlap(t0, pose(Position{62.0f - 0.009f, 0}, kRound32)));
    EXPECT_TRUE(bases_overlap(t0, pose(Position{62.0f - 0.011f, 0}, kRound32)));
    EXPECT_FALSE(bases_overlap(t0, pose(Position{0, -(76.0f - 0.009f)}, kRound32)));
    EXPECT_TRUE(bases_overlap(t0, pose(Position{0, -(76.0f - 0.011f)}, kRound32)));
    // Oval to oval: side to side and tip to side.
    EXPECT_FALSE(bases_overlap(t0, pose(Position{92.0f - 0.009f, 0}, kTyrannofex)));
    EXPECT_TRUE(bases_overlap(t0, pose(Position{92.0f - 0.011f, 0}, kTyrannofex)));
    EXPECT_FALSE(bases_overlap(t0, pose(Position{0, 106.0f - 0.009f}, kTyrannofex, 90.0f)));
    EXPECT_TRUE(bases_overlap(t0, pose(Position{0, 106.0f - 0.011f}, kTyrannofex, 90.0f)));
    // Touching bases do not overlap.
    EXPECT_FALSE(bases_overlap(t0, pose(Position{92.0f, 0}, kTyrannofex)));
    // Random poses: overlap <=> penetration depth > EPS (brute force), away from the threshold.
    brute::Rng r(64006);
    int checked = 0;
    for (int i = 0; i < 3000; ++i) {
        const BasePose a = pose(Position{0, 0}, random_oval(r), r.uniform_f(-180, 180));
        const Base bb = r.chance(0.5) ? random_oval(r) : random_round(r);
        const BasePose b = pose(random_near(r, a.position, 0.5 * (a.base.length_mm + std::max(bb.length_mm, bb.diameter_mm))),
                                bb, r.uniform_f(-180, 180));
        const double sd = bb.shape == BaseShape::Round
                              ? brute::point_ellipse_sd(brute::P2{b.position.x, b.position.y}, brute::ell_of(a)) -
                                    0.5 * bb.diameter_mm
                              : brute::support_signed_distance(brute::ell_of(a), brute::ell_of(b));
        if (std::abs(sd + EPS_MM) < 1e-4) continue;
        ASSERT_EQ(bases_overlap(a, b), sd < -EPS_MM) << i;
        ASSERT_EQ(bases_overlap(b, a), sd < -EPS_MM) << i;
        ++checked;
    }
    EXPECT_GT(checked, 2900);
}

TEST(D074_OvalBases, FacingZeroPutsTheLengthAlongYAndPositiveTurnsCounterClockwise) {
    // A 0-degree Tyrannofex is 120 mm along y (south-north) and 92 mm along x.
    EXPECT_EQ(base_extents(kTyrannofex, 0.0f), (BaseExtents{46.0f, 60.0f}));
    EXPECT_EQ(base_extents(kTyrannofex, 180.0f), (BaseExtents{46.0f, 60.0f}));
    const BaseExtents e90 = base_extents(kTyrannofex, 90.0f);
    EXPECT_NEAR(e90.half_x_mm, 60.0f, 1e-4f);
    EXPECT_NEAR(e90.half_y_mm, 46.0f, 1e-4f);
    const BaseExtents e45 = base_extents(kTyrannofex, 45.0f);
    EXPECT_NEAR(e45.half_x_mm, std::sqrt((46.0f * 46.0f + 60.0f * 60.0f) / 2.0f), 1e-3f);
    EXPECT_NEAR(e45.half_y_mm, e45.half_x_mm, 1e-3f);
    // Positive facings turn counter-clockwise (from +y towards -x): at 30 the tip is at 60 mm along (-sin 30, cos 30).
    using namespace oval_detail;
    const Ellipse e30 = ellipse_of(pose(Position{0, 0}, kTyrannofex, 30.0f));
    EXPECT_NEAR(e30.u.x, -0.5, 1e-12);
    EXPECT_NEAR(e30.u.y, std::sqrt(3.0) / 2.0, 1e-12);
    EXPECT_NEAR(point_ellipse_signed_distance(Vec{-30.0, 60.0 * std::sqrt(3.0) / 2.0}, e30), 0.0, 1e-9);
    // The clockwise point is well off the base: only the counter-clockwise one is its tip.
    EXPECT_GT(point_ellipse_signed_distance(Vec{30.0, 60.0 * std::sqrt(3.0) / 2.0}, e30), 1.0);
    // A round base ignores facing.
    EXPECT_EQ(base_extents(kRound32, 37.0f), (BaseExtents{16.0f, 16.0f}));
}

TEST(D074_OvalBases, FacingsNormaliseToMinus180Exclusive180Inclusive) {
    EXPECT_EQ(normalise_facing_deg(0.0f), 0.0f);
    EXPECT_EQ(normalise_facing_deg(180.0f), 180.0f);
    EXPECT_EQ(normalise_facing_deg(-180.0f), 180.0f);
    EXPECT_EQ(normalise_facing_deg(190.0f), -170.0f);
    EXPECT_EQ(normalise_facing_deg(-190.0f), 170.0f);
    EXPECT_EQ(normalise_facing_deg(540.0f), 180.0f);
    EXPECT_EQ(normalise_facing_deg(-540.0f), 180.0f);
    EXPECT_EQ(normalise_facing_deg(720.0f), 0.0f);
    EXPECT_EQ(normalise_facing_deg(359.5f), -0.5f);
    EXPECT_FALSE(std::signbit(normalise_facing_deg(-0.0f)));
    EXPECT_FALSE(std::signbit(normalise_facing_deg(-360.0f)));
    EXPECT_EQ(normalise_facing_deg(std::numeric_limits<float>::quiet_NaN()), 0.0f);
    EXPECT_EQ(normalise_facing_deg(std::numeric_limits<float>::infinity()), 0.0f);
    EXPECT_EQ(normalise_facing_deg(std::nextafter(-180.0f, 0.0f)), std::nextafter(-180.0f, 0.0f));
    brute::Rng r(64007);
    for (int i = 0; i < 100000; ++i) {
        const float f = i % 3 == 0 ? r.uniform_f(-1e7, 1e7) : r.uniform_f(-1000, 1000);
        const float n = normalise_facing_deg(f);
        ASSERT_GT(n, -180.0f) << f;
        ASSERT_LE(n, 180.0f) << f;
        ASSERT_EQ(normalise_facing_deg(n), n) << f;  // idempotent
        if (std::abs(f) < 1000.0f) ASSERT_NEAR(std::remainder(static_cast<double>(f) - n, 360.0), 0.0, 1e-3) << f;
    }
    // The turn the shorter way, in (-180, 180]; a half turn is +180.
    EXPECT_EQ(rotation_delta_deg(170.0f, -170.0f), 20.0f);
    EXPECT_EQ(rotation_delta_deg(-170.0f, 170.0f), -20.0f);
    EXPECT_EQ(rotation_delta_deg(0.0f, 180.0f), 180.0f);
    EXPECT_EQ(rotation_delta_deg(0.0f, -180.0f), 180.0f);
    EXPECT_EQ(rotation_delta_deg(-90.0f, 90.0f), 180.0f);
    EXPECT_EQ(rotation_delta_deg(10.0f, 10.0f), 0.0f);
    EXPECT_EQ(rotation_delta_deg(10.0f, 370.0f), 0.0f);
}

TEST(D074_OvalBases, BoundingCirclesHoldEveryFacing) {
    EXPECT_EQ(circumscribed_radius(kTyrannofex), 60.0f);
    EXPECT_EQ(inscribed_radius(kTyrannofex), 46.0f);
    EXPECT_EQ(circumscribed_radius(kRound32), 16.0f);
    EXPECT_EQ(inscribed_radius(kRound32), 16.0f);
    // The distance between bounding circles brackets the true distance.
    brute::Rng r(64008);
    for (int i = 0; i < 3000; ++i) {
        const BasePose a = pose(Position{0, 0}, random_oval(r), r.uniform_f(-180, 180));
        const Base bb = r.chance(0.5) ? random_oval(r) : random_round(r);
        const BasePose b = pose(random_near(r, a.position, 120.0), bb, r.uniform_f(-180, 180));
        const float cd = centre_distance(a.position, b.position);
        const float d = base_distance(a, b);
        ASSERT_GE(d + 1e-3f, std::max(0.0f, cd - circumscribed_radius(a.base) - circumscribed_radius(b.base))) << i;
        ASSERT_LE(d - 1e-3f, std::max(0.0f, cd - inscribed_radius(a.base) - inscribed_radius(b.base))) << i;
    }
}

TEST(D074_OvalBases, TheRoundOnlyFunctionsStillThrowForOvals) {
    // Callers that assume round bases stay loud until M65 converts them.
    EXPECT_THROW((void)base_radius(kTyrannofex), std::logic_error);
    EXPECT_THROW((void)base_distance(Position{0, 0}, kTyrannofex, Position{200, 0}, kRound32), std::logic_error);
    EXPECT_THROW((void)bases_overlap(Position{0, 0}, kRound32, Position{200, 0}, kTyrannofex), std::logic_error);
}

TEST(D074_OvalBases, TrigonometryIsPolynomialAndAccurate) {
    using namespace oval_detail;
    double s = 0.0;
    double c = 0.0;
    for (int k = -8; k <= 8; ++k) {  // exact at quarter turns
        sin_cos_deg(90.0 * k, s, c);
        const int q = ((k % 4) + 4) % 4;
        EXPECT_EQ(s, q == 1 ? 1.0 : (q == 3 ? -1.0 : 0.0)) << k;
        EXPECT_EQ(c, q == 0 ? 1.0 : (q == 2 ? -1.0 : 0.0)) << k;
    }
    brute::Rng r(64009);
    for (int i = 0; i < 100000; ++i) {
        const double deg = r.uniform(-720.0, 720.0);
        sin_cos_deg(deg, s, c);
        // The reference in long double where it has one (deg * pi / 180 in double is itself off by up to 1.5e-15).
        const long double rad = static_cast<long double>(deg) * 3.14159265358979323846264338327950288L / 180.0L;
        ASSERT_NEAR(s, static_cast<double>(std::sin(rad)), 4e-15) << deg;
        ASSERT_NEAR(c, static_cast<double>(std::cos(rad)), 4e-15) << deg;
        const double y = r.uniform(-100, 100);
        const double x = r.uniform(-100, 100);
        const double a = atan2_deg(y, x);
        ASSERT_NEAR(a, std::atan2(y, x) * 180.0 / brute::kPi, 1e-11) << y << " " << x;
        ASSERT_GT(a, -180.0);
        ASSERT_LE(a, 180.0);
    }
    EXPECT_EQ(atan2_deg(0.0, 0.0), 0.0);
    EXPECT_EQ(atan2_deg(0.0, -1.0), 180.0);
    EXPECT_EQ(atan2_deg(-0.0, -1.0), 180.0);
    EXPECT_NEAR(atan2_deg(1.0, 0.0), 90.0, 1e-12);
}

TEST(D074_OvalBases, TheKernelIsDeterministicFiniteAndBracketsItsValue) {
    brute::Rng r(64010);
    int widest = 0;
    double widest_mm = 0.0;
    for (int i = 0; i < 3000; ++i) {
        const BasePose a = pose(Position{r.uniform_f(-500, 500), r.uniform_f(-500, 500)}, random_oval(r),
                                r.uniform_f(-180, 180));
        const Base bb = random_oval(r);
        // Including coincident centres and huge distances.
        const Position pb = i % 10 == 0 ? a.position : random_near(r, a.position, 120.0);
        const BasePose b = pose(pb, bb, r.uniform_f(-180, 180));
        const oval_detail::SignedDistance s1 = oval_detail::pose_signed_distance(a, b);
        const oval_detail::SignedDistance s2 = oval_detail::pose_signed_distance(a, b);
        ASSERT_TRUE(std::isfinite(s1.value) && std::isfinite(s1.lo) && std::isfinite(s1.hi)) << i;
        ASSERT_EQ(std::bit_cast<std::uint64_t>(s1.value), std::bit_cast<std::uint64_t>(s2.value)) << i;
        ASSERT_LE(s1.lo, s1.value) << i;
        ASSERT_LE(s1.value, s1.hi) << i;
        ASSERT_LE(s1.hi - s1.lo, 1e-4) << i;
        if (s1.hi - s1.lo > widest_mm) {
            widest_mm = s1.hi - s1.lo;
            widest = i;
        }
    }
    RecordProperty("widest_bracket_mm", std::to_string(widest_mm));
    RecordProperty("widest_bracket_pose", widest);
}
