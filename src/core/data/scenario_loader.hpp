#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include <nlohmann/json.hpp>

#include "core/data/datasheet.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::core {

// Maps a scenario's "unit_file" entry to a parsed datasheet.
using DatasheetResolver = std::function<Datasheet(const std::string& unit_file)>;

// Builds the initial GameState from a scenario (D-003: no deployment step; units start in the
// scenario's formation, D-008). Validates 02.06 composition and wargear options, 03.02 set-up
// (coherency 03.03, unengaged) and D-009 wholly within the deployment zone.
// Throws DataError on any violation.
[[nodiscard]] GameState build_game_state(const nlohmann::json& scenario,
                                         const DatasheetResolver& resolve);

// Loads a scenario file; its "unit_file" paths are resolved against `repo_root`.
[[nodiscard]] GameState load_scenario(const std::filesystem::path& scenario_path,
                                      const std::filesystem::path& repo_root);

}  // namespace wh40k::core
