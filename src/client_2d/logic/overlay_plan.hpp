#pragma once

// Which ring overlays to draw now (D-028): each ring only while it is relevant to the pending decision,
// and only if its toggle is on. Display only: the distances are core constants and the game data, and
// the relevance comes from the pending decision kind and the flow state (D-024).

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "core/state/game_state.hpp"
#include "ring_outline.hpp"

namespace wh40k::client {

struct OverlayToggles {
    bool move_range = true;  // how far each model of a unit being moved can go, from its start position
    bool engagement = true;  // 2" around the enemies of a unit being moved (03.04)
    bool coherency = true;   // 2" around a unit being moved (03.03)
    bool ranges = true;      // the selected model's ranged weapon ranges, while choosing shots
    bool charge = true;      // the charge roll around a unit whose charge is being made (11.02)
};

// A ring `distance_mm` out from the edge of `model`'s base (distances are edge to edge, 01.04), centred
// on the model, or on `centre` when set (a movement range is drawn round the model's start position).
struct Ring {
    core::ModelId model;
    RingKind kind = RingKind::Engagement;
    float distance_mm = 0.0f;
    std::optional<core::Position> centre;
    friend bool operator==(const Ring&, const Ring&) = default;
};

[[nodiscard]] std::vector<Ring> plan_overlays(const core::GameState& state, std::optional<core::ModelId> selected_model,
                                              const OverlayToggles& toggles);

// Preview positions for models being dragged (D-028): their rings follow the cursor (a ring with a fixed
// `centre` stays put).
using PositionOverrides = std::vector<std::pair<core::ModelId, core::Position>>;
// D-074 part 3 (M66): preview facings for a dragged oval being turned: its rings turn with it.
using FacingOverrides = std::vector<std::pair<core::ModelId, float>>;

// The circles of `rings`: centred on each model (or its override), radius = base radius + the ring's
// distance (edge to edge, 01.04). Rings of models no longer on the battlefield are skipped.
// D-074 part 3: round an oval base, the Engagement Range, coherency and weapon range rings are the ellipse's offset
// outline at the model's facing (Circle::oval); the movement and charge rings stay circles of its circumscribed radius
// plus the distance (exact: a model may turn freely, 03.01, so some part of its base can reach all of that disc).
[[nodiscard]] std::vector<Circle> ring_circles(const core::GameState& state, const std::vector<Ring>& rings,
                                               const PositionOverrides& overrides = {},
                                               const FacingOverrides& facings = {});

// The ring's name and what it means in full, for the board tooltip and the legend's hover tooltip.
[[nodiscard]] std::string_view ring_name(RingKind kind);
[[nodiscard]] std::string_view ring_meaning(RingKind kind);
// M50 (D-061): one short line per ring for the sidebar legend; the full ring_meaning shows on hover.
[[nodiscard]] std::string_view ring_summary(RingKind kind);

}  // namespace wh40k::client
