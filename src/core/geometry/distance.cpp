#include "core/geometry/distance.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wh40k::core {

void throw_unsupported_base_shape() { throw std::logic_error("geometry: only round bases are supported in V1"); }

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
