#pragma once

// D-079 part 2 (M72): the auto-layout. Places an army's units in its deployment zone as D-008 formations (gap 0,
// depth D-064, ovals at facing 0, models in list order), so a custom battle needs no hand-written formation.
//
// - Ranks: each unit takes the fewest ranks r = 1..n that make it coherent (03.03) and fit the zone (at most the
//   zone's depth deep and the battlefield's width wide).
// - Shelves: units in list order, west to east, 1" apart; a unit that doesn't fit the shelf's width starts a new
//   shelf. The first shelf's front touches the zone's front line (D-008 zone_front); each further shelf stands 1"
//   behind the deepest unit of the shelf before it. Each shelf is centred on the battlefield.
// - Fallback: first-fit decreasing by width (each unit, widest first, on the first shelf it fits); failing that, the
//   army does not fit.
// - centre_x_in and depth_in are on a 1/8" grid, exact in float, so inches -> JSON -> mm reproduce the positions.
// - The result is verified by placing every model as the scenario loader does (place_formation) and checking the
//   loader's set-up rules within the side: coherency (03.03), wholly within the zone (D-009) and no overlap (03.02,
//   D-064). Deterministic: no state but the inputs.

#include <optional>
#include <string>
#include <vector>

#include "core/data/formation.hpp"
#include "core/geometry/primitives.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::core {

inline constexpr float kLayoutGapIn = 1.0f;      // between units on a shelf, and between shelves
inline constexpr float kLayoutGridIn = 0.125f;   // positions are multiples of 1/8"

// One unit's place: its formation's ranks, centre and depth behind the zone's front line (gap 0, facing 0).
struct LayoutSlot {
    int ranks = 1;
    float centre_x_in = 0.0f;
    float depth_in = 0.0f;
    friend bool operator==(const LayoutSlot&, const LayoutSlot&) = default;
};

struct ZoneLayout {
    std::vector<LayoutSlot> slots;  // one per unit, in the input order (empty when the army doesn't fit)
    std::string problem;            // D-079: why it doesn't fit; empty when it does
    [[nodiscard]] bool fits() const { return problem.empty(); }
};

// The formation a slot stands for (gap 0, anchor zone_front, facing 0), as the scenario loader would parse it.
[[nodiscard]] Formation slot_formation(const LayoutSlot& slot);

// 03.03 / D-079: the fewest ranks r = 1..n that make a unit of these bases (in list order) coherent and no wider than
// the battlefield and no deeper than the zone; nullopt if none does (or the unit has no models).
[[nodiscard]] std::optional<int> fewest_coherent_ranks(const std::vector<Base>& bases, const Battlefield& field,
                                                       const DeploymentZone& zone);

// D-079: the layout of a side's units (each its models' bases in list order) in `zone`. North and south zones only.
[[nodiscard]] ZoneLayout layout_zone(const std::vector<std::vector<Base>>& units, const Battlefield& field,
                                     const DeploymentZone& zone);

}  // namespace wh40k::core
