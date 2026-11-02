#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/geometry/primitives.hpp"
#include "core/model/unit.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

enum class BattlefieldEdge : std::uint8_t { North, South, East, West };

struct Battlefield {
    float width_mm = 0.0f;  // along x (west -> east)
    float depth_mm = 0.0f;  // along y (south -> north)
    friend bool operator==(const Battlefield&, const Battlefield&) = default;
};

// A deployment zone along one battlefield edge, `depth_mm` deep, spanning the full edge.
struct DeploymentZone {
    BattlefieldEdge edge = BattlefieldEdge::South;
    float depth_mm = 0.0f;
    friend bool operator==(const DeploymentZone&, const DeploymentZone&) = default;
};

[[nodiscard]] Rect zone_rect(const Battlefield& field, const DeploymentZone& zone);

struct SideInfo {
    std::string name;
    DeploymentZone deployment_zone;
    friend bool operator==(const SideInfo&, const SideInfo&) = default;
};

// The full game state: a plain, copyable, comparable, JSON-serializable value.
struct GameState {
    std::string scenario_name;
    Battlefield battlefield;
    int battle_rounds = 5;
    Side first_turn = Side::A;
    std::array<SideInfo, 2> sides;  // indexed by side_index(Side)
    std::vector<Unit> units;        // ordered by UnitId
    DiceState dice;                 // production PRNG (SeededDice) seed and position

    [[nodiscard]] const SideInfo& side(Side s) const { return sides[side_index(s)]; }
    [[nodiscard]] const Unit* find_unit(UnitId id) const;
    [[nodiscard]] Unit* find_unit(UnitId id);

    friend bool operator==(const GameState&, const GameState&) = default;
};

}  // namespace wh40k::core
