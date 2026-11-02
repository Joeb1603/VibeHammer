#include "core/geometry/engagement.hpp"

#include "core/geometry/distance.hpp"

namespace wh40k::core {

bool models_engaged(const PlacedModel& a, const PlacedModel& b) {
    // 03.04 / D-009: within engagement range means some part of the base is within 2".
    return within(base_distance(a.position, a.base, b.position, b.base), kEngagementRangeMm);
}

bool units_engaged(std::span<const PlacedModel> unit, std::span<const PlacedModel> enemy) {
    for (const PlacedModel& a : unit) {
        for (const PlacedModel& b : enemy) {
            if (models_engaged(a, b)) {
                return true;
            }
        }
    }
    return false;
}

std::vector<ModelId> engaged_models(std::span<const PlacedModel> unit,
                                    std::span<const PlacedModel> enemies) {
    std::vector<ModelId> out;
    for (const PlacedModel& a : unit) {
        for (const PlacedModel& b : enemies) {
            if (models_engaged(a, b)) {
                out.push_back(a.id);
                break;
            }
        }
    }
    return out;
}

}  // namespace wh40k::core
