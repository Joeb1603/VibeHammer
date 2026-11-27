#pragma once

#include <span>
#include <string>
#include <string_view>

#include "core/model/unit.hpp"

namespace wh40k::core {

// 02.05: does the unit have this keyword? Case-insensitive; faction keywords count too.
[[nodiscard]] bool has_keyword(const Unit& unit, std::string_view keyword);

// 24.01 / app 02.05.01 (M99, D-101): does the unit have any one of these keywords (a series separated by slashes means
// any one of them)? A keyword's singular and plural are alike ("Vehicle" / "Vehicles"), as is_explosives_or_grenades.
[[nodiscard]] bool has_any_keyword(const Unit& unit, std::span<const std::string> keywords);

// Keyword checks for one model. V1 units never mix keywords (no attached units, 19), so every model
// has its unit's keywords. Mixed Keywords in Units (Rules Appendix) needs per-model keywords
// post-V1; these helpers are the single place to change.
[[nodiscard]] bool is_character(const Unit& unit, const Model& model);
[[nodiscard]] bool is_monster_or_vehicle(const Unit& unit, const Model& model);
// D-099 (M97): 15.05's EXPLOSIVES/GRENADES model: either keyword, singular or plural alike (app 02.05.01 Using Keywords).
[[nodiscard]] bool is_explosives_or_grenades(const Unit& unit, const Model& model);

// 02.02: a model that has lost one or more wounds.
[[nodiscard]] bool is_wounded(const Model& model);

// Rules Appendix, Destroyed: when every model in a unit has been destroyed, that unit is destroyed.
// Destroyed models are removed from Unit::models.
[[nodiscard]] bool is_destroyed(const Unit& unit);

}  // namespace wh40k::core
