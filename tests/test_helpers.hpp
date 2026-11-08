#pragma once

#include <filesystem>

#include "core/data/scenario_loader.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/units.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::test {

// Inches -> mm, so test coordinates read like the rulebook.
constexpr float in(float inches) { return core::inches_to_mm(inches); }

// Battlefield position given in inches.
inline core::Position at(float x_in, float y_in) { return core::Position{in(x_in), in(y_in), 0.0f}; }

inline const core::Base kIntercessorBase{core::BaseShape::Round, 32.0f, 0.0f};
inline const core::Base kTermagantBase{core::BaseShape::Round, 28.5f, 0.0f};

inline core::PlacedModel placed(std::uint32_t id, core::Position pos,
                                const core::Base& base = kIntercessorBase) {
    return core::PlacedModel{core::ModelId{id}, pos, base};
}

inline std::filesystem::path repo_root() { return std::filesystem::path(WH40K_REPO_ROOT); }

inline core::GameState load_v1_scenario() {
    return core::load_scenario(repo_root() / "data/scenarios/v1_skirmish.json", repo_root());
}

// M59 / D-070 test fixtures (tests/data/scenarios, not listed by the client or packaged).
inline constexpr const char* kRostersFixture = "tests/data/scenarios/rosters_no_tyrannofex_44x60.json";
inline constexpr const char* kHavocsVsTyrantFixture = "tests/data/scenarios/havocs_vs_hive_tyrant_24x24.json";

// The two rosters without the Tyrannofex on the 44"x60" portrait board: 13 units.
inline core::GameState load_rosters_fixture() {
    return core::load_scenario(repo_root() / kRostersFixture, repo_root());
}

// The roster fixture with every Tyranid model moved `closer_in` inches south (36" - closer_in between the
// zones' front lines), so random play reaches shooting and fighting within a few turns.
inline core::GameState load_rosters_fixture_closer(float closer_in) {
    core::GameState gs = load_rosters_fixture();
    for (auto& u : gs.units) {
        if (u.side != core::Side::B) continue;
        for (auto& m : u.models) m.position.y -= in(closer_in);
    }
    return gs;
}

// The V1 scenario with the Termagants moved 1" towards the Intercessors, so every Intercessor is
// within 12" (half Bolt Rifle range) of the Termagant unit. At the D-008 start positions only the
// centre Intercessor is (the others are 0.01-0.07 mm further than 12").
inline core::GameState load_v1_scenario_closer() {
    core::GameState gs = load_v1_scenario();
    for (auto& m : gs.units[1].models) {
        m.position.y -= in(1);
    }
    return gs;
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
