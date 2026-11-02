#include "core/serialization/engine_json.hpp"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

#include "core/serialization/json_util.hpp"

namespace wh40k::core {

using json_detail::bad_enum;
using json_detail::get_opt;
using json_detail::put_opt;

// ---- enums ----
void to_json(Json& j, MoveType v) {
    switch (v) {
        case MoveType::RemainStationary: j = "RemainStationary"; return;
    }
    throw std::logic_error("to_json: invalid MoveType");
}
void from_json(const Json& j, MoveType& v) {
    const auto s = j.get<std::string>();
    if (s == "RemainStationary") v = MoveType::RemainStationary;
    else bad_enum("MoveType", j);
}

void to_json(Json& j, HitOutcome v) {
    switch (v) {
        case HitOutcome::Fail: j = "Fail"; return;
        case HitOutcome::Hit: j = "Hit"; return;
        case HitOutcome::CriticalHit: j = "CriticalHit"; return;
        case HitOutcome::AutoHit: j = "AutoHit"; return;
    }
    throw std::logic_error("to_json: invalid HitOutcome");
}
void from_json(const Json& j, HitOutcome& v) {
    const auto s = j.get<std::string>();
    if (s == "Fail") v = HitOutcome::Fail;
    else if (s == "Hit") v = HitOutcome::Hit;
    else if (s == "CriticalHit") v = HitOutcome::CriticalHit;
    else if (s == "AutoHit") v = HitOutcome::AutoHit;
    else bad_enum("HitOutcome", j);
}

void to_json(Json& j, WoundOutcome v) {
    switch (v) {
        case WoundOutcome::Fail: j = "Fail"; return;
        case WoundOutcome::Wound: j = "Wound"; return;
        case WoundOutcome::CriticalWound: j = "CriticalWound"; return;
    }
    throw std::logic_error("to_json: invalid WoundOutcome");
}
void from_json(const Json& j, WoundOutcome& v) {
    const auto s = j.get<std::string>();
    if (s == "Fail") v = WoundOutcome::Fail;
    else if (s == "Wound") v = WoundOutcome::Wound;
    else if (s == "CriticalWound") v = WoundOutcome::CriticalWound;
    else bad_enum("WoundOutcome", j);
}

void to_json(Json& j, SaveOutcome v) {
    j = (v == SaveOutcome::AttackFails) ? "AttackFails" : "InflictsDamage";
}
void from_json(const Json& j, SaveOutcome& v) {
    const auto s = j.get<std::string>();
    if (s == "AttackFails") v = SaveOutcome::AttackFails;
    else if (s == "InflictsDamage") v = SaveOutcome::InflictsDamage;
    else bad_enum("SaveOutcome", j);
}

// ---- rolls and results ----
void to_json(Json& j, const HitRoll& v) {
    j = Json{{"outcome", v.outcome}};
    put_opt(j, "unmodified", v.unmodified);
    put_opt(j, "modified", v.modified);
}
void from_json(const Json& j, HitRoll& v) {
    get_opt(j, "unmodified", v.unmodified);
    get_opt(j, "modified", v.modified);
    j.at("outcome").get_to(v.outcome);
}
void to_json(Json& j, const HitRolls& v) { j = Json{{"rolls", v.rolls}}; }
void from_json(const Json& j, HitRolls& v) { j.at("rolls").get_to(v.rolls); }

void to_json(Json& j, const WoundRoll& v) {
    j = Json{{"unmodified", v.unmodified}, {"modified", v.modified}, {"outcome", v.outcome}};
}
void from_json(const Json& j, WoundRoll& v) {
    j.at("unmodified").get_to(v.unmodified);
    j.at("modified").get_to(v.modified);
    j.at("outcome").get_to(v.outcome);
}
void to_json(Json& j, const WoundRolls& v) { j = Json{{"required", v.required}, {"rolls", v.rolls}}; }
void from_json(const Json& j, WoundRolls& v) {
    j.at("required").get_to(v.required);
    j.at("rolls").get_to(v.rolls);
}

void to_json(Json& j, const SaveCheck& v) {
    j = Json{{"unmodified", v.unmodified},
             {"modified", v.modified},
             {"invulnerable_save", v.invulnerable_save},
             {"outcome", v.outcome}};
}
void from_json(const Json& j, SaveCheck& v) {
    j.at("unmodified").get_to(v.unmodified);
    j.at("modified").get_to(v.modified);
    j.at("invulnerable_save").get_to(v.invulnerable_save);
    j.at("outcome").get_to(v.outcome);
}

void to_json(Json& j, const DamageResult& v) {
    j = Json{{"model", v.model}, {"damage", v.damage}, {"wounds_lost", v.wounds_lost}, {"destroyed", v.destroyed}};
}
void from_json(const Json& j, DamageResult& v) {
    j.at("model").get_to(v.model);
    j.at("damage").get_to(v.damage);
    j.at("wounds_lost").get_to(v.wounds_lost);
    j.at("destroyed").get_to(v.destroyed);
}

void to_json(Json& j, const SaveStep& v) {
    j = Json::object();
    put_opt(j, "group", v.group);
    put_opt(j, "check", v.check);
    put_opt(j, "damage", v.damage);
}
void from_json(const Json& j, SaveStep& v) {
    get_opt(j, "group", v.group);
    get_opt(j, "check", v.check);
    get_opt(j, "damage", v.damage);
}

void to_json(Json& j, const InflictDamageResult& v) {
    j = Json{{"steps", v.steps}, {"models_destroyed", v.models_destroyed}, {"attacks_lost", v.attacks_lost}};
}
void from_json(const Json& j, InflictDamageResult& v) {
    j.at("steps").get_to(v.steps);
    j.at("models_destroyed").get_to(v.models_destroyed);
    j.at("attacks_lost").get_to(v.attacks_lost);
}

// ---- actions ----
namespace action {

void to_json(Json& j, const GameAction& v) {
    j = Json{{"kind", std::string(action_name(v))}};
    std::visit(
        [&j](const auto& a) {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, StartMove>) {
                j["unit"] = a.unit;
                j["move_type"] = a.move_type;
            } else if constexpr (std::is_same_v<T, SelectAttackBatch>) {
                j["batch"] = a.batch;
            } else if constexpr (std::is_same_v<T, DeclareAllocationOrder>) {
                j["target"] = a.target;
                j["order"] = a.order;
            } else if constexpr (std::is_same_v<T, OrderCasualties>) {
                j["target"] = a.target;
                j["priority"] = a.priority;
            }
        },
        v);
}

void from_json(const Json& j, GameAction& v) {
    const auto kind = j.at("kind").get<std::string>();
    if (kind == "StartMove") {
        v = StartMove{j.at("unit").get<UnitId>(), j.at("move_type").get<MoveType>()};
    } else if (kind == "EndShootStep") {
        v = EndShootStep{};
    } else if (kind == "EndChargeStep") {
        v = EndChargeStep{};
    } else if (kind == "SelectAttackBatch") {
        v = SelectAttackBatch{j.at("batch").get<std::size_t>()};
    } else if (kind == "DeclareAllocationOrder") {
        v = DeclareAllocationOrder{j.at("target").get<UnitId>(), j.at("order").get<std::vector<std::size_t>>()};
    } else if (kind == "OrderCasualties") {
        v = OrderCasualties{j.at("target").get<UnitId>(), j.at("priority").get<std::vector<ModelId>>()};
    } else {
        bad_enum("GameAction", j);
    }
}

}  // namespace action

// ---- events ----
namespace event {

void to_json(Json& j, const GameEvent& v) {
    j = Json{{"kind", std::string(event_name(v))}, {"rule", std::string(event_rule(v))}};
    std::visit(
        [&j](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, BattleRoundStarted> || std::is_same_v<T, BattleRoundEnded>) {
                j["round"] = e.round;
            } else if constexpr (std::is_same_v<T, TurnStarted> || std::is_same_v<T, TurnEnded>) {
                j["round"] = e.round;
                j["side"] = e.side;
            } else if constexpr (std::is_same_v<T, PhaseStarted> || std::is_same_v<T, PhaseEnded>) {
                j["phase"] = e.phase;
                j["side"] = e.side;
            } else if constexpr (std::is_same_v<T, UnitRemainedStationary> || std::is_same_v<T, UnitDestroyed>) {
                j["unit"] = e.unit;
            } else if constexpr (std::is_same_v<T, AttackBatchesGathered>) {
                j["attacker"] = e.attacker;
                j["batches"] = e.batches;
            } else if constexpr (std::is_same_v<T, AttackBatchSelected>) {
                j["attacker"] = e.attacker;
                j["batch"] = e.batch;
                j["target"] = e.target;
                j["attack_dice"] = e.attack_dice;
            } else if constexpr (std::is_same_v<T, HitRollsMade> || std::is_same_v<T, WoundRollsMade>) {
                j["batch"] = e.batch;
                j["rolls"] = e.rolls;
            } else if constexpr (std::is_same_v<T, AllocationOrderDeclared>) {
                j["target"] = e.target;
                j["groups"] = e.groups;
            } else if constexpr (std::is_same_v<T, SaveRollsMade>) {
                j["target"] = e.target;
                j["rolls"] = e.rolls;
            } else if constexpr (std::is_same_v<T, SavesResolved>) {
                j["target"] = e.target;
                j["result"] = e.result;
            } else if constexpr (std::is_same_v<T, ModelDestroyed>) {
                j["unit"] = e.unit;
                j["model"] = e.model;
            } else if constexpr (std::is_same_v<T, AttacksLost>) {
                j["target"] = e.target;
                j["batch"] = e.batch;
                j["attack_dice"] = e.attack_dice;
            } else if constexpr (std::is_same_v<T, GameOver>) {
                put_opt(j, "winner", e.winner);
                j["reason"] = e.reason;
            } else {
                static_assert(sizeof(T) == 0, "unhandled event");
            }
        },
        v);
}

void from_json(const Json& j, GameEvent& v) {
    const auto kind = j.at("kind").get<std::string>();
    if (kind == "BattleRoundStarted") {
        v = BattleRoundStarted{j.at("round").get<int>()};
    } else if (kind == "BattleRoundEnded") {
        v = BattleRoundEnded{j.at("round").get<int>()};
    } else if (kind == "TurnStarted") {
        v = TurnStarted{j.at("round").get<int>(), j.at("side").get<Side>()};
    } else if (kind == "TurnEnded") {
        v = TurnEnded{j.at("round").get<int>(), j.at("side").get<Side>()};
    } else if (kind == "PhaseStarted") {
        v = PhaseStarted{j.at("phase").get<Phase>(), j.at("side").get<Side>()};
    } else if (kind == "PhaseEnded") {
        v = PhaseEnded{j.at("phase").get<Phase>(), j.at("side").get<Side>()};
    } else if (kind == "UnitRemainedStationary") {
        v = UnitRemainedStationary{j.at("unit").get<UnitId>()};
    } else if (kind == "AttackBatchesGathered") {
        v = AttackBatchesGathered{j.at("attacker").get<UnitId>(), j.at("batches").get<std::vector<AttackBatch>>()};
    } else if (kind == "AttackBatchSelected") {
        v = AttackBatchSelected{j.at("attacker").get<UnitId>(), j.at("batch").get<std::size_t>(),
                                j.at("target").get<UnitId>(), j.at("attack_dice").get<int>()};
    } else if (kind == "HitRollsMade") {
        v = HitRollsMade{j.at("batch").get<std::size_t>(), j.at("rolls").get<HitRolls>()};
    } else if (kind == "WoundRollsMade") {
        v = WoundRollsMade{j.at("batch").get<std::size_t>(), j.at("rolls").get<WoundRolls>()};
    } else if (kind == "AllocationOrderDeclared") {
        v = AllocationOrderDeclared{j.at("target").get<UnitId>(), j.at("groups").get<std::vector<AllocationGroup>>()};
    } else if (kind == "SaveRollsMade") {
        v = SaveRollsMade{j.at("target").get<UnitId>(), j.at("rolls").get<std::vector<int>>()};
    } else if (kind == "SavesResolved") {
        v = SavesResolved{j.at("target").get<UnitId>(), j.at("result").get<InflictDamageResult>()};
    } else if (kind == "ModelDestroyed") {
        v = ModelDestroyed{j.at("unit").get<UnitId>(), j.at("model").get<ModelId>()};
    } else if (kind == "UnitDestroyed") {
        v = UnitDestroyed{j.at("unit").get<UnitId>()};
    } else if (kind == "AttacksLost") {
        v = AttacksLost{j.at("target").get<UnitId>(), j.at("batch").get<std::size_t>(), j.at("attack_dice").get<int>()};
    } else if (kind == "GameOver") {
        GameOver g;
        get_opt(j, "winner", g.winner);
        j.at("reason").get_to(g.reason);
        v = g;
    } else {
        bad_enum("GameEvent", j);
    }
}

}  // namespace event

}  // namespace wh40k::core
