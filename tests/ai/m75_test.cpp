// M75 (D-082 part 1): the builder-battle replay. A custom battle made through ArmyBuilder edits (the client's army
// builder: a non-stock Tyranid list with a split loadout group and a Rupture cannon, against an edited Chaos Space
// Marine list, Tyranids first), auto-placed, played by the Easy bot on both sides through battle round 1. It lives in
// the ai tests because the Easy bot plays it; replaying it needs only core.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <variant>

#include "army_builder.hpp"
#include "auto_actions.hpp"
#include "bot.hpp"
#include "core/data/datasheet.hpp"
#include "core/engine/replay.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "session.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::client::ArmyBuilder;
using wh40k::test::repo_root;

namespace {

// The battle as a scenario file (written by the regenerate test), so the replay names it as M73's does.
constexpr const char* kBattleScenario = "tests/data/scenarios/m75_builder_battle_44x60.json";
constexpr const char* kReplayFile = "m75_builder_battle.json";
constexpr std::uint64_t kReplaySeed = 1;

std::filesystem::path saved_path() { return std::filesystem::path(WH40K_REPLAY_DIR) / kReplayFile; }

void edit(const std::optional<std::string>& refused) {
    if (refused) throw std::logic_error("the builder refused an edit: " + *refused);
}

// The battle as a player would build it in the army builder (each line one click or a few).
ArmyBuilder edited_builder() {
    ArmyBuilder b(repo_root());
    // Side A, Chaos Space Marines: renamed; the boltgun Legionaries (unit 5) trade a boltgun for a heavy melee weapon.
    edit(b.rename(Side::A, "Black Legion"));
    edit(b.set_count(Side::A, 4, 1, 8));
    edit(b.add_model_type(Side::A, 4, "Legionary w/ heavy melee weapon"));
    // Side B, Tyranids: the first Termagants split into 9 with Fleshborers and 1 with a spinefist ("+ another
    // loadout"); the Tyrannofex takes the Rupture cannon (+20 points).
    edit(b.rename(Side::B, "Hive Fleet Leviathan"));
    edit(b.split_group(Side::B, 0, 0));
    edit(b.set_option(Side::B, 0, 1, 0, 1));
    edit(b.set_option(Side::B, 5, 0, 0, 0));
    b.set_first_turn(Side::B);
    return b;
}

struct BuilderGame {
    Replay replay;
    std::size_t rejected = 0;
    std::size_t fallbacks = 0;
};

// Easy vs Easy through GameSession as the client plays a bot game (the client's automatic skips, nothing
// auto-allocated for a bot), stopped at the first decision of battle round 2.
BuilderGame play_builder_battle(std::uint64_t seed = kReplaySeed) {
    BuilderGame g;
    g.replay.initial.scenario = kBattleScenario;
    g.replay.initial.seed = seed;
    wh40k::client::GameSession session(replay_initial_state(g.replay, repo_root()));
    std::array<wh40k::ai::Bot, 2> bots{wh40k::ai::Bot(wh40k::ai::BotConfig{wh40k::ai::BotLevel::Easy}),
                                       wh40k::ai::Bot(wh40k::ai::BotConfig{wh40k::ai::BotLevel::Easy})};
    wh40k::client::AutoSettings autos;
    autos.skip_empty_steps = true;
    autos.auto_allocate = false;
    autos.bot_controlled = {true, true};
    const auto in_round_one = [&] { return session.state().flow.battle_round <= 1 && !session.state().result; };
    while (session.pending() && in_round_one() && session.history().size() < 2000) {
        (void)wh40k::client::run_auto_actions(session, autos);
        if (!session.pending() || !in_round_one()) break;
        const Side side = session.pending()->side;
        const wh40k::ai::BotChoice choice = bots[side_index(side)].decide(session.state(), side);
        if (choice.stopped || !session.submit(choice.action).ok()) {
            ++g.rejected;
            break;
        }
    }
    g.fallbacks = bots[0].fallbacks() + bots[1].fallbacks();
    g.replay.steps = session.history();
    g.replay.expect = replay_expectation(session.state());
    g.replay.description = (
        "M75: a custom battle built in the army builder (ArmyBuilder edits, D-082): side A, Chaos Space Marines "
        "renamed Black Legion, its boltgun Legionaries trading a boltgun for a heavy melee weapon (960 points); side "
        "B, Tyranids renamed Hive Fleet Leviathan, the first Termagants split into two loadouts (9 Fleshborers and 1 "
        "spinefist) and the Tyrannofex with a Rupture cannon (975 points); Tyranids first, auto-placed (D-079). The "
        "Easy bot plays both sides through battle round 1, with dice seed " +
        std::to_string(seed) + ". Its scenario file is the builder's battle (custom_battle_scenario). Replaying it "
        "needs only the engine.");
    return g;
}

}  // namespace

TEST(M75_Replay, TheBuilderBattleReplaysThroughBattleRoundOne) {
    const Replay saved = load_replay(saved_path());
    ASSERT_TRUE(saved.initial.scenario.has_value());
    EXPECT_EQ(*saved.initial.scenario, kBattleScenario);
    EXPECT_EQ(saved.initial.seed, kReplaySeed);
    EXPECT_FALSE(saved.initial.state.has_value());
    // Its initial state is the builder's battle for those edits, with the replay's seed.
    const ArmyBuilder b = edited_builder();
    GameState expected = b.battle_state();
    expected.dice = DiceState{kReplaySeed, 0};
    const GameState initial = replay_initial_state(saved, repo_root());
    EXPECT_EQ(initial, expected);
    EXPECT_EQ(initial.first_turn, Side::B);
    EXPECT_EQ(initial.sides[1].name, "Hive Fleet Leviathan");
    EXPECT_EQ(b.points(Side::A), 960);  // as the description says
    EXPECT_EQ(b.points(Side::B), 975);
    const ReplayResult first = run_replay(saved, repo_root());
    ASSERT_TRUE(first.ok()) << "step " << *first.failed_step << ": " << first.reasons.front();
    EXPECT_EQ(replay_expectation(first.final_state), saved.expect);
    EXPECT_EQ(first.final_state.flow.battle_round, 2);  // stopped at battle round 2's first decision
    const ReplayResult second = run_replay(saved, repo_root());
    EXPECT_EQ(first.final_state, second.final_state);  // deterministic
}

TEST(M75_Replay, TheScenarioIsTheBuildersBattle) {
    // The scenario file is the builder's battle_scenario for the edits (written by the regenerate test).
    EXPECT_EQ(read_json_file(repo_root() / kBattleScenario), edited_builder().battle_scenario());
}

TEST(M75_Replay, TheSavedFileMatchesTheGame) {
    const Replay saved = load_replay(saved_path());
    const BuilderGame g = play_builder_battle();
    EXPECT_EQ(g.rejected, 0u);
    EXPECT_EQ(g.fallbacks, 0u);
    ASSERT_EQ(saved.steps.size(), g.replay.steps.size());
    EXPECT_EQ(nlohmann::json(saved).dump(2), nlohmann::json(g.replay).dump(2));
}

// Writes tests/data/scenarios/m75_builder_battle_44x60.json and tests/replays/m75_builder_battle.json. Run on purpose
// only:
//   wh40k_ai_tests --gtest_also_run_disabled_tests --gtest_filter=M75_Replay.DISABLED_RegenerateBuilderBattleReplay
TEST(M75_Replay, DISABLED_RegenerateBuilderBattleReplay) {
    {
        std::ofstream out(repo_root() / kBattleScenario, std::ios::binary);
        out << edited_builder().battle_scenario().dump(2) << "\n";
    }
    const BuilderGame g = play_builder_battle();
    ASSERT_EQ(g.rejected, 0u);
    ASSERT_EQ(g.fallbacks, 0u);
    const ReplayResult r = run_replay(g.replay, repo_root());
    ASSERT_TRUE(r.ok()) << "step " << *r.failed_step << ": " << r.reasons.front();
    ASSERT_EQ(r.final_state.flow.battle_round, 2);
    // Both sides shot in battle round 1.
    for (const Side side : {Side::A, Side::B}) {
        EXPECT_TRUE(std::any_of(g.replay.steps.begin(), g.replay.steps.end(), [&](const ReplayStep& s) {
            return s.side == side && std::holds_alternative<action::Shoot>(s.action);
        })) << (side == Side::A ? "A" : "B");
    }
    if (::testing::Test::HasFailure()) return;  // never overwrite the committed replay with a broken game
    save_replay(g.replay, saved_path());
}

// Picks the seed: battle round 1 played clean, both sides shooting.
TEST(M75_Replay, DISABLED_SearchBuilderBattleSeed) {
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        const BuilderGame g = play_builder_battle(seed);
        bool shot[2] = {false, false};
        for (const ReplayStep& s : g.replay.steps) {
            if (std::holds_alternative<action::Shoot>(s.action)) shot[side_index(s.side)] = true;
        }
        std::printf("seed %llu: rejected %zu, fallbacks %zu, steps %zu, shot %d/%d\n",
                    static_cast<unsigned long long>(seed), g.rejected, g.fallbacks, g.replay.steps.size(),
                    shot[0] ? 1 : 0, shot[1] ? 1 : 0);
    }
}
