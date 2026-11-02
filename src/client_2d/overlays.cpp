#include "overlays.hpp"

#include <algorithm>
#include <vector>

#include <raylib.h>

#include "core/geometry/coherency.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/units.hpp"

namespace wh40k::client {

namespace {

// A ring `distance_mm` out from the edge of the model's base.
void ring(const BoardCamera& cam, const core::Model& m, float distance_mm, Color colour, float thickness = 1.5f) {
    const float r = cam.to_px(m.base.diameter_mm * 0.5f + distance_mm);
    const Vector2 c = cam.to_screen(m.position);
    DrawRing(c, std::max(0.0f, r - thickness * 0.5f), r + thickness * 0.5f, 0.0f, 360.0f, 64, colour);
}

}  // namespace

void draw_overlays(const core::GameState& state, const BoardCamera& cam, const OverlayToggles& toggles,
                   std::optional<core::UnitId> selected_unit, std::optional<core::ModelId> selected_model,
                   core::Side friendly) {
    if (toggles.engagement) {
        for (const core::Unit& u : state.units) {
            if (u.side == friendly) continue;
            for (const core::Model& m : u.models) ring(cam, m, core::kEngagementRangeMm, Color{230, 60, 60, 170});
        }
    }
    const core::Unit* sel = selected_unit ? state.find_unit(*selected_unit) : nullptr;
    if (sel != nullptr && toggles.coherency) {
        for (const core::Model& m : sel->models) {
            ring(cam, m, core::kCoherencyNearMm, Color{120, 220, 120, 170});
            ring(cam, m, core::kCoherencyFarMm, Color{120, 220, 120, 60}, 1.0f);
        }
    }
    if (sel != nullptr && toggles.ranges && selected_model) {
        for (const core::Model& m : sel->models) {
            if (m.id != *selected_model) continue;
            std::vector<float> ranges;
            for (const core::Weapon& w : m.weapons) {
                for (const core::WeaponProfile& p : w.profiles) {
                    if (p.range_mm && std::find(ranges.begin(), ranges.end(), *p.range_mm) == ranges.end()) {
                        ranges.push_back(*p.range_mm);
                    }
                }
            }
            for (const float r : ranges) ring(cam, m, r, Color{240, 200, 80, 140});
        }
    }
    if (toggles.charge && core::phase_of(state.flow.stage) == core::Phase::Charge) {
        for (const core::Unit& u : state.units) {
            if (!u.phase_flags.charge_roll) continue;
            const float roll = core::inches_to_mm(static_cast<float>(*u.phase_flags.charge_roll));
            for (const core::Model& m : u.models) ring(cam, m, roll, Color{200, 120, 240, 150});
        }
    }
}

}  // namespace wh40k::client
