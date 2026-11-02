#include "move_plan.hpp"

#include <algorithm>

#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/path.hpp"

namespace wh40k::client {

std::vector<core::Position> model_path(const core::GameState& state, core::ModelId model) {
    if (!state.flow.move) return {};
    for (const core::ModelPath& p : state.flow.move->paths) {
        if (p.model == model) return p.points;
    }
    if (const auto start = core::move_start_position(state, model)) return {*start};
    return {};
}

core::action::ProposeModelMove extend_path(const core::GameState& state, core::ModelId model, core::Position to) {
    std::vector<core::Position> points = model_path(state, model);
    core::action::ProposeModelMove a{model, {}};
    if (points.size() > 1) a.path.assign(points.begin() + 1, points.end());  // without the start position
    a.path.push_back(to);
    return a;
}

std::vector<core::action::ProposeModelMove> translate_unit(const core::GameState& state, core::UnitId unit,
                                                           float dx_mm, float dy_mm) {
    std::vector<core::action::ProposeModelMove> out;
    const core::Unit* u = state.find_unit(unit);
    if (u == nullptr) return out;
    for (const core::Model& m : u->models) {
        const core::Position to{m.position.x + dx_mm, m.position.y + dy_mm, m.position.z};
        out.push_back(extend_path(state, m.id, to));
    }
    return out;
}

std::optional<Ruler> ruler(const core::GameState& state, core::ModelId model, std::optional<core::Position> to) {
    const auto max = core::max_move_distance_mm(state, model);
    std::vector<core::Position> points = model_path(state, model);
    if (!max || points.empty()) return std::nullopt;
    if (to) points.push_back(*to);
    Ruler r;
    r.moved_mm = core::path_length(points);
    r.max_mm = *max;
    r.over = core::more_than(r.moved_mm, r.max_mm);
    return r;
}

}  // namespace wh40k::client
