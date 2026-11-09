#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <optional>

#include "core/dice/dice.hpp"
#include "core/geometry/primitives.hpp"
#include "core/model/unit.hpp"
#include "core/state/decision.hpp"
#include "core/state/flow.hpp"
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

// How the game is decided when no side is wiped out (scenario "result"). D-007: a draw after the last battle round.
// D-076 (M67, amends D-007): the side that destroyed more points wins (core/state/points_destroyed.hpp).
enum class ResultRule : std::uint8_t { D007Draw, PointsDestroyed };

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
    // D-081 (M74): the units not yet set up (a deployment by hand), ordered by UnitId. They are not on the battlefield
    // and not in `units`, so no query sees them; setting one up moves it into `units` at its id position. Non-empty
    // only in Stage::NotStarted and Stage::Deploy. JSON "undeployed", omitted when empty.
    std::vector<Unit> undeployed;
    DiceState dice;                 // production PRNG (SeededDice) seed and position
    std::array<int, 2> command_points{};  // indexed by side_index; 08.02 (D-023: both start at 0)
    FlowState flow;                 // where the game is (07-12) and what is in progress
    std::optional<PendingDecision> pending;  // whose input the engine waits for; nullopt when not
                                             // started or over
    std::optional<GameResult> result;        // set when the game ends (D-007)
    // D-076: how the game is decided after the last battle round. JSON "result_rule", omitted at the default (D-007),
    // so every older file loads and saves unchanged. With PointsDestroyed every unit has points (Unit::points).
    ResultRule result_rule = ResultRule::D007Draw;

    [[nodiscard]] const SideInfo& side(Side s) const { return sides[side_index(s)]; }
    [[nodiscard]] const Unit* find_unit(UnitId id) const;
    [[nodiscard]] Unit* find_unit(UnitId id);

    friend bool operator==(const GameState&, const GameState&) = default;
};

}  // namespace wh40k::core
