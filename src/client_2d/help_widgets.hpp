#pragma once

// ImGui widgets for the rules help (D-037, M24): ability effect lines, hover tooltips and hoverable
// keyword / ability tokens. Display only.

#include <string>
#include <vector>

#include <imgui.h>

#include "rules_help.hpp"

namespace wh40k::client {

// Active: green, inactive: red, info: white, not in this game: orange.
[[nodiscard]] ImVec4 effect_colour(EffectKind kind);

// One line per effect, in its colour.
void draw_effect_lines(const std::vector<EffectLine>& lines);

// The body of a help tooltip: each entry's title, rule, summary and whether this game applies it.
void draw_help(const std::vector<HelpEntry>& entries);

// A tooltip with `entries` if the last item is hovered.
void help_tooltip_for_last_item(const std::vector<HelpEntry>& entries);

// Text tokens on one wrapped line; hovering one shows its help. `colour` for the text.
void draw_help_tokens(const std::vector<HelpEntry>& tokens, ImVec4 colour);

}  // namespace wh40k::client
