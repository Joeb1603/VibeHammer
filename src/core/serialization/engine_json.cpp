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

// ---- leadership, hazards, mortal wounds ----
void to_json(Json& j, const TwoD6& v) { j = v.dice; }
void from_json(const Json& j, TwoD6& v) { j.get_to(v.dice); }
void to_json(Json& j, const LeadershipRoll& v) {
    j = Json{{"roll", v.roll}, {"target", v.target}, {"success", v.success}};
}
void from_json(const Json& j, LeadershipRoll& v) {
    j.at("roll").get_to(v.roll);
    j.at("target").get_to(v.target);
    j.at("success").get_to(v.success);
}
void to_json(Json& j, const BattleShockRoll& v) {
    j = Json{{"leadership", v.leadership}, {"becomes_battle_shocked", v.becomes_battle_shocked}};
}
void from_json(const Json& j, BattleShockRoll& v) {
    j.at("leadership").get_to(v.leadership);
    j.at("becomes_battle_shocked").get_to(v.becomes_battle_shocked);
}
void to_json(Json& j, const HazardRolls& v) {
    j = Json{{"rolls", v.rolls}, {"failures", v.failures}, {"mortal_wounds", v.mortal_wounds}};
}
void from_json(const Json& j, HazardRolls& v) {
    j.at("rolls").get_to(v.rolls);
    j.at("failures").get_to(v.failures);
    j.at("mortal_wounds").get_to(v.mortal_wounds);
}
void to_json(Json& j, const MortalWoundsResult& v) {
    j = Json{{"steps", v.steps}, {"models_destroyed", v.models_destroyed}, {"not_inflicted", v.not_inflicted}};
}
void from_json(const Json& j, MortalWoundsResult& v) {
    j.at("steps").get_to(v.steps);
    j.at("models_destroyed").get_to(v.models_destroyed);
    j.at("not_inflicted").get_to(v.not_inflicted);
}

// ---- enums ----

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
void to_json(Json& j, const HitRolls& v) {
    j = Json{{"rolls", v.rolls}};
    if (v.sustained_hits != 0) j["sustained_hits"] = v.sustained_hits;  // 24.36; omitted when 0
}
void from_json(const Json& j, HitRolls& v) {
    j.at("rolls").get_to(v.rolls);
    v.sustained_hits = j.value("sustained_hits", 0);
}

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
void to_json(Json& j, const WoundReroll& v) {
    j = Json{{"index", v.index}, {"before", v.before}, {"after", v.after}};
}
void from_json(const Json& j, WoundReroll& v) {
    j.at("index").get_to(v.index);
    j.at("before").get_to(v.before);
    j.at("after").get_to(v.after);
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
                if (a.fall_back_mode) j["fall_back_mode"] = *a.fall_back_mode;
            } else if constexpr (std::is_same_v<T, ProposeModelMove>) {
                j["model"] = a.model;
                j["path"] = a.path;
            } else if constexpr (std::is_same_v<T, Shoot>) {
                j["unit"] = a.unit;
                j["type"] = a.type;
                j["shots"] = a.shots;
            } else if constexpr (std::is_same_v<T, DeclareCharge> || std::is_same_v<T, SkipChargeMove>) {
                j["unit"] = a.unit;
            } else if constexpr (std::is_same_v<T, StartChargeMove>) {
                j["unit"] = a.unit;
                j["targets"] = a.targets;
            } else if constexpr (std::is_same_v<T, StartPileIn> || std::is_same_v<T, StartConsolidation>) {
                j["unit"] = a.unit;
                j["targets"] = a.targets;
            } else if constexpr (std::is_same_v<T, RemoveModelForCoherency>) {
                j["unit"] = a.unit;
                j["model"] = a.model;
            } else if constexpr (std::is_same_v<T, SkipPileIn>) {
                j["unit"] = a.unit;
            } else if constexpr (std::is_same_v<T, SelectToFight>) {
                j["unit"] = a.unit;
                j["type"] = a.type;
            } else if constexpr (std::is_same_v<T, Fight>) {
                j["unit"] = a.unit;
                j["declarations"] = a.declarations;
            } else if constexpr (std::is_same_v<T, SelectAttackBatch>) {
                j["batch"] = a.batch;
            } else if constexpr (std::is_same_v<T, DeclareAllocationOrder>) {
                j["target"] = a.target;
                j["order"] = a.order;
            } else if constexpr (std::is_same_v<T, OrderCasualties>) {
                j["target"] = a.target;
                j["priority"] = a.priority;
            } else if constexpr (std::is_same_v<T, RerollWoundRolls>) {
                j["indices"] = a.indices;  // 24.38 (D-072)
            }
        },
        v);
}

void from_json(const Json& j, GameAction& v) {
    const auto kind = j.at("kind").get<std::string>();
    if (kind == "StartMove") {
        StartMove a{j.at("unit").get<UnitId>(), j.at("move_type").get<MoveType>(), std::nullopt};
        if (j.contains("fall_back_mode")) a.fall_back_mode = j.at("fall_back_mode").get<FallBackMode>();
        v = a;
    } else if (kind == "ProposeModelMove") {
        v = ProposeModelMove{j.at("model").get<ModelId>(), j.at("path").get<std::vector<Position>>()};
    } else if (kind == "CommitUnitMove") {
        v = CommitUnitMove{};
    } else if (kind == "ResetUnitMove") {
        v = ResetUnitMove{};
    } else if (kind == "Shoot") {
        v = Shoot{j.at("unit").get<UnitId>(), j.at("type").get<ShootingType>(),
                  j.at("shots").get<std::vector<ShotDeclaration>>()};
    } else if (kind == "EndShootStep") {
        v = EndShootStep{};
    } else if (kind == "DeclareCharge") {
        v = DeclareCharge{j.at("unit").get<UnitId>()};
    } else if (kind == "StartChargeMove") {
        v = StartChargeMove{j.at("unit").get<UnitId>(), j.at("targets").get<std::vector<UnitId>>()};
    } else if (kind == "SkipChargeMove") {
        v = SkipChargeMove{j.at("unit").get<UnitId>()};
    } else if (kind == "StartPileIn") {
        v = StartPileIn{j.at("unit").get<UnitId>(), j.at("targets").get<std::vector<UnitId>>()};
    } else if (kind == "SkipPileIn") {
        v = SkipPileIn{j.at("unit").get<UnitId>()};
    } else if (kind == "EndPileIns") {
        v = EndPileIns{};
    } else if (kind == "SelectToFight") {
        v = SelectToFight{j.at("unit").get<UnitId>(), j.at("type").get<FightType>()};
    } else if (kind == "PassFight") {
        v = PassFight{};
    } else if (kind == "Fight") {
        v = Fight{j.at("unit").get<UnitId>(), j.at("declarations").get<std::vector<MeleeDeclaration>>()};
    } else if (kind == "StartConsolidation") {
        v = StartConsolidation{j.at("unit").get<UnitId>(), j.at("targets").get<std::vector<UnitId>>()};
    } else if (kind == "RemoveModelForCoherency") {
        v = RemoveModelForCoherency{j.at("unit").get<UnitId>(), j.at("model").get<ModelId>()};
    } else if (kind == "EndConsolidations") {
        v = EndConsolidations{};
    } else if (kind == "EndChargeStep") {
        v = EndChargeStep{};
    } else if (kind == "SelectAttackBatch") {
        v = SelectAttackBatch{j.at("batch").get<std::size_t>()};
    } else if (kind == "DeclareAllocationOrder") {
        v = DeclareAllocationOrder{j.at("target").get<UnitId>(), j.at("order").get<std::vector<std::size_t>>()};
    } else if (kind == "OrderCasualties") {
        v = OrderCasualties{j.at("target").get<UnitId>(), j.at("priority").get<std::vector<ModelId>>()};
    } else if (kind == "RerollWoundRolls") {
        v = RerollWoundRolls{j.at("indices").get<std::vector<std::size_t>>()};
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
            } else if constexpr (std::is_same_v<T, MoveStarted>) {
                j["unit"] = e.unit;
                j["move_type"] = e.move_type;
                put_opt(j, "mode", e.mode);
            } else if constexpr (std::is_same_v<T, AdvanceRolled>) {
                j["unit"] = e.unit;
                j["roll"] = e.roll;
                j["reused"] = e.reused;
            } else if constexpr (std::is_same_v<T, HazardRollsMade>) {
                j["unit"] = e.unit;
                j["rolls"] = e.rolls;
            } else if constexpr (std::is_same_v<T, MortalWoundsInflicted>) {
                j["unit"] = e.unit;
                j["result"] = e.result;
            } else if constexpr (std::is_same_v<T, ModelMoveProposed>) {
                j["unit"] = e.unit;
                j["model"] = e.model;
                j["path"] = e.path;
                j["distance_mm"] = e.distance_mm;
            } else if constexpr (std::is_same_v<T, UnitMoveCommitted>) {
                j["unit"] = e.unit;
                j["move_type"] = e.move_type;
            } else if constexpr (std::is_same_v<T, UnitMoveReverted>) {
                j["unit"] = e.unit;
                j["reasons"] = e.reasons;
            } else if constexpr (std::is_same_v<T, BattleShockRolled>) {
                j["unit"] = e.unit;
                j["roll"] = e.roll;
            } else if constexpr (std::is_same_v<T, ShootingStarted>) {
                j["unit"] = e.unit;
                j["type"] = e.type;
                j["selections"] = e.selections;
            } else if constexpr (std::is_same_v<T, ChargeDeclared>) {
                j["unit"] = e.unit;
            } else if constexpr (std::is_same_v<T, ChargeRolled>) {
                j["unit"] = e.unit;
                j["roll"] = e.roll;
                j["eligible_targets"] = e.eligible_targets;
            } else if constexpr (std::is_same_v<T, ChargeTargetsSelected>) {
                j["unit"] = e.unit;
                j["targets"] = e.targets;
            } else if constexpr (std::is_same_v<T, ChargeResolved>) {
                j["unit"] = e.unit;
                j["charge_move_made"] = e.charge_move_made;
                j["reason"] = e.reason;
            } else if constexpr (std::is_same_v<T, PileInStarted>) {
                j["unit"] = e.unit;
                j["targets"] = e.targets;
            } else if constexpr (std::is_same_v<T, UnitSelectedToFight>) {
                j["unit"] = e.unit;
                j["type"] = e.type;
                j["forced"] = e.forced;
            } else if constexpr (std::is_same_v<T, FightPassed>) {
                j["side"] = e.side;
            } else if constexpr (std::is_same_v<T, MeleeAttacksDeclared>) {
                j["unit"] = e.unit;
                j["selections"] = e.selections;
            } else if constexpr (std::is_same_v<T, FightStepEnded>) {
                j["reason"] = e.reason;
            } else if constexpr (std::is_same_v<T, ConsolidationStarted>) {
                j["unit"] = e.unit;
                j["mode"] = e.mode;
                j["targets"] = e.targets;
            } else if constexpr (std::is_same_v<T, UnitsForcedToFight>) {
                j["units"] = e.units;
            } else if constexpr (std::is_same_v<T, CommandPointGained>) {
                j["side"] = e.side;
                j["total"] = e.total;
            } else if constexpr (std::is_same_v<T, BattleShockEnded>) {
                j["unit"] = e.unit;
            } else if constexpr (std::is_same_v<T, ModelRemovedForCoherency>) {
                j["unit"] = e.unit;
                j["model"] = e.model;
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
            } else if constexpr (std::is_same_v<T, WoundRollsRerolled>) {
                j["batch"] = e.batch;  // 24.38 (D-072)
                j["rerolls"] = e.rerolls;
                j["rolls"] = e.rolls;
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
    } else if (kind == "MoveStarted") {
        MoveStarted m{j.at("unit").get<UnitId>(), j.at("move_type").get<MoveType>(), std::nullopt};
        get_opt(j, "mode", m.mode);
        v = m;
    } else if (kind == "AdvanceRolled") {
        v = AdvanceRolled{j.at("unit").get<UnitId>(), j.at("roll").get<int>(), j.at("reused").get<bool>()};
    } else if (kind == "HazardRollsMade") {
        v = HazardRollsMade{j.at("unit").get<UnitId>(), j.at("rolls").get<HazardRolls>()};
    } else if (kind == "MortalWoundsInflicted") {
        v = MortalWoundsInflicted{j.at("unit").get<UnitId>(), j.at("result").get<MortalWoundsResult>()};
    } else if (kind == "ModelMoveProposed") {
        v = ModelMoveProposed{j.at("unit").get<UnitId>(), j.at("model").get<ModelId>(),
                              j.at("path").get<std::vector<Position>>(), j.at("distance_mm").get<float>()};
    } else if (kind == "UnitMoveCommitted") {
        v = UnitMoveCommitted{j.at("unit").get<UnitId>(), j.at("move_type").get<MoveType>()};
    } else if (kind == "UnitMoveReverted") {
        v = UnitMoveReverted{j.at("unit").get<UnitId>(), j.at("reasons").get<std::vector<std::string>>()};
    } else if (kind == "BattleShockRolled") {
        v = BattleShockRolled{j.at("unit").get<UnitId>(), j.at("roll").get<BattleShockRoll>()};
    } else if (kind == "ShootingStarted") {
        v = ShootingStarted{j.at("unit").get<UnitId>(), j.at("type").get<ShootingType>(),
                            j.at("selections").get<std::vector<WeaponSelection>>()};
    } else if (kind == "ChargeDeclared") {
        v = ChargeDeclared{j.at("unit").get<UnitId>()};
    } else if (kind == "ChargeRolled") {
        v = ChargeRolled{j.at("unit").get<UnitId>(), j.at("roll").get<TwoD6>(),
                         j.at("eligible_targets").get<std::vector<UnitId>>()};
    } else if (kind == "ChargeTargetsSelected") {
        v = ChargeTargetsSelected{j.at("unit").get<UnitId>(), j.at("targets").get<std::vector<UnitId>>()};
    } else if (kind == "ChargeResolved") {
        v = ChargeResolved{j.at("unit").get<UnitId>(), j.at("charge_move_made").get<bool>(),
                           j.at("reason").get<std::string>()};
    } else if (kind == "PileInStarted") {
        v = PileInStarted{j.at("unit").get<UnitId>(), j.at("targets").get<std::vector<UnitId>>()};
    } else if (kind == "UnitSelectedToFight") {
        v = UnitSelectedToFight{j.at("unit").get<UnitId>(), j.at("type").get<FightType>(), j.at("forced").get<bool>()};
    } else if (kind == "FightPassed") {
        v = FightPassed{j.at("side").get<Side>()};
    } else if (kind == "MeleeAttacksDeclared") {
        v = MeleeAttacksDeclared{j.at("unit").get<UnitId>(), j.at("selections").get<std::vector<WeaponSelection>>()};
    } else if (kind == "FightStepEnded") {
        v = FightStepEnded{j.at("reason").get<std::string>()};
    } else if (kind == "ConsolidationStarted") {
        v = ConsolidationStarted{j.at("unit").get<UnitId>(), j.at("mode").get<ConsolidationMode>(),
                                 j.at("targets").get<std::vector<UnitId>>()};
    } else if (kind == "UnitsForcedToFight") {
        v = UnitsForcedToFight{j.at("units").get<std::vector<UnitId>>()};
    } else if (kind == "CommandPointGained") {
        v = CommandPointGained{j.at("side").get<Side>(), j.at("total").get<int>()};
    } else if (kind == "BattleShockEnded") {
        v = BattleShockEnded{j.at("unit").get<UnitId>()};
    } else if (kind == "ModelRemovedForCoherency") {
        v = ModelRemovedForCoherency{j.at("unit").get<UnitId>(), j.at("model").get<ModelId>()};
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
    } else if (kind == "WoundRollsRerolled") {
        v = WoundRollsRerolled{j.at("batch").get<std::size_t>(), j.at("rerolls").get<std::vector<WoundReroll>>(),
                               j.at("rolls").get<WoundRolls>()};
    } else {
        bad_enum("GameEvent", j);
    }
}

}  // namespace event

}  // namespace wh40k::core
