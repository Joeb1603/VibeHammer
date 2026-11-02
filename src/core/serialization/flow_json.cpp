// JSON for the attack data stored in GameState, the flow state and pending decisions.

#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

#include "core/serialization/json.hpp"
#include "core/serialization/json_util.hpp"

namespace wh40k::core {

using json_detail::bad_enum;
using json_detail::get_opt;
using json_detail::put_opt;

namespace {

Json abilities_to_json(const std::vector<WeaponAbility>& abilities) {
    Json out = Json::array();
    for (const auto& a : abilities) {
        Json e;
        ability::to_json(e, a);
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<WeaponAbility> abilities_from_json(const Json& j) {
    std::vector<WeaponAbility> out;
    for (const Json& e : j) {
        WeaponAbility a;
        ability::from_json(e, a);
        out.push_back(std::move(a));
    }
    return out;
}

}  // namespace

// ---- unit phase flags ----
void to_json(Json& j, const UnitPhaseFlags& v) {
    j = Json{{"selected_to_move", v.selected_to_move},
             {"desperate_escape_hazards_made", v.desperate_escape_hazards_made}};
    put_opt(j, "advance_roll", v.advance_roll);
}
void from_json(const Json& j, UnitPhaseFlags& v) {
    j.at("selected_to_move").get_to(v.selected_to_move);
    get_opt(j, "advance_roll", v.advance_roll);
    j.at("desperate_escape_hazards_made").get_to(v.desperate_escape_hazards_made);
}

// ---- moves ----
void to_json(Json& j, MoveType v) { j = std::string(move_type_name(v)); }
void from_json(const Json& j, MoveType& v) {
    const auto t = move_type_from_name(j.get<std::string>());
    if (!t) bad_enum("MoveType", j);
    v = *t;
}
void to_json(Json& j, FallBackMode v) { j = std::string(fall_back_mode_name(v)); }
void from_json(const Json& j, FallBackMode& v) {
    const auto m = fall_back_mode_from_name(j.get<std::string>());
    if (!m) bad_enum("FallBackMode", j);
    v = *m;
}
void to_json(Json& j, const ModelStart& v) { j = Json{{"model", v.model}, {"position", v.position}}; }
void from_json(const Json& j, ModelStart& v) {
    j.at("model").get_to(v.model);
    j.at("position").get_to(v.position);
}
void to_json(Json& j, const ModelPath& v) { j = Json{{"model", v.model}, {"points", v.points}}; }
void from_json(const Json& j, ModelPath& v) {
    j.at("model").get_to(v.model);
    j.at("points").get_to(v.points);
}
void to_json(Json& j, const MoveInProgress& v) {
    j = Json{{"unit", v.unit},
             {"type", v.type},
             {"starts", v.starts},
             {"paths", v.paths},
             {"pending_mortal_wounds", v.pending_mortal_wounds}};
    put_opt(j, "mode", v.mode);
}
void from_json(const Json& j, MoveInProgress& v) {
    j.at("unit").get_to(v.unit);
    j.at("type").get_to(v.type);
    get_opt(j, "mode", v.mode);
    j.at("starts").get_to(v.starts);
    j.at("paths").get_to(v.paths);
    j.at("pending_mortal_wounds").get_to(v.pending_mortal_wounds);
}

// ---- attack data ----
void to_json(Json& j, AttackType v) { j = (v == AttackType::Shooting) ? "Shooting" : "Fighting"; }
void from_json(const Json& j, AttackType& v) {
    const auto s = j.get<std::string>();
    if (s == "Shooting") v = AttackType::Shooting;
    else if (s == "Fighting") v = AttackType::Fighting;
    else bad_enum("AttackType", j);
}

void to_json(Json& j, const AttackContext& v) {
    j = Json{{"type", v.type}, {"heavy_conditions_met", v.heavy_conditions_met}};
}
void from_json(const Json& j, AttackContext& v) {
    j.at("type").get_to(v.type);
    j.at("heavy_conditions_met").get_to(v.heavy_conditions_met);
}

void to_json(Json& j, const WeaponSelection& v) {
    j = Json{{"attacker", v.attacker},
             {"weapon_index", v.weapon_index},
             {"profile_index", v.profile_index},
             {"target", v.target},
             {"target_within_half_range", v.target_within_half_range},
             {"target_models_at_selection", v.target_models_at_selection}};
    put_opt(j, "declared_attacks", v.declared_attacks);
}
void from_json(const Json& j, WeaponSelection& v) {
    j.at("attacker").get_to(v.attacker);
    j.at("weapon_index").get_to(v.weapon_index);
    j.at("profile_index").get_to(v.profile_index);
    j.at("target").get_to(v.target);
    get_opt(j, "declared_attacks", v.declared_attacks);
    j.at("target_within_half_range").get_to(v.target_within_half_range);
    j.at("target_models_at_selection").get_to(v.target_models_at_selection);
}

void to_json(Json& j, const AttackProfile& v) {
    j = Json{{"torrent", v.torrent},
             {"S", v.strength},
             {"AP", v.armour_penetration},
             {"D", v.damage},
             {"hit_modifier", v.hit_modifier},
             {"wound_modifier", v.wound_modifier},
             {"attack_time_abilities", abilities_to_json(v.attack_time_abilities)}};
    put_opt(j, "skill", v.skill);
}
void from_json(const Json& j, AttackProfile& v) {
    get_opt(j, "skill", v.skill);
    j.at("torrent").get_to(v.torrent);
    j.at("S").get_to(v.strength);
    j.at("AP").get_to(v.armour_penetration);
    j.at("D").get_to(v.damage);
    j.at("hit_modifier").get_to(v.hit_modifier);
    j.at("wound_modifier").get_to(v.wound_modifier);
    v.attack_time_abilities = abilities_from_json(j.at("attack_time_abilities"));
}

void to_json(Json& j, const AttackBatch& v) {
    j = Json{{"target", v.target},
             {"profile", v.profile},
             {"selections", v.selections},
             {"attack_dice", v.attack_dice}};
}
void from_json(const Json& j, AttackBatch& v) {
    j.at("target").get_to(v.target);
    j.at("profile").get_to(v.profile);
    j.at("selections").get_to(v.selections);
    j.at("attack_dice").get_to(v.attack_dice);
}

void to_json(Json& j, const AllocationGroup& v) {
    j = Json{{"models", v.models}, {"character", v.character}, {"W", v.wounds}};
    put_opt(j, "Sv", v.save);
    put_opt(j, "InSv", v.invuln_save);
}
void from_json(const Json& j, AllocationGroup& v) {
    j.at("models").get_to(v.models);
    j.at("character").get_to(v.character);
    j.at("W").get_to(v.wounds);
    get_opt(j, "Sv", v.save);
    get_opt(j, "InSv", v.invuln_save);
}

// ---- flow ----
void to_json(Json& j, Stage v) { j = std::string(stage_name(v)); }
void from_json(const Json& j, Stage& v) {
    const auto s = stage_from_name(j.get<std::string>());
    if (!s) bad_enum("Stage", j);
    v = *s;
}

void to_json(Json& j, Phase v) { j = std::string(phase_name(v)); }
void from_json(const Json& j, Phase& v) {
    const auto p = phase_from_name(j.get<std::string>());
    if (!p) bad_enum("Phase", j);
    v = *p;
}

void to_json(Json& j, ActivationKind v) {
    switch (v) {
        case ActivationKind::Move: j = "Move"; return;
        case ActivationKind::Shoot: j = "Shoot"; return;
        case ActivationKind::Fight: j = "Fight"; return;
    }
    throw std::logic_error("to_json: invalid ActivationKind");
}
void from_json(const Json& j, ActivationKind& v) {
    const auto s = j.get<std::string>();
    if (s == "Move") v = ActivationKind::Move;
    else if (s == "Shoot") v = ActivationKind::Shoot;
    else if (s == "Fight") v = ActivationKind::Fight;
    else bad_enum("ActivationKind", j);
}

void to_json(Json& j, const Activation& v) { j = Json{{"unit", v.unit}, {"kind", v.kind}}; }
void from_json(const Json& j, Activation& v) {
    j.at("unit").get_to(v.unit);
    j.at("kind").get_to(v.kind);
}

void to_json(Json& j, const BatchInProgress& v) {
    j = Json{{"batch", v.batch},
             {"wounds", v.wounds},
             {"groups", v.groups},
             {"order", v.order},
             {"save_rolls", v.save_rolls}};
}
void from_json(const Json& j, BatchInProgress& v) {
    j.at("batch").get_to(v.batch);
    j.at("wounds").get_to(v.wounds);
    j.at("groups").get_to(v.groups);
    j.at("order").get_to(v.order);
    j.at("save_rolls").get_to(v.save_rolls);
}

void to_json(Json& j, const AttackFlow& v) {
    j = Json{{"attacker", v.attacker},
             {"context", v.context},
             {"selections", v.selections},
             {"batches", v.batches},
             {"batch_done", v.batch_done}};
    put_opt(j, "current_target", v.current_target);
    put_opt(j, "current", v.current);
}
void from_json(const Json& j, AttackFlow& v) {
    j.at("attacker").get_to(v.attacker);
    j.at("context").get_to(v.context);
    j.at("selections").get_to(v.selections);
    j.at("batches").get_to(v.batches);
    v.batch_done = j.at("batch_done").get<std::vector<bool>>();
    get_opt(j, "current_target", v.current_target);
    get_opt(j, "current", v.current);
}

void to_json(Json& j, const FlowState& v) {
    j = Json{{"stage", v.stage}, {"battle_round", v.battle_round}, {"turn_in_round", v.turn_in_round}};
    put_opt(j, "activation", v.activation);
    put_opt(j, "attack", v.attack);
    put_opt(j, "move", v.move);
}
void from_json(const Json& j, FlowState& v) {
    j.at("stage").get_to(v.stage);
    j.at("battle_round").get_to(v.battle_round);
    j.at("turn_in_round").get_to(v.turn_in_round);
    get_opt(j, "activation", v.activation);
    get_opt(j, "attack", v.attack);
    get_opt(j, "move", v.move);
}

void to_json(Json& j, const GameResult& v) {
    j = Json{{"reason", v.reason}};
    put_opt(j, "winner", v.winner);
}
void from_json(const Json& j, GameResult& v) {
    get_opt(j, "winner", v.winner);
    j.at("reason").get_to(v.reason);
}

// ---- decisions ----
void to_json(Json& j, const PendingDecision& v) {
    j = Json{{"side", v.side}, {"kind", std::string(decision_name(v.kind))}};
    std::visit(
        [&j](const auto& k) {
            using T = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<T, decision::MoveModels>) {
                j["unit"] = k.unit;
            } else if constexpr (std::is_same_v<T, decision::SelectAttackBatch>) {
                j["attacker"] = k.attacker;
            } else if constexpr (std::is_same_v<T, decision::DeclareAllocationOrder> ||
                                 std::is_same_v<T, decision::OrderCasualties>) {
                j["target"] = k.target;
            }
        },
        v.kind);
}
void from_json(const Json& j, PendingDecision& v) {
    j.at("side").get_to(v.side);
    const auto kind = j.at("kind").get<std::string>();
    if (kind == "MoveUnits") v.kind = decision::MoveUnits{};
    else if (kind == "MoveModels") v.kind = decision::MoveModels{j.at("unit").get<UnitId>()};
    else if (kind == "Shoot") v.kind = decision::Shoot{};
    else if (kind == "Charge") v.kind = decision::Charge{};
    else if (kind == "SelectAttackBatch") v.kind = decision::SelectAttackBatch{j.at("attacker").get<UnitId>()};
    else if (kind == "DeclareAllocationOrder") v.kind = decision::DeclareAllocationOrder{j.at("target").get<UnitId>()};
    else if (kind == "OrderCasualties") v.kind = decision::OrderCasualties{j.at("target").get<UnitId>()};
    else bad_enum("PendingDecision", j);
}

}  // namespace wh40k::core
