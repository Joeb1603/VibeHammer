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
    bool engagement = true;  // 2" around the enemies of a unit being moved (03.04)
    bool coherency = true;   // 2" around a unit being moved (03.03)
    bool ranges = true;      // the selected model's ranged weapon ranges, while choosing shots
    bool charge = true;      // the charge roll around a unit that has rolled to charge (11.02)
};

// A ring `distance_mm` out from the edge of `model`'s base (distances are edge to edge, 01.04).
struct Ring {
    core::ModelId model;
    RingKind kind = RingKind::Engagement;
    float distance_mm = 0.0f;
    friend bool operator==(const Ring&, const Ring&) = default;
};

[[nodiscard]] std::vector<Ring> plan_overlays(const core::GameState& state, std::optional<core::ModelId> selected_model,
                                              const OverlayToggles& toggles);

// Preview positions for models being dragged (D-028): their rings follow the cursor.
using PositionOverrides = std::vector<std::pair<core::ModelId, core::Position>>;

// The circles of `rings`: centred on each model (or its override), radius = base radius + the ring's
// distance (edge to edge, 01.04). Rings of models no longer on the battlefield are skipped.
[[nodiscard]] std::vector<Circle> ring_circles(const core::GameState& state, const std::vector<Ring>& rings,
                                               const PositionOverrides& overrides = {});

// The ring's name and what it means, for the legend and the board tooltip.
[[nodiscard]] std::string_view ring_name(RingKind kind);
[[nodiscard]] std::string_view ring_meaning(RingKind kind);

}  // namespace wh40k::client
