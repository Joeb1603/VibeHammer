#include "measure.hpp"

#include <cmath>

#include "core/geometry/units.hpp"

namespace wh40k::client {

float measure_mm(const core::Position& from, const core::Position& to) {
    return std::hypot(to.x - from.x, to.y - from.y);
}

std::string measure_label(const core::Position& from, const core::Position& to) {
    return core::format_inches(measure_mm(from, to));
}

}  // namespace wh40k::client
