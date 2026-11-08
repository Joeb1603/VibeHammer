#pragma once

// JSON for engine actions and events (and the roll/result types events carry).
// Actions and events are encoded as {"kind": "<name>", ...fields}; events also carry "rule".

#include <nlohmann/json.hpp>

#include "core/engine/actions.hpp"
#include "core/engine/events.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/serialization/json.hpp"

namespace wh40k::core {

void to_json(Json& j, const TwoD6& v);
void from_json(const Json& j, TwoD6& v);
void to_json(Json& j, const LeadershipRoll& v);
void from_json(const Json& j, LeadershipRoll& v);
void to_json(Json& j, const BattleShockRoll& v);
void from_json(const Json& j, BattleShockRoll& v);
void to_json(Json& j, const HazardRolls& v);
void from_json(const Json& j, HazardRolls& v);
void to_json(Json& j, const MortalWoundsResult& v);
void from_json(const Json& j, MortalWoundsResult& v);

void to_json(Json& j, HitOutcome v);
void from_json(const Json& j, HitOutcome& v);
void to_json(Json& j, const HitRoll& v);
void from_json(const Json& j, HitRoll& v);
void to_json(Json& j, const HitRolls& v);
void from_json(const Json& j, HitRolls& v);
void to_json(Json& j, WoundOutcome v);
void from_json(const Json& j, WoundOutcome& v);
void to_json(Json& j, const WoundRoll& v);
void from_json(const Json& j, WoundRoll& v);
void to_json(Json& j, const WoundRolls& v);
void from_json(const Json& j, WoundRolls& v);
void to_json(Json& j, const WoundReroll& v);
void from_json(const Json& j, WoundReroll& v);
void to_json(Json& j, SaveOutcome v);
void from_json(const Json& j, SaveOutcome& v);
void to_json(Json& j, const SaveCheck& v);
void from_json(const Json& j, SaveCheck& v);
void to_json(Json& j, const DamageResult& v);
void from_json(const Json& j, DamageResult& v);
void to_json(Json& j, const SaveStep& v);
void from_json(const Json& j, SaveStep& v);
void to_json(Json& j, const InflictDamageResult& v);
void from_json(const Json& j, InflictDamageResult& v);

namespace action {
// GameAction is a std::variant of action:: types, so ADL finds these here.
void to_json(Json& j, const GameAction& v);
void from_json(const Json& j, GameAction& v);
}  // namespace action

namespace event {
// GameEvent is a std::variant of event:: types, so ADL finds these here.
void to_json(Json& j, const GameEvent& v);
void from_json(const Json& j, GameEvent& v);
}  // namespace event

}  // namespace wh40k::core
