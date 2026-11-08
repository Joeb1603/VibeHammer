#include "core/geometry/path.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "core/geometry/distance.hpp"

namespace wh40k::core {

namespace {

// Shortest distance from point p to segment [a, b] on the battlefield plane.
float point_segment_distance(const Position& p, const Position& a, const Position& b) {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float len2 = abx * abx + aby * aby;
    float t = 0.0f;
    if (len2 > 0.0f) {
        t = std::clamp(((p.x - a.x) * abx + (p.y - a.y) * aby) / len2, 0.0f, 1.0f);
    }
    return centre_distance(p, Position{a.x + t * abx, a.y + t * aby, a.z});
}

std::string model_name(ModelId id) { return "model " + std::to_string(id.v); }

}  // namespace

float path_length(std::span<const Position> points) {
    float total = 0.0f;
    for (std::size_t i = 1; i < points.size(); ++i) {
        total += centre_distance(points[i - 1], points[i]);
    }
    return total;
}

std::vector<PathViolation> check_model_path(ModelId mover, const Base& base,
                                            std::span<const Position> points,
                                            const PathContext& context) {
    if (points.empty()) {
        throw std::logic_error("check_model_path: a path needs at least its start position");
    }
    std::vector<PathViolation> out;
    const float r = base_radius(base);

    // 03.01: its base cannot be moved through enemy models. The base sweeps a capsule along each
    // segment; it passes through an enemy base if they overlap by more than EPS_MM. Bases may touch.
    if (!context.may_move_through_enemy_models) {
        for (const PlacedModel& e : context.enemy) {
            if (e.id == mover) {
                continue;
            }
            const float limit = r + base_radius(e.base) - EPS_MM;
            for (std::size_t s = 0; s + 1 < points.size(); ++s) {
                if (point_segment_distance(e.position, points[s], points[s + 1]) < limit) {
                    out.push_back({PathViolationKind::CrossesEnemyModel, e.id, s,
                                   "03.01: " + model_name(mover) + "'s base cannot be moved through enemy " +
                                       model_name(e.id) + " (segment " + std::to_string(s + 1) + ")"});
                    break;  // one report per enemy model
                }
            }
        }
    }

    // 03.01: its base cannot cross the edge of the battlefield. The battlefield is convex, so the
    // swept base stays on it iff the base is wholly on it at every path point.
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (!wholly_within(context.battlefield, points[i], base)) {
            std::optional<std::size_t> seg;
            if (i > 0) {
                seg = i - 1;
            }
            out.push_back({PathViolationKind::LeavesBattlefield, std::nullopt, seg,
                           "03.01: " + model_name(mover) + "'s base cannot cross the edge of the battlefield"});
            break;
        }
    }

    // 03.01 Ending a Move: no model may be on another model.
    const Position& end = points.back();
    auto check_end = [&](std::span<const PlacedModel> models) {
        for (const PlacedModel& m : models) {
            if (m.id != mover && bases_overlap(end, base, m.position, m.base)) {
                out.push_back({PathViolationKind::EndsOnModel, m.id, std::nullopt,
                               "03.01: " + model_name(mover) + " cannot end its move on " +
                                   model_name(m.id)});
            }
        }
    };
    check_end(context.friendly);
    check_end(context.enemy);
    check_end(context.passable_enemy);  // 17.01 passes through them, but a move still can't end on them (D-073)
    return out;
}

}  // namespace wh40k::core
