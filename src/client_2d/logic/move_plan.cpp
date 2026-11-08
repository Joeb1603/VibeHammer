#include "move_plan.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/path.hpp"
#include "core/geometry/pose.hpp"
#include "core/geometry/units.hpp"
#include "core/state/queries.hpp"

namespace wh40k::client {

namespace {

const core::ModelPath* path_of(const core::GameState& state, core::ModelId model) {
    if (!state.flow.move) return nullptr;
    for (const core::ModelPath& p : state.flow.move->paths) {
        if (p.model == model) return &p;
    }
    return nullptr;
}

// "45°" (a whole number of degrees, else one decimal).
std::string degrees(float deg) {
    char buf[32];
    const float r = std::round(deg);
    if (std::fabs(deg - r) < 0.05f) {
        std::snprintf(buf, sizeof buf, "%.0f\u00b0", static_cast<double>(r));
    } else {
        std::snprintf(buf, sizeof buf, "%.1f\u00b0", static_cast<double>(deg));
    }
    return buf;
}

}  // namespace

std::vector<core::Position> model_path(const core::GameState& state, core::ModelId model) {
    if (!state.flow.move) return {};
    for (const core::ModelPath& p : state.flow.move->paths) {
        if (p.model == model) return p.points;
    }
    if (const auto start = core::move_start_position(state, model)) return {*start};
    return {};
}

std::vector<float> model_path_facings(const core::GameState& state, core::ModelId model) {
    if (!state.flow.move) return {};
    const std::optional<float> start = core::move_start_facing_deg(state, model);
    if (const core::ModelPath* p = path_of(state, model)) {
        if (!p->facings.empty()) return p->facings;
        return std::vector<float>(p->points.size(), start.value_or(0.0f));
    }
    if (core::move_start_position(state, model)) return {start.value_or(0.0f)};
    return {};
}

core::action::ProposeModelMove extend_path(const core::GameState& state, core::ModelId model, core::Position to) {
    std::vector<core::Position> points = model_path(state, model);
    core::action::ProposeModelMove a{model, {}};
    if (points.size() > 1) a.path.assign(points.begin() + 1, points.end());  // without the start position
    a.path.push_back(to);
    // D-074 part 3: a proposal replaces the whole path, and one without facings keeps the move's start facing, so the
    // turns so far go with it (the new line at the facing the model has now).
    if (const core::ModelPath* p = path_of(state, model); p != nullptr && !p->facings.empty()) {
        a.facings.assign(p->facings.begin() + 1, p->facings.end());
        a.facings.push_back(p->facings.back());
    }
    return a;
}

core::action::ProposeModelMove extend_path_turned(const core::GameState& state, core::ModelId model,
                                                  core::Position to, float facing_deg, bool turn_at_end) {
    const std::vector<core::Position> points = model_path(state, model);
    const std::vector<float> facings = model_path_facings(state, model);
    core::action::ProposeModelMove a{model, {}, {}};
    if (points.empty() || facings.size() != points.size()) {  // not moving: the engine says why
        a.path.push_back(to);
        a.facings.push_back(facing_deg);
        return a;
    }
    a.path.assign(points.begin() + 1, points.end());
    a.facings.assign(facings.begin() + 1, facings.end());
    const core::Position& here = points.back();
    if (to == here) {
        a.path.push_back(here);  // a turn in place (03.01: rotating does not count towards the distance)
        a.facings.push_back(facing_deg);
    } else if (turn_at_end) {
        a.path.push_back(to);  // the line at the facing it has now, then the turn at its end
        a.facings.push_back(facings.back());
        a.path.push_back(to);
        a.facings.push_back(facing_deg);
    } else {
        a.path.push_back(to);  // the turn at the line's start, then the line
        a.facings.push_back(facing_deg);
    }
    return a;
}

TurnedDrop turned_drop(const core::GameState& state, core::ModelId model, core::Position to, float facing_deg) {
    const core::Model* m = core::find_model(state, model);
    if (m == nullptr || m->base.shape == core::BaseShape::Round ||
        core::footprint_turn_deg(m->facing_deg, facing_deg) == 0.0) {
        return TurnedDrop{extend_path(state, model, to), TurnedDrop::How::NoTurn};
    }
    const std::vector<core::Position> points = model_path(state, model);
    if (!points.empty() && to == points.back()) {
        return TurnedDrop{extend_path_turned(state, model, to, facing_deg, false), TurnedDrop::How::TurnInPlace};
    }
    TurnedDrop first{extend_path_turned(state, model, to, facing_deg, false), TurnedDrop::How::TurnThenMove};
    if (!state.pending) return first;
    const core::Side side = state.pending->side;
    const core::GameAction a = first.action;
    if (core::validate_action(state, side, a).ok()) return first;
    TurnedDrop second{extend_path_turned(state, model, to, facing_deg, true), TurnedDrop::How::MoveThenTurn};
    const core::GameAction b = second.action;
    if (core::validate_action(state, side, b).ok()) return second;
    return first;
}

float facings_turn_deg(const std::vector<float>& facings) {
    double total = 0.0;
    for (std::size_t i = 1; i < facings.size(); ++i) total += std::fabs(core::footprint_turn_deg(facings[i - 1], facings[i]));
    return static_cast<float>(total);
}

std::string describe_path(const std::vector<core::Position>& points, const std::vector<float>& facings, bool present) {
    const float distance = core::path_length(points);
    const std::string moved = std::string(present ? "moves " : "moved ") + core::format_inches(distance);
    const float turned = facings_turn_deg(facings);
    if (facings.size() != points.size() || !(turned >= 0.05f)) return moved;
    const std::string turn = std::string(present ? "turns " : "turned ") + degrees(turned);
    if (!core::more_than(distance, 0.0f)) return turn;
    // Where the turns are: before the first line that moves the model, after the last one, or between.
    std::size_t first_line = points.size();
    std::size_t last_line = 0;
    for (std::size_t k = 0; k + 1 < points.size(); ++k) {
        if (core::centre_distance(points[k], points[k + 1]) > 0.0f) {
            first_line = std::min(first_line, k);
            last_line = std::max(last_line, k);
        }
    }
    bool before = true;
    bool after = true;
    for (std::size_t k = 0; k + 1 < facings.size(); ++k) {
        if (core::footprint_turn_deg(facings[k], facings[k + 1]) == 0.0) continue;
        // The turn to facings[k + 1] is made at points[k], before line k.
        before = before && k <= first_line;
        after = after && k > last_line;
    }
    if (before) return turn + " then " + moved;
    if (after) return moved + " then " + turn;
    return turn + " and " + moved;
}

core::Position drag_drop(const core::Position& model_at_grab, const core::Position& grab, const core::Position& cursor) {
    return core::Position{model_at_grab.x + (cursor.x - grab.x), model_at_grab.y + (cursor.y - grab.y), model_at_grab.z};
}

std::vector<core::action::ProposeModelMove> translate_unit(const core::GameState& state, core::UnitId unit,
                                                           float dx_mm, float dy_mm) {
    std::vector<core::action::ProposeModelMove> out;
    const core::Unit* u = state.find_unit(unit);
    if (u == nullptr) return out;
    // D-027: a proposal may not end on a model at its current position, so the models furthest along the
    // move go first; for a rigid translation none of them then ends on a model still to move. Ties keep
    // unit order (stable sort), so the order is deterministic.
    std::vector<const core::Model*> order;
    for (const core::Model& m : u->models) order.push_back(&m);
    const auto ahead = [&](const core::Model* m) { return m->position.x * dx_mm + m->position.y * dy_mm; };
    std::stable_sort(order.begin(), order.end(),
                     [&](const core::Model* a, const core::Model* b) { return ahead(a) > ahead(b); });
    for (const core::Model* m : order) {
        const core::Position to{m->position.x + dx_mm, m->position.y + dy_mm, m->position.z};
        out.push_back(extend_path(state, m->id, to));
    }
    return out;
}

std::optional<Ruler> ruler(const core::GameState& state, core::ModelId model, std::optional<core::Position> to,
                           std::optional<float> facing_deg) {
    const auto max = core::max_move_distance_mm(state, model);
    std::vector<core::Position> points = model_path(state, model);
    if (!max || points.empty()) return std::nullopt;
    if (to) points.push_back(*to);
    Ruler r;
    r.moved_mm = core::path_length(points);
    r.max_mm = *max;
    r.over = core::more_than(r.moved_mm, r.max_mm);
    // D-074 part 3: the turns so far, and the drag's.
    std::vector<float> facings = model_path_facings(state, model);
    if (!facings.empty() && facing_deg) facings.push_back(*facing_deg);
    r.turned_deg = facings_turn_deg(facings);
    return r;
}

}  // namespace wh40k::client
