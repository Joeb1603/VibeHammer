// M47: the bots and the default casualty order (D-058): level Easy's casualties and its 03.03 pick, the fallback's,
// Hard's fourth casualty candidate, Easy's coherency in the arena, and the replay
// tests/replays/m47_default_casualties.json (Easy vs Easy, 20 Termagants).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "casualty_order.hpp"
#include "casualty_plan.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/queries.hpp"
#include "fallback.hpp"
#include "hard/hard_bot.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::in;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

BotConfig easy() { return BotConfig{BotLevel::Easy}; }

// Side A's Intercessors fire `shooters` Bolt Rifles (Focused Fire) at the Termagants, `damaging` of the attacks
// inflicting damage (each destroys a Termagant): side B's casualty decision (as M41's after_marine_volley).
GameState after_marine_volley(std::size_t shooters, int damaging) {
    GameState gs = wh40k::test::shoot_step_state(Side::A);
    std::vector<ShotDeclaration> shots;
    for (std::size_t i = 0; i < shooters; ++i) {
        shots.push_back(ShotDeclaration{gs.units[kMarines].models[i].id, 0, 0, gs.units[kGants].id});
    }
    const int dice_count = static_cast<int>(shooters) * 2;  // [RAPID FIRE 1] within half range
    std::vector<int> rolls;
    for (int i = 0; i < dice_count; ++i) rolls.push_back(i < damaging ? 6 : 1);  // hits
    for (int i = 0; i < damaging; ++i) rolls.push_back(6);                        // wounds
    for (int i = 0; i < damaging; ++i) rolls.push_back(1);                        // saves fail
    ScriptedDice dice(rolls);
    (void)apply_action(gs, Side::A, action::Shoot{gs.units[kMarines].id, ShootingType::Normal, shots}, dice);
    EXPECT_TRUE(gs.pending && std::holds_alternative<decision::OrderCasualties>(gs.pending->kind));
    return gs;
}

// M28's End-of-Turn setup with one wanderer: Intercessor `wanderer` steps 5" east of the line in the Movement
// phase, so side A must remove a model in the End of Turn step (03.03).
GameState end_of_turn_out_of_coherency(std::size_t wanderer) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (gs.flow.stage != Stage::Charge) {
        (void)apply_action(gs, gs.pending->side, legal_actions(gs, gs.pending->side).front());
    }
    gs.units[kMarines].models[wanderer].position.x += in(5.0f) + in(1.26f) * static_cast<float>(4 - wanderer);
    (void)apply_action(gs, Side::A, action::EndChargeStep{});
    EXPECT_TRUE(gs.pending && std::holds_alternative<decision::RestoreCoherency>(gs.pending->kind));
    return gs;
}

// The replay: Easy vs Easy on the 20-Termagant scenario (D-058). Seed 121's game has casualty decisions on both
// sides and a Regaining Coherency removal (03.03), which the default order makes rare: 2 of the 200 Release arena
// seeds (DISABLED_PrintSeeds found none in seeds 1-40).
constexpr const char* kReplayScenario = "data/scenarios/bot_20_termagants.json";
constexpr std::uint64_t kReplaySeed = 121;

struct BotGame {
    Replay replay;
    GameRecord record;
};

BotGame play_default_casualties_game(std::uint64_t seed = kReplaySeed) {
    BotGame g;
    g.replay.description =
        "M47: a whole game played by the bot on both sides, level Easy vs Easy, from the 20-Termagant scenario with "
        "dice seed " + std::to_string(seed) +
        ": every casualty order is the default order, furthest from the action first, keeping coherency (D-058). "
        "Replaying it needs only the engine.";
    g.replay.initial.scenario = kReplayScenario;
    g.replay.initial.seed = seed;
    client::GameSession session(replay_initial_state(g.replay, wh40k::test::repo_root()));
    g.record = play_bot_session(session, easy(), easy());
    g.replay.steps = session.history();
    g.replay.expect = replay_expectation(session.state());
    return g;
}

struct DecisionCounts {
    int casualties = 0;          // OrderCasualties answered
    int casualties_default = 0;  // ... with CasualtyPlan's default order at that step
    int coherency = 0;           // RestoreCoherency answered
    int coherency_default = 0;   // ... with default_coherency_removal at that step
    std::array<int, 2> casualties_by_side{};
};

// Steps through the replay with core only and checks each casualty and coherency answer against the default.
DecisionCounts count_decisions(const Replay& replay) {
    DecisionCounts c;
    GameState gs = replay_initial_state(replay, wh40k::test::repo_root());
    start_game(gs);
    for (const ReplayStep& step : replay.steps) {
        if (const auto* o = std::get_if<action::OrderCasualties>(&step.action)) {
            ++c.casualties;
            ++c.casualties_by_side[side_index(step.side)];
            if (*o == client::CasualtyPlan(gs, o->target).action()) ++c.casualties_default;
        }
        if (const auto* r = std::get_if<action::RemoveModelForCoherency>(&step.action)) {
            ++c.coherency;
            if (client::default_coherency_removal(gs, r->unit) == r->model) ++c.coherency_default;
        }
        (void)apply_action(gs, step.side, step.action);
    }
    return c;
}

}  // namespace

// ---- Easy ----

TEST(M47_Easy, CasualtiesUseTheDefaultOrder) {
    // Two Termagants die to a volley: Easy (the Termagants) answers with the default order, the rear rank first.
    const GameState gs = after_marine_volley(2, 2);
    const UnitId gants = gs.units[kGants].id;
    Bot bot(easy());
    const BotChoice c = bot.decide(gs, Side::B);
    ASSERT_TRUE(std::holds_alternative<action::OrderCasualties>(c.action)) << c.reason;
    EXPECT_EQ(std::get<action::OrderCasualties>(c.action), client::CasualtyPlan(gs, gants).action());
    EXPECT_EQ(std::get<action::OrderCasualties>(c.action).priority, client::default_casualty_order(gs, gants));
    GameState after = gs;
    (void)apply_action(after, Side::B, c.action);
    EXPECT_EQ(after.units[kGants].models.size(), 8u);
    EXPECT_TRUE(unit_coherency(after.units[kGants]).ok());
    // The two lost are rear-rank models (further from the Intercessors than any front-rank model).
    float front_y = 1e9f;
    for (const Model& m : gs.units[kGants].models) front_y = std::min(front_y, m.position.y);
    int front_left = 0;
    for (const Model& m : after.units[kGants].models) front_left += m.position.y <= front_y + EPS_MM ? 1 : 0;
    EXPECT_EQ(front_left, 5);
    EXPECT_EQ(bot.fallbacks(), 0u);
}

TEST(M47_Easy, RemovesTheModelThatRestoresCoherency) {
    // An Intercessor wandered off (03.03, End of Turn): before M47 Easy removed the first model offered (the
    // Sergeant) and then still had to remove the wanderer; now it removes the wanderer, and that is enough.
    GameState gs = end_of_turn_out_of_coherency(3);
    const UnitId marines = gs.units[kMarines].id;
    const ModelId wanderer = gs.units[kMarines].models[3].id;
    const std::vector<GameAction> legal = legal_actions(gs, Side::A);
    ASSERT_FALSE(legal.empty());
    EXPECT_EQ(legal.front(), (GameAction{action::RemoveModelForCoherency{marines, gs.units[kMarines].models[0].id}}));
    Bot bot(easy());
    const BotChoice c = bot.decide(gs, Side::A);
    EXPECT_EQ(c.action, (GameAction{action::RemoveModelForCoherency{marines, wanderer}})) << c.reason;
    EXPECT_EQ(client::default_coherency_removal(gs, marines), wanderer);
    (void)apply_action(gs, Side::A, c.action);
    EXPECT_TRUE(unit_coherency(gs.units[kMarines]).ok());
    EXPECT_EQ(gs.units[kMarines].models.size(), 4u);
    ASSERT_TRUE(gs.pending.has_value());
    EXPECT_FALSE(std::holds_alternative<decision::RestoreCoherency>(gs.pending->kind));
    EXPECT_EQ(bot.fallbacks(), 0u);
}

// ---- the fallback ----

TEST(M47_Fallback, UsesTheDefaultOrderAndTheDefaultCoherencyPick) {
    const GameState casualties = after_marine_volley(1, 1);
    const FallbackResult f = fallback_action(casualties, Side::B);
    ASSERT_TRUE(f.action.has_value());
    EXPECT_EQ(*f.action, GameAction{client::CasualtyPlan(casualties, casualties.units[kGants].id).action()});

    const GameState coherency = end_of_turn_out_of_coherency(3);
    const FallbackResult g = fallback_action(coherency, Side::A);
    ASSERT_TRUE(g.action.has_value());
    const UnitId marines = coherency.units[kMarines].id;
    EXPECT_EQ(*g.action, (GameAction{action::RemoveModelForCoherency{
                             marines, *client::default_coherency_removal(coherency, marines)}}));
    EXPECT_EQ(std::get<action::RemoveModelForCoherency>(*g.action).model, coherency.units[kMarines].models[3].id);
}

// ---- Hard ----

TEST(M47_Hard, TheDefaultOrderIsAFourthCasualtyCandidate) {
    // Three Termagants die to a volley; Hard (as the Termagants) weighs Normal's order, the front models first,
    // the rear models first and the default order, each previewed and valued.
    const GameState gs = after_marine_volley(2, 3);
    const UnitId gants = gs.units[kGants].id;
    hard::HardContext context;
    analysis::OddsCache cache;
    std::vector<hard::HardCasualtyCandidate> cands;
    const std::optional<PolicyResult> r = hard::hard_casualties(context, cache, gs, Side::B, &cands);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(cands.size(), 4u);
    EXPECT_EQ(cands[0].text, "Normal's order");
    EXPECT_EQ(cands[1].text, "the front models first");
    EXPECT_EQ(cands[2].text, "the rear models first");
    EXPECT_EQ(cands[3].text, "the default order, furthest from the enemy first");
    EXPECT_EQ(cands[3].priority, client::default_casualty_order(gs, gants));
    for (const hard::HardCasualtyCandidate& c : cands) EXPECT_TRUE(c.value.has_value()) << c.text;
    // The answer is one of the candidates, and the same as the Hard bot's.
    const auto* chosen = std::get_if<action::OrderCasualties>(&r->action);
    ASSERT_NE(chosen, nullptr);
    EXPECT_TRUE(std::any_of(cands.begin(), cands.end(),
                            [&](const hard::HardCasualtyCandidate& c) { return c.priority == chosen->priority; }));
    const BotChoice b = Bot(BotConfig{BotLevel::Hard}).decide(gs, Side::B);
    EXPECT_EQ(b.action, r->action) << b.reason;
    // A short reason that keeps its odds: "Termagants: casualties, the default order, ..." has no nested
    // parentheses before Hard's block.
    EXPECT_EQ(cands[3].text.find('('), std::string::npos);
}

// ---- the arena (Debug, a few seeds; the Release numbers are in PLAN.md, M47) ----

TEST(M47_Arena, EasyTermagantsKeepCoherency) {
    // 20 Termagants, Normal and Easy against Easy: before M47 Easy's Termagants lost 128 and 37 models to
    // coherency over 200 Release games (D-057); with the default order 5 and 2 (PLAN.md, M47). Seeds 1-8 here:
    // before M47 6 (Normal vs Easy) and 1 (Easy vs Easy), now 0 and 0; the bound leaves a margin of 1.
    const GameState scenario = load_scenario(wh40k::test::repo_root() / kReplayScenario, wh40k::test::repo_root());
    int total = 0;
    for (const BotLevel a : {BotLevel::Normal, BotLevel::Easy}) {
        const ArenaResult r = run_arena(scenario, 8, 1, BotConfig{a}, easy());
        EXPECT_TRUE(r.clean()) << format_arena(r, scenario);
        EXPECT_EQ(r.fallbacks, 0u) << format_arena(r, scenario);
        EXPECT_LE(r.coherency_removed[1], 1) << format_arena(r, scenario);
        total += r.coherency_removed[1];
    }
    EXPECT_LE(total, 1);
}

// ---- the replay ----

TEST(M47_Replay, TheDefaultCasualtiesGameIsDeterministicAndReplaysWithCoreOnly) {
    const BotGame first = play_default_casualties_game();
    const BotGame second = play_default_casualties_game();
    EXPECT_EQ(Json(first.replay).dump(2), Json(second.replay).dump(2));  // byte-identical when saved
    EXPECT_TRUE(first.record.finished);
    EXPECT_EQ(first.record.rejected, 0u);
    EXPECT_EQ(first.record.reverted, 0u);
    EXPECT_EQ(first.record.fallbacks, 0u);

    const ReplayResult r = run_replay(Json::parse(Json(first.replay).dump()).get<Replay>(), wh40k::test::repo_root());
    ASSERT_TRUE(r.ok()) << "step " << *r.failed_step << ": " << r.reasons.front();
    EXPECT_EQ(replay_expectation(r.final_state), first.replay.expect);
    EXPECT_TRUE(r.final_state.result.has_value());

    // Every casualty decision (both sides are Easy, and nothing is auto-allocated for a bot) is the default order
    // at its step, and every Regaining Coherency removal the default pick.
    const DecisionCounts c = count_decisions(first.replay);
    EXPECT_GT(c.casualties_by_side[0], 0);
    EXPECT_GT(c.casualties_by_side[1], 0);
    EXPECT_EQ(c.casualties_default, c.casualties);
    EXPECT_EQ(c.coherency_default, c.coherency);
    EXPECT_GT(c.coherency, 0);
}

TEST(M47_Replay, TheSavedFileMatchesTheGame) {
    const Replay saved = load_replay(std::filesystem::path(WH40K_REPLAY_DIR) / "m47_default_casualties.json");
    const BotGame g = play_default_casualties_game();
    EXPECT_EQ(Json(saved).dump(2), Json(g.replay).dump(2));
}

// Writes tests/replays/m47_default_casualties.json. Run on purpose only:
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M47_Replay.DISABLED_RegenerateDefaultCasualtiesReplay
TEST(M47_Replay, DISABLED_RegenerateDefaultCasualtiesReplay) {
    const BotGame g = play_default_casualties_game();
    ASSERT_TRUE(g.record.finished);
    ASSERT_EQ(g.record.rejected, 0u);
    ASSERT_EQ(g.record.reverted, 0u);
    ASSERT_EQ(g.record.fallbacks, 0u);
    const ReplayResult r = run_replay(g.replay, wh40k::test::repo_root());
    ASSERT_TRUE(r.ok()) << "step " << *r.failed_step << ": " << r.reasons.front();
    if (::testing::Test::HasFailure()) return;  // never overwrite the committed replay with a broken game
    save_replay(g.replay, std::filesystem::path(WH40K_REPLAY_DIR) / "m47_default_casualties.json");
}

// Finds seeds for the replay (run on purpose only): casualty and coherency decisions per game.
TEST(M47_Replay, DISABLED_PrintSeeds) {
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {  // and 121, the replay's
        const BotGame g = play_default_casualties_game(seed);
        const DecisionCounts c = count_decisions(g.replay);
        std::printf("seed %llu: casualties A %d B %d (default %d of %d), coherency %d (default %d), round %d, steps "
                    "%zu, removed for coherency A %d B %d, finished %d\n",
                    static_cast<unsigned long long>(seed), c.casualties_by_side[0], c.casualties_by_side[1],
                    c.casualties_default, c.casualties, c.coherency, c.coherency_default, g.record.end_round,
                    g.record.steps, g.record.coherency_removed[0], g.record.coherency_removed[1],
                    g.record.finished ? 1 : 0);
    }
}
