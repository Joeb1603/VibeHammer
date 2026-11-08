#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "core/state/game_state.hpp"

namespace wh40k::client {

// D-065 (M55): how a unit is told apart from the other units of its side. A plain RGB value, so this stays
// raylib-free (wh40k_client_logic); the client turns it into a raylib Color.
struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    friend bool operator==(const Rgb&, const Rgb&) = default;
};

// The side colour used before M55 (blue for A, red for B). Shade 1 of each side is this colour.
[[nodiscard]] Rgb side_rgb(core::Side side);

// The number of shades each side has; a side with more units repeats them (index wraps). D-065: six since M55, ten
// since M67 (the first six unchanged).
inline constexpr int kUnitShades = 10;

// Shade `index` (1-based) of `side`'s palette. Shade 1 is side_rgb(side).
[[nodiscard]] Rgb side_shade(core::Side side, int index);

struct UnitStyle {
    Rgb tint;               // fill colour of the unit's bases
    int index = 1;          // 1-based position among its side's units, in GameState::units order
    int side_units = 1;     // how many units its side has
    std::string label;      // number drawn on the board ("2"); empty when the side has one unit
    std::string display_name;  // "Termagants 2"; the datasheet name alone when the side has one unit
};

// The style of unit `id`; nullopt when the game has no such unit.
[[nodiscard]] std::optional<UnitStyle> unit_style(const core::GameState& state, core::UnitId id);

// unit_style(...).display_name, or "unit N" for an unknown id.
[[nodiscard]] std::string unit_display_name(const core::GameState& state, core::UnitId id);

// The tint, or side_rgb of side A for an unknown id (callers draw known units only).
[[nodiscard]] Rgb unit_tint(const core::GameState& state, core::UnitId id);

}  // namespace wh40k::client
