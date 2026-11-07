// M50 (D-061): the sidebar legend shows one short line per ring (ring_summary); the full ring_meaning shows on
// hover there and in the board's ring tooltip, unchanged.

#include <gtest/gtest.h>

#include <array>
#include <string_view>

#include "overlay_plan.hpp"

using namespace wh40k::client;

namespace {
constexpr std::array kAllRings{RingKind::MoveRange, RingKind::Engagement, RingKind::Coherency,
                               RingKind::WeaponRange, RingKind::ChargeDistance};
}  // namespace

TEST(M50_RingText, EveryRingHasAShortSummary) {
    for (const RingKind k : kAllRings) {
        const std::string_view s = ring_summary(k);
        SCOPED_TRACE(std::string(ring_name(k)));
        EXPECT_FALSE(s.empty());
        EXPECT_LE(s.size(), 100u);
        EXPECT_EQ(s.find('\n'), std::string_view::npos);
        EXPECT_EQ(s.find(". "), std::string_view::npos) << "one sentence";
    }
}

TEST(M50_RingText, TheFullMeaningsAreUnchanged) {
    const std::string_view move = ring_meaning(RingKind::MoveRange);
    EXPECT_NE(move.find("D-040"), std::string_view::npos);
    EXPECT_NE(move.find("D-053"), std::string_view::npos);
    EXPECT_NE(move.find("03.01"), std::string_view::npos);
    EXPECT_GT(move.size(), ring_summary(RingKind::MoveRange).size());
    EXPECT_NE(ring_meaning(RingKind::Engagement).find("03.04"), std::string_view::npos);
    EXPECT_NE(ring_meaning(RingKind::Coherency).find("03.03"), std::string_view::npos);
}

TEST(M50_RingText, EverySummaryIsNoLongerThanItsMeaning) {
    for (const RingKind k : kAllRings) {
        SCOPED_TRACE(std::string(ring_name(k)));
        EXPECT_LE(ring_summary(k).size(), ring_meaning(k).size());
    }
}
