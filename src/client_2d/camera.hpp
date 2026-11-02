#pragma once

#include <raylib.h>

#include "core/geometry/primitives.hpp"

namespace wh40k::client {

// The one place where battlefield millimetres and screen pixels meet.
// World: mm, origin at the battlefield's south-west corner, +y north (core geometry convention).
// Screen: pixels, origin top-left, +y down.
class BoardCamera {
public:
    static constexpr float kMinPxPerMm = 0.2f;
    static constexpr float kMaxPxPerMm = 40.0f;

    // Fits a board of the given size into `viewport`, centred, with a small margin.
    void fit(float board_width_mm, float board_depth_mm, Rectangle viewport);

    [[nodiscard]] Vector2 to_screen(float x_mm, float y_mm) const;
    [[nodiscard]] Vector2 to_screen(const core::Position& p) const { return to_screen(p.x, p.y); }
    [[nodiscard]] core::Position to_world(Vector2 screen) const;
    [[nodiscard]] float to_px(float mm) const { return mm * px_per_mm_; }
    // Screen rectangle covering a world rectangle.
    [[nodiscard]] Rectangle to_screen(const core::Rect& r) const;

    [[nodiscard]] float px_per_mm() const { return px_per_mm_; }
    // Sets the zoom, keeping the world point under `anchor` fixed on screen (zoom to cursor).
    void set_zoom(float px_per_mm, Vector2 anchor);
    void zoom_by(float factor, Vector2 anchor) { set_zoom(px_per_mm_ * factor, anchor); }
    void pan(Vector2 delta_px);

private:
    Vector2 origin_{0.0f, 0.0f};  // screen position of world (0, 0)
    float px_per_mm_ = 1.0f;
};

}  // namespace wh40k::client
