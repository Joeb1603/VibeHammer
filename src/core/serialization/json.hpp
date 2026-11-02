#pragma once

// JSON round-trip for every GameState type (nlohmann ADL hooks).
// Enums serialise as strings, optionals as null, DiceExpr as an int (fixed) or a dice string.
// Mandatory: every new state field gets to_json/from_json here and a round-trip test.

#include <nlohmann/json.hpp>

#include "core/geometry/primitives.hpp"
#include "core/model/characteristics.hpp"
#include "core/model/dice_expr.hpp"
#include "core/model/unit.hpp"
#include "core/model/weapon.hpp"
#include "core/state/game_state.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

using Json = nlohmann::json;

void to_json(Json& j, Side v);
void from_json(const Json& j, Side& v);
void to_json(Json& j, const UnitId& v);
void from_json(const Json& j, UnitId& v);
void to_json(Json& j, const ModelId& v);
void from_json(const Json& j, ModelId& v);

void to_json(Json& j, const Position& v);
void from_json(const Json& j, Position& v);
void to_json(Json& j, BaseShape v);
void from_json(const Json& j, BaseShape& v);
void to_json(Json& j, const Base& v);
void from_json(const Json& j, Base& v);
void to_json(Json& j, const Rect& v);
void from_json(const Json& j, Rect& v);

void to_json(Json& j, const DiceExpr& v);
void from_json(const Json& j, DiceExpr& v);
void to_json(Json& j, const Characteristics& v);
void from_json(const Json& j, Characteristics& v);
void to_json(Json& j, WeaponKind v);
void from_json(const Json& j, WeaponKind& v);
void to_json(Json& j, const WeaponProfile& v);
void from_json(const Json& j, WeaponProfile& v);
void to_json(Json& j, const Weapon& v);
void from_json(const Json& j, Weapon& v);

void to_json(Json& j, const Model& v);
void from_json(const Json& j, Model& v);
void to_json(Json& j, const AbilityText& v);
void from_json(const Json& j, AbilityText& v);
void to_json(Json& j, const UnitTurnFlags& v);
void from_json(const Json& j, UnitTurnFlags& v);
void to_json(Json& j, const Unit& v);
void from_json(const Json& j, Unit& v);

void to_json(Json& j, BattlefieldEdge v);
void from_json(const Json& j, BattlefieldEdge& v);
void to_json(Json& j, const Battlefield& v);
void from_json(const Json& j, Battlefield& v);
void to_json(Json& j, const DeploymentZone& v);
void from_json(const Json& j, DeploymentZone& v);
void to_json(Json& j, const SideInfo& v);
void from_json(const Json& j, SideInfo& v);
void to_json(Json& j, const GameState& v);
void from_json(const Json& j, GameState& v);

namespace ability {
// WeaponAbility is a std::variant of ability:: types, so ADL finds these here.
// Encoded as {"kind": "RapidFire", "x": 1} / {"kind": "Unknown", "text": "..."}.
void to_json(Json& j, const WeaponAbility& v);
void from_json(const Json& j, WeaponAbility& v);
}  // namespace ability

}  // namespace wh40k::core
