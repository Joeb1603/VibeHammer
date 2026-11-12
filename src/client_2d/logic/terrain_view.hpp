#pragma once

// M88 (D-087, D-088): what the client shows of the terrain: the shapes to draw, the hover text over an area and why an
// enemy unit can't be targeted. Every fact comes from core's read-only queries (D-024); no rules here. Plain ASCII
// only (D-034).

#include <optional>
#include <string>
#include <vector>

#include "core/geometry/terrain_shape.hpp"
#include "core/rules/shooting_types.hpp"
#include "core/state/game_state.hpp"
#include "core/state/terrain.hpp"
#include "core/types/ids.hpp"

namespace wh40k::client {

// One shape to draw: a terrain area (its outline) or a terrain feature (filled by its category).
struct TerrainMark {
    bool is_area = true;
    core::TerrainShape shape;                     // mm
    core::TerrainCategory category = core::TerrainCategory::Exposed;  // features only
    bool obscuring = false;                       // areas only: 13.10
    std::string label;                            // the area's name (areas only; empty for features)
    friend bool operator==(const TerrainMark&, const TerrainMark&) = default;
};

// Every area (in file order), then every feature (area by area, in file order). Empty without terrain.
[[nodiscard]] std::vector<TerrainMark> terrain_marks(const core::GameState& state);

// The hover text over `p` (mm): for each area `p` is in, its name (and objective), whether it is obscuring (13.10) or
// hides INFANTRY/BEASTS/SWARM (13.09), and each of its features with its category (and height, 13.06). Empty when `p`
// is in no area.
[[nodiscard]] std::vector<std::string> terrain_tooltip(const core::GameState& state, const core::Position& p);

// The Game panel's lines: one per area ("Centre (objective): 4 dense walls, obscuring, hides"). Empty without terrain.
[[nodiscard]] std::vector<std::string> terrain_summary(const core::GameState& state);

// Why `shooter` can't select `target` with the shooting type: the engine's reason for the first of its models' weapons
// that is refused for the target itself (13.09 Hidden, 13.10 / 13.11, 06.01, range, engaged...), skipping reasons about
// the weapon alone. nullopt when some declaration at the target is valid, or there is no reason to give.
[[nodiscard]] std::optional<std::string> target_refusal_text(const core::GameState& state, core::UnitId shooter,
                                                             core::ShootingType type, core::UnitId target);

}  // namespace wh40k::client
