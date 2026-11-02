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
#include "core/rules/attack_types.hpp"
#include "core/rules/shooting_types.hpp"
#include "core/state/decision.hpp"
#include "core/state/flow.hpp"
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
void to_json(Json& j, const UnitPhaseFlags& v);
void from_json(const Json& j, UnitPhaseFlags& v);
void to_json(Json& j, const Unit& v);
void from_json(const Json& j, Unit& v);

// ---- attack data (rules/attack_types.hpp), stored while an attack is in progress ----
void to_json(Json& j, AttackType v);
void from_json(const Json& j, AttackType& v);
void to_json(Json& j, const AttackContext& v);
void from_json(const Json& j, AttackContext& v);
void to_json(Json& j, const WeaponSelection& v);
void from_json(const Json& j, WeaponSelection& v);
void to_json(Json& j, const AttackProfile& v);
void from_json(const Json& j, AttackProfile& v);
void to_json(Json& j, const AttackBatch& v);
void from_json(const Json& j, AttackBatch& v);
void to_json(Json& j, const AllocationGroup& v);
void from_json(const Json& j, AllocationGroup& v);

// ---- flow (state/flow.hpp) and decisions (state/decision.hpp) ----
void to_json(Json& j, Stage v);
void from_json(const Json& j, Stage& v);
void to_json(Json& j, Phase v);
void from_json(const Json& j, Phase& v);
void to_json(Json& j, ActivationKind v);
void from_json(const Json& j, ActivationKind& v);
void to_json(Json& j, const Activation& v);
void from_json(const Json& j, Activation& v);
void to_json(Json& j, MoveType v);
void from_json(const Json& j, MoveType& v);
void to_json(Json& j, FallBackMode v);
void from_json(const Json& j, FallBackMode& v);
void to_json(Json& j, const ModelStart& v);
void from_json(const Json& j, ModelStart& v);
void to_json(Json& j, const ModelPath& v);
void from_json(const Json& j, ModelPath& v);
void to_json(Json& j, const MoveInProgress& v);
void from_json(const Json& j, MoveInProgress& v);
void to_json(Json& j, ShootingType v);
void from_json(const Json& j, ShootingType& v);
void to_json(Json& j, const ShotDeclaration& v);
void from_json(const Json& j, ShotDeclaration& v);
void to_json(Json& j, const BatchInProgress& v);
void from_json(const Json& j, BatchInProgress& v);
void to_json(Json& j, const AttackFlow& v);
void from_json(const Json& j, AttackFlow& v);
void to_json(Json& j, const FlowState& v);
void from_json(const Json& j, FlowState& v);
void to_json(Json& j, const GameResult& v);
void from_json(const Json& j, GameResult& v);
void to_json(Json& j, const PendingDecision& v);
void from_json(const Json& j, PendingDecision& v);

void to_json(Json& j, const DiceState& v);
void from_json(const Json& j, DiceState& v);

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
