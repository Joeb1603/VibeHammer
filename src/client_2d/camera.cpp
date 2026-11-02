#include "camera.hpp"

#include <algorithm>

namespace wh40k::client {

void BoardCamera::fit(float board_width_mm, float board_depth_mm, Rectangle viewport) {
    constexpr float kMargin = 0.92f;
    px_per_mm_ = std::clamp(std::min(viewport.width / board_width_mm, viewport.height / board_depth_mm) * kMargin,
                            kMinPxPerMm, kMaxPxPerMm);
    origin_.x = viewport.x + (viewport.width - board_width_mm * px_per_mm_) * 0.5f;
    origin_.y = viewport.y + (viewport.height + board_depth_mm * px_per_mm_) * 0.5f;  // south edge
}

Vector2 BoardCamera::to_screen(float x_mm, float y_mm) const {
    return Vector2{origin_.x + x_mm * px_per_mm_, origin_.y - y_mm * px_per_mm_};
}

core::Position BoardCamera::to_world(Vector2 screen) const {
    return core::Position{(screen.x - origin_.x) / px_per_mm_, (origin_.y - screen.y) / px_per_mm_, 0.0f};
}

Rectangle BoardCamera::to_screen(const core::Rect& r) const {
    const Vector2 top_left = to_screen(r.min_x, r.max_y);
    return Rectangle{top_left.x, top_left.y, (r.max_x - r.min_x) * px_per_mm_, (r.max_y - r.min_y) * px_per_mm_};
}

void BoardCamera::set_zoom(float px_per_mm, Vector2 anchor) {
    const core::Position world = to_world(anchor);
    px_per_mm_ = std::clamp(px_per_mm, kMinPxPerMm, kMaxPxPerMm);
    origin_.x = anchor.x - world.x * px_per_mm_;
    origin_.y = anchor.y + world.y * px_per_mm_;
}

void BoardCamera::pan(Vector2 delta_px) {
    origin_.x += delta_px.x;
    origin_.y += delta_px.y;
}

}  // namespace wh40k::client
