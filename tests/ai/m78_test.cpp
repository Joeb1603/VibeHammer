// M78 (D-083 part 0): measuring teamwork. The arena's teamwork statistics (gang-ups, lone engagements, shooting spread,
// contact) counted on hand-built roster positions by the functions the arena uses, the --per-game line unchanged, and
// the regression switch Generation::M77 (the hidden arena levels normal-m77 / hard-m77): today it plays as the current
// levels, and it plays exactly as the pre-batch bots (main at c54c47c), pinned by that baseline's per-game hashes.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "arena.hpp"
#include "bot.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/events.hpp"
#include "core/geometry/distance.hpp"
#include "core/state/queries.hpp"
#include "generation.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
namespace mf = wh40k::test::monsters;

namespace {

BotConfig level(BotLevel l, Generation g = Generation::Current) {
    BotConfig c;
    c.level = l;
    c.generation = g;
    return c;
}

GameState scenario(const char* file) {
    const auto root = wh40k::test::repo_root();
    return load_scenario(root / "data/scenarios" / file, root);
}

float radius_in(const Unit& u) { return mm_to_inches(base_radius(u.models.front().base)); }

// The monster fixture (D-073, the rosters' units on the 44"x60" board): the Screamer-killer at (30, 45), every other
// unit far from it and from each other (more than 12" apart), so each test moves in only the units it needs.
GameState spread_fixture() {
    GameState gs = mf::fixture();
    mf::place_rank(gs.units[mf::kScreamer], 30.0f, 45.0f);
    mf::place_rank(gs.units[mf::kLegionaries], 10.0f, 5.0f);
    mf::place_rank(gs.units[mf::kLord], 40.0f, 5.0f);
    mf::place_rank(gs.units[mf::kHavocs], 10.0f, 25.0f);
    mf::place_rank(gs.units[mf::kTyrant], 40.0f, 25.0f);
    mf::place_rank(gs.units[mf::kGuard], 5.0f, 55.0f);
    return gs;
}

// Puts unit `i`'s rank 1" south of the Screamer-killer (edge to edge from the Screamer's centre straight south).
void engage_south(GameState& gs, std::size_t i) {
    const Unit& s = gs.units[mf::kScreamer];
    mf::place_rank(gs.units[i], 30.0f, 45.0f - 1.0f - radius_in(s) - radius_in(gs.units[i]));
}

// The same, 1" east of it.
void engage_east(GameState& gs, std::size_t i) {
    const Unit& s = gs.units[mf::kScreamer];
    mf::place_rank(gs.units[i], 30.0f + 1.0f + radius_in(s) + radius_in(gs.units[i]), 45.0f);
}

std::vector<UnitId> ids(std::initializer_list<std::size_t> indices, const GameState& gs) {
    std::vector<UnitId> out;
    for (const std::size_t i : indices) out.push_back(gs.units[i].id);
    return out;
}

const std::vector<GameEvent> kFightStarts{event::PhaseStarted{Phase::Fight, Side::A}};

std::string first_line(const std::string& text) { return text.substr(0, text.find('\n')); }

}  // namespace

// ---- Gang-ups and lone engagements (taken at the start of each Fight phase) ----

TEST(M78_Arena, TwoUnitsEngagedWithOneEnemyCountAsAGangUp) {
    GameState gs = spread_fixture();
    engage_south(gs, mf::kLegionaries);  // the chainsword Legionaries' rank, 1" south of the Screamer-killer
    engage_east(gs, mf::kLord);          // and the Chaos Lord, 1" east of it
    const Unit& screamer = gs.units[mf::kScreamer];
    ASSERT_EQ(engaged_enemy_units(gs, gs.units[mf::kLegionaries]), ids({mf::kScreamer}, gs));
    ASSERT_EQ(engaged_enemy_units(gs, gs.units[mf::kLord]), ids({mf::kScreamer}, gs));
    ASSERT_EQ(engaged_enemy_units(gs, screamer), ids({mf::kLord, mf::kLegionaries}, gs));

    const EngagementSnapshot snap = engagement_snapshot(gs);
    EXPECT_EQ(snap.ganged_up[side_index(Side::A)], ids({mf::kScreamer}, gs));  // one gang-up, on the Screamer-killer
    EXPECT_TRUE(snap.ganged_up[side_index(Side::B)].empty());
    EXPECT_TRUE(snap.lone[side_index(Side::A)].empty());  // each of the two has the other engaged with its enemy
    // The Screamer-killer is alone: no other Tyranid unit is within 12" of either unit it fights.
    EXPECT_EQ(snap.lone[side_index(Side::B)], ids({mf::kScreamer}, gs));

    // The arena's counter: one Fight phase gives one gang-up; a second Fight phase in the same position another one,
    // on the same enemy unit (distinct enemy units ganged up on: still one). No other event counts them.
    TeamworkCounter counter;
    counter.observe({}, gs);
    EXPECT_EQ(counter.stats().gang_ups[0], 0);
    counter.observe(kFightStarts, gs);
    EXPECT_EQ(counter.stats().gang_ups[0], 1);
    EXPECT_EQ(counter.stats().ganged_units[0], 1);
    EXPECT_EQ(counter.stats().lone_engagements[1], 1);
    counter.observe({event::PhaseEnded{Phase::Fight, Side::A}}, gs);
    counter.observe({event::PhaseStarted{Phase::Fight, Side::B}}, gs);
    const TeamworkStats t = counter.stats();
    EXPECT_EQ(t.gang_ups[0], 2);
    EXPECT_EQ(t.ganged_units[0], 1);
    EXPECT_EQ(t.gang_ups[1], 0);
    EXPECT_EQ(t.lone_engagements[0], 0);
    EXPECT_EQ(t.lone_engagements[1], 2);

    // The Tyrant and the Tyrant Guard both engaged with the Legionaries (the monster fixture's engaged layout): side B's
    // gang-up. Nobody is alone: the Chaos Lord is within 12" of both Tyranid units, and each Tyranid unit has the other
    // engaged with the Legionaries.
    const GameState engaged = mf::fixture(true);
    const EngagementSnapshot e = engagement_snapshot(engaged);
    EXPECT_EQ(e.ganged_up[side_index(Side::B)], ids({mf::kLegionaries}, engaged));
    EXPECT_TRUE(e.ganged_up[side_index(Side::A)].empty());
    EXPECT_TRUE(e.lone[side_index(Side::B)].empty());
    EXPECT_TRUE(e.lone[side_index(Side::A)].empty());
}

TEST(M78_Arena, ALoneEngagementIsCounted) {
    GameState gs = spread_fixture();
    engage_south(gs, mf::kLegionaries);  // the Legionaries alone against the Screamer-killer
    const EngagementSnapshot alone = engagement_snapshot(gs);
    EXPECT_EQ(alone.lone[side_index(Side::A)], ids({mf::kLegionaries}, gs));
    EXPECT_EQ(alone.lone[side_index(Side::B)], ids({mf::kScreamer}, gs));
    EXPECT_TRUE(alone.ganged_up[0].empty());
    EXPECT_TRUE(alone.ganged_up[1].empty());

    // The Chaos Lord 8" east of the Screamer-killer (not engaged, within 12" of it): support, no lone engagement.
    const Unit& screamer = gs.units[mf::kScreamer];
    mf::place_rank(gs.units[mf::kLord], 30.0f + 8.0f + radius_in(screamer) + radius_in(gs.units[mf::kLord]), 45.0f);
    const std::optional<float> lord_gap = unit_distance(gs.units[mf::kLord], screamer);
    ASSERT_TRUE(lord_gap.has_value());
    ASSERT_TRUE(within(*lord_gap, 12.0f * MM_PER_INCH));
    ASSERT_FALSE(is_engaged(gs, gs.units[mf::kLord]));
    const EngagementSnapshot supported = engagement_snapshot(gs);
    EXPECT_TRUE(supported.lone[side_index(Side::A)].empty());
    EXPECT_TRUE(supported.ganged_up[side_index(Side::A)].empty());  // the Lord is not engaged: no gang-up either

    // 13" west of it: more than 12", the Legionaries' engagement is lone again.
    mf::place_rank(gs.units[mf::kLord], 30.0f - 13.0f - radius_in(screamer) - radius_in(gs.units[mf::kLord]), 45.0f);
    ASSERT_TRUE(more_than(*unit_distance(gs.units[mf::kLord], screamer), 12.0f * MM_PER_INCH));
    EXPECT_EQ(engagement_snapshot(gs).lone[side_index(Side::A)], ids({mf::kLegionaries}, gs));

    // In the counter, once per Fight phase; nothing is counted outside a Fight phase's start.
    TeamworkCounter counter;
    counter.observe({}, gs);
    counter.observe({event::PhaseStarted{Phase::Shooting, Side::A}}, gs);
    EXPECT_EQ(counter.stats().lone_engagements[0], 0);
    counter.observe(kFightStarts, gs);
    EXPECT_EQ(counter.stats().lone_engagements[0], 1);
    EXPECT_EQ(counter.stats().lone_engagements[1], 1);
}

// ---- Shooting spread ----

TEST(M78_Arena, ShootingSpreadCountsDistinctTargets) {
    const GameState gs = spread_fixture();
    const UnitId havocs = gs.units[mf::kHavocs].id;
    const UnitId lord = gs.units[mf::kLord].id;
    const UnitId tyrant = gs.units[mf::kTyrant].id;
    const UnitId screamer = gs.units[mf::kScreamer].id;
    const UnitId guard = gs.units[mf::kGuard].id;
    const auto selection = [](const Unit& u, std::size_t model, UnitId target) {
        WeaponSelection s;
        s.attacker = u.models[model].id;
        s.target = target;
        return s;
    };
    // The Havocs: three lascannons at the Tyrant, two at the Screamer-killer: two distinct targets.
    const Unit& h = gs.units[mf::kHavocs];
    event::ShootingStarted split{havocs, ShootingType::Normal,
                                 {selection(h, 0, tyrant), selection(h, 1, tyrant), selection(h, 2, screamer),
                                  selection(h, 3, tyrant), selection(h, 4, screamer)}};
    EXPECT_EQ(distinct_targets(split), 2);
    // The Lord: one pistol at the Tyrant.
    const event::ShootingStarted one{lord, ShootingType::Normal, {selection(gs.units[mf::kLord], 0, tyrant)}};
    EXPECT_EQ(distinct_targets(one), 1);
    EXPECT_EQ(distinct_targets(event::ShootingStarted{havocs, ShootingType::Normal, {}}), 0);
    // Three targets.
    const event::ShootingStarted three{havocs, ShootingType::Normal,
                                       {selection(h, 0, tyrant), selection(h, 1, screamer), selection(h, 2, guard)}};
    EXPECT_EQ(distinct_targets(three), 3);

    // The counter: side A's shooting phase with both Shoots (6 selections, 4 at the Tyrant): 2 Shoots of 3 distinct
    // targets, one shooting phase whose top target has 4 of 6. A second phase, all at one target: share 1.
    TeamworkCounter counter;
    counter.observe({}, gs);
    counter.observe({event::PhaseStarted{Phase::Shooting, Side::A}, split}, gs);
    counter.observe({one, event::PhaseEnded{Phase::Shooting, Side::A}}, gs);
    TeamworkStats t = counter.stats();
    EXPECT_EQ(t.shoots[0], 2);
    EXPECT_EQ(t.shoot_targets[0], 3);
    EXPECT_EQ(t.shooting_phases[0], 1);
    EXPECT_DOUBLE_EQ(t.top_share_sum[0], 4.0 / 6.0);
    EXPECT_EQ(t.shoots[1], 0);
    EXPECT_EQ(t.shooting_phases[1], 0);
    counter.observe({event::PhaseStarted{Phase::Shooting, Side::A}, one}, gs);
    t = counter.stats();  // a phase still open counts as ended
    EXPECT_EQ(t.shoots[0], 3);
    EXPECT_EQ(t.shooting_phases[0], 2);
    EXPECT_DOUBLE_EQ(t.top_share_sum[0], 4.0 / 6.0 + 1.0);
}

// ---- Contact ----

TEST(M78_Arena, ContactCountsUnitsNeverWithin12AndTheFirstEngagementRound) {
    GameState gs = spread_fixture();  // every unit more than 12" from every enemy unit
    TeamworkCounter counter;
    counter.observe({}, gs);
    TeamworkStats t = counter.stats();
    EXPECT_EQ(t.units[0], 3);
    EXPECT_EQ(t.units[1], 3);
    EXPECT_EQ(t.never_within_12[0], 3);
    EXPECT_EQ(t.never_within_12[1], 3);
    EXPECT_EQ(t.first_engagement_round[0], 0);
    // The Havocs about 9" from the Tyrant: both come within 12" (and stay counted when they part again).
    const Unit& tyrant = gs.units[mf::kTyrant];
    mf::place_rank(gs.units[mf::kHavocs], 26.0f, 25.0f);
    ASSERT_TRUE(within(*unit_distance(gs.units[mf::kHavocs], tyrant), 12.0f * MM_PER_INCH));
    counter.observe({}, gs);
    mf::place_rank(gs.units[mf::kHavocs], 10.0f, 25.0f);
    counter.observe({}, gs);
    t = counter.stats();
    EXPECT_EQ(t.never_within_12[0], 2);
    EXPECT_EQ(t.never_within_12[1], 2);
    EXPECT_EQ(t.first_engagement_round[0], 0);
    // An engagement in battle round 2: both sides' first engagement round (an engagement is both sides').
    gs.flow.battle_round = 2;
    engage_south(gs, mf::kLegionaries);
    counter.observe({}, gs);
    gs.flow.battle_round = 3;
    counter.observe({}, gs);
    t = counter.stats();
    EXPECT_EQ(t.first_engagement_round[0], 2);
    EXPECT_EQ(t.first_engagement_round[1], 2);
    EXPECT_EQ(t.never_within_12[0], 1);  // the Lord
    EXPECT_EQ(t.never_within_12[1], 1);  // the Tyrant Guard
    // A unit destroyed before it came within 12" still counts as never within 12"; it is counted once.
    gs.units[mf::kGuard].models.clear();
    counter.observe({}, gs);
    EXPECT_EQ(counter.stats().never_within_12[1], 1);
    EXPECT_EQ(counter.stats().units[1], 3);
}

// ---- The --per-game line is unchanged ----

TEST(M78_Arena, StatisticsDoNotChangeThePerGameLine) {
    // bot_20_termagants Normal vs Normal, seed 1: the pre-batch baseline's line (main at c54c47c), a game with fights.
    const GameState s = scenario("bot_20_termagants.json");
    const PerGameReport r = run_per_game(s, 1, 1, level(BotLevel::Normal), level(BotLevel::Normal));
    EXPECT_EQ(first_line(r.text),
              "game 1: finished 1 winner A round 2 steps 69 surviving 5/0 wounds lost 1/20 coherency 0/0 fight steps "
              "2/3 fight moves 2/3 rejected 0 reverted 0 fallbacks 0 actions 04bfbe00cd6674d0 state d55512b94297d01b "
              "trace 65802985f119abbe");
    // The same game's record has its teamwork statistics (they are only in the summary).
    const GameRecord g = play_bot_game(s, 1, level(BotLevel::Normal), level(BotLevel::Normal));
    EXPECT_EQ(g.teamwork.units[0], 1);
    EXPECT_EQ(g.teamwork.units[1], 1);
    EXPECT_GE(g.teamwork.first_engagement_round[0], 1);
    EXPECT_EQ(g.teamwork.first_engagement_round[0], g.teamwork.first_engagement_round[1]);
    EXPECT_EQ(g.teamwork.gang_ups[0] + g.teamwork.gang_ups[1], 0);  // one unit a side: never a gang-up
    EXPECT_GT(g.teamwork.lone_engagements[0] + g.teamwork.lone_engagements[1], 0);
    EXPECT_GT(g.teamwork.shoots[0], 0);
    const ArenaResult a = run_arena(s, 1, 1, level(BotLevel::Normal), level(BotLevel::Normal));
    EXPECT_NE(format_arena(a, s).find("teamwork (M78), per game: gang-ups A 0.00 / B 0.00"), std::string::npos);
}

// ---- The regression switch: Generation::M77 ----

// M78_Regression.TheM77LevelsPlayAsTheCurrentOnes (Strike and rosters games gave the same per-game lines at the
// current and the m77 levels) held until M80, which changes play on purpose: replaced by
// M80_Regression.TheCurrentLevelsDifferFromM77OnTheRosters (m80_test.cpp).

TEST(M78_Regression, TheM77LevelsMatchThePreBatchHashes) {
    // The pre-batch baseline (Release arena of main at c54c47c, --per-game 1): normal-m77 and hard-m77 must keep playing
    // these games exactly, through every Teamwork milestone (M79-M83).
    struct Pinned {
        const char* file;
        BotConfig a;
        BotConfig b;
        const char* line;
    };
    const BotConfig normal = level(BotLevel::Normal, Generation::M77);
    const BotConfig hard = level(BotLevel::Hard, Generation::M77);
    const BotConfig easy = level(BotLevel::Easy);
    const std::vector<Pinned> pinned = {
        {"strike_44x60.json", normal, normal,
         "game 2: finished 1 winner A round 3 steps 150 surviving 14/0 wounds lost 2/40 coherency 0/0 fight steps 0/0 "
         "fight moves 0/0 rejected 0 reverted 0 fallbacks 0 actions 63999db34ef636fe state ddfa9bced92df194 trace "
         "a5e0f1ce80c2892d"},
        {"strike_44x60.json", normal, normal,
         "game 3: finished 1 winner A round 3 steps 131 surviving 14/0 wounds lost 2/40 coherency 0/0 fight steps 0/0 "
         "fight moves 0/0 rejected 0 reverted 0 fallbacks 0 actions ec81ebe1de7ea547 state 7cc47a0514665e04 trace "
         "617a5d6a6a796371"},
        {"rosters_44x60.json", normal, normal,
         "game 11: finished 1 winner B round 5 steps 511 surviving 22/35 wounds lost 39/20 coherency 0/0 fight steps 0/0 "
         "fight moves 0/0 rejected 0 reverted 0 fallbacks 0 actions 7d2ef56c1b4e958f state ff32fb959326fd51 trace "
         "fe489cc69574f8e6"},
        {"rosters_44x60.json", normal, normal,
         "game 63: finished 1 winner A round 5 steps 498 surviving 28/34 wounds lost 27/43 coherency 0/0 fight steps 0/0 "
         "fight moves 0/0 rejected 0 reverted 0 fallbacks 0 actions a5946d7612812c6b state 073752c5d52faec7 trace "
         "8e134a32b7a1925c"},
        // Easy vs Hard: Hard's Tyranids (9 units: they move as Normal, D-078; Hard's charges, casualties and
        // consolidations).
        {"rosters_44x60.json", easy, hard,
         "game 5: finished 1 winner B round 5 steps 574 surviving 0/32 wounds lost 90/22 coherency 0/0 fight steps 6/0 "
         "fight moves 6/0 rejected 0 reverted 0 fallbacks 0 actions 3d57069a2ee97fd7 state b8d9c09cb86bc2c0 trace "
         "24ff1254f7062519"},
    };
    for (const Pinned& p : pinned) {
        const std::string line = p.line;
        const std::uint64_t seed = std::stoull(line.substr(5, line.find(':') - 5));
        const PerGameReport r = run_per_game(scenario(p.file), 1, seed, p.a, p.b);
        EXPECT_EQ(first_line(r.text), line) << p.file;
    }
}
