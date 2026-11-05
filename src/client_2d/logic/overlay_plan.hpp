#pragma once

// Which ring overlays to draw now (D-028): each ring only while it is relevant to the pending decision,
// and only if its toggle is on. Display only: the distances are core constants and the game data, and
// the relevance comes from the pending decision kind and the flow state (D-024).

#include <optional>
#include <vector>

#include "core/state/game_state.hpp"

namespace wh40k::client {

struct OverlayToggles {
    bool engagement = true;  // 2" around the enemies of a unit being moved (03.04)
    bool coherency = true;   // 2" around a unit being moved (03.03)
    bool ranges = true;      // the selected model's ranged weapon ranges, while choosing shots
    bool charge = true;      // the charge roll around a unit that has rolled to charge (11.02)
};

enum class RingKind { Engagement, Coherency, WeaponRange, ChargeDistance };

// A ring `distance_mm` out from the edge of `model`'s base (distances are edge to edge, 01.04).
struct Ring {
    core::ModelId model;
    RingKind kind = RingKind::Engagement;
    float distance_mm = 0.0f;
    friend bool operator==(const Ring&, const Ring&) = default;
};

[[nodiscard]] std::vector<Ring> plan_overlays(const core::GameState& state, std::optional<core::ModelId> selected_model,
                                              const OverlayToggles& toggles);

}  // namespace wh40k::client
