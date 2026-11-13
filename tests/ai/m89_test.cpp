// M89 (D-090): the bot plays for cover. Its move score sees what terrain does to the hypothetical shots (13.08 cover,
// 13.09 Hidden, 13.10 / 13.11 sight) through analysis::TerrainSight, its candidates take spots in cover, out of sight
// and round walls, and hand deployment prefers cover. With the cover weight 0, or without terrain, it plays as at M88.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "analysis/odds.hpp"
#include "analysis/terrain_sight.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/terrain_shape.hpp"
#include "core/geometry/visibility.hpp"
#include "core/rules/terrain.hpp"
#include "deploy_policy.hpp"
#include "normal/move_score.hpp"
#include "normal/movement.hpp"
#include "simulator.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
namespace mf = wh40k::test::monsters;
using wh40k::test::at;
using wh40k::test::in;
using wh40k::test::repo_root;

namespace {

constexpr const char* kTerrainScenario = "data/scenarios/incursion_terrain_44x60.json";

GameState terrain_scenario() { return load_scenario(repo_root() / kTerrainScenario, repo_root()); }

BasePose round_at(float x_in, float y_in, float diameter_mm = 32.0f) {
    return BasePose{at(x_in, y_in), Base{BaseShape::Round, diameter_mm, 0.0f}, 0.0f};
}

TerrainShape rect_in(float x0, float x1, float y0, float y1) {
    return polygon_shape({at(x0, y0), at(x1, y0), at(x1, y1), at(x0, y1)});
}

// An area round a rectangle, with one feature of `category` (a Dense one 5" tall) in it.
TerrainArea area_with(const std::string& name, const TerrainShape& area, const TerrainShape& feature,
                      TerrainCategory category) {
    TerrainFeature f;
    f.name = name + " feature";
    f.category = category;
    if (category == TerrainCategory::Dense) f.height_mm = in(5);
    f.shape = feature;
    TerrainArea a;
    a.name = name;
    a.shape = area;
    a.features.push_back(f);
    return a;
}

normal::MoveWeights with_cover(double cover) {
    normal::MoveWeights w;
    w.cover = cover;
    return w;
}

std::vector<Position> positions_of(const Unit& u) {
    std::vector<Position> out;
    for (const Model& m : u.models) out.push_back(m.position);
    return out;
}

// The monsters fixture at Side B's Move Units decision with `terrain` added and the Tyrant Guard's (INFANTRY, three
// 50 mm models round (6, 40)) Normal move started. The Chaos Lord at (16, 36) is its closest enemy unit.
GameState guard_moving(std::vector<TerrainArea> terrain) {
    GameState gs = mf::at_movement();
    gs.terrain = std::move(terrain);
    apply_action(gs, Side::B, action::StartMove{gs.units[mf::kGuard].id, MoveType::Normal, std::nullopt});
    return gs;
}

normal::PlannedEnd stays(const GameState& gs, std::size_t unit) {
    normal::PlannedEnd p;
    p.type = MoveType::Normal;
    p.positions = positions_of(gs.units[unit]);
    return p;
}

// A crater round the Tyrant Guard (13.08: INFANTRY within any area has the benefit of cover).
TerrainArea guard_crater() {
    return area_with("Crater", circle_shape(at(6, 40), in(10)), circle_shape(at(6, 44), in(1)), TerrainCategory::Exposed);
}

// A crater 6" north of the Tyrant Guard, within its Move.
TerrainArea crater_north() {
    return area_with("Crater", circle_shape(at(6, 47), in(6)), circle_shape(at(6, 47), in(2)), TerrainCategory::Exposed);
}

[[maybe_unused]] TerrainArea unused_crater() {
    return area_with("Crater", circle_shape(at(6, 40), in(10)), circle_shape(at(6, 44), in(1)), TerrainCategory::Exposed);
}

// A ruin between the Tyrant Guard and every Chaos unit (13.10: an obscuring area neither is within).
TerrainArea screening_ruin() {
    return area_with("Ruin", rect_in(11, 13, 28, 52), rect_in(11.5f, 12.5f, 29, 51), TerrainCategory::Dense);
}

std::uint32_t exact_mask(const GameState& gs, const BasePose& p) {
    std::uint32_t mask = 0;
    for (std::size_t i = 0; i < gs.terrain.size(); ++i) {
        if (model_within_area(p, gs.terrain[i])) mask |= std::uint32_t{1} << i;
    }
    return mask;
}

}  // namespace

// D-090: the three-line estimate agrees with core's exact search (has_line_of_sight past the sight blockers, D-088)
// on most pairs of 32 mm bases across the terrain scenario within 24" of each other.
TEST(M89_Sight, TheEstimateAgreesWithCoresSearchOnMostPairs) {
    const GameState gs = terrain_scenario();
    analysis::TerrainSight sight(gs);
    TerrainView view(gs);
    std::uint64_t seed = 89;
    const auto next = [&] {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<float>((seed >> 33) % 100000) / 100000.0f;
    };
    int pairs = 0;
    int agree = 0;
    int blocked = 0;
    int full_pairs = 0;
    int full_agree = 0;
    while (pairs < 4000) {
        const BasePose a = round_at(1.0f + 42.0f * next(), 1.0f + 58.0f * next());
        const BasePose b = round_at(1.0f + 42.0f * next(), 1.0f + 58.0f * next());
        if (base_distance(a, b) > in(24)) continue;
        if (solid_feature_under(gs, a) || solid_feature_under(gs, b)) continue;
        const std::uint32_t ma = exact_mask(gs, a);
        const std::uint32_t mb = exact_mask(gs, b);
        ASSERT_EQ(sight.mask_at(a), ma);
        ASSERT_EQ(sight.mask_at(b), mb);
        const PosedModel pa{ModelId{1}, a.position, a.base, 0.0f};
        const PosedModel pb{ModelId{2}, b.position, b.base, 0.0f};
        const bool exact = has_line_of_sight(pa, pb, std::span<const PosedModel>{}, view.sight_blockers(ma | mb));
        const analysis::SightLine est = sight.line(a, ma, b, mb, false, true);
        ++pairs;
        if (!exact) ++blocked;
        if (est.visible == exact) ++agree;
        if (exact) {
            ++full_pairs;
            const bool full = fully_visible_past(pa, pb, view.cover_blockers(ma | mb));
            if (est.fully_visible == full) ++full_agree;
        }
    }
    std::printf("[ M89 ] sight: %d of %d pairs agree (%d blocked); fully visible: %d of %d\n", agree, pairs, blocked,
                full_agree, full_pairs);
    EXPECT_GT(blocked, 200);  // the sample does test terrain
    EXPECT_GE(agree, pairs * 95 / 100);
    EXPECT_GE(full_agree, full_pairs * 90 / 100);
}

// ==== analysis::ShotContext (D-090) ====

TEST(M89_Odds, CoverWorsensEveryProfilesBsByOneAndUnseenGivesNoVolley) {
    const GameState gs = mf::fixture();
    analysis::OddsCache cache;
    const Unit& legionaries = gs.units[mf::kLegionaries];
    const Unit& tyrant = gs.units[mf::kTyrant];
    analysis::ShotContext c;
    c.gap_mm = in(6);
    const analysis::Volley open = analysis::unit_ranged_volley(cache, legionaries, tyrant, c);
    c.target_cover = true;
    const analysis::Volley covered = analysis::unit_ranged_volley(cache, legionaries, tyrant, c);
    ASSERT_FALSE(open.empty());
    ASSERT_EQ(open.size(), covered.size());
    for (std::size_t i = 0; i < open.size(); ++i) {
        ASSERT_EQ(open[i].profile.skill.has_value(), covered[i].profile.skill.has_value());
        if (open[i].profile.skill) EXPECT_EQ(*covered[i].profile.skill, *open[i].profile.skill + 1);  // 13.08 (D-089)
        EXPECT_EQ(open[i].profile.hit_modifier, covered[i].profile.hit_modifier);  // the characteristic, not the roll
    }
    const analysis::TargetSnapshot snap = analysis::snapshot_of(tyrant);
    EXPECT_LT(analysis::expected_outcome(cache, covered, snap).model_equivalents,
              analysis::expected_outcome(cache, open, snap).model_equivalents);
    c.visible = false;
    EXPECT_TRUE(analysis::unit_ranged_volley(cache, legionaries, tyrant, c).empty());
}

// ==== The move score (D-090) ====

TEST(M89_MoveScore, WithoutTerrainOrAtCoverZeroTheScoreIsTheTerrainBlindOne) {
    const GameState with = guard_moving({guard_crater(), screening_ruin()});
    GameState bare = with;
    bare.terrain.clear();
    analysis::OddsCache cache;
    const UnitId guard = with.units[mf::kGuard].id;
    normal::MoveScoreContext off(cache, with, guard, with_cover(0.0));
    normal::MoveScoreContext blind(cache, bare, guard, with_cover(1.0));
    EXPECT_FALSE(off.cover_spots());
    EXPECT_FALSE(blind.cover_spots());
    for (const normal::MoveCandidate& c : normal::move_candidates(with)) {
        normal::PlannedEnd p;
        p.type = MoveType::Normal;
        p.positions = c.ends;
        p.longest_path_mm = c.longest_mm;
        const normal::MoveScore a = normal::score_move_plan(off, p);
        const normal::MoveScore b = normal::score_move_plan(blind, p);
        EXPECT_EQ(a.total, b.total) << c.label;  // bit for bit
        EXPECT_EQ(a.hold, b.hold) << c.label;
        EXPECT_FALSE(a.in_cover || a.hidden || a.unseen);
    }
}

TEST(M89_MoveScore, InCoverTheEnemysReplyCostsLess) {
    // The Chaos Lord in the north-west corner, so the closest enemy unit can't keep out of its own range.
    GameState crater = guard_moving({guard_crater()});
    mf::place_rank(crater.units[mf::kLord], 1.0f, 58.5f);
    GameState bare = crater;
    bare.terrain.clear();
    analysis::OddsCache cache;
    const UnitId guard = crater.units[mf::kGuard].id;
    normal::MoveScoreContext cover(cache, crater, guard);
    normal::MoveScoreContext blind(cache, bare, guard);
    ASSERT_TRUE(cover.cover_spots());
    const normal::PlannedEnd p = stays(crater, mf::kGuard);
    const normal::MoveScore a = normal::score_move_plan(cover, p);
    const normal::MoveScore b = normal::score_move_plan(blind, p);
    EXPECT_TRUE(a.in_cover);  // 13.08: INFANTRY within an area, against every attacker
    EXPECT_FALSE(a.unseen);
    EXPECT_GT(a.hold, b.hold);  // the reply's BS worsened by 1 (D-089)
}

TEST(M89_MoveScore, BehindAnObscuringRuinTheClosestEnemyCantSeeUsWhereItStands) {
    const GameState ruin = guard_moving({screening_ruin()});
    analysis::OddsCache cache;
    normal::MoveScoreContext ctx(cache, ruin, ruin.units[mf::kGuard].id);
    ASSERT_NE(ctx.enemy(), nullptr);
    EXPECT_EQ(ctx.enemy()->id, ruin.units[mf::kLord].id);
    const std::vector<Position> here = positions_of(ruin.units[mf::kGuard]);
    const normal::MoveScoreContext::ReplyFacts f = ctx.begin_reply(here, true);
    ctx.end_reply();
    EXPECT_TRUE(f.unseen);    // 13.10: every line crosses the ruin
    EXPECT_FALSE(f.hidden);   // not within an area with a Dense feature
    EXPECT_FALSE(f.in_cover);  // no cover from an area (and unseen is not "in cover")
}

TEST(M89_MoveScore, HiddenOnlyWithoutRangedAttacksThisTurn) {
    // A ruin round the Tyrant Guard with a Dense wall west of it: 13.09 while it makes no ranged attacks.
    const TerrainArea ruin = area_with("Ruin", rect_in(1, 10, 36, 44), rect_in(1.2f, 1.7f, 37, 43), TerrainCategory::Dense);
    const GameState gs = guard_moving({ruin});
    analysis::OddsCache cache;
    normal::MoveScoreContext ctx(cache, gs, gs.units[mf::kGuard].id);
    const std::vector<Position> here = positions_of(gs.units[mf::kGuard]);
    const normal::MoveScoreContext::ReplyFacts quiet = ctx.begin_reply(here, false);
    ctx.end_reply();
    EXPECT_TRUE(quiet.hidden);
    EXPECT_TRUE(quiet.in_cover);
    const normal::MoveScoreContext::ReplyFacts shooting = ctx.begin_reply(here, true);
    ctx.end_reply();
    EXPECT_FALSE(shooting.hidden);  // its ranged attacks this turn are the previous turn's in the enemy's turn
    EXPECT_TRUE(shooting.in_cover);
}

TEST(M89_MoveScore, TheBlendAtHalfIsHalfway) {
    // Our shooting this turn: the Legionaries (Side A) at the Tyrant across an obscuring ruin for half their models.
    GameState gs = mf::at_movement(false, Side::A);
    gs.terrain.push_back(area_with("Ruin", rect_in(15, 21, 27.2f, 29), rect_in(15.5f, 20.5f, 27.5f, 28.5f),
                                   TerrainCategory::Dense));
    apply_action(gs, Side::A, action::StartMove{gs.units[mf::kLegionaries].id, MoveType::Normal, std::nullopt});
    const UnitId legionaries = gs.units[mf::kLegionaries].id;
    const std::vector<Position> here = positions_of(gs.units[mf::kLegionaries]);
    analysis::OddsCache cache;
    const auto shoot = [&](double cover) {
        normal::MoveScoreContext ctx(cache, gs, legionaries, with_cover(cover));
        EXPECT_EQ(ctx.enemy()->id, gs.units[mf::kTyrant].id);
        return ctx.our_shooting(here, ShootingType::Normal, true);
    };
    const double blind = shoot(0.0);
    const double seen = shoot(1.0);
    EXPECT_LT(seen, blind);  // some Legionaries can't see the Tyrant past the ruin
    EXPECT_NEAR(shoot(0.5), 0.5 * blind + 0.5 * seen, 1e-12);
}

// ==== Candidates (D-090) ====

TEST(M89_Candidates, CoverSpotsOnlyWithCoverOnAndAfterTheOthers) {
    const GameState gs = guard_moving({crater_north(), screening_ruin()});
    const std::vector<normal::MoveCandidate> m88 = normal::move_candidates(gs);
    const std::vector<normal::MoveCandidate> m89 = normal::move_candidates(gs, nullptr, false, true);
    ASSERT_GT(m89.size(), m88.size());
    for (std::size_t i = 0; i < m88.size(); ++i) EXPECT_EQ(m89[i].ends, m88[i].ends);  // unchanged, in order
    int cover = 0;
    int behind = 0;
    for (std::size_t i = m88.size(); i < m89.size(); ++i) {
        if (m89[i].label.find("into cover in Crater") != std::string::npos) ++cover;
        if (m89[i].label.find("behind Ruin") != std::string::npos) ++behind;
        Simulator sim(gs, 89);  // the engine agrees
        for (const action::ProposeModelMove& p : m89[i].proposals) ASSERT_TRUE(sim.step(Side::B, p).ok()) << m89[i].label;
    }
    EXPECT_GT(cover, 0);
    EXPECT_GT(behind, 0);
}

TEST(M89_Candidates, InfantryHidesInADenseRuin) {
    const TerrainArea ruin = area_with("Ruin", rect_in(1, 12, 37, 47), rect_in(1.2f, 1.7f, 38, 46), TerrainCategory::Dense);
    const GameState gs = guard_moving({ruin});
    int hides = 0;
    for (const normal::MoveCandidate& c : normal::move_candidates(gs, nullptr, false, true)) {
        if (c.label.find("hides in Ruin") == std::string::npos) continue;
        ++hides;
        for (std::size_t i = 0; i < c.ends.size(); ++i) {
            EXPECT_TRUE(model_within_area(BasePose{c.ends[i], gs.units[mf::kGuard].models[i].base, 0.0f}, ruin));
        }
    }
    EXPECT_EQ(hides, 1);
}

TEST(M89_Candidates, AMonsterGoesRoundAWallItCantCross) {
    // The Hive Tyrant at (22, 30) with a 5" wall from x 18 to 26 just north of it; its closest enemy unit, the Chaos Lord
    // moved to (22, 40), beyond it (the Legionaries moved out of the way to (22, 5)).
    GameState gs = mf::at_movement();
    mf::place_rank(gs.units[mf::kLord], 22.0f, 40.0f);
    mf::place_rank(gs.units[mf::kLegionaries], 22.0f, 5.0f);
    gs.terrain.push_back(area_with("Ruin", rect_in(17, 27, 32, 34), rect_in(18, 26, 32.5f, 33), TerrainCategory::Dense));
    apply_action(gs, Side::B, action::StartMove{gs.units[mf::kTyrant].id, MoveType::Normal, std::nullopt});
    const TerrainShape& wall = gs.terrain[0].features[0].shape;
    const Model& tyrant = gs.units[mf::kTyrant].models.front();
    int detours = 0;
    int past = 0;  // detours that get past the wall
    for (const normal::MoveCandidate& c : normal::move_candidates(gs, nullptr, false, true)) {
        if (c.label.find("round the wall") == std::string::npos) continue;
        ++detours;
        ASSERT_EQ(c.proposals.size(), 1u);
        const std::vector<Position>& path = c.proposals.front().path;
        ASSERT_EQ(path.size(), 2u);
        const BasePose start{tyrant.position, tyrant.base, 0.0f};
        EXPECT_FALSE(translation_crosses_shape(start, path[0], wall));
        EXPECT_FALSE(translation_crosses_shape(BasePose{path[0], tyrant.base, 0.0f}, path[1], wall));
        std::printf("%s: via (%.2f, %.2f) to (%.2f, %.2f)\n", c.label.c_str(), path[0].x / 25.4, path[0].y / 25.4,
                    path[1].x / 25.4, path[1].y / 25.4);
        if (path[1].y > in(33)) ++past;
        Simulator sim(gs, 89);
        ASSERT_TRUE(sim.step(Side::B, c.proposals.front()).ok()) << c.label;
        ASSERT_TRUE(sim.step(Side::B, action::CommitUnitMove{}).ok()) << c.label;
    }
    EXPECT_GE(detours, 2);
    EXPECT_GE(past, 1);
}

// ==== Hand deployment (D-090) ====

TEST(M89_Deploy, InfantrySetsUpInCoverWhenItCan) {
    ArmySource source = load_army_source(repo_root());
    source.mission = load_mission_block(repo_root() / "data/scenarios/incursion_objectives_44x60.json");
    source.terrain = load_terrain_block(repo_root() / kTerrainScenario);
    int set_ups = 0;
    int more_cover = 0;
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        GameState gs = random_armies_battle(source, seed, Side::A, DeploymentMode::Hand);
        if (!gs.pending) start_game(gs);
        while (gs.pending && std::holds_alternative<decision::DeployUnits>(gs.pending->kind)) {
            const Side side = wh40k::test::decider(gs);
            const std::vector<GameAction> legal = legal_actions(gs, side);
            const std::optional<PolicyResult> on = heuristic_deploy_policy(gs, side, legal, true);
            ASSERT_TRUE(on.has_value());
            if (const auto* d = std::get_if<action::DeployUnit>(&on->action)) {
                // The engine's own proposal for the same unit.
                for (const GameAction& a : legal) {
                    const auto* p = std::get_if<action::DeployUnit>(&a);
                    if (p == nullptr || is_template(a) || p->unit != d->unit) continue;
                    ++set_ups;
                    EXPECT_GE(deploy_cover_count(gs, *d), deploy_cover_count(gs, *p));
                    if (deploy_cover_count(gs, *d) > deploy_cover_count(gs, *p)) ++more_cover;
                    break;
                }
            }
            ASSERT_TRUE(validate_action(gs, side, on->action).ok());
            apply_action(gs, side, on->action);
        }
    }
    std::printf("[ M89 ] %d set-ups, %d with more cover than the proposal\n", set_ups, more_cover);
    EXPECT_GT(more_cover, 0);
}

// ==== Arena and regression (D-090) ====

namespace {

std::string per_game_field(const std::string& line, const std::string& key) {
    const std::size_t at = line.find(key + " ");
    if (at == std::string::npos) return "";
    const std::size_t start = at + key.size() + 1;
    return line.substr(start, line.find_first_of(" \n", start) - start);
}

// M88's Release arena on the terrain scenario, seed 1 (`--per-game 1`).
constexpr const char* kM88Actions = "9adc6beead7f055c";
constexpr const char* kM88State = "d7c20c6aabaa3603";
constexpr const char* kM88Trace = "d066bf6ecacfd5c7";

}  // namespace

TEST(M89_Regression, AtCoverZeroTheTerrainScenarioPlaysAsAtM88) {
    BotConfig m88;
    m88.move.cover = 0.0;
    const PerGameReport r = run_per_game(terrain_scenario(), 1, 1, m88, m88);
    EXPECT_TRUE(r.clean);
    EXPECT_EQ(per_game_field(r.text, "actions"), kM88Actions);
    EXPECT_EQ(per_game_field(r.text, "state"), kM88State);
    EXPECT_EQ(per_game_field(r.text, "trace"), kM88Trace);
}

TEST(M89_Regression, TheDefaultWeightsPlayTheTerrainScenarioDifferently) {
    const PerGameReport r = run_per_game(terrain_scenario(), 1, 1, BotConfig{}, BotConfig{});
    EXPECT_TRUE(r.clean);
    EXPECT_NE(per_game_field(r.text, "actions"), kM88Actions);
}

TEST(M89_Arena, TheSummaryHasTheCoverLineOnTerrainOnly) {
    const GameState scenario = terrain_scenario();
    const ArenaResult r = run_arena(scenario, 1, 2, BotConfig{}, BotConfig{});
    EXPECT_TRUE(r.clean());
    const std::string text = format_arena(r, scenario);
    EXPECT_NE(text.find("cover (M89), by target side: enemy ranged weapon selections into cover A "), std::string::npos)
        << text;
    const GameState rosters = wh40k::test::load_rosters_scenario();
    EXPECT_EQ(format_arena(run_arena(rosters, 0, 1, BotConfig{}, BotConfig{}), rosters).find("cover (M89)"),
              std::string::npos);
}
