#pragma once

// D-074 (M64): a base with a facing. Pure geometry: nothing in the game state carries a facing yet (M65 adds it).
//
// Facing convention (D-074):
// - `facing_deg` is a float in degrees, normalised to (-180, 180].
// - At 0 an oval's major axis (its length) lies along the battlefield's y axis (south-north), so an oval facing the
//   enemy across the portrait board has facing 0.
// - Positive angles turn counter-clockwise seen from above with y pointing north (from +y towards -x): at 90 the
//   major axis lies along x.
// - A round base ignores facing.
//
// Oval base (D-070, D-074): Base{shape = Oval, diameter_mm = width (minor axis), length_mm = length (major axis)},
// length_mm >= diameter_mm (the unit loader checks it). The Tyrannofex is 92 x 120 mm.

#include "core/geometry/primitives.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

// D-074: the same facing in (-180, 180]. A non-finite input gives 0. Never returns -0.
[[nodiscard]] float normalise_facing_deg(float deg) noexcept;

// D-074 / 03.01 Rotating a Model: the signed turn from `from_deg` to `to_deg` the shorter way, in (-180, 180]
// (positive is counter-clockwise; a half turn is +180).
[[nodiscard]] float rotation_delta_deg(float from_deg, float to_deg) noexcept;

// Where a base is and which way it faces.
struct BasePose {
    Position position;
    Base base;
    float facing_deg = 0.0f;
    friend bool operator==(const BasePose&, const BasePose&) = default;
};

// D-074 bounding circles (for M66's bots): every part of the base is within the circumscribed radius of its centre,
// and the inscribed circle is inside the base, whatever the facing. Round: half the diameter for both. Oval: half the
// length and half the width.
[[nodiscard]] constexpr float circumscribed_radius(const Base& base) noexcept {
    if (base.shape == BaseShape::Round) return base.diameter_mm * 0.5f;
    return (base.length_mm > base.diameter_mm ? base.length_mm : base.diameter_mm) * 0.5f;
}
[[nodiscard]] constexpr float inscribed_radius(const Base& base) noexcept {
    if (base.shape == BaseShape::Round) return base.diameter_mm * 0.5f;
    return (base.length_mm < base.diameter_mm ? base.length_mm : base.diameter_mm) * 0.5f;
}

// A model with a facing: PlacedModel (coherency.hpp) plus `facing_deg`. The pose-aware overloads of the model-level
// geometry (models_engaged, check_coherency, closest_*, has_line_of_sight, check_model_path, ...) take these.
struct PosedModel {
    ModelId id;
    Position position;
    Base base;
    float facing_deg = 0.0f;
    [[nodiscard]] BasePose pose() const noexcept { return BasePose{position, base, facing_deg}; }
};

}  // namespace wh40k::core
