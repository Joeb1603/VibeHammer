// M94 (D-095): engine-paced dice rolls in the client: GameSession's automatic rolls per side, the Undo rule, the
// pre-roll card, and format-2 saves (format 1 migrated on load).

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/engine/engine.hpp"
#include "dice_cards.hpp"
#include "saved_game.hpp"
#include "session.hpp"
#include "session_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"
#include "undo_groups.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::random_session_game;
using wh40k::test::rifle_volley;
using wh40k::test::shoot_step;

namespace {

const std::filesystem::path kLegacySave =
    std::filesystem::path(WH40K_REPO_ROOT) / "tests" / "data" / "legacy" / "m93_save_format1.json";

// The save M93 wrote for the same game (format 1, every roll made inside the step that led to it): this session's
// save (format 2 at M94, 3 since M95) with its roll steps and declined windows taken out. D-095's roll closure makes them
// the same: without its rolls the history is M93's, and the state is the same.
nlohmann::json format1_save(const GameSession& s) {
    nlohmann::json j = s.to_save_json();
    j["format"] = kReplayFormatRollsInline;
    nlohmann::json steps = nlohmann::json::array();
    for (const nlohmann::json& step : j.at("steps")) {
        // M95 (D-096): nor the declined windows (the session declines them; format 1 records neither).
        const auto& kind = step.at("action").at("kind");
        if (kind != "RollDice" && kind != "DeclineStratagems") steps.push_back(step);
    }
    j["steps"] = std::move(steps);
    return j;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

// Writes tests/data/legacy/m93_save_format1.json, the fixture for D-095's migration of format-1 saves: the format-1
// save of random_session_game(5, 11, 50), as M93 wrote it (M94 wrote it first with the M93 engine). Run on purpose only:
//   wh40k_client_tests --gtest_also_run_disabled_tests --gtest_filter=M94_Legacy.DISABLED_WriteFormat1Save
TEST(M94_Legacy, DISABLED_WriteFormat1Save) {
    std::filesystem::create_directories(kLegacySave.parent_path());
    std::ofstream out(kLegacySave, std::ios::binary);
    out << format1_save(random_session_game(5, 11, 50)).dump(2) << '\n';
}

TEST(M94_Legacy, TheSaveFixtureIsTheM93SaveOfTheSameGame) {
    // The fixture M93's engine wrote is, byte for byte, this game's save with its roll steps taken out: so it is a
    // format-1 save of the game M94_Saves loads, and the generator above rewrites the same file.
    const std::string fixture = read_text(kLegacySave);
    ASSERT_FALSE(fixture.empty());
    EXPECT_EQ(format1_save(random_session_game(5, 11, 50)).dump(2) + "\n", fixture);
}

// ==== GameSession: automatic rolls per side ====

TEST(M94_Session, TheSessionRollsForItsAutoSidesAndWaitsForTheOthers) {
    // Side A's rolls are made for it; Side B's save rolls (05.03: the defender's) wait for Side B.
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        GameSession s(shoot_step(seed), {true, false});
        const std::size_t before = s.history().size();
        ASSERT_TRUE(s.submit(rifle_volley(s.state())).ok());
        if (!roll_pending(s.state())) continue;  // nothing wounded: no save to wait for
        SCOPED_TRACE("seed " + std::to_string(seed));
        EXPECT_TRUE(std::holds_alternative<action::Shoot>(s.history()[before].action));
        for (std::size_t i = before + 1; i < s.history().size(); ++i) {
            // Side A's, made at once (M95, D-096: and its windows, declined at once).
            const GameAction& a = s.history()[i].action;
            EXPECT_TRUE(std::holds_alternative<action::RollDice>(a) || std::holds_alternative<action::DeclineStratagems>(a));
            EXPECT_EQ(s.history()[i].side, Side::A);
        }
        EXPECT_EQ(s.pending()->side, Side::B);
        EXPECT_EQ(std::get<decision::RollDice>(s.pending()->kind).roll, RollKind::Save);
        EXPECT_EQ(s.log().back().label.rfind("Step", 0), 0u);
        // Turning Side B's on makes the waiting roll at once.
        s.set_auto_roll(Side::B, true);
        const std::vector<GameAction> made = s.roll_automatically();
        ASSERT_FALSE(made.empty());
        EXPECT_EQ(made.front(), (GameAction{action::RollDice{RollKind::Save, s.state().units[1].id}}));
        EXPECT_FALSE(roll_pending(s.state()));
        return;
    }
    FAIL() << "no seed wounded the Termagants";
}

TEST(M94_Session, ARollStepIsLoggedWithItsKind) {
    GameSession s(shoot_step(1), {false, false}, {false, false});  // M95 (D-096): no automatic decline after the roll
    ASSERT_TRUE(s.submit(rifle_volley(s.state())).ok());
    ASSERT_TRUE(roll_pending(s.state()));
    const auto& d = std::get<decision::RollDice>(s.pending()->kind);
    ASSERT_TRUE(s.submit(action::RollDice{d.roll, d.unit}).ok());
    EXPECT_NE(s.log().back().label.find("RollDice (Hit)"), std::string::npos) << s.log().back().label;
    EXPECT_EQ(wh40k::test::events_of<event::HitRollsMade>(s.log().back().events).size(), 1u);
}

// ==== The pre-roll card ====

TEST(M94_RollPrompt, EachPreRollCardIsTheCardOfItsRollFaceDown) {
    // Random play with every roll made by hand: before each roll its card (who rolls, what for, what it needs, the dice
    // face down); after it, the card of its events says the same with the faces showing (D-033).
    std::map<RollKind, int> seen;
    const std::vector<GameState> scenarios{wh40k::test::load_v1_scenario_closer(), wh40k::test::load_rosters_scenario()};
    for (std::size_t n = 0; n < scenarios.size(); ++n) {
        for (std::uint64_t seed = 1; seed <= 3; ++seed) {
            GameSession s = GameSession::new_game(scenarios[n], seed, {false, false});
            wh40k::test::TestRng rng(seed + 100 * n);
            for (int i = 0; i < 400 && s.pending(); ++i) {
                if (!roll_pending(s.state())) {
                    ASSERT_TRUE(s.submit(wh40k::test::random_action(s.state(), rng)).ok());
                    continue;
                }
                const std::optional<DiceCard> prompt = roll_prompt(s.state(), s.log());
                ASSERT_TRUE(prompt.has_value());
                const auto& d = std::get<decision::RollDice>(s.pending()->kind);
                SCOPED_TRACE(std::string(roll_kind_name(d.roll)) + " at step " + std::to_string(s.history().size()));
                ++seen[d.roll];
                EXPECT_EQ(prompt->roller, s.pending()->side);
                EXPECT_FALSE(prompt->dice.empty());
                for (const DieFace& f : prompt->dice) EXPECT_EQ(f.value, 0);  // face down
                const std::size_t from = s.log().size();
                ASSERT_TRUE(s.submit(action::RollDice{d.roll, d.unit}).ok());
                const std::vector<DiceCard> cards = dice_cards(s.state(), s.log(), from);
                ASSERT_FALSE(cards.empty());
                EXPECT_EQ(cards.front().title, prompt->title);
                EXPECT_EQ(cards.front().roller, prompt->roller);
                EXPECT_EQ(cards.front().purpose, prompt->purpose);
                EXPECT_EQ(cards.front().need, prompt->need);
                // D-096 (M95): a damage roll is one attack's D, so every card has the dice its prompt showed.
                EXPECT_EQ(cards.front().dice.size(), prompt->dice.size());
            }
        }
    }
    for (const RollKind k : {RollKind::Advance, RollKind::Charge, RollKind::Hit, RollKind::Wound, RollKind::Save}) {
        EXPECT_GT(seen[k], 0) << roll_kind_name(k);
    }
}

TEST(M94_RollPrompt, NoCardWithoutARollWaiting) {
    const GameSession s(shoot_step(1), {false, false});
    EXPECT_FALSE(roll_prompt(s.state(), s.log()).has_value());
}

// ==== Undo (D-039): a roll is never taken back on its own ====

TEST(M94_Undo, UndoTakesARollBackWithTheInputThatLedToIt) {
    GameSession s(shoot_step(1), {false, false}, {false, false});  // M95 (D-096): the roll's window waits
    const GameState before = s.state();
    const std::size_t start = s.history().size();
    UndoGroups groups;
    groups.begin(start);
    ASSERT_TRUE(s.submit(rifle_volley(s.state())).ok());
    groups.end(s.history().size());
    ASSERT_TRUE(roll_pending(s.state()));
    // The Roll button: a step, but no Undo group of its own (the client's submit_roll).
    const auto& d = std::get<decision::RollDice>(s.pending()->kind);
    ASSERT_TRUE(s.submit(action::RollDice{d.roll, d.unit}).ok());
    EXPECT_EQ(s.history().size(), start + 2);
    ASSERT_TRUE(groups.undo(s));
    EXPECT_EQ(s.history().size(), start);
    EXPECT_EQ(s.state(), before);
    EXPECT_FALSE(s.auto_roll(Side::A));  // kept by the rebuild
    EXPECT_FALSE(s.auto_roll(Side::B));
}

TEST(M94_Undo, TruncateNeverStopsBetweenAnActionAndItsRolls) {
    GameSession s(shoot_step(1), {false, false});
    const GameState before = s.state();
    const std::size_t start = s.history().size();
    ASSERT_TRUE(s.submit(rifle_volley(s.state())).ok());
    const auto& d = std::get<decision::RollDice>(s.pending()->kind);
    ASSERT_TRUE(s.submit(action::RollDice{d.roll, d.unit}).ok());
    // Keeping the volley but not its roll would leave the roll to make again: the volley goes too.
    s.truncate(start + 1);
    EXPECT_EQ(s.history().size(), start);
    EXPECT_EQ(s.state(), before);
    // A plain undo of the volley alone, with the session making Side A's rolls itself.
    GameSession a(shoot_step(1), {true, true});
    ASSERT_TRUE(a.submit(rifle_volley(a.state())).ok());
    EXPECT_GT(a.history().size(), start + 1);  // the volley and its rolls
    ASSERT_TRUE(a.undo());
    EXPECT_EQ(a.history().size(), start);
    EXPECT_EQ(a.state(), before);
}

// ==== Saves (D-025): format 2; a format-1 save loads with its rolls added as steps ====

TEST(M94_Saves, AnM93SaveLoadsWithItsRollsAsSteps) {
    const GameSession loaded = GameSession::load(kLegacySave);
    const GameSession again = random_session_game(5, 11, 50);  // the same choices, played now
    EXPECT_EQ(loaded.state(), again.state());
    EXPECT_EQ(loaded.history(), again.history());
    EXPECT_GT(loaded.history().size(), 50u);  // its 50 steps and their rolls
    EXPECT_EQ(loaded.to_save_json().at("format"), 4);  // D-096 (M95): saved again as format 3; D-097 (M96): 4
    EXPECT_EQ(GameSession::from_save_json(loaded.to_save_json()).state(), loaded.state());
}

TEST(M94_Saves, AGameSavedWhileARollWaitsLoadsStillWaitingForIt) {
    GameSession s(shoot_step(1), {false, false});
    ASSERT_TRUE(s.submit(rifle_volley(s.state())).ok());
    ASSERT_TRUE(roll_pending(s.state()));
    const GameSession loaded = GameSession::from_save_json(s.to_save_json());
    EXPECT_EQ(loaded.state(), s.state());
    EXPECT_TRUE(roll_pending(loaded.state()));  // no roll is made by loading
    EXPECT_EQ(loaded.history(), s.history());
}

// Writes two hotseat saves for screenshots of the pre-roll card (xvfb-run, --load): one waiting for Side A's hit
// rolls, one for Side B's save rolls. Run on purpose only:
//   WH40K_SCREEN_DIR=<dir> wh40k_client_tests --gtest_also_run_disabled_tests --gtest_filter=M94_Screens.*
TEST(M94_Screens, DISABLED_WriteSavesWaitingForARoll) {
    const char* dir = std::getenv("WH40K_SCREEN_DIR");
    if (dir == nullptr) GTEST_SKIP() << "set WH40K_SCREEN_DIR";
    GameSession hit(shoot_step(1), {false, false});
    ASSERT_TRUE(hit.submit(rifle_volley(hit.state())).ok());
    ASSERT_TRUE(roll_pending(hit.state()));
    save_game_file(std::filesystem::path(dir) / "m94_hit_prompt.json", hit, SavePlayers{});
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        GameSession save(shoot_step(seed), {true, false});
        ASSERT_TRUE(save.submit(rifle_volley(save.state())).ok());
        if (!roll_pending(save.state())) continue;
        save_game_file(std::filesystem::path(dir) / "m94_save_prompt.json", save, SavePlayers{});
        return;
    }
    FAIL() << "no seed wounded the Termagants";
}
