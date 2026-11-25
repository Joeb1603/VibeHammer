// M94 (D-095): engine-paced dice rolls in the client: GameSession's automatic rolls per side, the Undo rule, the
// pre-roll card, and format-2 saves (format 1 migrated on load).

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "core/engine/engine.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;

namespace {

const std::filesystem::path kLegacySave =
    std::filesystem::path(WH40K_REPO_ROOT) / "tests" / "data" / "legacy" / "m93_save_format1.json";

// A short V1 game (the Termagants 1" closer) played by random legal actions through a session: moves, shots, charges
// and fights, every roll made inside the action that led to it. Seeds chosen so the game shoots and charges.
GameSession random_session_game(std::uint64_t seed, std::uint64_t rng_seed, int actions) {
    GameState scenario = wh40k::test::load_v1_scenario_closer();
    GameSession s = GameSession::new_game(std::move(scenario), seed);
    wh40k::test::TestRng rng(rng_seed);
    for (int i = 0; i < actions && s.pending(); ++i) {
        const GameAction a = wh40k::test::random_action(s.state(), rng);
        const ValidationResult r = s.submit(a);
        EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.reasons.front());
        if (!r.ok()) break;
    }
    return s;
}

}  // namespace

// Writes tests/data/legacy/m93_save_format1.json: a save made by the M93 engine (format 1, every roll inside its action),
// kept as the fixture for D-095's migration of format-1 saves. M94 ran it once, before any roll point moved. Run on
// purpose only (it must be run with an M93 engine to make a format-1 file):
//   wh40k_client_tests --gtest_also_run_disabled_tests --gtest_filter=M94_Legacy.DISABLED_WriteFormat1Save
TEST(M94_Legacy, DISABLED_WriteFormat1Save) {
    const GameSession s = random_session_game(5, 11, 50);
    std::filesystem::create_directories(kLegacySave.parent_path());
    s.save(kLegacySave);
}
