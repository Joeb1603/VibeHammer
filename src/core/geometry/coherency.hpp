#pragma once

#include <span>
#include <vector>

#include "core/geometry/primitives.hpp"
#include "core/geometry/units.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

inline constexpr float kCoherencyNearMm = 2.0f * MM_PER_INCH;  // 03.03 first clause
inline constexpr float kCoherencyFarMm = 9.0f * MM_PER_INCH;   // 03.03 second clause

struct PlacedModel {
    ModelId id;
    Position position;
    Base base;
};

// 03.03: the two clauses are checked independently.
struct CoherencyResult {
    // Models not within 2" of at least one other model in the unit.
    std::vector<ModelId> isolated;
    // Models not within 9" of every other model in the unit.
    std::vector<ModelId> too_far;

    [[nodiscard]] bool ok() const noexcept { return isolated.empty() && too_far.empty(); }
};

// 03.03 Coherency. A unit of one model is always in coherency. Vertical (5") clauses are
// trivially satisfied in V1 (no terrain).
[[nodiscard]] CoherencyResult check_coherency(std::span<const PlacedModel> models);

}  // namespace wh40k::core
