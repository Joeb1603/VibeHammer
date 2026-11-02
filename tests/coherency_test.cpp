#include <gtest/gtest.h>

#include <vector>

#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/units.hpp"
#include "core/state/queries.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::in;
using wh40k::test::placed;

namespace {

// A line of n 32 mm bases along x, `gap_mm` edge to edge between neighbours. Ids 1..n.
std::vector<PlacedModel> line(int n, float gap_mm) {
    std::vector<PlacedModel> out;
    for (int i = 0; i < n; ++i) {
        const float x = static_cast<float>(i) * (32.0f + gap_mm);
        out.push_back(placed(static_cast<std::uint32_t>(i + 1), Position{x, 0.0f}));
    }
    return out;
}

std::vector<ModelId> ids(std::initializer_list<std::uint32_t> v) {
    std::vector<ModelId> out;
    for (auto x : v) out.push_back(ModelId{x});
    return out;
}

}  // namespace

// PLAN M3 acceptance: 6 Intercessors, 1.9" gaps. 2" clause passes; 9" clause fails because the end
// models are about 14.5" apart edge to edge.
TEST(R03_03_Coherency, SixInLineWith1_9InchGapsFailsOnlyThe9InchClause) {
    const auto models = line(6, in(1.9f));
    const float ends = base_distance(models.front().position, models.front().base,
                                     models.back().position, models.back().base);
    EXPECT_NEAR(mm_to_inches(ends), 14.54f, 0.01f);

    const CoherencyResult r = check_coherency(models);
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(r.isolated.empty());  // every model has a neighbour within 2"
    // Models 1,2 are more than 9" from 5,6 and vice versa; 3 and 4 are within 9" of everyone.
    EXPECT_EQ(r.too_far, ids({1, 2, 5, 6}));
}

// PLAN M3 acceptance (reverse): within 9" of every other model, but one model more than 2" from all.
TEST(R03_03_Coherency, OneModelMoreThan2InchesFromAllFailsOnlyThe2InchClause) {
    const std::vector<PlacedModel> models{
        placed(1, Position{0.0f, 0.0f}),
        placed(2, Position{32.0f + in(1), 0.0f}),
        placed(3, Position{2 * 32.0f + in(1) + in(3), 0.0f}),  // 3" from model 2
    };
    const CoherencyResult r = check_coherency(models);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.isolated, ids({3}));
    EXPECT_TRUE(r.too_far.empty());
}

TEST(R03_03_Coherency, Exactly2InchesApartIsWithin) {
    EXPECT_TRUE(check_coherency(line(2, in(2))).ok());
}

TEST(R03_03_Coherency, TwoInchesPlusEpsilonIsNotWithin) {
    const CoherencyResult r = check_coherency(line(2, in(2) + 2 * EPS_MM));
    EXPECT_EQ(r.isolated, ids({1, 2}));
}

TEST(R03_03_Coherency, Exactly9InchesEndToEndIsWithin) {
    // 5 bases, end models exactly 9" apart edge to edge: 4*(32 + gap) - 32 = 9".
    const float gap = (in(9) + 32.0f) / 4.0f - 32.0f;
    ASSERT_LT(gap, in(2));
    EXPECT_TRUE(check_coherency(line(5, gap)).ok());
}

TEST(R03_03_Coherency, NineInchesPlusEpsilonEndToEndFails9InchClause) {
    const float gap = (in(9) + 32.0f + 4 * EPS_MM) / 4.0f - 32.0f;
    const CoherencyResult r = check_coherency(line(5, gap));
    EXPECT_TRUE(r.isolated.empty());
    EXPECT_EQ(r.too_far, ids({1, 5}));
}

TEST(R03_03_Coherency, V1ScenarioUnitsAreCoherent) {
    const GameState gs = wh40k::test::load_v1_scenario();
    for (const Unit& u : gs.units) {
        EXPECT_TRUE(unit_coherency(u).ok()) << u.name;
    }
}
