#pragma once

// D-008 / D-064 / D-074 part 2: a unit's formation in a scenario file, and how its models are placed from it. Lifted
// out of the scenario loader verbatim (M72, D-079 part 2) so the auto-layout (zone_layout.hpp) places models exactly as
// the loader does; every shipped scenario places its models bit for bit as before.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/model/unit.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::core {

struct Formation {
    int ranks = 1;
    float gap_mm = 0.0f;
    float centre_x_mm = 0.0f;
    float depth_mm = 0.0f;  // D-064: front rank's distance behind the zone's front line
    float raw_facing_deg = 0.0f;  // D-074 part 2: formation.facing_deg as given (0 when absent)
};

// Parses a scenario unit's "formation" object. Throws DataError ("scenario: <where>: ...") on bad data.
[[nodiscard]] Formation parse_formation(const nlohmann::json& j, const std::string& where);

// D-008: ranks centred on centre_x, front rank's bases touching the zone's front line, further
// ranks behind it; models fill each rank left to right (low x to high x) in listed order.
// D-064: the whole formation stands `depth_mm` further back (towards the owning side's edge).
// D-074 part 2 (M65): each model is placed by its base's half extents along x and y at the formation's facing
// (base_extents: exactly the radius for a round base, so round formations are placed bit-for-bit as before). A rank is
// as wide as its models' x extents plus the gaps; the rank pitch is the deepest model's y extent plus the gap.
// Sets each model's position and facing. Throws DataError ("scenario: <where>: ...") for an east/west zone, a round
// base with a facing, or a malformed oval.
void place_formation(std::vector<Model>& models, const Formation& f, const Battlefield& field,
                     const DeploymentZone& zone, const std::string& where);

}  // namespace wh40k::core
