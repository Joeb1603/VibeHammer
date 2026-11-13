// M88 (D-087, D-088, D-089): the bot plays legally around terrain (it does not value it yet: M89). Its move candidates
// never end on a Light or Dense feature, a MONSTER's lines never cross a Dense wall over 2", whole games on the terrain
// scenario are clean, the arena's --terrain battles carry the terrain block, and its summary has the terrain line.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "arena.hpp"
#include "auto_actions.hpp"
#include "bot.hpp"
#include "core/engine/replay.hpp"
#include "core/rules/shooting.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "session.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/terrain_shape.hpp"
#include "core/rules/terrain.hpp"
#include "core/state/queries.hpp"
#include "normal/movement.hpp"
#include "simulator.hpp"
#include "state_hash.hpp"
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

TerrainShape rect_in(float x0, float x1, float y0, float y1) {
    return polygon_shape({at(x0, y0), at(x1, y0), at(x1, y1), at(x0, y1)});
}

// The monsters fixture at Side B's Move Units decision with a ruin north of the Tyrant (22, 30): a 5" wall at y
// 32.5-33 from x 18 to 26, and `unit`'s Normal move started.
GameState by_the_wall(std::size_t unit) {
    GameState gs = mf::at_movement();
    TerrainFeature wall;
    wall.name = "Ruin wall";
    wall.category = TerrainCategory::Dense;
    wall.height_mm = in(5);
    wall.shape = rect_in(18, 26, 32.5f, 33);
    TerrainArea ruin;
    ruin.name = "Ruin";
    ruin.shape = rect_in(17, 27, 32, 34);
    ruin.features.push_back(wall);
    gs.terrain.push_back(ruin);
    apply_action(gs, Side::B, action::StartMove{gs.units[unit].id, MoveType::Normal, std::nullopt});
    return gs;
}

}  // namespace

TEST(M88_Candidates, AMonstersLinesNeverCrossTheWallAndNoCandidateEndsOnIt) {
    const GameState origin = by_the_wall(mf::kTyrant);
    const TerrainShape& wall = origin.terrain[0].features[0].shape;
    const Model& tyrant = origin.units[mf::kTyrant].models.front();
    const std::vector<normal::MoveCandidate> cands = normal::move_candidates(origin);
    ASSERT_FALSE(cands.empty());
    int north = 0;
    for (const normal::MoveCandidate& c : cands) {
        ASSERT_EQ(c.ends.size(), 1u);
        const Position end = c.ends.front();
        EXPECT_FALSE(translation_crosses_shape(BasePose{tyrant.position, tyrant.base, tyrant.facing_deg}, end, wall))
            << c.label;
        EXPECT_FALSE(overlaps_shape(BasePose{end, tyrant.base, tyrant.facing_deg}, wall)) << c.label;
        if (end.y > tyrant.position.y + in(1)) ++north;
        // And the engine agrees.
        Simulator sim(origin, 88);
        for (const action::ProposeModelMove& p : c.proposals) ASSERT_TRUE(sim.step(Side::B, p).ok()) << c.label;
    }
    EXPECT_GT(north, 0);  // it still goes north, round the wall's ends
}

TEST(M88_Candidates, InfantryMayCrossTheWallButNoModelEndsOnIt) {
    GameState gs = mf::at_movement();
    // The Tyrant Guard at (6, 40), a 5" wall east of it.
    TerrainFeature wall;
    wall.name = "Ruin wall";
    wall.category = TerrainCategory::Dense;
    wall.height_mm = in(5);
    wall.shape = rect_in(9, 9.5f, 36, 44);
    TerrainArea ruin;
    ruin.name = "Ruin";
    ruin.shape = rect_in(8, 10, 35, 45);
    ruin.features.push_back(wall);
    gs.terrain.push_back(ruin);
    apply_action(gs, Side::B, action::StartMove{gs.units[mf::kGuard].id, MoveType::Normal, std::nullopt});
    const std::vector<Model>& models = gs.units[mf::kGuard].models;
    int crossed = 0;
    for (const normal::MoveCandidate& c : normal::move_candidates(gs)) {
        for (std::size_t i = 0; i < c.ends.size(); ++i) {
            EXPECT_FALSE(overlaps_shape(BasePose{c.ends[i], models[i].base, 0.0f}, gs.terrain[0].features[0].shape))
                << c.label;
            if (c.ends[i].x > in(9.5f)) ++crossed;
        }
    }
    EXPECT_GT(crossed, 0);  // INFANTRY moves through dense walls (13.06)
}

TEST(M88_Game, ABotGameOnTheTerrainScenarioIsClean) {
    const GameRecord g = play_bot_game(terrain_scenario(), 3, BotConfig{}, BotConfig{});
    EXPECT_TRUE(g.finished);
    EXPECT_EQ(g.rejected, 0u);
    EXPECT_EQ(g.reverted, 0u);
    EXPECT_EQ(g.fallbacks, 0u);
    EXPECT_GT(g.teamwork.ranged_selections[0] + g.teamwork.ranged_selections[1], 0);
}

TEST(M88_Arena, TerrainBattlesCarryTheBlockAndTheSummaryHasTheTerrainLine) {
    ArmySource source = load_army_source(repo_root());
    EXPECT_FALSE(source.terrain.has_value());  // a bare battlefield unless given
    source.mission = load_mission_block(repo_root() / "data/scenarios/incursion_objectives_44x60.json");
    EXPECT_TRUE(random_armies_battle(source, 7, Side::A).terrain.empty());
    source.terrain = load_terrain_block(repo_root() / kTerrainScenario);
    const GameState battle = random_armies_battle(source, 7, Side::A);
    EXPECT_EQ(battle.terrain.size(), 9u);
    const GameState scenario = terrain_scenario();
    const ArenaResult r = run_arena(scenario, 1, 5, BotConfig{}, BotConfig{});
    EXPECT_TRUE(r.clean());
    const std::string text = format_arena(r, scenario);
    EXPECT_NE(text.find("terrain (M88): ranged weapon selections with the benefit of cover A "), std::string::npos)
        << text;
    // Without terrain, no terrain line.
    const GameState rosters = wh40k::test::load_rosters_scenario();
    EXPECT_EQ(format_arena(run_arena(rosters, 0, 1, BotConfig{}, BotConfig{}), rosters).find("terrain (M88)"),
              std::string::npos);
}

TEST(M88_StateHash, TerrainAndTheRecordHashOnlyWhenPresent) {
    GameState gs = wh40k::test::load_v1_scenario();
    const std::uint64_t bare = state_hash(gs);
    GameState with = gs;
    with.units[0].ranged_attacks.this_turn = true;
    EXPECT_NE(state_hash(with), bare);
    GameState terrain = terrain_scenario();
    GameState no_terrain = terrain;
    no_terrain.terrain.clear();
    EXPECT_NE(state_hash(terrain), state_hash(no_terrain));
    // A bare state hashes as it always did: nothing new is fed for an empty terrain list or an unset record.
    GameState copy = gs;
    copy.terrain.clear();
    copy.units[0].ranged_attacks = RangedAttackRecord{};
    EXPECT_EQ(state_hash(copy), bare);
}

// ==== The M88 replay: tests/replays/m88_terrain.json ====

namespace {

constexpr const char* kTerrainReplayFile = "m88_terrain.json";
constexpr std::uint64_t kTerrainReplaySeed = 80;  // chosen by M88_Replay.DISABLED_SearchTerrainSeed

std::filesystem::path terrain_replay_path() { return std::filesystem::path(WH40K_REPLAY_DIR) / kTerrainReplayFile; }

// The scripted move (the user's choice, M88): at Side B's Move Units decision, a round MONSTER of `side` that hasn't
// moved goes round a Dense wall it can't move through, in two straight lines: from its position to a point just off
// one of the wall's corners, then on to an end that the straight line from its position would reach only through the
// wall. The first such move found (units, features, corners and ends in a fixed order), validated by the engine, with
// the move ending legally; nullopt if there is none.
std::optional<std::vector<GameAction>> detour_move(const GameState& gs, Side side) {
    if (!gs.pending || gs.pending->side != side || !std::holds_alternative<decision::MoveUnits>(gs.pending->kind)) {
        return std::nullopt;
    }
    for (const Unit& u : gs.units) {
        if (u.side != side || u.models.size() != 1 || u.phase_flags.selected_to_move) continue;
        const Model& m = u.models.front();
        if (m.base.shape != BaseShape::Round) continue;
        std::vector<const TerrainShape*> walls;
        for (const TerrainArea& a : gs.terrain) {
            for (const TerrainFeature& f : a.features) {
                if (blocks_move(f, u)) walls.push_back(&f.shape);
            }
        }
        if (walls.empty()) continue;
        const action::StartMove start{u.id, MoveType::Normal, std::nullopt};
        if (!validate_action(gs, side, start).ok()) continue;
        GameState moving = gs;
        apply_action(moving, side, start);
        const std::optional<float> max = max_move_distance_mm(moving, m.id);
        if (!max) continue;
        const float r = base_radius(m.base);
        const BasePose from{m.position, m.base, 0.0f};
        for (const TerrainShape* wall : walls) {
            const Position mid = shape_inner_point(*wall);
            for (const Position& v : wall->vertices) {
                const float dx = v.x - mid.x;
                const float dy = v.y - mid.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (!(len > 0.0f)) continue;
                const float out = r + in(0.3f);
                const Position corner{v.x + dx / len * out, v.y + dy / len * out, 0.0f};
                const float first = centre_distance(m.position, corner);
                if (first > *max - in(1)) continue;
                for (int k = 0; k < 16; ++k) {
                    const float a = 2.0f * 3.14159265f * static_cast<float>(k) / 16.0f;
                    for (float step = in(1); first + step <= *max; step += in(1)) {
                        const Position end{corner.x + std::cos(a) * step, corner.y + std::sin(a) * step, 0.0f};
                        if (!translation_crosses_shape(from, end, *wall)) continue;  // not a detour
                        const action::ProposeModelMove propose{m.id, {corner, end}};
                        if (!validate_action(moving, side, propose).ok()) continue;
                        GameState after = moving;
                        apply_action(after, side, propose);
                        if (!move_end_problems(after).empty()) continue;
                        return std::vector<GameAction>{start, propose, action::CommitUnitMove{}};
                    }
                }
            }
        }
    }
    return std::nullopt;
}

struct TerrainReplay {
    Replay replay;
    bool detour = false;           // the scripted move was made
    int covered = 0;               // weapon selections with the benefit of cover
    int refused_hidden = 0;        // enemy units in range refused for 13.09 at a Shoot
    int hidden_at_shoot = 0;       // enemy units wholly hidden at a Shoot (in range or not)
    std::size_t rejected = 0;
};

// Bot play from battle round 1 on the terrain scenario with `seed`, through GameSession (the client's automatic skips
// included), with the scripted detour at Side B's first Move Units decision of battle round 2 or later that has one.
// Stops at the first decision of the battle round after the one with the detour (or after round 4 without one).
TerrainReplay play_terrain_replay(std::uint64_t seed) {
    TerrainReplay out;
    const auto describe = [&](int detour_round) {
        out.replay.description =
            "M88: the bot playing both sides on the terrain scenario (data/scenarios/incursion_terrain_44x60.json, "
            "D-087-D-089) with dice seed " + std::to_string(seed) + ", from battle round 1 to the end of battle round " +
            std::to_string(detour_round) + ". One move is scripted, in battle round " + std::to_string(detour_round) +
            ": a MONSTER goes round a dense wall it can't move through (13.06) in two straight lines. The game shows "
            "shots at targets with the benefit of cover (13.08) and targets refused because they are hidden (13.09). "
            "Checked by the engine replay only, so later bot changes leave it valid.";
    };
    describe(0);
    out.replay.initial.scenario = kTerrainScenario;
    out.replay.initial.seed = seed;
    wh40k::client::GameSession session(replay_initial_state(out.replay, repo_root()));
    // M89 (D-090): the M88 bot, which doesn't value terrain (cover 0 plays exactly as before M89).
    BotConfig m88;
    m88.move.cover = 0.0;
    std::array<Bot, 2> bots{Bot(m88), Bot(m88)};
    wh40k::client::AutoSettings autos;
    autos.skip_empty_steps = true;
    autos.auto_allocate = false;
    autos.bot_controlled = {true, true};
    std::optional<int> stop_round;
    std::size_t logged = 0;
    const auto count = [&] {
        for (; logged < session.log().size(); ++logged) {
            for (const GameEvent& e : session.log()[logged].events) {
                if (const auto* sh = std::get_if<event::ShootingStarted>(&e)) {
                    for (const WeaponSelection& sel : sh->selections) out.covered += sel.benefit_of_cover ? 1 : 0;
                }
            }
        }
    };
    for (int guard = 0; guard < 4000 && session.pending(); ++guard) {
        (void)wh40k::client::run_auto_actions(session, autos, [&](const GameAction& a) { return session.submit(a).ok(); });
        count();
        if (!session.pending()) break;
        const GameState& gs = session.state();
        if (gs.flow.battle_round >= (stop_round ? *stop_round : 5)) break;
        const Side side = session.pending()->side;
        if (!out.detour && gs.flow.battle_round >= 2 && side == Side::B) {
            if (const auto moves = detour_move(gs, side)) {
                for (const GameAction& a : *moves) {
                    if (!session.submit(a).ok()) ++out.rejected;
                }
                out.detour = true;
                describe(gs.flow.battle_round);
                stop_round = gs.flow.battle_round + 1;
                count();
                continue;
            }
        }
        const BotChoice choice = bots[side_index(side)].decide(session.state(), side);
        if (choice.stopped) break;
        if (const auto* shoot = std::get_if<action::Shoot>(&choice.action)) {
            // 13.09: the enemy units hidden from this unit now (a hidden unit and, within range, a refused target).
            const Unit* u = gs.find_unit(shoot->unit);
            for (const Unit& e : gs.units) {
                if (u == nullptr || e.side == u->side || e.models.empty()) continue;
                const bool all_hidden = std::all_of(e.models.begin(), e.models.end(),
                                                    [&](const Model& m) { return is_hidden(gs, e, m); });
                if (!all_hidden) continue;
                ++out.hidden_at_shoot;
                for (const Model& m : u->models) {
                    if (visibility_problem(gs, m, e).kind == VisibilityProblemKind::Hidden) {
                        ++out.refused_hidden;
                        break;
                    }
                }
            }
        }
        if (!session.submit(choice.action).ok()) {
            ++out.rejected;
            break;
        }
        count();
    }
    out.replay.steps = session.history();
    out.replay.expect = replay_expectation(session.state());
    return out;
}

}  // namespace

// Writes tests/replays/m88_terrain.json. Run on purpose only:
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M88_Replay.DISABLED_RegenerateTerrainReplay
TEST(M88_Replay, DISABLED_RegenerateTerrainReplay) {
    const TerrainReplay t = play_terrain_replay(kTerrainReplaySeed);
    ASSERT_EQ(t.rejected, 0u);
    ASSERT_TRUE(t.detour);
    ASSERT_GT(t.covered, 0);
    ASSERT_GT(t.hidden_at_shoot, 0);
    ASSERT_GT(t.refused_hidden, 0);
    const ReplayResult r = run_replay(t.replay, repo_root());
    ASSERT_TRUE(r.ok()) << "step " << *r.failed_step << ": " << r.reasons.front();
    if (::testing::Test::HasFailure()) return;  // never overwrite the committed replay with a broken game
    save_replay(t.replay, terrain_replay_path());
}

// Prints, per seed, what the game shows (run on purpose, in Release):
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M88_Replay.DISABLED_SearchTerrainSeed
TEST(M88_Replay, DISABLED_SearchTerrainSeed) {
    for (std::uint64_t seed = 1; seed <= 150; ++seed) {
        const TerrainReplay t = play_terrain_replay(seed);
        if (t.hidden_at_shoot == 0 || !t.detour) continue;
        std::printf("seed %llu: detour %d, steps %zu, covered %d, hidden at a shoot %d, refused for hidden %d, "
                    "rejected %zu\n",
                    static_cast<unsigned long long>(seed), t.detour ? 1 : 0, t.replay.steps.size(), t.covered,
                    t.hidden_at_shoot, t.refused_hidden, t.rejected);
    }
}
