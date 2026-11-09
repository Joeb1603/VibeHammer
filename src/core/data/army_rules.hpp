#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/data/datasheet.hpp"

namespace wh40k::core {

// D-079 / 02.07: how a model's weapons decompose into its model type's fixed weapons plus one option of each
// slot. `options` holds the chosen option's index for each slot, in slot order; `problem` (empty when the
// weapons fit) cites 02.07 and D-079.
struct SlotFit {
    std::vector<std::size_t> options;
    std::string problem;

    [[nodiscard]] bool fits() const { return problem.empty(); }
};

// Read-only. `weapons` is a multiset (order ignored). The first decomposition in slot order and option order
// (depth-first) is returned, so the result is deterministic. Limits are not checked here.
[[nodiscard]] SlotFit loadout_fits_slots(const ModelWargear& wargear, const std::vector<std::string>& weapons);

// D-079 / 02.07 (M75): a model's weapons for one option of each slot: the fixed weapons, then each chosen option's
// weapons in slot order (the order default_unit and the stock lists use). Precondition: one option index per slot,
// each in range.
[[nodiscard]] std::vector<std::string> loadout_weapons(const ModelWargear& wargear,
                                                       const std::vector<std::size_t>& options);

// The points of the options a fit chose (0 for a fit that failed).
[[nodiscard]] int fit_option_points(const ModelWargear& wargear, const SlotFit& fit);

// D-079: a unit of `models` models costs the last size bracket with min_models <= models (before option points
// and the copy surcharge). Precondition: `lb.points` is not empty; below the first bracket, the first.
[[nodiscard]] int bracket_points(const ListBuilding& lb, int models);

// D-079: a limit's maximum for a unit of `models` models: `below_models.max` below `below_models.models`
// models, else `max` plus `per_models.add` for every `per_models.models` models (rounded down).
[[nodiscard]] int limit_max(const WargearLimit& limit, int models);

}  // namespace wh40k::core
