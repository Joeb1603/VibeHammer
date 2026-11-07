// M55 (D-065): each unit gets its own shade of its side's colour and a per-side number, so several units of
// the same datasheet can be told apart on the board, in the sidebar and in the text.

#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "action_text.hpp"
#include "core/data/scenario_loader.hpp"
#include "test_helpers.hpp"
#include "unit_style.hpp"

using namespace wh40k::client;
using wh40k::core::GameState;
using wh40k::core::Side;
using wh40k::core::UnitId;

namespace {

GameState strike() {
    return wh40k::core::load_scenario(wh40k::test::repo_root() / "data/scenarios/strike_44x60.json",
                                      wh40k::test::repo_root());
}

// CIE L*a*b* (D65) of an sRGB colour, for a perceptual distance (CIE76 delta E).
std::tuple<double, double, double> lab(Rgb c) {
    auto lin = [](std::uint8_t v) {
        const double u = v / 255.0;
        return u <= 0.04045 ? u / 12.92 : std::pow((u + 0.055) / 1.055, 2.4);
    };
    const double r = lin(c.r), g = lin(c.g), b = lin(c.b);
    const double x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047;
    const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    const double z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;
    auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
    return {116.0 * f(y) - 16.0, 500.0 * (f(x) - f(y)), 200.0 * (f(y) - f(z))};
}

double delta_e(Rgb p, Rgb q) {
    const auto [l1, a1, b1] = lab(p);
    const auto [l2, a2, b2] = lab(q);
    return std::sqrt((l1 - l2) * (l1 - l2) + (a1 - a2) * (a1 - a2) + (b1 - b2) * (b1 - b2));
}

// The board's greyed "can't act" look (board_view.cpp: 80% of the way to {55,55,60}).
Rgb greyed(Rgb c) {
    auto ch = [](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * 0.8f);
    };
    return Rgb{ch(c.r, 55), ch(c.g, 55), ch(c.b, 60)};
}

constexpr Rgb kBoard{46, 58, 48};       // board_view.cpp kBoardFill
constexpr Rgb kOutline{245, 245, 245};  // raylib RAYWHITE, the base outline and selection ring

}  // namespace

TEST(M55_UnitTint, OneUnitPerSideKeepsTheSideColourAndHasNoLabel) {
    const GameState gs = wh40k::test::load_v1_scenario();
    ASSERT_EQ(gs.units.size(), 2u);
    for (const auto& u : gs.units) {
        const auto style = unit_style(gs, u.id);
        ASSERT_TRUE(style);
        EXPECT_EQ(style->index, 1);
        EXPECT_EQ(style->side_units, 1);
        EXPECT_TRUE(style->label.empty());
        EXPECT_EQ(style->display_name, u.name);
        EXPECT_EQ(unit_display_name(gs, u.id), u.name);
    }
    // The pre-M55 side colours, unchanged.
    EXPECT_EQ(side_rgb(Side::A), (Rgb{52, 110, 200}));
    EXPECT_EQ(side_rgb(Side::B), (Rgb{190, 60, 60}));
    EXPECT_EQ(unit_tint(gs, gs.units[0].id), side_rgb(gs.units[0].side));
    EXPECT_EQ(unit_tint(gs, gs.units[1].id), side_rgb(gs.units[1].side));
}

TEST(M55_UnitTint, IndexFollowsUnitOrderWithinEachSide) {
    const GameState gs = strike();
    ASSERT_EQ(gs.units.size(), 7u);
    int a = 0;
    int b = 0;
    for (const auto& u : gs.units) {
        const int expected = u.side == Side::A ? ++a : ++b;
        const auto style = unit_style(gs, u.id);
        ASSERT_TRUE(style);
        EXPECT_EQ(style->index, expected) << "unit " << u.id.v;
        EXPECT_EQ(style->side_units, u.side == Side::A ? 3 : 4);
        EXPECT_EQ(style->label, std::to_string(expected));
        EXPECT_EQ(style->tint, side_shade(u.side, expected));
    }
    EXPECT_EQ(unit_tint(gs, UnitId{1}), side_rgb(Side::A));  // the first unit of a side keeps the side colour
    EXPECT_EQ(unit_tint(gs, UnitId{4}), side_rgb(Side::B));
}

TEST(M55_UnitTint, DisplayNamesForTheStrikeScenario) {
    const GameState gs = strike();
    const std::vector<std::string> expected{"Intercessor Squad 1", "Intercessor Squad 2", "Intercessor Squad 3",
                                            "Termagants 1",        "Termagants 2",        "Termagants 3",
                                            "Termagants 4"};
    for (std::uint32_t id = 1; id <= 7; ++id) {
        EXPECT_EQ(unit_display_name(gs, UnitId{id}), expected[id - 1]);
    }
    EXPECT_EQ(unit_display_name(gs, UnitId{99}), "unit 99");
    EXPECT_FALSE(unit_style(gs, UnitId{99}));
    // The action text uses the same name, with the id as before.
    EXPECT_EQ(action_label(gs, wh40k::core::GameAction{wh40k::core::action::DeclareCharge{UnitId{5}}}),
              "Declare charge: Termagants 2 (5)");
    const GameState v1 = wh40k::test::load_v1_scenario();
    EXPECT_EQ(action_label(v1, wh40k::core::GameAction{wh40k::core::action::DeclareCharge{v1.units[1].id}}),
              "Declare charge: Termagants (" + std::to_string(v1.units[1].id.v) + ")");
}

TEST(M55_UnitTint, Deterministic) {
    const GameState gs = strike();
    for (const auto& u : gs.units) {
        const auto first = unit_style(gs, u.id);
        const auto again = unit_style(strike(), u.id);
        ASSERT_TRUE(first && again);
        EXPECT_EQ(first->tint, again->tint);
        EXPECT_EQ(first->label, again->label);
        EXPECT_EQ(first->display_name, again->display_name);
    }
    EXPECT_EQ(side_shade(Side::B, 1 + kUnitShades), side_shade(Side::B, 1));  // a seventh unit wraps round
}

TEST(M55_UnitTint, ShadesAreDistinctWithinASideAndBetweenSides) {
    for (const Side side : {Side::A, Side::B}) {
        std::set<std::tuple<int, int, int>> seen;
        for (int i = 1; i <= kUnitShades; ++i) {
            const Rgb p = side_shade(side, i);
            seen.insert({p.r, p.g, p.b});
            for (int j = i + 1; j <= kUnitShades; ++j) {
                EXPECT_GE(delta_e(p, side_shade(side, j)), 20.0) << "side shades " << i << " and " << j;
            }
        }
        EXPECT_EQ(seen.size(), static_cast<std::size_t>(kUnitShades));
    }
    for (int i = 1; i <= kUnitShades; ++i) {
        for (int j = 1; j <= kUnitShades; ++j) {
            EXPECT_GE(delta_e(side_shade(Side::A, i), side_shade(Side::B, j)), 40.0) << "A" << i << " vs B" << j;
        }
    }
}

TEST(M55_UnitTint, ShadesStandOutFromTheBoardTheGreyedLookAndTheOutline) {
    for (const Side side : {Side::A, Side::B}) {
        for (int i = 1; i <= kUnitShades; ++i) {
            const Rgb p = side_shade(side, i);
            EXPECT_GE(delta_e(p, kBoard), 45.0) << "shade " << i;
            EXPECT_GE(delta_e(p, greyed(p)), 35.0) << "shade " << i;
            EXPECT_GE(delta_e(p, kOutline), 25.0) << "shade " << i;
        }
    }
}
