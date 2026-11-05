#include "overlays.hpp"

#include <algorithm>

namespace wh40k::client {

Color ring_color(RingKind kind) {
    switch (kind) {
        case RingKind::Engagement: return Color{230, 60, 60, 170};
        case RingKind::Coherency: return Color{120, 220, 120, 170};
        case RingKind::WeaponRange: return Color{240, 200, 80, 140};
        case RingKind::ChargeDistance: return Color{200, 120, 240, 150};
    }
    return WHITE;
}

void draw_overlays(const core::GameState& state, const BoardCamera& cam, const std::vector<Ring>& rings,
                   float ui_scale) {
    for (const Ring& ring : rings) {
        for (const core::Unit& u : state.units) {
            for (const core::Model& m : u.models) {
                if (m.id != ring.model) continue;
                const float thickness = 1.5f * ui_scale;
                const float r = cam.to_px(m.base.diameter_mm * 0.5f + ring.distance_mm);
                DrawRing(cam.to_screen(m.position), std::max(0.0f, r - thickness * 0.5f), r + thickness * 0.5f,
                         0.0f, 360.0f, 64, ring_color(ring.kind));
            }
        }
    }
}

}  // namespace wh40k::client
