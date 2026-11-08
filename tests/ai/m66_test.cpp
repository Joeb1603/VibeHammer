// M66 (D-074 part 3, D-075): the bots with an oval base on the battlefield. Every level keeps the Tyrannofex's facing
// and plays its fixtures with no rejected action, reverted move or fallback; the bots' bounding circles (bounds.hpp)
// hold the base at every facing and are exactly the old radius formulas for a round base; the parking step finds room
// for a 120 mm oval.

#include <gtest/gtest.h>

#include <initializer_list>
#include <random>
#include <string>
#include <variant>
#include <vector>

#include "arena.hpp"
#include "bot.hpp"
#include "bounds.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/oval.hpp"
#include "core/state/queries.hpp"
#include "normal/movement.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::at;
using wh40k::test::in;
namespace tf = wh40k::test::tyrannofex;

namespace {

BotConfig level(BotLevel l) {
    BotConfig c;
    c.level = l;
    return c;
}

const Base kOval = tf::kOval;

}  // namespace

// ---- bounds.hpp ----

TEST(M66_Bounds, TheCirclesHoldTheBaseAtEveryFacing) {
    EXPECT_EQ(avoid_radius_mm(kOval), 60.0f);
    EXPECT_EQ(reach_radius_mm(kOval), 46.0f);
    // For any facing and any other base, the true 01.04 distance lies between the circles' distances.
    std::mt19937 rng(66);
    std::uniform_real_distribution<float> pos(0.0f, 400.0f);
    std::uniform_real_distribution<float> turn(-180.0f, 180.0f);
    const Base round{BaseShape::Round, 32.0f, 0.0f};
    for (int i = 0; i < 2000; ++i) {
        const Position a{pos(rng), pos(rng), 0.0f};
        const Position b{pos(rng), pos(rng), 0.0f};
        const BasePose oval{a, kOval, turn(rng)};
        for (const BasePose& other : {BasePose{b, round, 0.0f}, BasePose{b, kOval, turn(rng)}}) {
            const float exact = base_distance(oval, other);
            EXPECT_LE(avoid_gap_mm(a, kOval, b, other.base), exact + 1e-3f);
            EXPECT_GE(reach_gap_mm(a, kOval, b, other.base), exact - 1e-3f);
            // Avoid: a pair the circles don't refuse never overlaps.
            if (!avoid_overlap(a, kOval, b, other.base)) EXPECT_FALSE(bases_overlap(oval, other));
        }
    }
    // The edge: the half extents at the facing.
    EXPECT_EQ(edge_extents(kOval, 0.0f), (BaseExtents{46.0f, 60.0f}));
    EXPECT_EQ(edge_extents(kOval, 90.0f), (BaseExtents{60.0f, 46.0f}));
}

TEST(M66_Bounds, ForARoundBaseEveryFormulaIsTheOldOneToTheBit) {
    // D-074 part 3: the bots' bit-identical round-only play rests on these.
    std::mt19937 rng(166);
    std::uniform_real_distribution<float> pos(0.0f, 1500.0f);
    for (const float d : {25.0f, 28.5f, 32.0f, 40.0f, 50.0f, 60.0f, 90.0f}) {
        const Base b{BaseShape::Round, d, 0.0f};
        EXPECT_EQ(avoid_radius_mm(b), base_radius(b));
        EXPECT_EQ(reach_radius_mm(b), base_radius(b));
        EXPECT_EQ(2.0f * avoid_radius_mm(b), d);  // the formation's spacing
        const BaseExtents e = edge_extents(b, 0.0f);
        EXPECT_EQ(e.half_x_mm, base_radius(b));
        EXPECT_EQ(e.half_y_mm, base_radius(b));
        for (int i = 0; i < 5000; ++i) {
            const Position p{pos(rng), pos(rng), 0.0f};
            const Position q{pos(rng) * 0.05f + p.x, pos(rng) * 0.05f + p.y, 0.0f};
            const Base o{BaseShape::Round, i % 2 == 0 ? 32.0f : 28.5f, 0.0f};
            EXPECT_EQ(avoid_gap_mm(p, b, q, o), base_distance(p, b, q, o));
            EXPECT_EQ(reach_gap_mm(p, b, q, o), base_distance(p, b, q, o));
            EXPECT_EQ(avoid_overlap(p, b, q, o), bases_overlap(p, b, q, o));
            // The engine's pose-aware distance for a round pair is the same float too (core, M65).
            EXPECT_EQ(base_distance(BasePose{p, b, 0.0f}, BasePose{q, o, 0.0f}), base_distance(p, b, q, o));
        }
    }
}

// ---- D-075: the parking step ----

TEST(M66_Park, TheParkStepFindsRoomForA120mmOval) {
    // A unit of two Tyrannofexes, side by side 5" apart, swapping places: each one's end is on the other's start (a
    // cycle, D-027), so one is parked first, clear of its start at every facing, and proposed again later.
    GameState gs = tf::fixture(Side::B);
    Unit& u = gs.units[tf::kTyrannofex];
    Model second = u.models[0];
    second.id = ModelId{999};
    u.models.push_back(second);
    u.models[0].position = at(16.0f, 40.0f);
    u.models[1].position = at(21.0f, 40.0f);
    u.models[1].facing_deg = 90.0f;
    start_game(gs);
    wh40k::test::play_neutral_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind);
    });
    apply_action(gs, Side::B, action::StartMove{u.id, MoveType::Normal, std::nullopt});
    const std::vector<Position> ends{at(21.0f, 40.0f), at(16.0f, 40.0f)};
    const auto moves = normal::slot_moves(gs, ends);
    ASSERT_TRUE(moves);
    ASSERT_EQ(moves->size(), 3u);  // one parked, the other moved, the parked one moved again
    // The park is a full circumscribed diameter plus 1 mm from its start.
    const auto& park = (*moves)[0];
    const Model& parked = *find_model(gs, park.model);
    EXPECT_FLOAT_EQ(centre_distance(parked.position, park.path.back()), 2.0f * avoid_radius_mm(kOval) + 1.0f);
    for (const auto& m : *moves) {
        EXPECT_TRUE(m.facings.empty());  // no bot turns an oval
        const ValidationResult r = validate_action(gs, Side::B, m);
        ASSERT_TRUE(r.ok()) << (r.reasons.empty() ? std::string() : r.reasons.front());
        apply_action(gs, Side::B, m);
    }
    EXPECT_EQ(gs.units[tf::kTyrannofex].models[0].position, ends[0]);
    EXPECT_EQ(gs.units[tf::kTyrannofex].models[1].position, ends[1]);
    EXPECT_EQ(gs.units[tf::kTyrannofex].models[1].facing_deg, 90.0f);
    EXPECT_TRUE(move_end_problems(gs).empty());
}

// ---- every level with the Tyrannofex ----

namespace {

// Side B's Move Units (the Tyrannofex may move), its Normal move, its charge, and the Legionaries' charge into it.
std::vector<std::pair<std::string, GameState>> tyrannofex_decisions() {
    std::vector<std::pair<std::string, GameState>> states;
    {
        GameState gs = tf::fixture(Side::B);
        start_game(gs);
        wh40k::test::play_neutral_until(gs, [](const GameState& g) {
            return g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind);
        });
        states.emplace_back("move units", gs);
        apply_action(gs, Side::B, action::StartMove{gs.units[tf::kTyrannofex].id, MoveType::Normal, std::nullopt});
        states.emplace_back("its normal move", gs);
    }
    {
        GameState gs = tf::fixture(Side::B);
        tf::the_tyrannofex(gs).position = at(22.0f, tf::kRankY + 5.0f);
        tf::the_tyrannofex(gs).facing_deg = 45.0f;
        start_game(gs);
        wh40k::test::play_neutral_until(gs, [](const GameState& g) {
            return g.pending && std::holds_alternative<decision::Charge>(g.pending->kind);
        });
        states.emplace_back("its charge", gs);
    }
    {
        GameState gs = tf::fixture(Side::A);
        tf::the_tyrannofex(gs).facing_deg = 90.0f;
        start_game(gs);
        wh40k::test::play_neutral_until(gs, [](const GameState& g) {
            return g.pending && std::holds_alternative<decision::Charge>(g.pending->kind) && g.pending->side == Side::A;
        });
        states.emplace_back("a charge into it", gs);
    }
    return states;
}

void expect_legal_decisions(std::initializer_list<BotLevel> levels) {
    for (const auto& [name, gs] : tyrannofex_decisions()) {
        ASSERT_TRUE(gs.pending) << name;
        for (const BotLevel l : levels) {
            Bot bot(level(l));
            const BotChoice c = bot.decide(gs, gs.pending->side);
            ASSERT_FALSE(c.stopped) << name << " level " << static_cast<int>(l);
            const ValidationResult r = validate_action(gs, gs.pending->side, c.action);
            EXPECT_TRUE(r.ok()) << name << " level " << static_cast<int>(l) << ": "
                                << (r.reasons.empty() ? std::string() : r.reasons.front());
            EXPECT_EQ(bot.fallbacks(), 0u) << name << " level " << static_cast<int>(l);
        }
    }
}

}  // namespace

TEST(M66_Bots, EasyAndNormalDecideLegallyAtTheTyrannofexsDecisions) {
    expect_legal_decisions({BotLevel::Easy, BotLevel::Normal});
}

TEST(M66_Bots, HardDecidesLegallyAtTheTyrannofexsDecisions) { expect_legal_decisions({BotLevel::Hard}); }

namespace {

struct Pairing {
    BotLevel a;
    BotLevel b;
    std::uint64_t seed;
};

// Each game from `scenario`: finished, no rejected action, no reverted move, no fallback.
void expect_clean_games(const GameState& scenario, std::initializer_list<Pairing> pairings) {
    for (const Pairing& p : pairings) {
        const GameRecord g = play_bot_game(scenario, p.seed, level(p.a), level(p.b), 8000);
        const std::string label = "seed " + std::to_string(p.seed) + " " + std::to_string(static_cast<int>(p.a)) +
                                  " vs " + std::to_string(static_cast<int>(p.b));
        EXPECT_TRUE(g.finished) << label << (g.problems.empty() ? "" : ": " + g.problems.front());
        EXPECT_EQ(g.rejected, 0u) << label;
        EXPECT_EQ(g.reverted, 0u) << label;
        EXPECT_EQ(g.fallbacks, 0u) << label;
    }
}

void expect_clean_pairings(const GameState& scenario, int games, std::uint64_t first_seed,
                           std::initializer_list<std::pair<BotLevel, BotLevel>> levels) {
    for (const auto& [a, b] : levels) {
        for (int i = 0; i < games; ++i) {
            expect_clean_games(scenario, {Pairing{a, b, first_seed + static_cast<std::uint64_t>(i)}});
        }
    }
}

}  // namespace

TEST(M66_Bots, EasyAndNormalPlayTheTyrannofexClean) {
    // The hand-placed fixture (five units, the Tyrannofex among them), either side first, and the 14-unit roster
    // fixture with the Tyrannofex once each way. The 30-game pairings on the roster fixture are the disabled test
    // below and the arena (M66 Results).
    for (const Side first : {Side::A, Side::B}) {
        expect_clean_pairings(tf::fixture(first), 2, 660 + static_cast<std::uint64_t>(first == Side::A ? 0 : 10),
                              {{BotLevel::Easy, BotLevel::Easy},
                               {BotLevel::Normal, BotLevel::Normal},
                               {BotLevel::Easy, BotLevel::Normal},
                               {BotLevel::Normal, BotLevel::Easy}});
    }
    const GameState roster = wh40k::test::load_tyrannofex_fixture();
    expect_clean_games(roster, {Pairing{BotLevel::Easy, BotLevel::Easy, 1}, Pairing{BotLevel::Normal, BotLevel::Easy, 2}});
}

// 30 games each pairing of Easy and Normal on the roster fixture with the Tyrannofex: a few minutes in Release, so run by
// hand:  wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M66_Bots.DISABLED_*
TEST(M66_Bots, DISABLED_EasyAndNormalThirtyGamesEachPairingOnTheRosterFixture) {
    expect_clean_pairings(wh40k::test::load_tyrannofex_fixture(), 30, 1,
                          {{BotLevel::Easy, BotLevel::Easy},
                           {BotLevel::Normal, BotLevel::Normal},
                           {BotLevel::Easy, BotLevel::Normal},
                           {BotLevel::Normal, BotLevel::Easy}});
}

// Hard with the Tyrannofex takes minutes a game even in Release (Hard for the roster units is M69's), so by hand too:
// four games on the hand-placed fixture, either side first. Hard on the 14-unit roster fixture is the arena's
// (wh40k_bot_arena --a hard --b hard --scenario tests/data/scenarios/rosters_tyrannofex_44x60.json; M66 Results).
TEST(M66_Bots, DISABLED_HardPlaysTheTyrannofexClean) {
    expect_clean_games(tf::fixture(Side::B), {Pairing{BotLevel::Hard, BotLevel::Hard, 661},
                                              Pairing{BotLevel::Hard, BotLevel::Normal, 662}});
    expect_clean_games(tf::fixture(Side::A), {Pairing{BotLevel::Easy, BotLevel::Hard, 663},
                                              Pairing{BotLevel::Hard, BotLevel::Hard, 664}});
}
