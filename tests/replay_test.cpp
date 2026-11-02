// Replays (D-014): an initial state plus (side, action) steps reproduce the game exactly.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/engine/replay.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::decider;
using wh40k::test::repo_root;
using wh40k::test::select;
using wh40k::test::shoot_step_state;
using wh40k::test::TestRng;

namespace {

const std::filesystem::path kReplayDir{WH40K_REPLAY_DIR};

// Plays random actions through a Match until GameOver or `max_actions`.
void random_play(Match& match, TestRng& rng, int max_actions) {
    for (int i = 0; i < max_actions && match.state().pending; ++i) {
        const GameAction a = wh40k::test::random_action(match.state(), rng);
        const auto r = match.submit(decider(match.state()), a);
        ASSERT_TRUE(r.ok()) << r.reasons[0];
    }
}

void expect_replays_identically(const Match& match) {
    const Replay replay = Json::parse(Json(match.to_replay("test")).dump()).get<Replay>();  // via JSON
    const ReplayResult result = run_replay(replay, repo_root());
    ASSERT_TRUE(result.ok()) << "step " << *result.failed_step << ": " << result.reasons[0];
    EXPECT_EQ(Json(result.final_state).dump(), Json(match.state()).dump());
    EXPECT_EQ(result.events, match.events());
    EXPECT_EQ(replay_expectation(result.final_state), replay.expect);
}

}  // namespace

TEST(Replay, RandomGamesFromTheScenarioReplayIdentically) {
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        GameState initial = wh40k::test::load_v1_scenario();
        initial.dice.seed = seed;
        Match match(initial);
        TestRng rng(seed);
        random_play(match, rng, 200);
        expect_replays_identically(match);
    }
}

TEST(Replay, AttackFlowGamesReplayIdentically) {
    int steps = 0;
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        GameState gs = shoot_step_state(Side::A, seed);
        std::vector<WeaponSelection> sel;
        for (std::size_t i = 0; i < 5; ++i) {
            sel.push_back(select(gs, 0, i, 0, i % 2, gs.units[1].id));
        }
        begin_attacks(gs, gs.units[0].id, AttackContext{AttackType::Shooting, true}, sel);  // seeded dice
        Match match(gs);  // starts mid-attack, at the first decision
        TestRng rng(seed);
        random_play(match, rng, 200);
        steps += static_cast<int>(match.steps().size());
        expect_replays_identically(match);
    }
    EXPECT_GT(steps, 0);
}

TEST(Replay, InvalidStepIsReportedWithItsReasons) {
    Replay replay;
    replay.initial.scenario = "data/scenarios/v1_skirmish.json";
    replay.initial.seed = 3;
    replay.steps.push_back(ReplayStep{Side::B, action::EndShootStep{}});
    const ReplayResult result = run_replay(replay, repo_root());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(*result.failed_step, 0u);
    EXPECT_EQ(result.reasons.at(0).rfind("09.02: ", 0), 0u) << result.reasons[0];
}

TEST(Replay, JsonRoundTripsBothInitialForms) {
    Replay from_scenario;
    from_scenario.description = "scenario form";
    from_scenario.initial.scenario = "data/scenarios/v1_skirmish.json";
    from_scenario.initial.seed = 0xFFFFFFFFFFFFFFFFull;
    from_scenario.steps = {ReplayStep{Side::A, action::StartMove{UnitId{1}, MoveType::RemainStationary}},
                           ReplayStep{Side::B, action::OrderCasualties{UnitId{2}, {ModelId{7}, ModelId{6}}}}};
    from_scenario.expect = Json{{"stage", "GameOver"}};
    EXPECT_EQ(Json(from_scenario).get<Replay>(), from_scenario);

    Replay from_state;
    from_state.initial.state = wh40k::test::load_v1_scenario();
    EXPECT_EQ(Json(from_state).get<Replay>(), from_state);
}

// Every saved replay is a regression test: it must run clean and match its expect block.
TEST(Replay, SavedReplayFilesPass) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(kReplayDir)) {
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    ASSERT_FALSE(files.empty()) << "no replays in " << kReplayDir;
    for (const auto& file : files) {
        SCOPED_TRACE(file.filename().string());
        const Replay replay = load_replay(file);
        const ReplayResult first = run_replay(replay, repo_root());
        ASSERT_TRUE(first.ok()) << "step " << *first.failed_step << ": " << first.reasons[0];
        EXPECT_EQ(replay_expectation(first.final_state), replay.expect);
        const ReplayResult second = run_replay(replay, repo_root());
        EXPECT_EQ(first.final_state, second.final_state);  // deterministic
    }
}

// Rewrites tests/replays/. Run on purpose only:
//   wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=Replay.DISABLED_RegenerateReplays
TEST(Replay, DISABLED_RegenerateReplays) {
    // M5: a full five-round game where every decision passes.
    Replay replay;
    replay.description =
        "M5: full game from the V1 scenario where every decision passes (remain stationary, end shooting, "
        "end charges); ends in a draw after battle round 5 (D-007).";
    replay.initial.scenario = "data/scenarios/v1_skirmish.json";
    replay.initial.seed = 1;
    GameState gs = replay_initial_state(replay, repo_root());
    start_game(gs);
    while (gs.pending) {
        const Side side = decider(gs);
        const GameAction a = legal_actions(gs, side).front();
        replay.steps.push_back(ReplayStep{side, a});
        apply_action(gs, side, a);
    }
    replay.expect = replay_expectation(gs);
    save_replay(replay, kReplayDir / "m5_full_game_all_pass.json");

    // M7: movement. A Normal move, an Advance (seeded roll), a move reverted for coherency (03.01), then
    // every later decision passes.
    Replay moves;
    moves.description =
        "M7: Side A Normal-moves its Intercessors 3\" north; Side B advances its Termagants 2\" south; "
        "Side A then moves one Intercessor 3\" west, which breaks coherency so the move reverts (03.01, "
        "03.03), and remains stationary instead. Every later decision passes; the game is a draw.";
    moves.initial.scenario = "data/scenarios/v1_skirmish.json";
    moves.initial.seed = 7;
    GameState g = replay_initial_state(moves, repo_root());
    start_game(g);
    auto act = [&](const GameAction& a) {
        const Side side = decider(g);
        const auto v = validate_action(g, side, a);
        ASSERT_TRUE(v.ok()) << v.reasons[0];
        moves.steps.push_back(ReplayStep{side, a});
        apply_action(g, side, a);
    };
    auto shift_unit = [&](std::size_t unit, float dx_in, float dy_in) {
        for (const Model& m : g.units[unit].models) {
            const Position s = *move_start_position(g, m.id);
            act(action::ProposeModelMove{m.id, {Position{s.x + wh40k::test::in(dx_in), s.y + wh40k::test::in(dy_in), 0}}});
        }
    };
    act(action::StartMove{g.units[0].id, MoveType::Normal, std::nullopt});
    shift_unit(0, 0, 3);
    act(action::CommitUnitMove{});
    act(action::EndShootStep{});
    act(action::EndChargeStep{});
    act(action::StartMove{g.units[1].id, MoveType::Advance, std::nullopt});
    shift_unit(1, 0, -2);
    act(action::CommitUnitMove{});
    act(action::EndShootStep{});
    act(action::EndChargeStep{});
    act(action::StartMove{g.units[0].id, MoveType::Normal, std::nullopt});
    const Model& first = g.units[0].models[0];
    const Position s = *move_start_position(g, first.id);
    act(action::ProposeModelMove{first.id, {Position{s.x - wh40k::test::in(3), s.y, 0}}});
    act(action::CommitUnitMove{});  // reverts: model 1 is more than 2" from the others
    while (g.pending) {
        act(legal_actions(g, decider(g)).front());
    }
    moves.expect = replay_expectation(g);
    save_replay(moves, kReplayDir / "m7_movement.json");

    // M8: shooting. Normal shooting after a 3" move (Heavy and Rapid Fire apply), then the Termagants
    // shoot back, both resolved with seeded dice; the defender orders casualties (D-013).
    Replay shots;
    shots.description =
        "M8: Side A Normal-moves its Intercessors 3\" north (no model more than 3\", so [HEAVY] applies) and "
        "fires every Bolt Rifle on Focused Fire at the Termagants, 9\" away (within half range: [RAPID FIRE 1]). "
        "Side B remains stationary and fires every Fleshborer back (Normal shooting, 18\"). Casualties are "
        "removed in unit order (D-013). Every later decision passes.";
    shots.initial.scenario = "data/scenarios/v1_skirmish.json";
    shots.initial.seed = 8;
    GameState s8 = replay_initial_state(shots, repo_root());
    start_game(s8);
    auto play = [&](const GameAction& a) {
        const Side side = decider(s8);
        const auto v = validate_action(s8, side, a);
        ASSERT_TRUE(v.ok()) << v.reasons[0];
        shots.steps.push_back(ReplayStep{side, a});
        apply_action(s8, side, a);
    };
    // The first legal action, with a casualty-order template filled in unit order.
    auto first_legal = [&]() -> GameAction {
        GameAction a = legal_actions(s8, decider(s8)).front();
        if (auto* oc = std::get_if<action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
            oc->priority = wh40k::test::model_ids(*s8.find_unit(oc->target));
        }
        return a;
    };
    auto volley = [&](std::size_t unit, std::size_t weapon, std::size_t profile, std::size_t target) {
        action::Shoot a{s8.units[unit].id, ShootingType::Normal, {}};
        for (const Model& m : s8.units[unit].models) {
            a.shots.push_back(ShotDeclaration{m.id, weapon, profile, s8.units[target].id});
        }
        return a;
    };
    auto resolve_attacks = [&]() {
        while (s8.flow.attack) play(first_legal());
    };
    play(action::StartMove{s8.units[0].id, MoveType::Normal, std::nullopt});
    for (const Model& m : s8.units[0].models) {
        const Position p = *move_start_position(s8, m.id);
        play(action::ProposeModelMove{m.id, {Position{p.x, p.y + wh40k::test::in(3), 0}}});
    }
    play(action::CommitUnitMove{});
    play(volley(0, 0, 0, 1));  // Bolt Rifle, Focused Fire
    resolve_attacks();
    ASSERT_FALSE(s8.units[1].models.empty()) << "pick a seed where the Termagants survive";
    play(action::EndShootStep{});
    play(action::EndChargeStep{});
    play(action::StartMove{s8.units[1].id, MoveType::RemainStationary, std::nullopt});
    play(volley(1, 0, 0, 0));  // Fleshborer
    resolve_attacks();
    while (s8.pending) play(first_legal());
    shots.expect = replay_expectation(s8);
    save_replay(shots, kReplayDir / "m8_shooting.json");
}
