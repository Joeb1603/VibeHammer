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
// scenario's formation, D-008, with D-064's optional depth_in). Validates 02.06 composition and
// wargear options, 03.02 set-up (coherency 03.03, unengaged, no model on another model: D-064) and
// D-009 wholly within the deployment zone.
// D-081 (M74): with "deployment": "alternating" the units are built into GameState::undeployed instead (no formation,
// north and south zones only; each must have a legal spot in its zone with nothing else on the battlefield), to be
// set up by hand during the deployment step.
// D-087 (M88): an optional "terrain" block (areas and features, in inches; an area may link to a mission objective),
// checked by check_terrain_data: no formation may put a model on a Light or Dense feature.
// Throws DataError on any violation.
[[nodiscard]] GameState build_game_state(const nlohmann::json& scenario,
                                         const DatasheetResolver& resolve);

// D-084 (M86): the "mission" block of a scenario file (custom battles copy it, custom_battle_scenario). Throws
// DataError if the file has none.
[[nodiscard]] nlohmann::json load_mission_block(const std::filesystem::path& scenario_path);

// D-087 (M88): the "terrain" block of a scenario file (custom battles copy it, custom_battle_scenario). Throws
// DataError if the file has none.
[[nodiscard]] nlohmann::json load_terrain_block(const std::filesystem::path& scenario_path);

// Loads a scenario file; its "unit_file" paths are resolved against `repo_root`.
[[nodiscard]] GameState load_scenario(const std::filesystem::path& scenario_path,
                                      const std::filesystem::path& repo_root);

}  // namespace wh40k::core
