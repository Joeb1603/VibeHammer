#pragma once

#include <string_view>

#include "core/model/unit.hpp"

namespace wh40k::core {

// 02.05: does the unit have this keyword? Case-insensitive; faction keywords count too.
[[nodiscard]] bool has_keyword(const Unit& unit, std::string_view keyword);

// Keyword checks for one model. V1 units never mix keywords (no attached units, 19), so every model
// has its unit's keywords. Mixed Keywords in Units (Rules Appendix) needs per-model keywords
// post-V1; these helpers are the single place to change.
[[nodiscard]] bool is_character(const Unit& unit, const Model& model);
[[nodiscard]] bool is_monster_or_vehicle(const Unit& unit, const Model& model);

// 02.02: a model that has lost one or more wounds.
[[nodiscard]] bool is_wounded(const Model& model);

// Rules Appendix, Destroyed: when every model in a unit has been destroyed, that unit is destroyed.
// Destroyed models are removed from Unit::models.
[[nodiscard]] bool is_destroyed(const Unit& unit);

}  // namespace wh40k::core
