#include "overlays.hpp"

#include <algorithm>
#include <cmath>

namespace wh40k::client {

Color ring_color(RingKind kind) {
    switch (kind) {
        case RingKind::MoveRange: return Color{110, 190, 255, 200};
        case RingKind::Engagement: return Color{230, 60, 60, 190};
        case RingKind::Coherency: return Color{120, 220, 120, 190};
        case RingKind::WeaponRange: return Color{240, 200, 80, 160};
        case RingKind::ChargeDistance: return Color{200, 120, 240, 170};
    }
    return WHITE;
}

void draw_overlays(const BoardCamera& cam, const std::vector<Arc>& arcs, float ui_scale) {
    constexpr float kStepRad = 3.0f * 3.14159265f / 180.0f;
    const float thickness = 1.5f * ui_scale;
    for (const Arc& arc : arcs) {
        const float sweep = arc.end_rad - arc.start_rad;
        const int steps = std::max(8, static_cast<int>(std::ceil(sweep / kStepRad)));
        Vector2 prev = cam.to_screen(arc.centre.x + arc.radius_mm * std::cos(arc.start_rad),
                                     arc.centre.y + arc.radius_mm * std::sin(arc.start_rad));
        for (int i = 1; i <= steps; ++i) {
            const float a = arc.start_rad + sweep * static_cast<float>(i) / static_cast<float>(steps);
            const Vector2 next = cam.to_screen(arc.centre.x + arc.radius_mm * std::cos(a),
                                               arc.centre.y + arc.radius_mm * std::sin(a));
            DrawLineEx(prev, next, thickness, ring_color(arc.kind));
            prev = next;
        }
    }
}

}  // namespace wh40k::client
