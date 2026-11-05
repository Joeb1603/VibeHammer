#include "overlay_plan.hpp"

#include <algorithm>
#include <variant>

#include "core/geometry/coherency.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/units.hpp"

namespace wh40k::client {

namespace {

const core::Model* find_model(const core::GameState& state, core::ModelId id, const core::Unit** unit_out) {
    for (const core::Unit& u : state.units) {
        for (const core::Model& m : u.models) {
            if (m.id == id) {
                *unit_out = &u;
                return &m;
            }
        }
    }
    return nullptr;
}

}  // namespace

std::vector<Ring> plan_overlays(const core::GameState& state, std::optional<core::ModelId> selected_model,
                                const OverlayToggles& toggles) {
    std::vector<Ring> out;
    if (!state.pending) return out;
    const core::PendingDecision& pending = *state.pending;

    // While models are being moved: engagement range around the mover's enemies, coherency around the mover.
    if (const auto* d = std::get_if<core::decision::MoveModels>(&pending.kind)) {
        const core::Unit* mover = state.find_unit(d->unit);
        if (mover != nullptr) {
            if (toggles.engagement) {
                for (const core::Unit& u : state.units) {
                    if (u.side == mover->side) continue;
                    for (const core::Model& m : u.models) out.push_back({m.id, RingKind::Engagement, core::kEngagementRangeMm});
                }
            }
            if (toggles.coherency) {
                // Only the 2" clause: a 9" ring around every model is clutter (D-028).
                for (const core::Model& m : mover->models) out.push_back({m.id, RingKind::Coherency, core::kCoherencyNearMm});
            }
        }
    }

    // While choosing shots: the selected model's ranged weapon ranges, if it is the deciding side's.
    if (toggles.ranges && selected_model && std::holds_alternative<core::decision::Shoot>(pending.kind)) {
        const core::Unit* unit = nullptr;
        if (const core::Model* m = find_model(state, *selected_model, &unit); m != nullptr && unit->side == pending.side) {
            std::vector<float> ranges;
            for (const core::Weapon& w : m->weapons) {
                for (const core::WeaponProfile& p : w.profiles) {
                    if (p.range_mm && std::find(ranges.begin(), ranges.end(), *p.range_mm) == ranges.end()) {
                        ranges.push_back(*p.range_mm);
                    }
                }
            }
            for (const float r : ranges) out.push_back({m->id, RingKind::WeaponRange, r});
        }
    }

    // In the Charge phase: the charge roll around a unit that has rolled.
    if (toggles.charge && core::phase_of(state.flow.stage) == core::Phase::Charge) {
        for (const core::Unit& u : state.units) {
            if (!u.phase_flags.charge_roll) continue;
            const float roll = core::inches_to_mm(static_cast<float>(*u.phase_flags.charge_roll));
            for (const core::Model& m : u.models) out.push_back({m.id, RingKind::ChargeDistance, roll});
        }
    }
    return out;
}

}  // namespace wh40k::client
