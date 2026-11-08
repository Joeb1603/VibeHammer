// M64 (D-074 part 1): 03.01 moving and rotating bases of any shape: translation sweeps, turns in place against the
// battlefield edge and other bases, and the pose-aware path check.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "core/geometry/oval.hpp"
#include "core/geometry/path.hpp"
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
const Rect kBoard{0.0f, 0.0f, in(44), in(60)};

BasePose pose(Position p, const Base& b, float facing = 0.0f) { return BasePose{p, b, facing}; }

Base random_oval(brute::Rng& r) {
    if (r.chance(0.2)) return kTyrannofex;
    const float w = r.uniform_f(25.0, 110.0);
    return Base{BaseShape::Oval, w, r.chance(0.05) ? w : w * r.uniform_f(1.0, 1.6)};
}
Base random_round(brute::Rng& r) { return Base{BaseShape::Round, r.uniform_f(25.0, 60.0), 0.0f}; }
Base random_any(brute::Rng& r) { return r.chance(0.5) ? random_oval(r) : random_round(r); }
double semi_major(const Base& b) { return 0.5 * std::max(b.diameter_mm, b.length_mm); }

// Brute force for a straight line (03.01): the swept area meets the obstacle where some placement along the segment
// does, so its distance is the smallest signed distance of the placements. Each placement's signed distance comes
// from the support-function duality on 1440 normals (the bases' support radii don't change along the move).
double brute_translation_min(const BasePose& start, const Position& end, const BasePose& obstacle) {
    const brute::Ell a = brute::ell_of(start);
    const brute::Ell b = brute::ell_of(obstacle);
    constexpr int kNormals = 1440;
    std::vector<brute::P2> n(kNormals);
    std::vector<double> s(kNormals);
    for (int k = 0; k < kNormals; ++k) {
        const double th = 2.0 * brute::kPi * k / kNormals;
        n[static_cast<std::size_t>(k)] = brute::P2{std::cos(th), std::sin(th)};
        s[static_cast<std::size_t>(k)] =
            brute::ellipse_support(a, n[static_cast<std::size_t>(k)]) + brute::ellipse_support(b, n[static_cast<std::size_t>(k)]);
    }
    const auto sd_at = [&](double t) {
        const double cx = start.position.x + t * (end.x - start.position.x);
        const double cy = start.position.y + t * (end.y - start.position.y);
        double best = -std::numeric_limits<double>::infinity();
        for (int k = 0; k < kNormals; ++k) {
            const brute::P2& nk = n[static_cast<std::size_t>(k)];
            best = std::max(best, nk.x * (b.c.x - cx) + nk.y * (b.c.y - cy) - s[static_cast<std::size_t>(k)]);
        }
        return best;
    };
    constexpr int kSteps = 400;
    double best = std::numeric_limits<double>::infinity();
    int best_k = 0;
    for (int k = 0; k <= kSteps; ++k) {
        const double v = sd_at(static_cast<double>(k) / kSteps);
        if (v < best) {
            best = v;
            best_k = k;
        }
    }
    const double lo = std::max(0.0, (best_k - 1.0) / kSteps);
    const double hi = std::min(1.0, (best_k + 1.0) / kSteps);
    return std::min(best, brute::golden_min(sd_at, lo, hi, 50));
}

// The margin between the base and the board's edge at `facing` (closed form, std trig), negative outside.
double edge_margin_at(const Rect& r, const BasePose& p, double facing_deg) {
    const brute::Ell e = brute::ell_of(BasePose{p.position, p.base, static_cast<float>(facing_deg)});
    const double f = facing_deg * brute::kPi / 180.0;
    const double s = std::sin(f);
    const double c = std::cos(f);
    const double hx = std::sqrt(e.a * e.a * s * s + e.b * e.b * c * c);
    const double hy = std::sqrt(e.a * e.a * c * c + e.b * e.b * s * s);
    return std::min(std::min(p.position.x - r.min_x, r.max_x - p.position.x) - hx,
                    std::min(p.position.y - r.min_y, r.max_y - p.position.y) - hy);
}

// The smallest value of f over the turn from `from` by `delta` degrees: `samples` samples, refined around the best.
template <class F>
double dense_turn_min(const F& f, double from, double delta, int samples) {
    double best = std::numeric_limits<double>::infinity();
    int best_k = 0;
    for (int k = 0; k <= samples; ++k) {
        const double v = f(from + delta * k / samples);
        if (v < best) {
            best = v;
            best_k = k;
        }
    }
    const double lo = from + delta * std::max(0.0, best_k - 1.0) / samples;
    const double hi = from + delta * std::min(static_cast<double>(samples), best_k + 1.0) / samples;
    return std::min(best, brute::golden_min(f, std::min(lo, hi), std::max(lo, hi), 60));
}

bool has(const std::vector<PathViolation>& v, PathViolationKind k) {
    return std::any_of(v.begin(), v.end(), [&](const PathViolation& p) { return p.kind == k; });
}

}  // namespace

// ============================== straight lines ==============================

TEST(R03_01_OvalSweeps, TranslationMatchesDenseSamplingAlongTheSegment) {
    brute::Rng r(64101);
    int compared = 0;
    int crossing = 0;
    int clear = 0;
    for (int i = 0; i < 500; ++i) {
        // At least one oval: an oval mover, an oval obstacle, or both.
        const int kind = i % 3;
        const Base mb = kind == 1 ? random_round(r) : random_oval(r);
        const Base ob = kind == 2 ? random_round(r) : random_oval(r);
        const BasePose start = pose(Position{0, 0}, mb, r.uniform_f(-180, 180));
        const double ang = r.uniform(0, 2 * brute::kPi);
        const double len = r.uniform(0.0, 300.0);
        const Position end{static_cast<float>(len * std::cos(ang)), static_cast<float>(len * std::sin(ang))};
        // The obstacle near the line: a random point along it, offset sideways by about the two bases' size.
        const double t = r.uniform(-0.2, 1.2);
        const double side = r.uniform(-1.0, 1.0) * (semi_major(mb) + semi_major(ob) + 10.0);
        const Position op{static_cast<float>(t * end.x - side * std::sin(ang)),
                          static_cast<float>(t * end.y + side * std::cos(ang))};
        const BasePose obstacle = pose(op, ob, r.uniform_f(-180, 180));
        const double expected = brute_translation_min(start, end, obstacle);
        if (expected > 0.02) {
            ASSERT_NEAR(translation_signed_distance(start, end, obstacle), expected, 0.01) << i;
            ++compared;
        }
        if (std::abs(expected + EPS_MM) > 0.002) {
            ASSERT_EQ(translation_crosses(start, end, obstacle), expected < -EPS_MM) << i << " " << expected;
            (expected < -EPS_MM ? crossing : clear) += 1;
        }
    }
    EXPECT_GT(compared, 100);
    EXPECT_GT(crossing, 100);
    EXPECT_GT(clear, 100);
}

TEST(R03_01_OvalSweeps, TranslationExamples) {
    // A Tyrannofex facing 90 (its length along x) moving 200 mm north passes a 32 mm base 0.5 mm off its tip's path.
    const BasePose t90 = pose(Position{0, 0}, kTyrannofex, 90.0f);
    const Position north{0, 200.0f};
    EXPECT_FALSE(translation_crosses(t90, north, pose(Position{60.0f + 16.0f + 0.5f, 100.0f}, kRound32)));
    EXPECT_TRUE(translation_crosses(t90, north, pose(Position{60.0f + 16.0f - 0.5f, 100.0f}, kRound32)));
    EXPECT_NEAR(translation_signed_distance(t90, north, pose(Position{60.0f + 16.0f + 0.5f, 100.0f}, kRound32)), 0.5f,
                1e-4f);
    // Facing 0 its width (46 mm each side) is across the move, so the same base is 13.5 mm clear.
    const BasePose t0 = pose(Position{0, 0}, kTyrannofex, 0.0f);
    EXPECT_FALSE(translation_crosses(t0, north, pose(Position{60.0f + 16.0f - 0.5f, 100.0f}, kRound32)));
    EXPECT_NEAR(translation_signed_distance(t0, north, pose(Position{75.5f, 100.0f}, kRound32)), 75.5f - 46.0f - 16.0f,
                1e-4f);
    // An Intercessor moving east past a Tyrannofex 0.5 mm clear of its tip, and turned so its side is 14.5 mm clear.
    const BasePose marine = pose(Position{-200.0f, 0}, kRound32);
    const Position east{200.0f, 0};
    EXPECT_FALSE(translation_crosses(marine, east, pose(Position{0, 16.0f + 60.0f + 0.5f}, kTyrannofex, 0.0f)));
    EXPECT_TRUE(translation_crosses(marine, east, pose(Position{0, 16.0f + 60.0f - 0.5f}, kTyrannofex, 0.0f)));
    EXPECT_FALSE(translation_crosses(marine, east, pose(Position{0, 16.0f + 60.0f - 0.5f}, kTyrannofex, 90.0f)));
    // Both ends clear, but the line passes through: the swept area is the hull of the two placements.
    const BasePose blocker = pose(Position{0, 0}, kRound32);
    EXPECT_FALSE(bases_overlap(pose(Position{-150.0f, 0}, kTyrannofex, 90.0f), blocker));
    EXPECT_FALSE(bases_overlap(pose(Position{150.0f, 0}, kTyrannofex, 90.0f), blocker));
    EXPECT_TRUE(translation_crosses(pose(Position{-150.0f, 0}, kTyrannofex, 90.0f), Position{150.0f, 0}, blocker));
    // A zero-length line is the bases' overlap.
    EXPECT_EQ(translation_crosses(t0, t0.position, pose(Position{61.0f, 0}, kRound32)),
              bases_overlap(t0, pose(Position{61.0f, 0}, kRound32)));
    EXPECT_TRUE(translation_crosses(t0, t0.position, pose(Position{61.0f, 0}, kRound32)));
}

TEST(R03_01_OvalSweeps, EdgeCrossing) {
    // Facing 90: 60 mm each side along x. Touching the west edge is on the battlefield; 0.02 mm over is not; less
    // than EPS over is (tolerance policy).
    EXPECT_TRUE(wholly_within(kBoard, pose(Position{60.0f, in(30)}, kTyrannofex, 90.0f)));
    EXPECT_FALSE(wholly_within(kBoard, pose(Position{59.98f, in(30)}, kTyrannofex, 90.0f)));
    EXPECT_TRUE(wholly_within(kBoard, pose(Position{59.995f, in(30)}, kTyrannofex, 90.0f)));
    EXPECT_TRUE(wholly_within(kBoard, pose(Position{46.0f, in(30)}, kTyrannofex, 0.0f)));
    EXPECT_FALSE(wholly_within(kBoard, pose(Position{46.0f, in(30)}, kTyrannofex, 90.0f)));
    // A straight line stays on the battlefield iff both ends do (the battlefield is convex).
    EXPECT_TRUE(translation_stays_within(kBoard, pose(Position{in(20), in(30)}, kTyrannofex, 90.0f), Position{60.0f, in(30)}));
    EXPECT_FALSE(translation_stays_within(kBoard, pose(Position{in(20), in(30)}, kTyrannofex, 90.0f), Position{59.0f, in(30)}));
    EXPECT_FALSE(translation_stays_within(kBoard, pose(Position{in(20), in(59)}, kTyrannofex, 0.0f), Position{in(20), in(30)}));
    // Random poses against boundary sampling, away from the threshold.
    brute::Rng r(64102);
    int checked = 0;
    for (int i = 0; i < 3000; ++i) {
        const Base b = random_oval(r);
        const BasePose p = pose(Position{r.uniform_f(0, 140), r.uniform_f(0, in(60))}, b, r.uniform_f(-180, 180));
        const brute::Ell e = brute::ell_of(p);
        double margin = std::numeric_limits<double>::infinity();
        for (int k = 0; k < 4096; ++k) {
            const brute::P2 q = brute::boundary(e, 2.0 * brute::kPi * k / 4096);
            margin = std::min({margin, q.x - kBoard.min_x, kBoard.max_x - q.x, q.y - kBoard.min_y, kBoard.max_y - q.y});
        }
        if (std::abs(margin + EPS_MM) < 0.01) continue;
        ASSERT_EQ(wholly_within(kBoard, p), margin >= -EPS_MM) << i;
        ++checked;
    }
    EXPECT_GT(checked, 2900);
}

// ============================== rotating ==============================

TEST(R03_01_OvalSweeps, RotationAgainstTheEdgeMatchesDenseAngleSampling) {
    brute::Rng r(64103);
    int blocked = 0;
    int clear = 0;
    for (int i = 0; i < 1000; ++i) {
        const Base b = random_oval(r);
        // Near the west edge, the south edge or the south-west corner.
        const float x = i % 3 == 1 ? r.uniform_f(in(10), in(30)) : r.uniform_f(20, 90);
        const float y = i % 3 == 0 ? r.uniform_f(in(10), in(50)) : r.uniform_f(20, 90);
        const BasePose start = pose(Position{x, y}, b, r.uniform_f(-180, 180));
        const float to = r.uniform_f(-180, 180);
        const double delta = rotation_delta_deg(start.facing_deg, to);
        const double expected = dense_turn_min([&](double f) { return edge_margin_at(kBoard, start, f); },
                                               start.facing_deg, delta, 20000);
        const RotationCheck check = rotation_against_edge(kBoard, start, to);
        EXPECT_TRUE(check.exact);
        ASSERT_NEAR(check.clearance_mm, expected, 0.01) << i;
        if (std::abs(expected + EPS_MM) > 0.001) {
            ASSERT_EQ(check.blocked, expected < -EPS_MM) << i;
            (check.blocked ? blocked : clear) += 1;
        }
    }
    EXPECT_GT(blocked, 100);
    EXPECT_GT(clear, 100);
}

TEST(R03_01_OvalSweeps, RotationAgainstTheEdgeExamples) {
    // 58 mm from the west edge: 56.8 mm across at facings 60 and 120 (clear), 60 mm at 90 (over by 2 mm).
    const BasePose near_west = pose(Position{58.0f, in(30)}, kTyrannofex, 60.0f);
    EXPECT_TRUE(wholly_within(kBoard, near_west));
    EXPECT_TRUE(wholly_within(kBoard, pose(near_west.position, kTyrannofex, 120.0f)));
    const RotationCheck mid = rotation_against_edge(kBoard, near_west, 120.0f);
    EXPECT_TRUE(mid.blocked);
    EXPECT_NEAR(mid.facing_deg, 90.0f, 1e-4f);
    EXPECT_NEAR(mid.clearance_mm, -2.0f, 1e-4f);
    // From -30 to 30 at 55 mm: the widest point of the turn is at its ends (49.9 mm): clear.
    const RotationCheck small_turn = rotation_against_edge(kBoard, pose(Position{55.0f, in(30)}, kTyrannofex, -30.0f), 30.0f);
    EXPECT_FALSE(small_turn.blocked);
    EXPECT_NEAR(small_turn.clearance_mm, 55.0f - std::sqrt(60.0f * 60.0f * 0.25f + 46.0f * 46.0f * 0.75f), 1e-3f);
    // A round base ignores the turn.
    EXPECT_FALSE(rotation_against_edge(kBoard, pose(Position{16.0f, in(30)}, kRound32, 0.0f), 90.0f).blocked);
    EXPECT_TRUE(rotation_against_edge(kBoard, pose(Position{15.9f, in(30)}, kRound32, 0.0f), 90.0f).blocked);
}

TEST(R03_01_OvalSweeps, RotationAgainstARoundBaseMatchesDenseAngleSampling) {
    brute::Rng r(64104);
    int blocked = 0;
    int clear = 0;
    for (int i = 0; i < 300; ++i) {
        const Base b = random_oval(r);
        const BasePose start = pose(Position{0, 0}, b, r.uniform_f(-180, 180));
        const Base ob = random_round(r);
        const double ang = r.uniform(0, 2 * brute::kPi);
        const double rho = r.uniform(0.0, semi_major(b) + 0.5 * ob.diameter_mm + 25.0);
        const BasePose obstacle = pose(Position{static_cast<float>(rho * std::cos(ang)), static_cast<float>(rho * std::sin(ang))}, ob);
        const float to = r.uniform_f(-180, 180);
        const double delta = rotation_delta_deg(start.facing_deg, to);
        const auto sd_at = [&](double f) {
            const double fr = f * brute::kPi / 180.0;
            oval_detail::Ellipse e = oval_detail::ellipse_along(b, oval_detail::Vec{}, oval_detail::Vec{-std::sin(fr), std::cos(fr)});
            return oval_detail::point_ellipse_signed_distance(oval_detail::Vec{obstacle.position.x, obstacle.position.y}, e) -
                   0.5 * ob.diameter_mm;
        };
        const double expected = dense_turn_min(sd_at, start.facing_deg, delta, 2000);
        const RotationCheck check = rotation_against_base(start, to, obstacle);
        EXPECT_TRUE(check.exact);
        ASSERT_NEAR(check.clearance_mm, expected, 0.01) << i;
        if (std::abs(expected + EPS_MM) > 0.001) {
            ASSERT_EQ(check.blocked, expected < -EPS_MM) << i;
            (check.blocked ? blocked : clear) += 1;
        }
    }
    EXPECT_GT(blocked, 30);
    EXPECT_GT(clear, 30);
}

TEST(R03_01_OvalSweeps, RotationAgainstARoundBaseExamples) {
    // A 32 mm base 74 mm from the Tyrannofex's centre, north-west (45 degrees from its 0-degree length): clear at
    // facings 0 and 90, but at 45 the tip points at it and passes 2 mm into it.
    const BasePose t0 = pose(Position{0, 0}, kTyrannofex, 0.0f);
    const float k = 74.0f / std::sqrt(2.0f);
    const BasePose nw = pose(Position{-k, k}, kRound32);
    EXPECT_FALSE(bases_overlap(t0, nw));
    EXPECT_FALSE(bases_overlap(pose(Position{0, 0}, kTyrannofex, 90.0f), nw));
    const RotationCheck through = rotation_against_base(t0, 90.0f, nw);
    EXPECT_TRUE(through.blocked);
    EXPECT_NEAR(through.facing_deg, 45.0f, 1e-3f);
    EXPECT_NEAR(through.clearance_mm, 74.0f - 60.0f - 16.0f, 1e-3f);
    // Turning to -90 goes the other way (the shorter way), away from it.
    EXPECT_FALSE(rotation_against_base(t0, -90.0f, nw).blocked);
    // Clear at the end only when the tip stops short.
    EXPECT_TRUE(rotation_against_base(t0, 90.0f, pose(Position{70.0f, 0}, kRound32)).blocked);  // 60 + 16 > 70
    EXPECT_FALSE(rotation_against_base(t0, 90.0f, pose(Position{80.0f, 0}, kRound32)).blocked);
    // A round mover ignores the turn: only its overlap.
    EXPECT_FALSE(rotation_against_base(pose(Position{0, 0}, kRound32), 90.0f, pose(Position{32.0f, 0}, kRound32)).blocked);
}

TEST(R03_01_OvalSweeps, RotationOvalAgainstOvalNeverReportsClearWhenSamplingFindsContact) {
    brute::Rng r(64105);
    int blocked = 0;
    int clear = 0;
    int near_contact = 0;
    for (int i = 0; i < 300; ++i) {
        const Base b = random_oval(r);
        const BasePose start = pose(Position{0, 0}, b, r.uniform_f(-180, 180));
        const Base ob = random_oval(r);
        const double ang = r.uniform(0, 2 * brute::kPi);
        const float ob_facing = r.uniform_f(-180, 180);
        const auto at_rho = [&](double rho) {
            return pose(Position{static_cast<float>(rho * std::cos(ang)), static_cast<float>(rho * std::sin(ang))}, ob,
                        ob_facing);
        };
        double rho = r.uniform(0.3, 1.05) * (semi_major(b) + semi_major(ob));
        if (i % 2 == 0) {
            // Near contact: 0 to 3 mm apart at the start facing (bisection on the static distance).
            const double gap = r.uniform(0.0, 3.0);
            double lo = 0.0;
            double hi = semi_major(b) + semi_major(ob) + 10.0;
            for (int k = 0; k < 60; ++k) {
                const double mid = 0.5 * (lo + hi);
                (signed_base_distance(start, at_rho(mid)) < gap ? lo : hi) = mid;
            }
            rho = hi;
            ++near_contact;
        }
        const BasePose obstacle = at_rho(rho);
        const float to = r.uniform_f(-180, 180);
        const double delta = rotation_delta_deg(start.facing_deg, to);
        const oval_detail::Ellipse eo = oval_detail::ellipse_of(obstacle);
        const oval_detail::Convex other{eo.c, eo.u, eo.v, eo.a, eo.b, 0.0, 0.0, {}};
        const auto sd_at = [&](double f) {
            const double fr = f * brute::kPi / 180.0;
            const oval_detail::Ellipse e =
                oval_detail::ellipse_along(b, oval_detail::Vec{}, oval_detail::Vec{-std::sin(fr), std::cos(fr)});
            // Refined until it is clear which side of -3 EPS/4 it lies (exact near the thresholds asserted below).
            return oval_detail::convex_signed_distance(oval_detail::Convex{e.c, e.u, e.v, e.a, e.b, 0.0, 0.0, {}}, other,
                                                       -0.75 * EPS_MM)
                .value;
        };
        const double expected = dense_turn_min(sd_at, start.facing_deg, delta, 720);
        const RotationCheck check = rotation_against_base(start, to, obstacle);
        EXPECT_FALSE(check.exact);
        if (expected < -EPS_MM) ASSERT_TRUE(check.blocked) << i << " " << expected;           // never clear on contact
        if (!check.blocked) ASSERT_GE(expected, -EPS_MM - 1e-6) << i;
        if (check.blocked) ASSERT_LT(expected, -0.5 * EPS_MM + 1e-6) << i << " " << expected;  // the stated margin
        (check.blocked ? blocked : clear) += 1;
    }
    EXPECT_GT(blocked, 50);
    EXPECT_GT(clear, 50);
    EXPECT_EQ(near_contact, 150);
    // Two Tyrannofexes side by side with a 1 mm gap: a 1-degree turn clears it, a quarter turn does not.
    const BasePose t0 = pose(Position{0, 0}, kTyrannofex, 0.0f);
    const BasePose neighbour = pose(Position{93.0f, 0}, kTyrannofex, 0.0f);
    EXPECT_FALSE(rotation_against_base(t0, 1.0f, neighbour).blocked);
    EXPECT_TRUE(rotation_against_base(t0, 90.0f, neighbour).blocked);
    EXPECT_FALSE(rotation_against_base(t0, 1.0f, neighbour).exact);
}

// ============================== the path check ==============================

TEST(D074_OvalBases, PosedPathCheckOfRoundBasesIsTheOldCheck) {
    // Round movers ignore facings: every violation, reason and order is the round-only check's.
    brute::Rng r(64106);
    const Base bases[] = {Base{BaseShape::Round, 32.0f, 0.0f}, Base{BaseShape::Round, 28.5f, 0.0f},
                          Base{BaseShape::Round, 40.0f, 0.0f}};
    const Rect board{0.0f, 0.0f, in(24), in(24)};
    int violations = 0;
    for (int trial = 0; trial < 2000; ++trial) {
        const auto random_models = [&](std::uint32_t first, std::size_t n) {
            std::vector<PlacedModel> placed;
            std::vector<PosedModel> posed;
            for (std::size_t k = 0; k < n; ++k) {
                const Position p{r.uniform_f(0, in(24)), r.uniform_f(0, in(24))};
                const Base& b = bases[r.next() % 3];
                placed.push_back(PlacedModel{ModelId{first + static_cast<std::uint32_t>(k)}, p, b});
                posed.push_back(PosedModel{ModelId{first + static_cast<std::uint32_t>(k)}, p, b, r.uniform_f(-180, 180)});
            }
            return std::pair{placed, posed};
        };
        const auto [f_round, f_posed] = random_models(10, r.next() % 6);
        const auto [e_round, e_posed] = random_models(100, r.next() % 8);
        const auto [p_round, p_posed] = random_models(200, r.next() % 3);
        std::vector<Position> points;
        std::vector<float> facings;
        const std::size_t n = 1 + r.next() % 4;
        for (std::size_t k = 0; k < n; ++k) {
            points.push_back(Position{r.uniform_f(-10, in(24) + 10), r.uniform_f(-10, in(24) + 10)});
            facings.push_back(r.uniform_f(-180, 180));
        }
        const bool through = r.chance(0.1);
        const Base& mover_base = bases[r.next() % 3];
        const ModelId mover = r.chance(0.2) && !e_round.empty() ? e_round.front().id : ModelId{1};
        const PathContext old_ctx{board, f_round, e_round, through, p_round};
        const PosedPathContext new_ctx{board, f_posed, e_posed, through, p_posed};
        const std::vector<PathViolation> expected = check_model_path(mover, mover_base, points, old_ctx);
        const std::vector<PathViolation> got = check_model_path(mover, mover_base, points, facings, new_ctx);
        ASSERT_EQ(got.size(), expected.size()) << trial;
        for (std::size_t k = 0; k < got.size(); ++k) {
            ASSERT_EQ(got[k].kind, expected[k].kind) << trial;
            ASSERT_EQ(got[k].other, expected[k].other) << trial;
            ASSERT_EQ(got[k].segment, expected[k].segment) << trial;
            ASSERT_EQ(got[k].reason, expected[k].reason) << trial;
        }
        violations += static_cast<int>(got.size());
    }
    EXPECT_GT(violations, 1000);
}

TEST(R03_01_OvalSweeps, OvalPathViolations) {
    const ModelId tyrannofex{1};
    const std::vector<PosedModel> enemy{PosedModel{ModelId{20}, at(20, 30), kRound32, 0.0f}};
    const PosedPathContext ctx{kBoard, {}, enemy};
    // Facing 90 (length along x) moving north past the enemy 0.5 mm clear of the tip's path, then 0.5 mm into it.
    const auto north_past = [&](float x_offset_mm, std::vector<float> facings) {
        const std::vector<Position> path{Position{in(20) - x_offset_mm, in(26)}, Position{in(20) - x_offset_mm, in(34)}};
        return check_model_path(tyrannofex, kTyrannofex, path, facings, ctx);
    };
    EXPECT_TRUE(north_past(60.0f + 16.0f + 0.5f, {90.0f, 90.0f}).empty());
    const auto crossed = north_past(60.0f + 16.0f - 0.5f, {90.0f, 90.0f});
    ASSERT_EQ(crossed.size(), 1u);
    EXPECT_EQ(crossed[0].kind, PathViolationKind::CrossesEnemyModel);
    EXPECT_EQ(crossed[0].other, ModelId{20});
    EXPECT_EQ(crossed[0].reason, "03.01: model 1's base cannot be moved through enemy model 20 (segment 1)");
    // Facing 0 the same line is 13.5 mm clear: the facing is part of the move.
    EXPECT_TRUE(north_past(60.0f + 16.0f - 0.5f, {0.0f, 0.0f}).empty());
    // Turning to 90 first (at the start point), then moving: the line crosses; a turn at the start into the enemy is a
    // turn violation.
    EXPECT_TRUE(has(north_past(60.0f + 16.0f - 0.5f, {0.0f, 90.0f}), PathViolationKind::CrossesEnemyModel));
    // The enemy 74 mm north-west of the centre: clear at facings 0 and 90, hit at 45 on the way.
    const float k = 74.0f / std::sqrt(2.0f);
    const std::vector<Position> beside{Position{in(20) + k, in(30) - k}, Position{in(20) + k, in(30) - k}};
    const auto turned = check_model_path(tyrannofex, kTyrannofex, beside, std::vector<float>{0.0f, 90.0f}, ctx);
    ASSERT_EQ(turned.size(), 1u);
    EXPECT_EQ(turned[0].kind, PathViolationKind::TurnCrossesEnemyModel);
    EXPECT_EQ(turned[0].segment, std::optional<std::size_t>{0});
    EXPECT_EQ(turned[0].reason, "03.01: model 1's base cannot be turned through enemy model 20 (before segment 1)");
    // Turning the other way (to -90, the shorter way there) misses it.
    EXPECT_TRUE(check_model_path(tyrannofex, kTyrannofex, beside, std::vector<float>{0.0f, -90.0f}, ctx).empty());
    // A turn that ends on the enemy reports the turn and the end.
    const std::vector<Position> right_beside{Position{in(20) - 70.0f, in(30)}, Position{in(20) - 70.0f, in(30)}};
    const auto onto_enemy = check_model_path(tyrannofex, kTyrannofex, right_beside, std::vector<float>{0.0f, 90.0f}, ctx);
    ASSERT_EQ(onto_enemy.size(), 2u);
    EXPECT_EQ(onto_enemy[0].kind, PathViolationKind::TurnCrossesEnemyModel);
    EXPECT_EQ(onto_enemy[1].kind, PathViolationKind::EndsOnModel);
    // A turn in place over the west edge.
    const std::vector<Position> at_west{Position{58.0f, in(30)}, Position{58.0f, in(30)}};
    const auto edge = check_model_path(tyrannofex, kTyrannofex, at_west, std::vector<float>{60.0f, 120.0f}, ctx);
    ASSERT_EQ(edge.size(), 1u);
    EXPECT_EQ(edge[0].kind, PathViolationKind::TurnLeavesBattlefield);
    EXPECT_EQ(edge[0].reason,
              "03.01: model 1's base cannot cross the edge of the battlefield while it turns (before segment 1)");
    // Moving to the edge: the end is off the battlefield at facing 90 but on it at facing 0.
    const std::vector<Position> west{Position{in(10), in(30)}, Position{50.0f, in(30)}};
    EXPECT_TRUE(has(check_model_path(tyrannofex, kTyrannofex, west, std::vector<float>{90.0f, 90.0f}, ctx),
                    PathViolationKind::LeavesBattlefield));
    EXPECT_TRUE(check_model_path(tyrannofex, kTyrannofex, west, std::vector<float>{0.0f, 0.0f}, ctx).empty());
    // Ending on a friendly model (D-027), and 17.01's passable enemies (crossed, not ended on).
    const std::vector<PosedModel> friends{PosedModel{ModelId{2}, Position{in(10) + 100.0f, in(10)}, kTyrannofex, 0.0f}};
    const std::vector<PosedModel> passable{PosedModel{ModelId{21}, at(10, 20), kRound32, 0.0f}};
    const PosedPathContext ctx2{kBoard, friends, {}, false, passable};
    const std::vector<Position> onto{at(10, 10), Position{in(10) + 91.0f, in(10)}};
    const auto ended = check_model_path(tyrannofex, kTyrannofex, onto, std::vector<float>{0.0f, 0.0f}, ctx2);
    ASSERT_EQ(ended.size(), 1u);
    EXPECT_EQ(ended[0].kind, PathViolationKind::EndsOnModel);
    EXPECT_EQ(ended[0].reason, "03.01: model 1 cannot end its move on model 2");
    const std::vector<Position> through_passable{at(10, 16), at(10, 24)};
    EXPECT_TRUE(check_model_path(tyrannofex, kTyrannofex, through_passable, std::vector<float>{0.0f, 0.0f}, ctx2).empty());
    const std::vector<Position> onto_passable{at(10, 16), at(10, 20)};
    EXPECT_TRUE(has(check_model_path(tyrannofex, kTyrannofex, onto_passable, std::vector<float>{0.0f, 0.0f}, ctx2),
                    PathViolationKind::EndsOnModel));
    // Desperate Escape style: enemies may be crossed (and turned through).
    const PosedPathContext through_ctx{kBoard, {}, enemy, true};
    const std::vector<Position> over{Position{in(20), in(26)}, Position{in(20), in(34)}};
    EXPECT_TRUE(check_model_path(tyrannofex, kTyrannofex, over, std::vector<float>{90.0f, 0.0f}, through_ctx).empty());
    // A round mover passing an oval enemy.
    const std::vector<PosedModel> oval_enemy{PosedModel{ModelId{30}, at(20, 30), kTyrannofex, 0.0f}};
    const PosedPathContext ctx3{kBoard, {}, oval_enemy};
    const std::vector<Position> marine_clear{Position{in(20) - 46.0f - 16.0f - 0.5f, in(20)},
                                             Position{in(20) - 46.0f - 16.0f - 0.5f, in(40)}};
    const std::vector<Position> marine_hit{Position{in(20) - 46.0f - 16.0f + 0.5f, in(20)},
                                           Position{in(20) - 46.0f - 16.0f + 0.5f, in(40)}};
    EXPECT_TRUE(check_model_path(ModelId{5}, kRound32, marine_clear, {}, ctx3).empty());
    EXPECT_TRUE(has(check_model_path(ModelId{5}, kRound32, marine_hit, {}, ctx3), PathViolationKind::CrossesEnemyModel));
    // One facing per point, or none.
    EXPECT_THROW((void)check_model_path(tyrannofex, kTyrannofex, over, std::vector<float>{0.0f}, ctx), std::logic_error);
    EXPECT_THROW((void)check_model_path(tyrannofex, kTyrannofex, std::vector<Position>{}, {}, ctx), std::logic_error);
}
