// M95 (D-096): reaction windows in the client: the session's saves (format 3; format 2 converted on load).

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <variant>

#include "core/engine/engine.hpp"
#include "session.hpp"
#include "session_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::random_session_game;
using wh40k::test::rifle_volley;
using wh40k::test::shoot_step;

namespace {

const std::filesystem::path kLegacyDir = std::filesystem::path(WH40K_REPO_ROOT) / "tests" / "data" / "legacy";

std::size_t declines_in(const std::vector<ReplayStep>& steps) {
    return static_cast<std::size_t>(std::count_if(steps.begin(), steps.end(), [](const ReplayStep& s) {
        return std::holds_alternative<action::DeclineStratagems>(s.action);
    }));
}

}  // namespace

// ==== Saves (D-025): format 3; a format-2 save loads with its windows declined as steps ====

TEST(M95_Saves, AnM94SaveLoadsWithItsWindowsDeclined) {
    // tests/data/legacy/m94_save_format2.json is random_session_game(5, 11, 50) as the M94 engine saved it (format 2).
    const GameSession loaded = GameSession::load(kLegacyDir / "m94_save_format2.json");
    const GameSession again = random_session_game(5, 11, 50);  // the same choices, played now
    EXPECT_EQ(loaded.state(), again.state());
    EXPECT_EQ(loaded.history(), again.history());
    EXPECT_GT(declines_in(loaded.history()), 0u);
    EXPECT_EQ(loaded.to_save_json(), again.to_save_json());
    EXPECT_EQ(loaded.to_save_json().at("format"), kReplayFormatWindowsRecorded);
}

TEST(M95_Saves, AnM94SaveWaitingForARollLoadsStillWaitingForIt) {
    // tests/data/legacy/m94_save_waiting_for_a_save_roll_format2.json is M94_Screens' save: Side A's volley, its hit and
    // wound rolls made by the session, Side B's save rolls waiting.
    const GameSession loaded = GameSession::load(kLegacyDir / "m94_save_waiting_for_a_save_roll_format2.json");
    ASSERT_TRUE(roll_pending(loaded.state()));
    EXPECT_EQ(loaded.state().pending->side, Side::B);
    EXPECT_EQ(std::get<decision::RollDice>(loaded.state().pending->kind).roll, RollKind::Save);
    // The same game played now: the session declines Side A's windows after its hit and wound rolls (D-096), and the
    // save rolls still wait.
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        GameSession now(shoot_step(seed), {true, false});
        ASSERT_TRUE(now.submit(rifle_volley(now.state())).ok());
        if (!roll_pending(now.state())) continue;
        EXPECT_EQ(loaded.state(), now.state());
        EXPECT_EQ(loaded.history(), now.history());
        EXPECT_EQ(declines_in(loaded.history()), 2u);
        return;
    }
    FAIL() << "no seed wounded the Termagants";
}

TEST(M95_Saves, AGameSavedWhileAWindowWaitsLoadsStillWaitingForIt) {
    GameSession s(shoot_step(1), {true, true}, {false, false});
    ASSERT_TRUE(s.submit(rifle_volley(s.state())).ok());
    ASSERT_TRUE(window_pending(s.state()));  // Side A's Command Re-roll, just after its hit rolls (15.02)
    EXPECT_EQ(s.state().pending->side, Side::A);
    const GameSession loaded = GameSession::from_save_json(s.to_save_json());
    EXPECT_EQ(loaded.state(), s.state());
    EXPECT_TRUE(window_pending(loaded.state()));  // loading answers nothing
    EXPECT_EQ(loaded.history(), s.history());
}
