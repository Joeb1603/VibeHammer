// M54: the auto moves' faster region tests give in_region's answers. hypot_less against std::hypot, the
// trimmed region against the whole one, and RayRefusals against in_region, over the reach areas of every
// moving model in random play (normal moves, advances, fall backs, charges, pile-ins and consolidations).

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/state/queries.hpp"
#include "reach_area.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::TestRng;

namespace {

float uniform(TestRng& rng, float lo, float hi) {
    return lo + (hi - lo) * static_cast<float>(rng.next() % 1000001) / 1000000.0f;
}

// The reach of every model of the moving unit, at each MoveModels decision of a few random games.
std::vector<MoveReach> sample_reaches() {
    std::vector<MoveReach> out;
    for (const auto& [scenario, seed] : std::vector<std::pair<const char*, std::uint64_t>>{
             {"v1_skirmish.json", 2},
             {"v1_skirmish.json", 9},
             {"bot_20_termagants.json", 4},
             {"strike_44x60.json", 6}}) {
        GameState gs = load_scenario(wh40k::test::repo_root() / "data/scenarios" / scenario, wh40k::test::repo_root());
        gs.dice.seed = seed;
        start_game(gs);
        TestRng rng(seed * 31 + 7);
        for (int i = 0; i < 500 && gs.pending && !gs.result; ++i) {
            if (gs.flow.move && std::holds_alternative<decision::MoveModels>(gs.pending->kind)) {
                if (const Unit* u = gs.find_unit(gs.flow.move->unit)) {
                    for (const Model& m : u->models) {
                        if (auto r = move_reach(gs, m.id)) out.push_back(*r);
                    }
                }
            }
            (void)apply_action(gs, gs.pending->side, wh40k::test::random_action(gs, rng));
        }
    }
    return out;
}

const std::vector<MoveReach>& reaches() {
    static const std::vector<MoveReach> r = sample_reaches();
    return r;
}

}  // namespace

TEST(M54_ReachRegion, HypotLessIsTheHypotTest) {
    TestRng rng(5401);
    const auto check = [](float dx, float dy, float r) {
        ASSERT_EQ(hypot_less(dx, dy, r), std::hypot(dx, dy) < r) << dx << " " << dy << " " << r;
    };
    for (int i = 0; i < 200000; ++i) {
        const float dx = uniform(rng, -200.0f, 200.0f);
        const float dy = uniform(rng, -200.0f, 200.0f);
        check(dx, dy, uniform(rng, -10.0f, 300.0f));
        // On the edge: r is the hypot itself, or one float either side of it.
        const float h = std::hypot(dx, dy);
        check(dx, dy, h);
        check(dx, dy, std::nextafter(h, 0.0f));
        check(dx, dy, std::nextafter(h, 1e30f));
    }
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float tiny = std::numeric_limits<float>::denorm_min();
    for (const float a : {0.0f, -0.0f, 1e-30f, tiny, 3.0f, -3.0f, 1e19f, 3e19f, 1e30f, inf, -inf, nan}) {
        for (const float b : {0.0f, 1e-30f, 4.0f, -4.0f, 2e19f, inf, nan}) {
            for (const float r : {0.0f, -0.0f, -1.0f, 1e-20f, 1e-9f, 5.0f, 5.000001f, 1e10f, 3e19f, inf, nan}) {
                check(a, b, r);
            }
        }
    }
}

TEST(M54_ReachRegion, PointSegmentCloserThanIsTheDistanceTest) {
    TestRng rng(5402);
    for (int i = 0; i < 100000; ++i) {
        const Position p{uniform(rng, 0.0f, 600.0f), uniform(rng, 0.0f, 600.0f), 0.0f};
        const Position a{uniform(rng, 0.0f, 600.0f), uniform(rng, 0.0f, 600.0f), 0.0f};
        const Position b = i % 7 == 0 ? a : Position{uniform(rng, 0.0f, 600.0f), uniform(rng, 0.0f, 600.0f), 0.0f};
        const float d = point_segment_distance(p, a, b);
        for (const float r : {uniform(rng, 0.0f, 300.0f), d, std::nextafter(d, 0.0f), std::nextafter(d, 1e30f)}) {
            ASSERT_EQ(point_segment_closer_than(p, a, b, r), d < r) << i;
        }
    }
}

TEST(M54_ReachRegion, TrimmedRegionGivesTheSameAnswerEverywhere) {
    ASSERT_GT(reaches().size(), 50u);
    TestRng rng(5403);
    int inside = 0;
    std::size_t dropped = 0;
    for (const MoveReach& r : reaches()) {
        const ReachRegion whole = model_region(r);
        const ReachRegion trimmed = trimmed_region(whole);
        dropped += whole.outside_all.size() - trimmed.outside_all.size();
        const float span = r.remaining_mm + 40.0f;
        for (int i = 0; i < 400; ++i) {
            const Position p{r.from.x + uniform(rng, -span, span), r.from.y + uniform(rng, -span, span), 0.0f};
            const bool expected = in_region(whole, p);
            ASSERT_EQ(in_region(trimmed, p), expected);
            inside += expected ? 1 : 0;
        }
    }
    EXPECT_GT(inside, 1000);
    EXPECT_GT(dropped, 0u);  // it does leave circles out
    // A whole-unit drag's region (shadows whose line starts away from the centre) is unchanged too.
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    const GameAction move = action::StartMove{gs.units[0].id, MoveType::Normal, std::nullopt};
    ASSERT_TRUE(validate_action(gs, Side::A, move).ok());
    (void)apply_action(gs, Side::A, move);
    const Position grab = gs.units[0].models.front().position;
    const ReachRegion unit_whole = unit_region(gs, gs.units[0].id, grab);
    const ReachRegion unit_trimmed = trimmed_region(unit_whole);
    for (int i = 0; i < 20000; ++i) {
        const Position p{grab.x + uniform(rng, -250.0f, 250.0f), grab.y + uniform(rng, -250.0f, 250.0f), 0.0f};
        ASSERT_EQ(in_region(unit_trimmed, p), in_region(unit_whole, p));
    }
}

TEST(M54_ReachRegion, RayRefusalsAreAllRefusedByInRegion) {
    // The auto moves' samples: 72 directions from the start, at whole millimetres (auto_fight_move) and every
    // 0.5 mm from 2.01 mm (fight_move_skip). Every spot RayRefusals refuses, in_region refuses.
    std::array<float, 72> cosines{};
    std::array<float, 72> sines{};
    for (int i = 0; i < 72; ++i) {
        const float a = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / 72;
        cosines[static_cast<std::size_t>(i)] = std::cos(a);
        sines[static_cast<std::size_t>(i)] = std::sin(a);
    }
    long refused = 0;
    long kept = 0;
    long kept_inside = 0;
    long group_refused = 0;  // in a move that must end closer to something (a charge, pile-in or consolidation)
    for (std::size_t n = 0; n < reaches().size(); n += 3) {
        const MoveReach& r = reaches()[n];
        const ReachRegion region = trimmed_region(model_region(r));
        for (const bool whole_mm : {true, false}) {
            std::vector<float> distances;
            if (whole_mm) {
                for (float d = 1.0f; d <= r.remaining_mm && distances.size() < 80; d += 1.0f) distances.push_back(d);
            } else {
                for (float d = 2.0f + EPS_MM; d <= r.remaining_mm && distances.size() < 80; d += 0.5f) {
                    distances.push_back(d);
                }
            }
            const RayRefusals refusals(region, r.from, cosines, sines, distances);
            for (std::size_t j = 0; j < distances.size(); ++j) {
                for (std::size_t i = 0; i < cosines.size(); ++i) {
                    const Position p{r.from.x + distances[j] * cosines[i], r.from.y + distances[j] * sines[i], 0.0f};
                    if (refusals.refused(i, j)) {
                        ASSERT_FALSE(in_region(region, p)) << "ray " << i << " at " << distances[j];
                        ++refused;
                        if (!region.inside_any.empty()) ++group_refused;
                    } else {
                        ++kept;
                        kept_inside += in_region(region, p) ? 1 : 0;
                    }
                }
            }
        }
    }
    EXPECT_GT(refused, 100000);  // many spots never need in_region
    EXPECT_GT(group_refused, 10000);
    EXPECT_GT(kept, 0);
    EXPECT_GT(kept_inside, 1000);
    EXPECT_FALSE(RayRefusals(ReachRegion{}, Position{}, cosines, sines, std::vector<float>{}).refused(0, 0));
}
