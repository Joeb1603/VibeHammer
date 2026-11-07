#include "board_view.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/geometry/units.hpp"

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

void draw_model(const core::Model& model, Color tint, const BoardCamera& cam, bool dimmed, float bold_px) {
    // Bases are round in V1; base.diameter_mm is the true size, drawn to scale.
    const Vector2 c = cam.to_screen(model.position);
    const float r = cam.to_px(model.base.diameter_mm * 0.5f);
    const Color fill = dimmed ? mix(tint, kGrey, 0.8f) : tint;
    DrawCircleV(c, r, fill);
    if (bold_px > 0.0f) {
        DrawRing(c, std::max(0.0f, r - bold_px), r, 0.0f, 360.0f, 48, RAYWHITE);
    } else {
        DrawCircleLinesV(c, r, dimmed ? Color{95, 95, 100, 255} : RAYWHITE);
    }

    // D-028: a damaged model shows one pip per wound, filled for the wounds it has left. Models at full
    // wounds show nothing (the tooltip gives the numbers).
    const int wounds = model.characteristics.wounds;
    if (model.wounds_remaining < wounds && wounds > 0) {
        const int shown = std::min(wounds, 6);
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
            const float dx = p.x - model.position.x;
            const float dy = p.y - model.position.y;
            const float r = model.base.diameter_mm * 0.5f;
            if (dx * dx + dy * dy <= r * r) {
                return HoveredModel{&unit, &model};
            }
        }
    }
    return std::nullopt;
}

}  // namespace wh40k::client
