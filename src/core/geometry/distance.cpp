#include "core/geometry/distance.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "core/geometry/units.hpp"

namespace wh40k::core {

namespace {

float round_radius(const Base& base) {
    if (base.shape != BaseShape::Round) {
        throw std::logic_error("geometry: only round bases are supported in V1");
    }
    return base.diameter_mm * 0.5f;
}

}  // namespace

float base_distance(const Position& a, const Base& base_a, const Position& b, const Base& base_b) {
    // V1 has no terrain: measure horizontally (z is always 0).
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float centre = std::sqrt(dx * dx + dy * dy);
    return std::max(0.0f, centre - round_radius(base_a) - round_radius(base_b));
}

bool wholly_within(const Rect& area, const Position& pos, const Base& base) {
    const float r = round_radius(base);
    return pos.x - r >= area.min_x - EPS_MM && pos.x + r <= area.max_x + EPS_MM &&
           pos.y - r >= area.min_y - EPS_MM && pos.y + r <= area.max_y + EPS_MM;
}

}  // namespace wh40k::core
