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
#include "core/state/queries.hpp"
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

// Writes a regenerated replay only if nothing has failed so far: an ASSERT inside a scripting lambda
// returns from the lambda, not the test, so a failed step must not let a different game overwrite a
// committed regression replay.
void save_if_clean(const Replay& replay, const std::filesystem::path& path) {
    if (::testing::Test::HasFailure()) {
        ADD_FAILURE() << "not writing " << path << ": an earlier step failed";
        return;
    }
    save_replay(replay, path);
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
    save_if_clean(replay, kReplayDir / "m5_full_game_all_pass.json");

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
    save_if_clean(moves, kReplayDir / "m7_movement.json");

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
    save_if_clean(shots, kReplayDir / "m8_shooting.json");

    // M9: a charge. The replay stops when the charge is resolved, before the Fight phase (M10), so it
    // stays valid when M10 adds fight decisions.
    Replay charge;
    charge.description =
        "M9: Side A Normal-moves its Intercessors 6\" north (6\" from the Termagants), skips shooting and "
        "declares a charge (seeded 2D6 of 6 or more). It selects the Termagants as the charge target and moves "
        "every model 2\" north: not engaged, so the move reverts (03.01, 11.04 After Moving) and the unit goes "
        "back to selecting targets with the same roll (D-018). It selects the Termagants again and moves every "
        "model 5\" north, ending 1\" away: the charge succeeds and the Intercessors have Fights First (24.13).";
    charge.initial.scenario = "data/scenarios/v1_skirmish.json";
    GameState c9;
    auto c_act = [&](const GameAction& a) {
        const Side side = decider(c9);
        const auto v = validate_action(c9, side, a);
        ASSERT_TRUE(v.ok()) << v.reasons[0];
        charge.steps.push_back(ReplayStep{side, a});
        apply_action(c9, side, a);
    };
    auto c_move_north = [&](float dy_in) {
        for (const Model& m : c9.units[0].models) {
            const Position p = *move_start_position(c9, m.id);
            c_act(action::ProposeModelMove{m.id, {Position{p.x, p.y + wh40k::test::in(dy_in), 0}}});
        }
    };
    // The first seed whose charge roll is 6 or more.
    for (std::uint64_t seed = 1;; ++seed) {
        charge.initial.seed = seed;
        charge.steps.clear();
        c9 = replay_initial_state(charge, repo_root());
        start_game(c9);
        c_act(action::StartMove{c9.units[0].id, MoveType::Normal, std::nullopt});
        c_move_north(6);
        c_act(action::CommitUnitMove{});
        c_act(action::EndShootStep{});
        c_act(action::DeclareCharge{c9.units[0].id});
        if (c9.flow.charge) break;  // a target was within the roll
        ASSERT_LT(seed, 100u);
    }
    c_act(action::StartChargeMove{c9.units[0].id, {c9.units[1].id}});
    c_move_north(2);
    c_act(action::CommitUnitMove{});  // reverts: not engaged with the charge target
    c_act(action::StartChargeMove{c9.units[0].id, {c9.units[1].id}});
    c_move_north(5);
    c_act(action::CommitUnitMove{});
    ASSERT_TRUE(c9.units[0].turn_flags.fights_first);
    charge.expect = replay_expectation(c9);
    save_if_clean(charge, kReplayDir / "m9_charge.json");

    // M10: the Fight phase after that charge, stopping when it ends (the next turn's Command phase is M11).
    Replay fight;
    fight.description =
        "M10: the M9 charge, then the Fight phase. No pile-in moves. The Intercessors fight first (Fights First, "
        "12.04) with Knives and Fists; casualties are removed in unit order (D-013), front rank first. The "
        "surviving Termagants fight back in Resolve Remaining Combats (an Overrun Fight with a 1.5\" pile-in if "
        "they are no longer engaged, 12.06). The Intercessors make an Ongoing Consolidation moving one model 0.5\" "
        "closer (12.08); the Termagants don't consolidate. Stops at Side B's first Move Units decision.";
    fight.initial = charge.initial;
    fight.steps = charge.steps;
    GameState f10 = replay_initial_state(fight, repo_root());
    start_game(f10);
    for (const ReplayStep& step : fight.steps) apply_action(f10, step.side, step.action);
    auto f_act = [&](const GameAction& a) {
        const Side side = decider(f10);
        const auto v = validate_action(f10, side, a);
        ASSERT_TRUE(v.ok()) << v.reasons[0];
        fight.steps.push_back(ReplayStep{side, a});
        apply_action(f10, side, a);
    };
    auto f_resolve = [&]() {
        while (f10.flow.attack) {
            GameAction a = legal_actions(f10, decider(f10)).front();
            if (auto* oc = std::get_if<action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
                oc->priority = wh40k::test::model_ids(*f10.find_unit(oc->target));
            }
            f_act(a);
        }
    };
    const UnitId marines = f10.units[0].id;
    const UnitId gants = f10.units[1].id;
    f_act(action::EndChargeStep{});
    f_act(action::EndPileIns{});
    f_act(action::EndPileIns{});
    f_act(action::SelectToFight{marines, FightType::Normal});
    f_act(wh40k::test::first_melee_options(f10, marines));
    f_resolve();
    ASSERT_FALSE(f10.units[1].models.empty()) << "the Termagants must survive to fight back";
    if (is_engaged(f10, f10.units[1])) {
        f_act(action::SelectToFight{gants, FightType::Normal});
    } else {
        // 12.06: the front rank is gone, so the survivors overrun: a 1.5" pile-in south, then they fight.
        f_act(action::SelectToFight{gants, FightType::Overrun});
        f_act(action::StartPileIn{gants, {marines}});
        for (const Model& m : f10.units[1].models) {
            const Position p = *move_start_position(f10, m.id);
            f_act(action::ProposeModelMove{m.id, {Position{p.x, p.y - wh40k::test::in(1.5f), 0}}});
        }
        f_act(action::CommitUnitMove{});
    }
    f_act(wh40k::test::first_melee_options(f10, gants));
    f_resolve();
    ASSERT_EQ(f10.flow.stage, Stage::Consolidate);
    f_act(action::StartConsolidation{marines, engaged_enemy_units(f10, f10.units[0])});
    const Model& west = f10.units[0].models.front();
    f_act(action::ProposeModelMove{west.id, {Position{west.position.x, west.position.y + wh40k::test::in(0.5f), 0}}});
    f_act(action::CommitUnitMove{});
    while (f10.pending && std::holds_alternative<decision::Consolidate>(f10.pending->kind)) {
        f_act(action::EndConsolidations{});
    }
    ASSERT_EQ(*f10.pending, (PendingDecision{Side::B, decision::MoveUnits{}}));
    fight.expect = replay_expectation(f10);
    save_if_clean(fight, kReplayDir / "m10_fight.json");

    // M11: the same game played to the end. Every later decision takes the first legal action (templates
    // filled: every model fights with its first melee option; casualties in unit order).
    Replay full;
    full.description =
        "M11: the M10 game continued to the end. Every later decision takes the first legal action: units "
        "remain stationary, skip shooting and charges, make no pile-in or consolidation moves, and fight "
        "whenever they are eligible (each model's first melee option). Both players gain 1 CP each Command "
        "phase (08.02). In Side B's Command phase the Termagants, below half-strength, make a battle-shock "
        "roll (08.03). The fight goes on in Side B's Fight phase until the Termagants are destroyed: Side A "
        "wins in battle round 1 (D-007).";
    full.initial = fight.initial;
    full.steps = fight.steps;
    GameState g11 = f10;
    while (g11.pending) {
        GameAction a = legal_actions(g11, decider(g11)).front();
        if (auto* oc = std::get_if<action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
            oc->priority = wh40k::test::model_ids(*g11.find_unit(oc->target));
        } else if (auto* f = std::get_if<action::Fight>(&a); f != nullptr && f->declarations.empty()) {
            a = wh40k::test::first_melee_options(g11, f->unit);
        }
        const Side side = decider(g11);
        const auto v = validate_action(g11, side, a);
        ASSERT_TRUE(v.ok()) << v.reasons[0];
        full.steps.push_back(ReplayStep{side, a});
        apply_action(g11, side, a);
    }
    ASSERT_TRUE(g11.result.has_value());
    full.expect = replay_expectation(g11);
    save_if_clean(full, kReplayDir / "m11_full_game.json");

    // M14: the close formation (D-008) and per-proposal overlap (D-027). Stops at Side B's first
    // Shooting decision, so later shooting changes don't touch it.
    Replay close;
    close.description =
        "M14: both units start in base contact (D-008). Side A moves its Intercessors 2\" east, the east-most "
        "model first so no proposal ends on a model yet to move (D-027), and the move is committed. Side A "
        "skips shooting and charges. Side B's two front-rank Termagants on the west swap places in three "
        "proposals: the first steps 1.5\" south, the second takes its place, then the first is proposed again "
        "into the second's place (a new proposal replaces its path). The move is committed. Stops at Side B's "
        "first Shooting decision.";
    close.initial.scenario = "data/scenarios/v1_skirmish.json";
    close.initial.seed = 14;
    GameState g14 = replay_initial_state(close, repo_root());
    start_game(g14);
    auto m_act = [&](const GameAction& a) {
        const Side side = decider(g14);
        const auto v = validate_action(g14, side, a);
        ASSERT_TRUE(v.ok()) << v.reasons[0];
        close.steps.push_back(ReplayStep{side, a});
        apply_action(g14, side, a);
    };
    auto start_of = [&](ModelId id) { return *move_start_position(g14, id); };
    m_act(action::StartMove{g14.units[0].id, MoveType::Normal, std::nullopt});
    for (auto it = g14.units[0].models.rbegin(); it != g14.units[0].models.rend(); ++it) {  // east-most first
        const Position start = start_of(it->id);
        m_act(action::ProposeModelMove{it->id, {Position{start.x + wh40k::test::in(2), start.y, 0}}});
    }
    m_act(action::CommitUnitMove{});
    m_act(action::EndShootStep{});
    m_act(action::EndChargeStep{});
    while (!(g14.pending && std::holds_alternative<decision::MoveUnits>(g14.pending->kind))) {
        m_act(legal_actions(g14, decider(g14)).front());  // Side A's empty Fight phase, Side B's Command phase
    }
    m_act(action::StartMove{g14.units[1].id, MoveType::Normal, std::nullopt});
    const ModelId gant_a = g14.units[1].models[0].id;
    const ModelId gant_b = g14.units[1].models[1].id;
    const Position p1 = start_of(gant_a);
    const Position p2 = start_of(gant_b);
    m_act(action::ProposeModelMove{gant_a, {Position{p1.x, p1.y - wh40k::test::in(1.5f), 0}}});
    m_act(action::ProposeModelMove{gant_b, {p1}});
    m_act(action::ProposeModelMove{gant_a, {p2}});
    m_act(action::CommitUnitMove{});
    ASSERT_EQ(*g14.pending, (PendingDecision{Side::B, decision::Shoot{}}));
    close.expect = replay_expectation(g14);
    save_if_clean(close, kReplayDir / "m14_close_formation.json");
}
