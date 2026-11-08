#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "core/geometry/coherency.hpp"  // PlacedModel
#include "core/geometry/pose.hpp"       // PosedModel
#include "core/types/ids.hpp"

namespace wh40k::core {

// D-009: a unit is within X of something if any of its models is, so the distance between two
// groups of models is the smallest base-to-base distance over all pairs (01.04).
// Returns nullopt if either group is empty.
[[nodiscard]] std::optional<float> closest_distance(std::span<const PlacedModel> a,
                                                    std::span<const PlacedModel> b);

// D-010 Closest Model: measured from `from` (the model using the rule). Returns every candidate
// tied for closest (equal within EPS_MM), in candidate order. Empty if there are no candidates.
// When more than one is returned, the controlling player of `from` chooses (D-010); geometry never
// breaks ties.
[[nodiscard]] std::vector<ModelId> closest_models(const PlacedModel& from,
                                                  std::span<const PlacedModel> candidates);

// D-010 Closest Unit: measured from the unit using the rule (`from`) to each candidate unit, using
// closest_distance. Returns the indices into `candidates` of every unit tied for closest, ascending.
// Empty candidate units are ignored.
[[nodiscard]] std::vector<std::size_t> closest_units(
    std::span<const PlacedModel> from, std::span<const std::vector<PlacedModel>> candidates);

// D-074 (M64): the same for models of any base shape with a facing, measured with base_distance(BasePose, BasePose).
// Round bases give the same answers as the functions above.
[[nodiscard]] std::optional<float> closest_distance(std::span<const PosedModel> a, std::span<const PosedModel> b);
[[nodiscard]] std::vector<ModelId> closest_models(const PosedModel& from, std::span<const PosedModel> candidates);
[[nodiscard]] std::vector<std::size_t> closest_units(std::span<const PosedModel> from,
                                                     std::span<const std::vector<PosedModel>> candidates);

}  // namespace wh40k::core
