#include "overlay_plan.hpp"

#include <algorithm>
#include <variant>

#include "core/engine/movement.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
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
            // Movement range: the model's base can end anywhere inside this bubble round its start position
            // (03.01: measured from the same point on the base; a new proposal replaces the whole path). A
            // charge move has the charge distance ring instead.
            const bool charge = state.flow.move && state.flow.move->type == core::MoveType::Charge;
            if (toggles.move_range && !charge) {
                for (const core::Model& m : mover->models) {
                    const auto max = core::max_move_distance_mm(state, m.id);
                    const auto start = core::move_start_position(state, m.id);
                    if (max && start) out.push_back({m.id, RingKind::MoveRange, *max, *start});
                }
            }
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

std::vector<Circle> ring_circles(const core::GameState& state, const std::vector<Ring>& rings,
                                 const PositionOverrides& overrides) {
    std::vector<Circle> out;
    for (const Ring& ring : rings) {
        const core::Unit* unit = nullptr;
        const core::Model* m = find_model(state, ring.model, &unit);
        if (m == nullptr) continue;
        core::Position centre = ring.centre.value_or(m->position);
        for (const auto& [id, at] : overrides) {
            if (id == ring.model && !ring.centre) centre = at;
        }
        out.push_back(Circle{centre, core::base_radius(m->base) + ring.distance_mm, ring.kind});
    }
    return out;
}

std::string_view ring_name(RingKind kind) {
    switch (kind) {
        case RingKind::MoveRange: return "Movement range";
        case RingKind::Engagement: return "Engagement range";
        case RingKind::Coherency: return "Coherency";
        case RingKind::WeaponRange: return "Weapon ranges";
        case RingKind::ChargeDistance: return "Charge distance";
    }
    return "";
}

std::string_view ring_meaning(RingKind kind) {
    switch (kind) {
        case RingKind::MoveRange:
            return "how far the moving unit's models can go: each model's base can end anywhere inside this "
                   "line, measured from where it started the move (03.01)";
        case RingKind::Engagement:
            return "2\" around the enemy models: a model inside it is engaged, and a Normal or Advance move "
                   "can't end there (03.04)";
        case RingKind::Coherency:
            return "2\" around each model of the moving unit: every model must end within 2\" of another "
                   "model of its unit (03.03)";
        case RingKind::WeaponRange: return "the selected model's weapon ranges, while choosing shots";
        case RingKind::ChargeDistance:
            return "the charge roll around a unit that rolled to charge: how far it can move (11.02)";
    }
    return "";
}

}  // namespace wh40k::client
