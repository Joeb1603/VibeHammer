#pragma once

// D-087 (M88): terrain in 2D. Terrain areas (13.01) holding terrain features of a category (13.02-13.05), as plain
// value types held in GameState::terrain (JSON "terrain", omitted when there is none, so every older file is
// unchanged). The rules that read them are in core/rules/terrain.hpp; the footprint geometry in
// core/geometry/terrain_shape.hpp.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/geometry/terrain_shape.hpp"
#include "core/geometry/units.hpp"

namespace wh40k::core {

struct GameState;

// 13.02: each terrain feature belongs to a terrain category.
enum class TerrainCategory : std::uint8_t {
    Exposed,  // 13.03: craters, razorwire, scattered debris
    Light,    // 13.04: barricades, low walls, statuary
    Dense,    // 13.05: buildings, ruins, armoured containers, woods
};

[[nodiscard]] std::string_view terrain_category_name(TerrainCategory c);  // also the JSON name: "exposed", ...
[[nodiscard]] std::optional<TerrainCategory> terrain_category_from_name(std::string_view name);

// 13.01: a terrain feature, wholly within its terrain area. D-087: Light and Dense features are solid footprints
// (no model is set up or ends a move on one); Exposed features are at ground level. Only a Dense feature has a height
// (13.06 reads it: 2" or less can be crossed by any model).
struct TerrainFeature {
    std::string name;
    TerrainCategory category = TerrainCategory::Exposed;
    std::optional<float> height_mm;  // Dense only
    TerrainShape shape;
    friend bool operator==(const TerrainFeature&, const TerrainFeature&) = default;
};

// 13.01: a terrain area, the area of the battlefield its boundary occupies, holding 1 to 16 features. 14.01: an area
// linked to a mission objective ("objective": its name) is that objective's circle, so the terrain area is the
// objective.
struct TerrainArea {
    std::string name;
    std::optional<std::string> objective;
    TerrainShape shape;
    std::vector<TerrainFeature> features;
    friend bool operator==(const TerrainArea&, const TerrainArea&) = default;
};

// D-087: bounds on the data (the loader and the state JSON refuse anything else as DataError).
inline constexpr std::size_t kMaxTerrainAreas = 32;
inline constexpr std::size_t kMaxTerrainFeatures = 16;  // per area
// 13.06: Dense features 2" or less in height can be moved through by any model.
inline constexpr float kDenseMoveHeightMm = 2.0f * MM_PER_INCH;
// 13.09: unless otherwise stated, a model's detection range is 15".
inline constexpr float kDetectionRangeMm = 15.0f * MM_PER_INCH;

// 13.04 / 13.05 / D-087: a Light or Dense feature is solid: no model is set up or ends a move on it.
[[nodiscard]] constexpr bool is_solid(TerrainCategory c) noexcept { return c != TerrainCategory::Exposed; }

// D-087: refuses (DataError, naming `where`) terrain that breaks the format:
// - a polygon that is not convex and counter-clockwise, has fewer than 3 or more than 16 vertices, or no area; a circle
//   with a diameter of 0 or less; a position that is not finite;
// - an area not wholly on the battlefield (EPS_MM, as D-084's objectives), or a feature not wholly within its area
//   (exact);
// - an area with no features or more than 16; more than 32 areas; an empty name;
// - a Dense feature without a height above 0, or a height on another category;
// - an objective link without a mission, to an unknown objective, to one already linked, or with a shape that isn't
//   that objective's circle;
// - a model on the battlefield (`units`) set up on a Light or Dense feature (D-087, D-027 overlap).
void check_terrain_data(const GameState& state, std::string_view where);

}  // namespace wh40k::core
