#include "core/geometry/distance.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wh40k::core {

float base_radius(const Base& base) {
    if (base.shape != BaseShape::Round) {
        throw std::logic_error("geometry: only round bases are supported in V1");
    }
    return base.diameter_mm * 0.5f;
}

float centre_distance(const Position& a, const Position& b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

float base_distance(const Position& a, const Base& base_a, const Position& b, const Base& base_b) {
    return std::max(0.0f, centre_distance(a, b) - base_radius(base_a) - base_radius(base_b));
}

bool bases_overlap(const Position& a, const Base& base_a, const Position& b, const Base& base_b) {
    return centre_distance(a, b) < base_radius(base_a) + base_radius(base_b) - EPS_MM;
}

bool within_area(const Rect& area, const Position& pos, const Base& base) {
    // Distance from the base centre to the nearest point of the rectangle.
    const float nx = std::clamp(pos.x, area.min_x, area.max_x);
    const float ny = std::clamp(pos.y, area.min_y, area.max_y);
    return within(centre_distance(pos, Position{nx, ny, pos.z}), base_radius(base));
}

bool wholly_within(const Rect& area, const Position& pos, const Base& base) {
    const float r = base_radius(base);
    return pos.x - r >= area.min_x - EPS_MM && pos.x + r <= area.max_x + EPS_MM &&
           pos.y - r >= area.min_y - EPS_MM && pos.y + r <= area.max_y + EPS_MM;
}

}  // namespace wh40k::core
