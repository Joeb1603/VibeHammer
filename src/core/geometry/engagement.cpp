#include "core/geometry/engagement.hpp"

#include "core/geometry/distance.hpp"
#include "core/geometry/oval.hpp"

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

// ---- D-074: posed models ----

bool models_engaged(const PosedModel& a, const PosedModel& b) {
    return within(base_distance(a.pose(), b.pose()), kEngagementRangeMm);
}

bool units_engaged(std::span<const PosedModel> unit, std::span<const PosedModel> enemy) {
    for (const PosedModel& a : unit) {
        for (const PosedModel& b : enemy) {
            if (models_engaged(a, b)) {
                return true;
            }
        }
    }
    return false;
}

std::vector<ModelId> engaged_models(std::span<const PosedModel> unit, std::span<const PosedModel> enemies) {
    std::vector<ModelId> out;
    for (const PosedModel& a : unit) {
        for (const PosedModel& b : enemies) {
            if (models_engaged(a, b)) {
                out.push_back(a.id);
                break;
            }
        }
    }
    return out;
}

}  // namespace wh40k::core
