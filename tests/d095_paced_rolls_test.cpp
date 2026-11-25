// D-095 (M94): engine-paced dice rolls. The engine stops with a RollDice decision at every roll point; apply_action
// answers them inline (the M93 behaviour), apply_action_paced stops at them.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::repo_root;

namespace {

const std::filesystem::path kReplayDir{WH40K_REPLAY_DIR};

std::vector<std::filesystem::path> replay_files() {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(kReplayDir)) {
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace

// The closure check's reference: every saved replay's events and final state, written to $WH40K_EVENT_DUMP_DIR (one
// file per replay). M94 ran it with the M93 engine before any roll point moved, then with the M94 engine on the
// regenerated format-2 files; the two dumps must be identical. Run on purpose only:
//   WH40K_EVENT_DUMP_DIR=<dir> wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D095_Closure.DISABLED_DumpReplayEvents
TEST(D095_Closure, DISABLED_DumpReplayEvents) {
    const char* dir = std::getenv("WH40K_EVENT_DUMP_DIR");
    if (dir == nullptr) GTEST_SKIP() << "set WH40K_EVENT_DUMP_DIR";
    std::filesystem::create_directories(dir);
    for (const auto& file : replay_files()) {
        SCOPED_TRACE(file.filename().string());
        const Replay replay = load_replay(file);
        const ReplayResult r = run_replay(replay, repo_root());
        ASSERT_TRUE(r.ok()) << "step " << *r.failed_step << ": " << r.reasons.front();
        std::ofstream out(std::filesystem::path(dir) / file.filename(), std::ios::binary);
        out << Json{{"events", r.events}, {"state", r.final_state}}.dump() << '\n';
    }
}
