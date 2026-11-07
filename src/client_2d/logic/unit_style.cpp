#include "unit_style.hpp"

#include <array>

namespace wh40k::client {

namespace {

// D-065: six shades per side, in the order they are handed out. Shade 1 is the pre-M55 side colour; the next
// ones change lightness first (light, then a hue step, then dark), so the first three or four units of a side
// are the easiest to tell apart. Every shade stays clear of the board (46,58,48), of its own greyed "can't act"
// look and of the white selection outline (checked in M55_UnitTint).
constexpr std::array<Rgb, kUnitShades> kShadesA{{
    {52, 110, 200},   // 1: side blue
    {130, 180, 245},  // 2: light blue
    {60, 175, 200},   // 3: teal
    {28, 62, 140},    // 4: navy
    {120, 110, 225},  // 5: periwinkle
    {175, 215, 250},  // 6: pale blue
}};
constexpr std::array<Rgb, kUnitShades> kShadesB{{
    {190, 60, 60},    // 1: side red
    {245, 130, 105},  // 2: salmon
    {225, 120, 40},   // 3: orange
    {125, 28, 40},    // 4: maroon
    {205, 60, 140},   // 5: crimson pink
    {245, 180, 170},  // 6: pale pink
}};

}  // namespace

Rgb side_rgb(core::Side side) { return side_shade(side, 1); }

Rgb side_shade(core::Side side, int index) {
    const auto& shades = side == core::Side::A ? kShadesA : kShadesB;
    const int i = index < 1 ? 0 : (index - 1) % kUnitShades;
    return shades[static_cast<std::size_t>(i)];
}

std::optional<UnitStyle> unit_style(const core::GameState& state, core::UnitId id) {
    const core::Unit* unit = state.find_unit(id);
    if (unit == nullptr) return std::nullopt;
    UnitStyle style;
    style.side_units = 0;
    for (const core::Unit& u : state.units) {
        if (u.side != unit->side) continue;
        ++style.side_units;
        if (u.id == id) style.index = style.side_units;
    }
    style.tint = side_shade(unit->side, style.index);
    // One unit on its side: no number, the name and colour as before M55 (V1 looks unchanged).
    if (style.side_units > 1) {
        style.label = std::to_string(style.index);
        style.display_name = unit->name + " " + style.label;
    } else {
        style.display_name = unit->name;
    }
    return style;
}

std::string unit_display_name(const core::GameState& state, core::UnitId id) {
    const auto style = unit_style(state, id);
    return style ? style->display_name : "unit " + std::to_string(id.v);
}

Rgb unit_tint(const core::GameState& state, core::UnitId id) {
    const auto style = unit_style(state, id);
    return style ? style->tint : side_rgb(core::Side::A);
}

}  // namespace wh40k::client
