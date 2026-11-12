#pragma once

// D-084 (M86): the objective mission's state. Objective areas (14.01, 14.02) with their controller and the victory
// points scored so far. Plain value types, held in GameState::mission (JSON "mission", omitted when there is none).
// The rules that use them are in core/state/objectives.hpp.

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "core/geometry/primitives.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

// 14.01: a terrain objective with no terrain effects (no terrain yet, D-084): a circular area on the battlefield.
struct ObjectiveArea {
    std::string name;           // e.g. "Centre", "A home"
    Position centre;            // mm
    float diameter_mm = 0.0f;   // > 0
    std::array<int, 2> vp{};    // D-084: the VP each side scores for controlling it, indexed by side_index
    // 14.02: the player controlling it (nullopt: neither). Determined at the end of each phase and turn.
    std::optional<Side> controller;
    // 14.03: the player whose army has secured it (nullopt: none). Nothing secures an objective yet (state only).
    std::optional<Side> secured_by;
    friend bool operator==(const ObjectiveArea&, const ObjectiveArea&) = default;
};

// D-084: the mission. Every number comes from the scenario file's "mission" block.
struct Mission {
    // Each player scores in their own Command phase in battle rounds first_scoring_round .. battle_rounds.
    int first_scoring_round = 2;
    // Kill VP at the end of the battle: kill_vp per full kill_points points destroyed (D-076's exact tally).
    int kill_points = 50;
    int kill_vp = 1;
    std::vector<ObjectiveArea> objectives;
    std::array<int, 2> objective_vp{};  // the objective VP scored so far, indexed by side_index
    friend bool operator==(const Mission&, const Mission&) = default;
};

}  // namespace wh40k::core
