// 14 Objectives and the D-084 mission (M86): the control checks at the end of each phase and turn (14.02, 14.03)
// and the Command phase scoring. The queries they use are in core/state/objectives.hpp.

#include "core/engine/internal.hpp"
#include "core/state/objectives.hpp"

namespace wh40k::core::detail {

void update_objective_control(GameState& state, bool end_of_phase, Events& events) {
    if (!state.mission) return;
    for (std::size_t i = 0; i < state.mission->objectives.size(); ++i) {
        // Levels first (they read the whole state), then the area is updated.
        const std::array<int, 2> levels = level_of_control(state, state.mission->objectives[i]);
        ObjectiveArea& area = state.mission->objectives[i];
        const ControlOutcome next = determine_control(area, levels, end_of_phase);
        const bool changed = next.controller != area.controller;
        area.controller = next.controller;
        area.secured_by = next.secured_by;
        if (changed) events.push_back(event::ObjectiveControlChanged{i, area.name, area.controller, levels});
    }
}

void score_objectives(GameState& state, Side side, Events& events) {
    if (!state.mission) return;
    Mission& m = *state.mission;
    if (state.flow.battle_round < m.first_scoring_round) return;  // D-084: battle rounds 2-5
    for (std::size_t i = 0; i < m.objectives.size(); ++i) {
        const ObjectiveArea& area = m.objectives[i];
        if (area.controller != side) continue;
        const int vp = area.vp[side_index(side)];
        if (vp <= 0) continue;
        m.objective_vp[side_index(side)] += vp;
        events.push_back(event::VictoryPointsScored{side, i, area.name, vp, m.objective_vp[side_index(side)]});
    }
}

}  // namespace wh40k::core::detail
