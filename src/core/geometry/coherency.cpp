#include "core/geometry/coherency.hpp"

#include "core/geometry/distance.hpp"
#include "core/geometry/units.hpp"

namespace wh40k::core {

CoherencyResult check_coherency(std::span<const PlacedModel> models) {
    CoherencyResult result;
    if (models.size() <= 1) {
        return result;  // 03.03 applies to units that contain more than one model
    }
    for (std::size_t i = 0; i < models.size(); ++i) {
        bool has_near_neighbour = false;
        bool all_within_far = true;
        for (std::size_t j = 0; j < models.size(); ++j) {
            if (i == j) {
                continue;
            }
            const float d = base_distance(models[i].position, models[i].base, models[j].position,
                                          models[j].base);
            if (within(d, kCoherencyNearMm)) {
                has_near_neighbour = true;
            }
            if (!within(d, kCoherencyFarMm)) {
                all_within_far = false;
            }
        }
        if (!has_near_neighbour) {
            result.isolated.push_back(models[i].id);
        }
        if (!all_within_far) {
            result.too_far.push_back(models[i].id);
        }
    }
    return result;
}

}  // namespace wh40k::core
