// M64 (D-074 part 1): 06.01 line of sight (D-017) with oval observers, observed models and blockers; and a disabled
// micro-benchmark of the oval kernel.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <vector>

#include "core/geometry/distance.hpp"
#include "core/geometry/oval.hpp"
#include "core/geometry/visibility.hpp"
#include "support/oval_brute_force.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::at;
using wh40k::test::in;
namespace brute = wh40k::test::oval;

namespace {

const Base kTyrannofex{BaseShape::Oval, 92.0f, 120.0f};
const Base kRound32{BaseShape::Round, 32.0f, 0.0f};
const Base kRound40{BaseShape::Round, 40.0f, 0.0f};
const Base kRound100{BaseShape::Round, 100.0f, 0.0f};

PosedModel model(std::uint32_t id, Position p, const Base& b, float facing = 0.0f) {
    return PosedModel{ModelId{id}, p, b, facing};
}

bool los(const PosedModel& a, const PosedModel& b, const std::vector<PosedModel>& blockers) {
    return has_line_of_sight(a, b, std::span<const PosedModel>(blockers));
}

// The corridor between a Tyrannofex's tip and a 100 mm base, `gap_mm` wide at y = 10", for two 32 mm bases due south
// and north of it (`slant_mm`: the observer that far west of the corridor's middle, the target that far east).
bool through_tip_corridor(float gap_mm, float slant_mm) {
    const float mid = 60.0f + gap_mm / 2.0f;
    const std::vector<PosedModel> blockers{model(10, Position{0, in(10)}, kTyrannofex, 90.0f),
                                           model(11, Position{60.0f + gap_mm + 50.0f, in(10)}, kRound100)};
    return los(model(1, Position{mid - slant_mm, 0}, kRound32), model(2, Position{mid + slant_mm, in(20)}, kRound32),
               blockers);
}

}  // namespace

TEST(R06_01_OvalLineOfSight, AnOvalBlockerBlocksTheLineItShould) {
    const PosedModel observer = model(1, at(0, 0), kRound32);
    const PosedModel target = model(2, at(0, 20), kRound32);
    // Across the line (its length along x) or along it (its 92 mm width across), it is wider than both bases.
    EXPECT_FALSE(los(observer, target, {model(10, at(0, 10), kTyrannofex, 90.0f)}));
    EXPECT_FALSE(los(observer, target, {model(10, at(0, 10), kTyrannofex, 0.0f)}));
    // Moved aside so its side is 1 mm clear of the edge-to-edge line: visible (the line passes 1 mm from it); 0.4 mm
    // clear is less than half the line's width: blocked.
    EXPECT_TRUE(los(observer, target, {model(10, Position{16.0f + 46.0f + 1.0f, in(10)}, kTyrannofex, 0.0f),
                                       model(11, Position{-16.0f - 46.0f - 1.0f, in(10)}, kTyrannofex, 0.0f)}));
    EXPECT_FALSE(los(observer, target, {model(10, Position{16.0f + 46.0f - 15.6f, in(10)}, kTyrannofex, 0.0f),
                                        model(11, Position{-16.0f - 46.0f + 15.6f, in(10)}, kTyrannofex, 0.0f)}));
    // Nothing near the line: visible.
    EXPECT_TRUE(los(observer, target, {model(10, at(20, 10), kTyrannofex, 0.0f)}));
}

TEST(R06_01_OvalLineOfSight, TheLineJustPastItsTipIsNotBlocked) {
    // 06.01 / D-017: a 1 mm line fits a gap of 1 mm (EPS tolerance: an overlap of up to 0.01 mm does not block).
    EXPECT_TRUE(through_tip_corridor(1.05f, 0.0f));
    EXPECT_FALSE(through_tip_corridor(0.95f, 0.0f));
    // Slanted bases: the centre-to-centre line clips the curves; a common tangent of the tip and the round base
    // (D-074's support-line tangents) finds the line through.
    EXPECT_TRUE(through_tip_corridor(1.01f, 8.0f));
    EXPECT_FALSE(through_tip_corridor(0.97f, 8.0f));
    // Two Tyrannofexes tip to tip across the line.
    const auto tips = [](float gap_mm, float slant_mm) {
        const float x = 60.0f + gap_mm / 2.0f;
        const std::vector<PosedModel> blockers{model(10, Position{-x, in(10)}, kTyrannofex, 90.0f),
                                               model(11, Position{x, in(10)}, kTyrannofex, -90.0f)};
        return los(model(1, Position{-slant_mm, 0}, kRound32), model(2, Position{slant_mm, in(20)}, kRound32), blockers);
    };
    EXPECT_TRUE(tips(1.05f, 0.0f));
    EXPECT_FALSE(tips(0.95f, 0.0f));
    EXPECT_TRUE(tips(1.01f, 8.0f));
    EXPECT_FALSE(tips(0.97f, 8.0f));
}

TEST(R06_01_OvalLineOfSight, AnOvalObserverSeesAroundABlocker) {
    // A 40 mm base between two 32 mm bases blocks every line between them...
    const std::vector<PosedModel> blocker{model(10, at(0, 5), kRound40)};
    const PosedModel target = model(2, at(0, 10), kRound32);
    EXPECT_FALSE(los(model(1, at(0, 0), kRound32), target, blocker));
    // ...but a Tyrannofex turned across the line sees past it from its tips, and so is seen.
    const PosedModel tyrannofex = model(1, at(0, 0), kTyrannofex, 90.0f);
    EXPECT_TRUE(los(tyrannofex, target, blocker));
    EXPECT_TRUE(los(target, tyrannofex, blocker));
    // Facing the target, its 92 mm width still clears the 40 mm base.
    EXPECT_TRUE(los(model(1, at(0, 0), kTyrannofex, 0.0f), target, blocker));
    // A blocker wider than the Tyrannofex blocks it at any facing.
    EXPECT_FALSE(los(model(1, at(0, 0), kTyrannofex, 90.0f), target, {model(10, at(0, 5), Base{BaseShape::Round, 160.0f, 0.0f})}));
}

TEST(R06_01_OvalLineOfSight, RoundOnlyInputsGiveTheD017Answer) {
    brute::Rng r(64201);
    const Base bases[] = {Base{BaseShape::Round, 32.0f, 0.0f}, Base{BaseShape::Round, 28.5f, 0.0f},
                          Base{BaseShape::Round, 40.0f, 0.0f}, Base{BaseShape::Round, 100.0f, 0.0f}};
    int visible = 0;
    int blocked = 0;
    for (int i = 0; i < 1500; ++i) {
        const Base& ba = bases[r.next() % 3];
        const Base& bb = bases[r.next() % 3];
        const Position pa{r.uniform_f(0, in(24)), r.uniform_f(0, in(6))};
        const Position pb{r.uniform_f(0, in(24)), r.uniform_f(in(10), in(24))};
        std::vector<PlacedModel> round_blockers;
        std::vector<PosedModel> posed_blockers;
        const std::size_t n = r.next() % 12;
        for (std::size_t k = 0; k < n; ++k) {
            const Position p{r.uniform_f(0, in(24)), r.uniform_f(in(5), in(11))};
            const Base& b = bases[r.next() % 4];
            round_blockers.push_back(PlacedModel{ModelId{static_cast<std::uint32_t>(10 + k)}, p, b});
            posed_blockers.push_back(PosedModel{ModelId{static_cast<std::uint32_t>(10 + k)}, p, b, r.uniform_f(-180, 180)});
        }
        const bool expected = has_line_of_sight(PlacedModel{ModelId{1}, pa, ba}, PlacedModel{ModelId{2}, pb, bb}, round_blockers);
        ASSERT_EQ(los(model(1, pa, ba, r.uniform_f(-180, 180)), model(2, pb, bb, r.uniform_f(-180, 180)), posed_blockers),
                  expected)
            << i;
        (expected ? visible : blocked) += 1;
    }
    EXPECT_GT(visible, 100);
    EXPECT_GT(blocked, 100);
}

TEST(R06_01_OvalLineOfSight, ADegenerateOvalBlocksLikeItsCircle) {
    // length == width: the oval code gives the circle's answers in the gap cases.
    const Base as_oval{BaseShape::Oval, 28.5f, 28.5f};
    const Base as_round{BaseShape::Round, 28.5f, 0.0f};
    for (const float gap : {0.8f, 0.95f, 1.05f, 1.2f}) {
        for (const Base& b : {as_oval, as_round}) {
            std::vector<PosedModel> wall;
            std::uint32_t id = 100;
            for (int k = 0; k < 6; ++k) {
                const float offset = 14.25f + gap / 2.0f + static_cast<float>(k) * 28.5f;
                wall.push_back(model(id++, Position{-offset, in(10)}, b, 33.0f));
                wall.push_back(model(id++, Position{offset, in(10)}, b, 33.0f));
            }
            EXPECT_EQ(los(model(1, at(0, 0), kRound32), model(2, at(0, 20), kRound32), wall), gap > 1.0f)
                << gap << (b.shape == BaseShape::Oval ? " oval" : " round");
        }
    }
}

// ---- performance (run by hand: --gtest_also_run_disabled_tests --gtest_filter=D074_Benchmark.*) ----

TEST(D074_Benchmark, DISABLED_OvalKernel) {
    brute::Rng r(64301);
    constexpr int n = 200000;
    std::vector<BasePose> ovals;
    std::vector<BasePose> rounds_a;
    std::vector<BasePose> rounds_b;
    std::vector<BasePose> others;
    for (int i = 0; i < n; ++i) {
        ovals.push_back(BasePose{Position{0, 0}, kTyrannofex, r.uniform_f(-180, 180)});
        const double ang = r.uniform(0, 2 * brute::kPi);
        const double d = r.uniform(0, 200);
        const Position p{static_cast<float>(d * std::cos(ang)), static_cast<float>(d * std::sin(ang))};
        rounds_a.push_back(BasePose{Position{r.uniform_f(0, 100), r.uniform_f(0, 100)}, kRound32, 0.0f});
        rounds_b.push_back(BasePose{p, kRound32, 0.0f});
        others.push_back(BasePose{p, Base{BaseShape::Oval, 60.0f, 80.0f}, r.uniform_f(-180, 180)});
    }
    volatile double sink = 0.0;
    const auto time = [&](const char* what, int calls, auto&& f) {
        const auto t0 = std::chrono::steady_clock::now();
        double s = 0.0;
        for (int i = 0; i < calls; ++i) s += f(i);
        const auto t1 = std::chrono::steady_clock::now();
        sink = sink + s;
        std::printf("%-44s %9.1f ns/call\n", what, std::chrono::duration<double, std::nano>(t1 - t0).count() / calls);
    };
    time("round-round base_distance (old, inline)", n, [&](int i) {
        return base_distance(rounds_a[i].position, rounds_a[i].base, rounds_b[i].position, rounds_b[i].base);
    });
    time("round-round base_distance (BasePose)", n, [&](int i) { return base_distance(rounds_a[i], rounds_b[i]); });
    time("round-oval base_distance", n, [&](int i) { return base_distance(ovals[i], rounds_b[i]); });
    time("oval-oval base_distance", n, [&](int i) { return base_distance(ovals[i], others[i]); });
    time("oval-oval bases_overlap", n, [&](int i) { return bases_overlap(ovals[i], others[i]) ? 1.0 : 0.0; });
    time("capsule vs oval translation_crosses", n, [&](int i) {
        return translation_crosses(rounds_b[i], Position{rounds_b[i].position.x + 100.0f, rounds_b[i].position.y}, ovals[i]) ? 1.0 : 0.0;
    });
    time("swept oval vs oval translation_crosses", n / 10, [&](int i) {
        return translation_crosses(ovals[i], Position{100.0f, 30.0f}, others[i]) ? 1.0 : 0.0;
    });
    time("rotation_against_base (round obstacle)", n, [&](int i) {
        return rotation_against_base(ovals[i], ovals[i].facing_deg + 90.0f, rounds_b[i]).blocked ? 1.0 : 0.0;
    });
    time("rotation_against_base (oval obstacle)", n / 100, [&](int i) {
        return rotation_against_base(ovals[i], ovals[i].facing_deg + 90.0f, others[i]).blocked ? 1.0 : 0.0;
    });
    time("rotation_against_edge", n, [&](int i) {
        return rotation_against_edge(Rect{0, 0, 100, 100}, ovals[i], ovals[i].facing_deg + 90.0f).blocked ? 1.0 : 0.0;
    });
    const std::vector<PosedModel> wall{model(10, at(-2, 10), kTyrannofex, 90.0f), model(11, at(3, 10), kRound40)};
    time("has_line_of_sight past an oval blocker", 2000, [&](int i) {
        return los(model(1, Position{static_cast<float>(i % 50), 0}, kRound32), model(2, at(0, 20), kRound32), wall) ? 1.0 : 0.0;
    });
    EXPECT_TRUE(sink == sink);
}
