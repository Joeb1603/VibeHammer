#include "board_view.hpp"

#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "base_shape.hpp"
#include "core/geometry/units.hpp"
#include "core/state/queries.hpp"

namespace wh40k::client {

namespace {

const Color kBoardFill{46, 58, 48, 255};
const Color kMinorGrid{70, 84, 72, 255};
const Color kMajorGrid{120, 138, 122, 255};
const Color kEdge{200, 210, 200, 255};

void draw_grid(const core::Battlefield& field, const BoardCamera& cam) {
    // 1" minor lines, 6" major lines.
    const int columns = static_cast<int>(std::lround(core::mm_to_inches(field.width_mm)));
    const int rows = static_cast<int>(std::lround(core::mm_to_inches(field.depth_mm)));
    for (int i = 0; i <= columns; ++i) {
        const float x = core::inches_to_mm(static_cast<float>(i));
        const bool major = i % 6 == 0;
        DrawLineEx(cam.to_screen(x, 0.0f), cam.to_screen(x, field.depth_mm), major ? 2.0f : 1.0f,
                   major ? kMajorGrid : kMinorGrid);
    }
    for (int i = 0; i <= rows; ++i) {
        const float y = core::inches_to_mm(static_cast<float>(i));
        const bool major = i % 6 == 0;
        DrawLineEx(cam.to_screen(0.0f, y), cam.to_screen(field.width_mm, y), major ? 2.0f : 1.0f,
                   major ? kMajorGrid : kMinorGrid);
    }
}

const Color kGrey{55, 55, 60, 255};

Color mix(Color a, Color b, float t) {
    auto channel = [t](unsigned char x, unsigned char y) {
        return static_cast<unsigned char>(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t);
    };
    return Color{channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a)};
}

// D-075: past W 6, a bar filled to the wounds left with "7/12" under it, across the base's lower half (the unit's
// number sits at the centre, D-065).
void draw_wound_bar(const WoundDisplay& w, Vector2 c, float half_width_px) {
    const float width = std::max(18.0f, half_width_px * 1.5f);
    const float height = std::clamp(half_width_px * 0.18f, 4.0f, 10.0f);
    const float below = std::max(8.0f, half_width_px * 0.45f);
    const Rectangle back{c.x - width * 0.5f, c.y + below - height * 0.5f, width, height};
    DrawRectangleRec(Rectangle{back.x - 1.0f, back.y - 1.0f, back.width + 2.0f, back.height + 2.0f}, BLACK);
    DrawRectangleRec(back, Color{60, 20, 20, 255});
    DrawRectangleRec(Rectangle{back.x, back.y, back.width * w.fraction(), back.height}, Color{120, 230, 120, 255});
    const int size = 10 * std::max(1, static_cast<int>(std::lround(std::clamp(half_width_px * 0.4f, 10.0f, 30.0f) / 10.0f)));
    const int tw = MeasureText(w.text.c_str(), size);
    const int tx = static_cast<int>(std::lround(c.x)) - tw / 2;
    const int ty = static_cast<int>(std::lround(back.y + back.height)) + 2;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx != 0 || dy != 0) DrawText(w.text.c_str(), tx + dx, ty + dy, size, BLACK);
        }
    }
    DrawText(w.text.c_str(), tx, ty, size, RAYWHITE);
}

void draw_model(const core::Model& model, Color tint, const BoardCamera& cam, bool dimmed, float bold_px) {
    // base.diameter_mm (an oval's width) and length_mm are the true size, drawn to scale (D-074 part 3: an oval at its
    // facing).
    const Vector2 c = cam.to_screen(model.position);
    const float r = cam.to_px(model.base.diameter_mm * 0.5f);
    const Color fill = dimmed ? mix(tint, kGrey, 0.8f) : tint;
    if (model.base.shape == core::BaseShape::Round) {
        DrawCircleV(c, r, fill);
        if (bold_px > 0.0f) {
            DrawRing(c, std::max(0.0f, r - bold_px), r, 0.0f, 360.0f, 48, RAYWHITE);
        } else {
            DrawCircleLinesV(c, r, dimmed ? Color{95, 95, 100, 255} : RAYWHITE);
        }
    } else {
        const core::BasePose pose = core::pose_of(model);
        fill_base(cam, pose, fill);
        if (bold_px > 0.0f) {
            draw_base_outline(cam, pose, -0.5f * bold_px, bold_px, RAYWHITE);
        } else {
            draw_base_outline(cam, pose, 0.0f, 1.0f, dimmed ? Color{95, 95, 100, 255} : RAYWHITE);
        }
    }

    // D-028: a damaged model shows one pip per wound, filled for the wounds it has left. Models at full
    // wounds show nothing (the tooltip gives the numbers). D-075: past W 6, a bar and a number.
    const WoundDisplay w = wound_display(model.wounds_remaining, model.characteristics.wounds);
    if (w.kind == WoundDisplay::Kind::Bar) {
        draw_wound_bar(w, c, r);
    } else if (w.kind == WoundDisplay::Kind::Pips) {
        const int shown = w.total;
        const float pip = std::max(3.0f, r * 0.24f);
        const float step = pip * 2.4f;
        const float x0 = c.x - step * static_cast<float>(shown - 1) * 0.5f;
        for (int i = 0; i < shown; ++i) {
            const Vector2 p{x0 + step * static_cast<float>(i), c.y};
            if (i < model.wounds_remaining) {
                DrawCircleV(p, pip, RAYWHITE);
            } else {
                DrawCircleLinesV(p, pip, RAYWHITE);
            }
        }
    }
}

// D-065: the unit's per-side number at the centre of its models, outlined in black so it reads on any shade.
// It scales with the bases (zoom) within limits set by the UI scale, and is left out when the bases are
// too small on screen for a number to read.
void draw_unit_label(const core::Unit& unit, const std::string& label, const BoardCamera& cam, bool dimmed,
                     float ui_scale) {
    if (label.empty() || unit.models.empty()) return;
    float x = 0.0f;
    float y = 0.0f;
    float radius_mm = 0.0f;
    for (const core::Model& m : unit.models) {
        x += m.position.x;
        y += m.position.y;
        radius_mm = std::max(radius_mm, m.base.diameter_mm * 0.5f);
    }
    const float n = static_cast<float>(unit.models.size());
    const float r_px = cam.to_px(radius_mm);
    if (r_px < 4.0f) return;  // zoomed far out: a number would be a smudge
    // raylib's default font is a 10 px bitmap: whole multiples of 10 stay crisp.
    const float wanted = std::clamp(r_px * 1.6f, 16.0f * ui_scale, 30.0f * ui_scale);
    const int size = 10 * std::max(1, static_cast<int>(std::lround(wanted / 10.0f)));
    const Vector2 c = cam.to_screen(x / n, y / n);
    const int w = MeasureText(label.c_str(), size);
    const int tx = static_cast<int>(std::lround(c.x)) - w / 2;
    const int ty = static_cast<int>(std::lround(c.y)) - size / 2;
    const int o = std::max(1, size / 10);
    for (int dx = -o; dx <= o; dx += o) {
        for (int dy = -o; dy <= o; dy += o) {
            if (dx != 0 || dy != 0) DrawText(label.c_str(), tx + dx, ty + dy, size, BLACK);
        }
    }
    DrawText(label.c_str(), tx, ty, size, dimmed ? Color{185, 185, 190, 255} : RAYWHITE);
}

}  // namespace

void fill_base(const BoardCamera& cam, const core::BasePose& pose, Color colour, float grow_px) {
    if (pose.base.shape == core::BaseShape::Round) {
        DrawCircleV(cam.to_screen(pose.position), cam.to_px(pose.base.diameter_mm * 0.5f) + grow_px, colour);
        return;
    }
    const float px = cam.px_per_mm();
    const std::vector<core::Position> pts = offset_outline(pose, px > 0.0f ? grow_px / px : 0.0f, 64);
    std::vector<Vector2> fan;
    fan.reserve(pts.size() + 2);
    fan.push_back(cam.to_screen(pose.position));
    for (const core::Position& p : pts) fan.push_back(cam.to_screen(p));
    fan.push_back(fan[1]);
    rlDisableBackfaceCulling();  // the screen's y runs down, so the world's counter-clockwise outline turns round
    DrawTriangleFan(fan.data(), static_cast<int>(fan.size()), colour);
    rlEnableBackfaceCulling();
}

void draw_world_loop(const BoardCamera& cam, const std::vector<core::Position>& points, float thick_px, Color colour) {
    if (points.size() < 2) return;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const core::Position& a = points[i];
        const core::Position& b = points[(i + 1) % points.size()];
        DrawLineEx(cam.to_screen(a), cam.to_screen(b), thick_px, colour);
    }
}

void draw_base_outline(const BoardCamera& cam, const core::BasePose& pose, float grow_px, float thick_px,
                       Color colour) {
    if (pose.base.shape == core::BaseShape::Round) {
        const float r = cam.to_px(pose.base.diameter_mm * 0.5f) + grow_px;
        if (thick_px <= 1.0f) {
            DrawCircleLinesV(cam.to_screen(pose.position), r, colour);
        } else {
            DrawRing(cam.to_screen(pose.position), std::max(0.0f, r - 0.5f * thick_px), r + 0.5f * thick_px, 0.0f,
                     360.0f, 48, colour);
        }
        return;
    }
    const float px = cam.px_per_mm();
    draw_world_loop(cam, offset_outline(pose, px > 0.0f ? grow_px / px : 0.0f, 72), thick_px, colour);
}

void draw_base_cross(const BoardCamera& cam, const core::BasePose& pose, float thick_px, Color colour) {
    // The cross's arms reach 0.7 of the way to the edge along the diagonals of the base's axes.
    const BaseAxes ax = pose.base.shape == core::BaseShape::Round ? BaseAxes{} : base_axes(pose.facing_deg);
    const float a = 0.35f * (pose.base.shape == core::BaseShape::Round ? pose.base.diameter_mm
                                                                       : std::max(pose.base.length_mm, pose.base.diameter_mm));
    const float b = 0.35f * pose.base.diameter_mm;
    const auto at = [&](float u, float v) {
        return cam.to_screen(pose.position.x + u * static_cast<float>(ax.ux) + v * static_cast<float>(ax.vx),
                             pose.position.y + u * static_cast<float>(ax.uy) + v * static_cast<float>(ax.vy));
    };
    DrawLineEx(at(-a, -b), at(a, b), thick_px, colour);
    DrawLineEx(at(-a, b), at(a, -b), thick_px, colour);
}

Color to_color(Rgb rgb) { return Color{rgb.r, rgb.g, rgb.b, 255}; }

Color side_color(core::Side side) { return to_color(side_rgb(side)); }

Color unit_color(const core::GameState& state, core::UnitId id) { return to_color(unit_tint(state, id)); }

void draw_board(const core::GameState& state, const BoardCamera& cam,
                const std::function<bool(core::UnitId)>& dimmed, std::optional<core::UnitId> emphasised,
                float ui_scale, const std::function<bool(core::UnitId, core::ModelId)>& model_dimmed) {
    const core::Rect board{0.0f, 0.0f, state.battlefield.width_mm, state.battlefield.depth_mm};
    DrawRectangleRec(cam.to_screen(board), kBoardFill);
    for (const core::Side side : {core::Side::A, core::Side::B}) {
        const core::Rect zone = core::zone_rect(state.battlefield, state.side(side).deployment_zone);
        DrawRectangleRec(cam.to_screen(zone), ColorAlpha(side_color(side), 0.18f));
    }
    draw_grid(state.battlefield, cam);
    DrawRectangleLinesEx(cam.to_screen(board), 2.0f, kEdge);

    for (const core::Unit& unit : state.units) {
        const bool grey = dimmed && dimmed(unit.id);
        const Color tint = unit_color(state, unit.id);  // D-065
        for (const core::Model& model : unit.models) {
            const bool model_grey = grey || (model_dimmed && model_dimmed(unit.id, model.id));
            draw_model(model, tint, cam, model_grey, emphasised == unit.id && !model_grey ? 3.0f * ui_scale : 0.0f);
        }
    }
    for (const core::Unit& unit : state.units) {  // labels on top of every base
        if (const auto style = unit_style(state, unit.id)) {
            draw_unit_label(unit, style->label, cam, dimmed && dimmed(unit.id), ui_scale);
        }
    }
}

std::optional<HoveredModel> model_at(const core::GameState& state, const BoardCamera& cam, Vector2 screen) {
    const core::Position p = cam.to_world(screen);
    for (const core::Unit& unit : state.units) {
        for (const core::Model& model : unit.models) {
            if (base_contains(core::pose_of(model), p)) return HoveredModel{&unit, &model};  // D-074 part 3
        }
    }
    return std::nullopt;
}

}  // namespace wh40k::client
