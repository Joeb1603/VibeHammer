#pragma once

// D-085 (M85): positions that only the pre-batch bots reached (Generation::M77, removed at M85), kept as GameState JSON
// under tests/data/states/ so the tests of the code that stays still start from them.

#include <fstream>
#include <stdexcept>
#include <string>

#include "core/serialization/json.hpp"
#include "core/state/game_state.hpp"
#include "test_helpers.hpp"

namespace wh40k::test {

inline core::GameState load_state_fixture(const std::string& name) {
    std::ifstream in(repo_root() / "tests/data/states" / name, std::ios::binary);
    if (!in) throw std::runtime_error("missing state fixture " + name);
    return nlohmann::json::parse(in).get<core::GameState>();
}

}  // namespace wh40k::test
