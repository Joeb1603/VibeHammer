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

void draw_model(const core::Unit& unit, const core::Model& model, const BoardCamera& cam) {
    // Bases are round in V1; base.diameter_mm is the true size, drawn to scale.
    const Vector2 c = cam.to_screen(model.position);
    const float r = cam.to_px(model.base.diameter_mm * 0.5f);
    const Color fill = side_color(unit.side);
    DrawCircleV(c, r, fill);
    DrawCircleLinesV(c, r, RAYWHITE);

    // Wounds remaining on multi-wound models.
    if (model.characteristics.wounds > 1) {
        const std::string text = std::to_string(model.wounds_remaining);
        const int size = std::clamp(static_cast<int>(r), 8, 40);
        const int width = MeasureText(text.c_str(), size);
        const Color colour = model.wounds_remaining < model.characteristics.wounds ? YELLOW : RAYWHITE;
        DrawText(text.c_str(), static_cast<int>(c.x) - width / 2, static_cast<int>(c.y) - size / 2, size, colour);
    }
}

}  // namespace

Color side_color(core::Side side) {
    return side == core::Side::A ? Color{52, 110, 200, 255} : Color{190, 60, 60, 255};
}

void draw_board(const core::GameState& state, const BoardCamera& cam) {
    const core::Rect board{0.0f, 0.0f, state.battlefield.width_mm, state.battlefield.depth_mm};
    DrawRectangleRec(cam.to_screen(board), kBoardFill);
    for (const core::Side side : {core::Side::A, core::Side::B}) {
        const core::Rect zone = core::zone_rect(state.battlefield, state.side(side).deployment_zone);
        DrawRectangleRec(cam.to_screen(zone), ColorAlpha(side_color(side), 0.18f));
    }
    draw_grid(state.battlefield, cam);
    DrawRectangleLinesEx(cam.to_screen(board), 2.0f, kEdge);

    for (const core::Unit& unit : state.units) {
        for (const core::Model& model : unit.models) {
            draw_model(unit, model, cam);
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
