#pragma once

// The free measuring ruler (D-035, M22): a left-drag on empty board measures from the press point to the
// cursor. Display only (D-024).

#include <string>

#include "core/geometry/primitives.hpp"

namespace wh40k::client {

// Straight-line distance between two battlefield points (mm), centre to centre on the plane.
[[nodiscard]] float measure_mm(const core::Position& from, const core::Position& to);

// The ruler's label: the distance in inches with two decimals, e.g. "5.00\"".
[[nodiscard]] std::string measure_label(const core::Position& from, const core::Position& to);

}  // namespace wh40k::client
