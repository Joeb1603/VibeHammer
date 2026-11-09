#include "unit_style.hpp"

#include <array>

#include "core/state/deployment.hpp"

namespace wh40k::client {

namespace {

// D-065: ten shades per side, in the order they are handed out. Shade 1 is the pre-M55 side colour; the next
// ones change lightness first (light, then a hue step, then dark), so the first three or four units of a side
// are the easiest to tell apart. Every shade stays clear of the board (46,58,48), of its own greyed "can't act"
// look and of the white selection outline (checked in M55_UnitTint). Shades 7-10 (M67) were chosen, each in turn,
// to be the furthest from the shades before them under simulated protanopia, deuteranopia and tritanopia, keeping
// A's cool and B's warm (checked in M67_UnitShades).
constexpr std::array<Rgb, kUnitShades> kShadesA{{
    {52, 110, 200},   // 1: side blue
    {130, 180, 245},  // 2: light blue
    {60, 175, 200},   // 3: teal
    {28, 62, 140},    // 4: navy
    {120, 110, 225},  // 5: periwinkle
    {175, 215, 250},  // 6: pale blue
    {85, 45, 175},    // 7: indigo (M67)
    {160, 240, 240},  // 8: pale cyan
    {155, 125, 205},  // 9: lavender
    {200, 130, 255},  // 10: light violet
}};
constexpr std::array<Rgb, kUnitShades> kShadesB{{
    {190, 60, 60},    // 1: side red
    {245, 130, 105},  // 2: salmon
    {225, 120, 40},   // 3: orange
    {125, 28, 40},    // 4: maroon
    {205, 60, 140},   // 5: crimson pink
    {245, 180, 170},  // 6: pale pink
    {250, 225, 145},  // 7: cream (M67)
    {185, 105, 110},  // 8: dusty rose
    {110, 55, 0},     // 9: brown
    {245, 165, 10},   // 10: amber
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
    if (unit == nullptr) unit = core::find_undeployed(state, id);  // D-081: a unit not yet set up has its style too
    if (unit == nullptr) return std::nullopt;
    UnitStyle style;
    style.side_units = 0;
    // D-065: the position among the side's units in GameState::units order. D-081: units not yet set up count at their
    // id positions (both lists are in id order, so they are merged by id), so numbers don't shift as units are set up.
    // With nothing undeployed this is the same loop over `units` as before.
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < state.units.size() || j < state.undeployed.size()) {
        const bool take_set_up =
            j == state.undeployed.size() || (i < state.units.size() && state.units[i].id < state.undeployed[j].id);
        const core::Unit& u = take_set_up ? state.units[i++] : state.undeployed[j++];
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
