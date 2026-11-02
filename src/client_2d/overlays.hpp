#pragma once

// Toggleable board overlays. Display only: rings are drawn at base radius + distance because distances are
// measured edge to edge (01.04); the distances come from core constants and the game data (D-024).

#include <optional>

#include "camera.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

struct OverlayToggles {
    bool coherency = true;   // 2" and 9" around the selected unit's models (03.03)
    bool engagement = true;  // 2" around enemy models (03.04)
    bool ranges = true;      // the selected model's ranged weapon ranges
    bool charge = true;      // the charge roll around a unit that has declared a charge (11.02)
};

// `friendly`: the side whose decision it is; its enemies get engagement rings.
void draw_overlays(const core::GameState& state, const BoardCamera& camera, const OverlayToggles& toggles,
                   std::optional<core::UnitId> selected_unit, std::optional<core::ModelId> selected_model,
                   core::Side friendly);

}  // namespace wh40k::client
