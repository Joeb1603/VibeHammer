#include "analysis/terrain_sight.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/geometry/distance.hpp"
#include "core/geometry/terrain_shape.hpp"
#include "core/state/terrain.hpp"

namespace wh40k::ai::analysis {

namespace {

bool overlaps(const core::Rect& a, const core::Rect& b) {
    return a.min_x <= b.max_x && b.min_x <= a.max_x && a.min_y <= b.max_y && b.min_y <= a.max_y;
}

core::Rect join(const core::Rect& a, const core::Rect& b) {
    return core::Rect{std::min(a.min_x, b.min_x), std::min(a.min_y, b.min_y), std::max(a.max_x, b.max_x),
                      std::max(a.max_y, b.max_y)};
}

// A 1 mm line (06.01) between two bases stays within the box round both, grown by its half width.
constexpr float kLineSlackMm = 1.0f;

}  // namespace

core::Rect pose_box(const core::BasePose& pose) {
    const float r = core::circumscribed_radius(pose.base) + core::EPS_MM;
    return core::Rect{pose.position.x - r, pose.position.y - r, pose.position.x + r, pose.position.y + r};
}

TerrainSight::TerrainSight(const core::GameState& state) : state_(state), view_(state), empty_(state.terrain.empty()) {
    for (std::size_t i = 0; i < state.terrain.size(); ++i) {
        if (core::has_dense(state.terrain[i])) dense_ |= std::uint32_t{1} << i;
        bounds_.push_back(core::shape_bounds(state.terrain[i].shape));
    }
}

std::uint32_t TerrainSight::mask_at(const core::BasePose& pose) const {
    std::uint32_t mask = 0;
    if (empty_) return mask;
    const core::Rect box = pose_box(pose);
    for (std::size_t i = 0; i < state_.terrain.size(); ++i) {
        if (overlaps(box, bounds_[i]) && core::model_within_area(pose, state_.terrain[i])) {
            mask |= std::uint32_t{1} << i;
        }
    }
    return mask;
}

bool TerrainSight::hidden_now(const core::Unit& unit, const core::Model& model) {
    return !empty_ && view_.hidden(unit, model);
}

namespace {

// D-090: a 1 mm line (06.01) from p to q crosses a shape when it comes within half its width of it.
constexpr double kHalfLineMm = 0.5;

bool clear(const core::Position& p, const core::Position& q, const std::vector<const core::TerrainShape*>& shapes) {
    const core::oval_detail::Vec a{p.x, p.y};
    const core::oval_detail::Vec b{q.x, q.y};
    for (const core::TerrainShape* s : shapes) {
        if (core::segment_shape_distance(a, b, *s) < kHalfLineMm) return false;
    }
    return true;
}

// Three points across a base, square to the line between the two bases (`nx`, `ny`: the unit normal): its centre and
// near either side (inside the base, so a line from them is a line from the model).
std::array<core::Position, 3> across(const core::BasePose& pose, float nx, float ny) {
    const float r = 0.9f * core::inscribed_radius(pose.base);
    const core::Position& c = pose.position;
    return {c, core::Position{c.x + nx * r, c.y + ny * r, 0.0f}, core::Position{c.x - nx * r, c.y - ny * r, 0.0f}};
}

}  // namespace

std::vector<const core::TerrainShape*> TerrainSight::near(std::span<const core::TerrainShape* const> shapes,
                                                          const core::Rect& box) {
    std::vector<const core::TerrainShape*> out;
    for (const core::TerrainShape* s : shapes) {
        if (overlaps(core::shape_bounds(*s), box)) out.push_back(s);
    }
    return out;
}

SightLine TerrainSight::line(const core::BasePose& from, std::uint32_t from_mask, const core::BasePose& to,
                             std::uint32_t to_mask, bool to_hidden, bool need_full) {
    SightLine out;
    if (empty_) return out;
    // 13.09: a hidden model is visible only to models within its 15" detection range (edge to edge, D-009).
    if (to_hidden && core::more_than(core::base_distance(from, to), core::kDetectionRangeMm)) {
        out.visible = false;
        out.fully_visible = false;
        return out;
    }
    core::Rect box = join(pose_box(from), pose_box(to));
    box.min_x -= kLineSlackMm;
    box.min_y -= kLineSlackMm;
    box.max_x += kLineSlackMm;
    box.max_y += kLineSlackMm;
    const std::uint32_t mask = from_mask | to_mask;
    const std::vector<const core::TerrainShape*> sight = near(view_.sight_blockers(mask), box);
    const std::vector<const core::TerrainShape*> cover =
        need_full ? near(view_.cover_blockers(mask), box) : std::vector<const core::TerrainShape*>{};
    if (sight.empty() && cover.empty()) return out;
    // D-090: an estimate, not core's exact search (the engine decides every real shot): three parallel lines, from
    // the centres and from near either side of both bases.
    float dx = to.position.x - from.position.x;
    float dy = to.position.y - from.position.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 1e-3f) {
        dx /= len;
        dy /= len;
    } else {
        dx = 0.0f;
        dy = 1.0f;
    }
    const float nx = -dy;
    const float ny = dx;
    const std::array<core::Position, 3> a = across(from, nx, ny);
    const std::array<core::Position, 3> b = across(to, nx, ny);
    if (!sight.empty()) {
        out.visible = clear(a[0], b[0], sight) || clear(a[1], b[1], sight) || clear(a[2], b[2], sight);
    }
    if (!out.visible) {
        out.fully_visible = false;
        return out;
    }
    if (need_full && !cover.empty()) {
        // D-088 Fully Visible, estimated: both sides of the target and its point nearest the observer, each reached
        // from some point of the observer past the intervening terrain.
        const float r = 0.9f * core::inscribed_radius(to.base);
        const std::array<core::Position, 3> parts = {
            b[1], b[2], core::Position{to.position.x - dx * r, to.position.y - dy * r, 0.0f}};
        for (const core::Position& q : parts) {
            if (!clear(a[0], q, cover) && !clear(a[1], q, cover) && !clear(a[2], q, cover)) {
                out.fully_visible = false;
                break;
            }
        }
    }
    return out;
}

std::vector<std::size_t> sight_samples(const std::vector<core::Position>& positions, const core::Position& towards,
                                       std::size_t n) {
    std::vector<std::size_t> out;
    if (positions.size() <= n) {
        for (std::size_t i = 0; i < positions.size(); ++i) out.push_back(i);
        return out;
    }
    if (n == 0) return out;
    core::Position c;
    for (const core::Position& p : positions) {
        c.x += p.x;
        c.y += p.y;
    }
    c.x /= static_cast<float>(positions.size());
    c.y /= static_cast<float>(positions.size());
    float dx = towards.x - c.x;
    float dy = towards.y - c.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 1e-3f) {
        dx /= len;
        dy /= len;
    } else {
        dx = 0.0f;
        dy = 1.0f;
    }
    std::size_t closest = 0;
    std::size_t left = 0;
    std::size_t right = 0;
    float best_d = 0.0f;
    float best_l = 0.0f;
    float best_r = 0.0f;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const float px = positions[i].x;
        const float py = positions[i].y;
        const float d = (px - towards.x) * (px - towards.x) + (py - towards.y) * (py - towards.y);
        const float side = (px - c.x) * -dy + (py - c.y) * dx;  // across the line, left positive
        if (i == 0 || d < best_d) {
            best_d = d;
            closest = i;
        }
        if (i == 0 || side > best_l) {
            best_l = side;
            left = i;
        }
        if (i == 0 || side < best_r) {
            best_r = side;
            right = i;
        }
    }
    for (const std::size_t i : {closest, left, right}) {
        if (out.size() < n && std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
    }
    // Fill up (n above 3, or repeats) in index order.
    for (std::size_t i = 0; i < positions.size() && out.size() < n; ++i) {
        if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
    }
    return out;
}

}  // namespace wh40k::ai::analysis
