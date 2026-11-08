#pragma once

#include <span>
#include <vector>

#include "core/geometry/pose.hpp"
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

// M54: 03.03 for a unit with one model tried at many spots while the others stay put. ok_at(p) is
// check_coherency(models with models[moved] at p).ok(), from the same distances and tests, with the
// other models' distances to each other worked out once (so each spot costs one pass over the unit).
class CoherencyWithOneMoved {
public:
    CoherencyWithOneMoved(std::span<const PlacedModel> models, std::size_t moved);
    [[nodiscard]] bool ok_at(const Position& p) const;

private:
    std::vector<PlacedModel> models_;
    std::size_t moved_ = 0;
    std::vector<char> near_without_moved_;  // within 2" of another model that isn't the moved one
    std::vector<char> far_without_moved_;   // within 9" of every other model that isn't the moved one
    bool others_too_far_ = false;           // some other model fails the 9" clause whatever the moved one does
};

// D-074 (M64): 03.03 for models of any base shape with a facing, measured with base_distance(BasePose, BasePose).
// Round bases give the same answers as check_coherency above.
[[nodiscard]] CoherencyResult check_coherency(std::span<const PosedModel> models);

// D-074: CoherencyWithOneMoved for posed models: ok_at(p, facing) is check_coherency(models with models[moved] at p
// facing `facing_deg`).ok().
class PosedCoherencyWithOneMoved {
public:
    PosedCoherencyWithOneMoved(std::span<const PosedModel> models, std::size_t moved);
    [[nodiscard]] bool ok_at(const Position& p, float facing_deg) const;

private:
    std::vector<PosedModel> models_;
    std::size_t moved_ = 0;
    std::vector<char> near_without_moved_;
    std::vector<char> far_without_moved_;
    bool others_too_far_ = false;
};

}  // namespace wh40k::core
