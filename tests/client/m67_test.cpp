// M67 (D-076, D-065 extended): the client side of the points tiebreak and the portrait roster scenario. The end-of-game
// text gives the result, the reason and both totals; the Game panel's line gives the running totals; the log line names
// the winner on points; the scenario is in the start screen's list; ten unit shades per side, the first six unchanged,
// told apart under simulated colour blindness.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "core/engine/engine.hpp"
#include "core/state/points_destroyed.hpp"
#include "event_text.hpp"
#include "phase_track.hpp"
#include "settings.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"
#include "unit_style.hpp"

using namespace wh40k::client;
using wh40k::core::GameEvent;
using wh40k::core::GameState;
using wh40k::core::Side;
using wh40k::core::UnitId;
using wh40k::test::repo_root;

namespace {

// Neutral answers from the start to the end of the game; the events of the last action.
std::vector<GameEvent> play_to_end(GameState& gs) {
    std::vector<GameEvent> last = wh40k::core::start_game(gs);
    for (int guard = 0; guard < 5000 && gs.pending; ++guard) {
        last = wh40k::core::apply_action(gs, wh40k::test::decider(gs), wh40k::test::neutral_action(gs));
    }
    return last;
}

const wh40k::core::event::GameOver& game_over(const std::vector<GameEvent>& events) {
    for (const GameEvent& e : events) {
        if (const auto* g = std::get_if<wh40k::core::event::GameOver>(&e)) return *g;
    }
    throw std::logic_error("no GameOver event");
}

// A roster game ended after battle round 1 with: the west Termagants destroyed (55) and the Hive Tyrant 3 of 10
// wounds down (58.5) for Side A; the Chaos Lord 1 of 5 down (19) for Side B.
GameState decided_on_points(std::vector<GameEvent>* last = nullptr) {
    GameState gs = wh40k::test::load_rosters_scenario();
    gs.battle_rounds = 1;
    gs.find_unit(UnitId{6})->models.clear();
    gs.find_unit(UnitId{10})->models[0].wounds_remaining -= 3;
    gs.find_unit(UnitId{2})->models[0].wounds_remaining -= 1;
    std::vector<GameEvent> events = play_to_end(gs);
    if (last != nullptr) *last = events;
    return gs;
}

// ---- colour checks (D-065) ----

std::array<double, 3> linear(Rgb c) {
    auto lin = [](std::uint8_t v) {
        const double u = v / 255.0;
        return u <= 0.04045 ? u / 12.92 : std::pow((u + 0.055) / 1.055, 2.4);
    };
    return {lin(c.r), lin(c.g), lin(c.b)};
}

// Machado, Oliveira and Fernandes (2009), severity 1, on linear sRGB: what a protanope, deuteranope or tritanope sees.
enum class Vision { Normal, Protanopia, Deuteranopia, Tritanopia };
constexpr double kMachado[3][3][3] = {
    {{0.152286, 1.052583, -0.204868}, {0.114503, 0.786281, 0.099216}, {-0.003882, -0.048116, 1.051998}},
    {{0.367322, 0.860646, -0.227968}, {0.280085, 0.672501, 0.047413}, {-0.011820, 0.042940, 0.968881}},
    {{1.255528, -0.076749, -0.178779}, {-0.078411, 0.930809, 0.147602}, {0.004733, 0.691367, 0.303900}},
};

// CIE L*a*b* (D65) of a colour as seen with `vision` (the M55 formula on the simulated linear RGB).
std::array<double, 3> lab(Rgb c, Vision vision) {
    std::array<double, 3> v = linear(c);
    if (vision != Vision::Normal) {
        const auto& m = kMachado[static_cast<int>(vision) - 1];
        std::array<double, 3> s{};
        for (int i = 0; i < 3; ++i) s[i] = std::clamp(m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2], 0.0, 1.0);
        v = s;
    }
    const double x = (0.4124 * v[0] + 0.3576 * v[1] + 0.1805 * v[2]) / 0.95047;
    const double y = 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2];
    const double z = (0.0193 * v[0] + 0.1192 * v[1] + 0.9505 * v[2]) / 1.08883;
    auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
    return {116.0 * f(y) - 16.0, 500.0 * (f(x) - f(y)), 200.0 * (f(y) - f(z))};
}

double delta_e(Rgb p, Rgb q, Vision vision) {
    const auto a = lab(p, vision);
    const auto b = lab(q, vision);
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

// WCAG 2 contrast ratio between two colours.
double contrast(Rgb p, Rgb q) {
    auto luminance = [](Rgb c) {
        const auto v = linear(c);
        return 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2];
    };
    const double a = luminance(p) + 0.05;
    const double b = luminance(q) + 0.05;
    return std::max(a, b) / std::min(a, b);
}

constexpr std::array<Vision, 3> kColourBlind{Vision::Protanopia, Vision::Deuteranopia, Vision::Tritanopia};

}  // namespace

// ==== The end of the game and the log ====

TEST(M67_EndPanel, TheResultTheReasonAndBothTotals) {
    const GameState gs = decided_on_points();
    const std::optional<GameOverText> t = game_over_text(gs);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->title, "Side A (Chaos Space Marines) wins!");
    EXPECT_EQ(t->winner, Side::A);
    EXPECT_EQ(t->reason,
              "D-076: battle round 1 has ended with both sides on the battlefield; points destroyed: Side A 113.5, Side B "
              "19; Side A wins on points destroyed");
    ASSERT_TRUE(t->points.has_value());
    EXPECT_EQ(*t->points, "Points destroyed: Side A (Chaos Space Marines) 113.5, Side B (Tyranids) 19");
    // The phase bar's result line is the reason, with the totals in it.
    EXPECT_EQ(phase_track(gs).result, gs.result->reason);
}

TEST(M67_EndPanel, ADrawOnPointsAndAWipeOutShowTheTotalsToo) {
    // A draw on points: nothing destroyed on either side.
    GameState draw = wh40k::test::load_rosters_scenario();
    draw.battle_rounds = 1;
    play_to_end(draw);
    const auto d = game_over_text(draw);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->title, "Draw");
    EXPECT_EQ(*d->points, "Points destroyed: Side A (Chaos Space Marines) 0, Side B (Tyranids) 0");
    // A wipe-out (D-007) in the points scenario: the reason is D-007's, the totals are those of the game's end.
    GameState wiped = wh40k::test::load_rosters_scenario();
    for (wh40k::core::Unit& u : wiped.units) {
        if (u.side == Side::B) u.models.clear();
    }
    wiped.find_unit(UnitId{3})->models.pop_back();  // a Terminator lost: 3 of 30, 37 points
    wh40k::core::start_game(wiped);
    const auto w = game_over_text(wiped);
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(w->title, "Side A (Chaos Space Marines) wins!");
    EXPECT_EQ(w->reason, "D-007: Side B has no models on the battlefield; Side A wins");
    EXPECT_EQ(*w->points, "Points destroyed: Side A (Chaos Space Marines) 955, Side B (Tyranids) 37");
    // Nothing changes for a scenario without the option.
    GameState v1 = wh40k::test::load_v1_scenario();
    play_to_end(v1);
    const auto plain = game_over_text(v1);
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ(plain->title, "Draw");
    EXPECT_FALSE(plain->points.has_value());
}

TEST(M67_Log, TheGameOverLineNamesTheWinnerOnPoints) {
    std::vector<GameEvent> last;
    (void)decided_on_points(&last);
    EXPECT_EQ(format_event(GameEvent{game_over(last)}),
              "[D-076] Battle round 1 ends: Side A wins on points destroyed (113.5 to 19)");
    // Side B's win names its own total first; a draw gives the one total.
    wh40k::core::event::GameOver b = game_over(last);
    b.winner = Side::B;
    b.points->display = {"7.5", "74"};
    EXPECT_EQ(format_event(GameEvent{b}), "[D-076] Battle round 1 ends: Side B wins on points destroyed (74 to 7.5)");
    b.winner.reset();
    b.points->display = {"0.3", "0.3"};
    EXPECT_EQ(format_event(GameEvent{b}), "[D-076] Battle round 1 ends: a draw on points destroyed (0.3 each)");
    // Without points, as before.
    const wh40k::core::event::GameOver plain{Side::A, "D-007: Side B has no models on the battlefield; Side A wins"};
    EXPECT_EQ(format_event(GameEvent{plain}), "[D-007] Game over: D-007: Side B has no models on the battlefield; Side A wins");
}

TEST(M67_GamePanel, TheRunningTotalsOnlyWithTheTiebreak) {
    GameState gs = wh40k::test::load_rosters_scenario();
    EXPECT_EQ(points_destroyed_text(gs), "Points destroyed: Side A 0, Side B 0");
    gs.find_unit(UnitId{13})->models.pop_back();       // a Genestealer: 2 of 10, 15
    gs.find_unit(UnitId{3})->models[0].wounds_remaining = 2;  // a Terminator wounded: 1 of 30, 12.33
    EXPECT_EQ(points_destroyed_text(gs), "Points destroyed: Side A 15, Side B 12.33");
    // After the game, the totals it was decided on.
    const GameState done = decided_on_points();
    EXPECT_EQ(points_destroyed_text(done), "Points destroyed: Side A 113.5, Side B 19");
    // No line for a scenario without the option, even with points in it.
    GameState v1 = wh40k::test::load_v1_scenario();
    EXPECT_FALSE(points_destroyed_text(v1).has_value());
    v1.units[0].points = 90;
    v1.units[0].starting_wounds = 10;
    EXPECT_FALSE(points_destroyed_text(v1).has_value());
}

// ==== The scenario list (D-055) ====

TEST(M67_Scenarios, TheListIncludesTheRosterScenario) {
    const std::vector<std::string> scenarios = list_scenarios(repo_root());
    EXPECT_NE(std::find(scenarios.begin(), scenarios.end(), "data/scenarios/rosters_44x60.json"), scenarios.end());
    EXPECT_TRUE(std::is_sorted(scenarios.begin(), scenarios.end()));
    const ScenarioInfo info = scenario_info(repo_root() / "data/scenarios/rosters_44x60.json");
    EXPECT_EQ(info.name, "Incursion - Chaos Space Marines vs Tyranids (44x60 portrait)");
    EXPECT_EQ(info.sides[0], "Chaos Space Marines");
    EXPECT_EQ(info.sides[1], "Tyranids");
    // The fixtures under tests/data stay out of the list.
    for (const std::string& s : scenarios) EXPECT_EQ(s.rfind("data/scenarios/", 0), 0u) << s;
}

// ==== The shades (D-065 extended) ====

TEST(M67_UnitShades, TenPerSideTheFirstSixUnchanged) {
    EXPECT_GE(kUnitShades, 10);
    // The M55 values, so the single-unit and Strike looks are unchanged.
    const std::array<Rgb, 6> a{{{52, 110, 200}, {130, 180, 245}, {60, 175, 200}, {28, 62, 140}, {120, 110, 225},
                                {175, 215, 250}}};
    const std::array<Rgb, 6> b{{{190, 60, 60}, {245, 130, 105}, {225, 120, 40}, {125, 28, 40}, {205, 60, 140},
                                {245, 180, 170}}};
    for (int i = 1; i <= 6; ++i) {
        EXPECT_EQ(side_shade(Side::A, i), a[static_cast<std::size_t>(i - 1)]) << i;
        EXPECT_EQ(side_shade(Side::B, i), b[static_cast<std::size_t>(i - 1)]) << i;
    }
    const GameState strike = wh40k::core::load_scenario(repo_root() / "data/scenarios/strike_44x60.json", repo_root());
    const std::vector<Rgb> strike_tints{a[0], a[1], a[2], b[0], b[1], b[2], b[3]};
    for (std::uint32_t id = 1; id <= 7; ++id) EXPECT_EQ(unit_tint(strike, UnitId{id}), strike_tints[id - 1]) << id;
    // The roster scenario: Side B's nine units get nine different shades (no wrap), 7-9 the new ones.
    const GameState rosters = wh40k::test::load_rosters_scenario();
    std::set<std::tuple<int, int, int>> b_tints;
    for (const wh40k::core::Unit& u : rosters.units) {
        if (u.side != Side::B) continue;
        const Rgb t = unit_tint(rosters, u.id);
        b_tints.insert({t.r, t.g, t.b});
    }
    EXPECT_EQ(b_tints.size(), 9u);
    EXPECT_EQ(unit_tint(rosters, UnitId{14}), side_shade(Side::B, 9));
    EXPECT_EQ(unit_style(rosters, UnitId{14})->label, "9");
}

TEST(M67_UnitShades, DistinctUnderSimulatedColourBlindness) {
    // Every pair of a side's shades that includes a new one (7-10) is at least 11 apart (CIE76 delta E) as a protanope,
    // a deuteranope and a tritanope would see them (Machado 2009, severity 1), and at least 20 in normal vision (M55's
    // bound). Every shade of A is at least 20 from every shade of B under each simulation. The M55 pairs are as they
    // were: their closest are 7.3 (A1 / A5, deuteranopia) and 7.1 (B2 / B3, tritanopia); the unit numbers tell those
    // apart.
    for (const Side side : {Side::A, Side::B}) {
        std::set<std::tuple<int, int, int>> seen;
        for (int i = 1; i <= kUnitShades; ++i) {
            const Rgb p = side_shade(side, i);
            seen.insert({p.r, p.g, p.b});
            for (int j = i + 1; j <= kUnitShades; ++j) {
                if (j <= 6) continue;  // M55's pairs
                const Rgb q = side_shade(side, j);
                EXPECT_GE(delta_e(p, q, Vision::Normal), 20.0) << i << " " << j;
                for (const Vision v : kColourBlind) {
                    EXPECT_GE(delta_e(p, q, v), 11.0) << i << " " << j << " vision " << static_cast<int>(v);
                }
            }
        }
        EXPECT_EQ(seen.size(), static_cast<std::size_t>(kUnitShades));
    }
    double closest_m55 = 1e9;
    for (int i = 1; i <= kUnitShades; ++i) {
        for (int j = 1; j <= kUnitShades; ++j) {
            for (const Vision v : kColourBlind) {
                EXPECT_GE(delta_e(side_shade(Side::A, i), side_shade(Side::B, j), v), 20.0)
                    << "A" << i << " vs B" << j << " vision " << static_cast<int>(v);
            }
        }
        for (int j = i + 1; j <= 6 && i <= 6; ++j) {
            for (const Side side : {Side::A, Side::B}) {
                for (const Vision v : kColourBlind) closest_m55 = std::min(closest_m55, delta_e(side_shade(side, i), side_shade(side, j), v));
            }
        }
    }
    EXPECT_NEAR(closest_m55, 7.1, 0.1);  // unchanged since M55 (recorded in D-065)
}

TEST(M67_UnitShades, BadgesAndLabelsStayLegibleOnEveryShade) {
    // The board number is white outlined in black, and the sidebar badge white with a black shadow: one of the two
    // always stands out from the shade by the WCAG AA ratio for text (4.5). On the greyed "can't act" look (D-029) the
    // number is light grey outlined in black: at least WCAG's 3 for large text (it is 20 px or more at the fitted zoom;
    // the lowest are 3.57, the pale cyan A8 and the cream B7, next to M55's pale blue A6 at 3.74).
    const Rgb white{255, 255, 255};
    const Rgb black{0, 0, 0};
    auto greyed = [](Rgb c) {
        auto ch = [](std::uint8_t x, std::uint8_t y) {
            return static_cast<std::uint8_t>(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * 0.8f);
        };
        return Rgb{ch(c.r, 55), ch(c.g, 55), ch(c.b, 60)};
    };
    for (const Side side : {Side::A, Side::B}) {
        for (int i = 1; i <= kUnitShades; ++i) {
            const Rgb p = side_shade(side, i);
            EXPECT_GE(std::max(contrast(p, white), contrast(p, black)), 4.5) << i;
            EXPECT_GE(std::max(contrast(greyed(p), Rgb{185, 185, 190}), contrast(greyed(p), black)), 3.0) << i;
        }
    }
}
