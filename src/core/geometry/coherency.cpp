#include "core/geometry/coherency.hpp"

#include "core/geometry/distance.hpp"
#include "core/geometry/oval.hpp"
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

CoherencyWithOneMoved::CoherencyWithOneMoved(std::span<const PlacedModel> models, std::size_t moved)
    : models_(models.begin(), models.end()), moved_(moved) {
    const std::size_t n = models_.size();
    near_without_moved_.assign(n, 0);
    far_without_moved_.assign(n, 1);
    for (std::size_t i = 0; i < n; ++i) {
        if (i == moved_) {
            continue;
        }
        for (std::size_t j = 0; j < n; ++j) {
            if (j == i || j == moved_) {
                continue;
            }
            const float d = base_distance(models_[i].position, models_[i].base, models_[j].position,
                                          models_[j].base);
            if (within(d, kCoherencyNearMm)) {
                near_without_moved_[i] = 1;
            }
            if (!within(d, kCoherencyFarMm)) {
                far_without_moved_[i] = 0;
                others_too_far_ = true;
            }
        }
    }
}

bool CoherencyWithOneMoved::ok_at(const Position& p) const {
    const std::size_t n = models_.size();
    if (n <= 1) {
        return true;  // 03.03 applies to units that contain more than one model
    }
    if (moved_ >= n) {
        return check_coherency(models_).ok();  // nothing moves
    }
    if (others_too_far_) {
        return false;
    }
    const PlacedModel& m = models_[moved_];
    bool moved_has_near = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (i == moved_) {
            continue;
        }
        const PlacedModel& o = models_[i];
        // Model i's own clauses, measured from i (as check_coherency does).
        const float from_other = base_distance(o.position, o.base, p, m.base);
        if (!near_without_moved_[i] && !within(from_other, kCoherencyNearMm)) {
            return false;
        }
        if (!within(from_other, kCoherencyFarMm)) {
            return false;
        }
        // The moved model's clauses, measured from it.
        const float from_moved = base_distance(p, m.base, o.position, o.base);
        if (within(from_moved, kCoherencyNearMm)) {
            moved_has_near = true;
        }
        if (!within(from_moved, kCoherencyFarMm)) {
            return false;
        }
    }
    return moved_has_near;
}

// ---- D-074: posed models (the same steps, measured with base_distance(BasePose, BasePose)) ----

CoherencyResult check_coherency(std::span<const PosedModel> models) {
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
            const float d = base_distance(models[i].pose(), models[j].pose());
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

PosedCoherencyWithOneMoved::PosedCoherencyWithOneMoved(std::span<const PosedModel> models, std::size_t moved)
    : models_(models.begin(), models.end()), moved_(moved) {
    const std::size_t n = models_.size();
    near_without_moved_.assign(n, 0);
    far_without_moved_.assign(n, 1);
    for (std::size_t i = 0; i < n; ++i) {
        if (i == moved_) {
            continue;
        }
        for (std::size_t j = 0; j < n; ++j) {
            if (j == i || j == moved_) {
                continue;
            }
            const float d = base_distance(models_[i].pose(), models_[j].pose());
            if (within(d, kCoherencyNearMm)) {
                near_without_moved_[i] = 1;
            }
            if (!within(d, kCoherencyFarMm)) {
                far_without_moved_[i] = 0;
                others_too_far_ = true;
            }
        }
    }
}

bool PosedCoherencyWithOneMoved::ok_at(const Position& p, float facing_deg) const {
    const std::size_t n = models_.size();
    if (n <= 1) {
        return true;  // 03.03 applies to units that contain more than one model
    }
    if (moved_ >= n) {
        return check_coherency(std::span<const PosedModel>(models_)).ok();  // nothing moves
    }
    if (others_too_far_) {
        return false;
    }
    const BasePose moved{p, models_[moved_].base, facing_deg};
    bool moved_has_near = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (i == moved_) {
            continue;
        }
        const BasePose other = models_[i].pose();
        const float from_other = base_distance(other, moved);
        if (!near_without_moved_[i] && !within(from_other, kCoherencyNearMm)) {
            return false;
        }
        if (!within(from_other, kCoherencyFarMm)) {
            return false;
        }
        const float from_moved = base_distance(moved, other);
        if (within(from_moved, kCoherencyNearMm)) {
            moved_has_near = true;
        }
        if (!within(from_moved, kCoherencyFarMm)) {
            return false;
        }
    }
    return moved_has_near;
}

}  // namespace wh40k::core
