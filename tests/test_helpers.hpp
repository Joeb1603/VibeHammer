#pragma once

#include <filesystem>

#include "core/data/scenario_loader.hpp"
#include "core/geometry/units.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::test {

// Inches -> mm, so test coordinates read like the rulebook.
constexpr float in(float inches) { return core::inches_to_mm(inches); }

inline std::filesystem::path repo_root() { return std::filesystem::path(WH40K_REPO_ROOT); }

inline core::GameState load_v1_scenario() {
    return core::load_scenario(repo_root() / "data/scenarios/v1_skirmish.json", repo_root());
}

inline const core::Unit& unit_of(const core::GameState& gs, core::Side side) {
    for (const auto& u : gs.units) {
        if (u.side == side) {
            return u;
        }
    }
    throw std::logic_error("no unit for side");
}

}  // namespace wh40k::test
