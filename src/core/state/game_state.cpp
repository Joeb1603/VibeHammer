#include "core/state/game_state.hpp"

#include <stdexcept>

namespace wh40k::core {

Rect zone_rect(const Battlefield& field, const DeploymentZone& zone) {
    switch (zone.edge) {
        case BattlefieldEdge::South:
            return Rect{0.0f, 0.0f, field.width_mm, zone.depth_mm};
        case BattlefieldEdge::North:
            return Rect{0.0f, field.depth_mm - zone.depth_mm, field.width_mm, field.depth_mm};
        case BattlefieldEdge::West:
            return Rect{0.0f, 0.0f, zone.depth_mm, field.depth_mm};
        case BattlefieldEdge::East:
            return Rect{field.width_mm - zone.depth_mm, 0.0f, field.width_mm, field.depth_mm};
    }
    throw std::logic_error("zone_rect: invalid BattlefieldEdge");
}

const Unit* GameState::find_unit(UnitId id) const {
    for (const Unit& u : units) {
        if (u.id == id) {
            return &u;
        }
    }
    return nullptr;
}

Unit* GameState::find_unit(UnitId id) {
    for (Unit& u : units) {
        if (u.id == id) {
            return &u;
        }
    }
    return nullptr;
}

}  // namespace wh40k::core
