// 09 Movement phase moves (09.04-09.07) and 03.01 Moving Units, as an engine sub-machine:
// StartMove -> [hazard casualties] -> ProposeModelMove* -> CommitUnitMove | ResetUnitMove.

#include "core/engine/movement.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <variant>

#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"
#include "core/geometry/closest.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/oval.hpp"
#include "core/geometry/path.hpp"
#include "core/geometry/units.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/charge.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/leadership.hpp"
#include "core/rules/mortal_wounds.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core {

namespace detail {

namespace {

const MoveInProgress& move_of(const GameState& state) {
    if (!state.flow.move) {
        throw std::logic_error("movement: no move in progress");
    }
    return *state.flow.move;
}

MoveInProgress& move_of(GameState& state) {
    if (!state.flow.move) {
        throw std::logic_error("movement: no move in progress");
    }
    return *state.flow.move;
}

std::string model_label(ModelId id) { return "model " + std::to_string(id.v); }

bool can_move(const Unit& u) {
    // 02.02: a model with M '-' can be set up but cannot otherwise be moved.
    return std::all_of(u.models.begin(), u.models.end(),
                       [](const Model& m) { return m.characteristics.move_mm.has_value(); });
}

std::vector<ModelId> ids_of(const Unit& u) {
    std::vector<ModelId> out;
    for (const Model& m : u.models) out.push_back(m.id);
    return out;
}

// Clears the move, and the activation unless a charge (11.02) or a fight (12.06 overrun pile-in) is
// still being resolved.
void finish_move(GameState& state) {
    state.flow.move.reset();
    if (!state.flow.charge && !state.flow.fight) {
        state.flow.activation.reset();
    }
}

bool is_movement_phase_move(MoveType t) {
    return t == MoveType::Normal || t == MoveType::Advance || t == MoveType::FallBack;
}

void revert_move(GameState& state, std::vector<std::string> reasons, Events& events) {
    MoveInProgress& mv = move_of(state);
    Unit* u = state.find_unit(mv.unit);
    for (const ModelStart& s : mv.starts) {
        if (Model* m = find_model(*u, s.model)) {
            m->position = s.position;
            m->facing_deg = s.facing_deg;  // D-074: its facing at the start of the move too (03.01 Ending a Move)
        }
    }
    // 03 sidebar: the unit has not been selected to make that move, so it can be selected again. A charge
    // move goes back to selecting charge targets with the same charge roll (D-018).
    // Pile-in and consolidation moves go back to the same decision (D-022).
    if (is_movement_phase_move(mv.type)) {
        u->phase_flags.selected_to_move = false;
    }
    events.push_back(event::UnitMoveReverted{mv.unit, std::move(reasons)});
    finish_move(state);
}

// D-074 part 2 (M65): why a proposal's facings can't be used, or nullopt. None (no turn) or one per path vertex, each
// a finite number of degrees; a round base has no facing, so only 0s.
std::optional<std::string> facings_problem(const Model& m, const action::ProposeModelMove& a) {
    if (a.facings.empty()) return std::nullopt;
    if (a.facings.size() != a.path.size()) {
        return "D-074: " + model_label(a.model) + "'s move gives " + std::to_string(a.facings.size()) +
               " facings for " + std::to_string(a.path.size()) + " path points; give one facing per path point, or none";
    }
    for (std::size_t k = 0; k < a.facings.size(); ++k) {
        if (!std::isfinite(a.facings[k])) {
            return "D-074: " + model_label(a.model) + "'s facing at path point " + std::to_string(k + 1) +
                   " is not a finite number of degrees";
        }
    }
    if (m.base.shape == BaseShape::Round &&
        std::any_of(a.facings.begin(), a.facings.end(), [](float f) { return f != 0.0f; })) {
        return "D-074: " + model_label(a.model) + " has a round base, which has no facing; its facings must be 0";
    }
    return std::nullopt;
}

// D-074 part 2: a proposal's path as the kernel checks it and the state keeps it.
// - `points`: the move's start position, then each straight line's end (03.01).
// - `facings`: for an oval, its facing at each point, modulo 180 (footprint_facing_deg): the start facing first, then
//   the proposal's (or the start facing at every point when it gives none). Empty for a round base.
// - `kernel_facings`: the same, unwrapped: each is the one before plus the footprint turn between them (at most 90
//   degrees, footprint_turn_deg), so the kernel's own "shorter way" turn sweeps exactly that turn. Empty for a round
//   base (the kernel ignores a round base's facing).
struct ProposedPath {
    std::vector<Position> points;
    std::vector<float> facings;
    std::vector<float> kernel_facings;
};

ProposedPath proposed_path(const GameState& state, const Model& m, const action::ProposeModelMove& a) {
    ProposedPath p;
    p.points.reserve(a.path.size() + 1);
    p.points.push_back(*move_start_position(state, a.model));
    p.points.insert(p.points.end(), a.path.begin(), a.path.end());
    if (m.base.shape == BaseShape::Round) return p;
    const float start = footprint_facing_deg(move_start_facing_deg(state, a.model).value_or(0.0f));
    p.facings.reserve(p.points.size());
    p.kernel_facings.reserve(p.points.size());
    p.facings.push_back(start);
    p.kernel_facings.push_back(start);
    for (std::size_t k = 0; k < a.path.size(); ++k) {
        const float f = a.facings.empty() ? start : footprint_facing_deg(a.facings[k]);
        const double turn = footprint_turn_deg(p.facings.back(), f);
        p.kernel_facings.push_back(static_cast<float>(static_cast<double>(p.kernel_facings.back()) + turn));
        p.facings.push_back(f);
    }
    return p;
}

// The model's pose at a point of the path (a round base has facing 0).
BasePose pose_at(const Model& m, const ProposedPath& p, std::size_t i) {
    return BasePose{p.points[i], m.base, p.facings.empty() ? 0.0f : p.facings[i]};
}

}  // namespace

// ---- StartMove for the moves that move models ----

std::vector<GameAction> legal_moves_for_unit(const GameState& state, const Unit& u) {
    std::vector<GameAction> out{action::StartMove{u.id, MoveType::RemainStationary, std::nullopt}};  // 09.04
    if (!can_move(u)) {
        return out;
    }
    if (!is_engaged(state, u)) {
        out.push_back(action::StartMove{u.id, MoveType::Normal, std::nullopt});   // 09.05
        out.push_back(action::StartMove{u.id, MoveType::Advance, std::nullopt});  // 09.06
    } else {
        if (!u.battle_shocked) {
            out.push_back(action::StartMove{u.id, MoveType::FallBack, FallBackMode::OrderedRetreat});  // 09.07
        }
        out.push_back(action::StartMove{u.id, MoveType::FallBack, FallBackMode::DesperateEscape});
    }
    return out;
}

ValidationResult validate_move_type(const GameState& state, const Unit& u, const action::StartMove& a) {
    if (a.move_type != MoveType::FallBack && a.fall_back_mode) {
        return ValidationResult::failure("09.02: only a Fall-back move selects a fall-back mode");
    }
    if (a.move_type == MoveType::RemainStationary) {
        return ValidationResult::success();  // 09.04: eligible if any unit
    }
    if (!can_move(u)) {
        return ValidationResult::failure("02.02: " + unit_label(u.id) + " has a model with M '-' and cannot move");
    }
    const bool engaged = is_engaged(state, u);
    switch (a.move_type) {
        case MoveType::Normal:
            if (engaged) return ValidationResult::failure("09.05: a Normal move needs the unit to be unengaged");
            return ValidationResult::success();
        case MoveType::Advance:
            if (engaged) return ValidationResult::failure("09.06: an Advance move needs the unit to be unengaged");
            return ValidationResult::success();
        case MoveType::FallBack:
            if (!engaged) return ValidationResult::failure("09.07: a Fall-back move needs the unit to be engaged");
            if (!a.fall_back_mode) return ValidationResult::failure("09.07: select a fall-back mode");
            if (*a.fall_back_mode == FallBackMode::OrderedRetreat && u.battle_shocked) {
                return ValidationResult::failure(
                    "09.07: a battle-shocked unit cannot select Ordered Retreat; it must select Desperate Escape");
            }
            return ValidationResult::success();
        case MoveType::Charge:
            return ValidationResult::failure("09.02: a Charge move is made in the Charge phase (11.02, 11.04)");
        case MoveType::PileIn:
        case MoveType::Consolidation:
            return ValidationResult::failure("09.02: pile-in and consolidation moves are made in the Fight phase "
                                             "(12.03, 12.08)");
        case MoveType::RemainStationary:
            break;
    }
    return ValidationResult::success();
}

void begin_move(GameState& state, const action::StartMove& a, DiceSource& dice, Events& events) {
    Unit& u = *state.find_unit(a.unit);
    events.push_back(event::MoveStarted{u.id, a.move_type, a.fall_back_mode});

    MoveInProgress mv;
    mv.unit = u.id;
    mv.type = a.move_type;
    mv.mode = a.fall_back_mode;

    if (a.move_type == MoveType::Advance) {
        // 09.06 Before Moving: advance roll. D-005: a roll already made this phase is reused.
        const bool reused = u.phase_flags.advance_roll.has_value();
        if (!reused) {
            u.phase_flags.advance_roll = dice.roll_d6();
        }
        events.push_back(event::AdvanceRolled{u.id, *u.phase_flags.advance_roll, reused});
    }
    if (a.move_type == MoveType::FallBack && a.fall_back_mode == FallBackMode::DesperateEscape &&
        !u.phase_flags.desperate_escape_hazards_made) {
        // 09.07 Before Moving: a hazard roll for each model (06.03). D-005: made once per phase.
        const HazardRolls rolls = make_hazard_rolls(dice, u, static_cast<int>(u.models.size()));
        u.phase_flags.desperate_escape_hazards_made = true;
        events.push_back(event::HazardRollsMade{u.id, rolls});
        mv.pending_mortal_wounds = rolls.mortal_wounds;
    }
    for (const Model& m : u.models) {
        mv.starts.push_back(ModelStart{m.id, m.position, m.facing_deg});
    }
    state.flow.activation = Activation{u.id, ActivationKind::Move};  // 01.03
    state.flow.move = std::move(mv);
}

// ---- the sub-machine ----

std::optional<PendingDecision> move_decision(const GameState& state) {
    const MoveInProgress& mv = move_of(state);
    const Unit& u = *state.find_unit(mv.unit);
    if (is_destroyed(u)) {
        return std::nullopt;  // the move ends (move_progress)
    }
    if (mv.pending_mortal_wounds > 0) {
        // 06.02 Select Model, answered once (D-013). Every mortal wound selects a model, so the player is
        // always asked, even when the selection is forced (D-051).
        return PendingDecision{u.side, decision::OrderCasualties{u.id}};
    }
    return PendingDecision{u.side, decision::MoveModels{u.id}};
}

void inflict_mortal_wounds_by_priority(GameState&, Unit& u, int count, const std::vector<ModelId>& priority,
                                       Events& events) {
    const ModelChooser by_priority = [&priority](const Unit&, std::span<const ModelId> options) {
        for (const ModelId id : priority) {
            if (std::find(options.begin(), options.end(), id) != options.end()) return id;
        }
        throw std::logic_error("06.02: the casualty order has no legal model");
    };
    const MortalWoundsResult result = inflict_mortal_wounds(u, count, by_priority);
    events.push_back(event::MortalWoundsInflicted{u.id, result});
    for (const DamageResult& d : result.steps) {
        if (d.destroyed) events.push_back(event::ModelDestroyed{u.id, d.model});
    }
    if (is_destroyed(u)) {
        events.push_back(event::UnitDestroyed{u.id});
    }
}

void resolve_move_mortal_wounds(GameState& state, const std::vector<ModelId>& priority, Events& events) {
    MoveInProgress& mv = move_of(state);
    Unit& u = *state.find_unit(mv.unit);
    const int count = mv.pending_mortal_wounds;
    mv.pending_mortal_wounds = 0;
    inflict_mortal_wounds_by_priority(state, u, count, priority, events);
}

void move_progress(GameState& state, DiceSource&, Events& events) {
    const MoveInProgress& mv = move_of(state);
    const Unit& u = *state.find_unit(mv.unit);
    if (is_destroyed(u)) {
        finish_move(state);  // every model was destroyed before it could move
        return;
    }
    if (mv.pending_mortal_wounds > 0) {
        // Not reached since D-051 (move_decision always asks); kept so progress can't stall.
        resolve_move_mortal_wounds(state, ids_of(u), events);
        return;
    }
    throw std::logic_error("move_progress: the move is waiting for a decision");
}

// ---- 03.01 MoveModels ----

std::vector<GameAction> legal_for(const GameState& state, const decision::MoveModels& d) {
    std::vector<GameAction> out;
    for (const Model& m : state.find_unit(d.unit)->models) {
        out.push_back(action::ProposeModelMove{m.id, {}});  // template: the path is open (D-004)
    }
    out.push_back(action::CommitUnitMove{});
    out.push_back(action::ResetUnitMove{});
    return out;
}

ValidationResult validate_details(const GameState& state, Side, const action::ProposeModelMove& a) {
    const MoveInProgress& mv = move_of(state);
    const Unit& u = *state.find_unit(mv.unit);
    const Model* m = find_model(u, a.model);
    if (m == nullptr) {
        return ValidationResult::failure("03.01: " + model_label(a.model) + " is not in the moving unit (" +
                                         unit_label(mv.unit) + ")");
    }
    if (a.path.empty()) {
        return ValidationResult::failure("03.01: the path is empty; give the end point of each straight line");
    }
    if (const auto problem = facings_problem(*m, a)) {
        return ValidationResult::failure(*problem);  // D-074 part 2
    }
    const ProposedPath path = proposed_path(state, *m, a);
    const BasePose start = pose_at(*m, path, 0);
    const BasePose end = pose_at(*m, path, path.points.size() - 1);

    // 02.02: a model with M '-' can be set up but cannot otherwise be moved (any move type, including
    // pile-in, consolidation and charge moves; D-074: nor turned).
    const std::optional<float> max_mm = max_move_distance_mm(state, a.model);
    if (!m->characteristics.move_mm || !max_mm) {
        return ValidationResult::failure("02.02: " + model_label(a.model) + " has M '-' and cannot be moved");
    }
    ValidationResult r;
    // 03.01 Moving a Model in a Straight Line: total distance <= the move type's maximum distance. Rotating a model
    // does not count towards the distance it has moved (03.01), so this is the centre's straight lines only.
    const float length = path_length(path.points);
    const float max = *max_mm;
    if (more_than(length, max)) {
        r.reasons.push_back(std::string(move_type_rule(mv.type)) + ": " + model_label(a.model) + " would move " +
                            format_inches(length) + " but the maximum distance is " + format_inches(max));
    }
    // 03.01: enemy models and battlefield edges, for every straight line and (D-074) every turn. D-027: the path may
    // pass through friendly models but not end on any model at its current position (including models of this unit
    // already moved); the commit still checks 03.01 Ending a Move for the whole unit.
    // 17.01 (D-073): a MONSTER/VEHICLE model's Normal or Advance move may pass through (and turn through) enemy models
    // that are not MONSTER/VEHICLE models (still not end on them).
    std::vector<PosedModel> enemies;
    std::vector<PosedModel> passable;
    if (moves_through_non_monster_enemies(mv.type, u, *m)) {
        for (const Unit& other : state.units) {
            if (other.side == u.side) continue;
            for (const Model& o : other.models) {
                (is_monster_or_vehicle(other, o) ? enemies : passable).push_back(posed_of(o));
            }
        }
    } else {
        enemies = posed_enemy_models(state, u.side);
    }
    const std::vector<PosedModel> friends = posed_friendly_models(state, u.side);
    const PosedPathContext context{Rect{0.0f, 0.0f, state.battlefield.width_mm, state.battlefield.depth_mm}, friends,
                                   enemies, mv.mode == FallBackMode::DesperateEscape,  // 09.07 While Moving
                                   passable};
    for (const PathViolation& v : check_model_path(a.model, m->base, path.points, path.kernel_facings, context)) {
        r.reasons.push_back(v.reason);
    }
    if (mv.type == MoveType::Charge && !ends_closer_to_a_charge_target(state, start, end, mv.targets)) {
        // 11.04 While Moving (03.01: checked for each model that is moved; D-074: a turn does not count as moving
        // closer).
        r.reasons.push_back("11.04: " + model_label(a.model) + " must end its move closer to one or more charge "
                            "targets");
    }
    const bool pile_in = mv.type == MoveType::PileIn;
    const bool ongoing = mv.consolidation_mode == ConsolidationMode::Ongoing;
    if (pile_in || mv.type == MoveType::Consolidation) {
        const std::string rule = pile_in ? "12.03" : "12.08";
        // 12.03 / 12.08 Ongoing While Moving: models in base contact with an enemy model cannot be moved (D-074: nor
        // turned; measured at the start pose).
        if ((pile_in || ongoing) && in_base_contact_with_enemy(state, u, start)) {
            r.reasons.push_back(rule + ": " + model_label(a.model) +
                                " is in base contact with an enemy model and cannot be moved");
        }
        // Each model that is moved must end its move closer to the closest target (D-022; D-074: a turn does not
        // count as moving closer).
        if (!ends_closer_to_closest_target(state, start, end, mv.targets)) {
            r.reasons.push_back(rule + ": " + model_label(a.model) + " must end its move closer to the closest " +
                                (pile_in ? "pile-in target" : "selected enemy unit"));
        }
        // TODO(if-possible): 12.03 / 12.08 While Moving: "and engaged with it if possible".
    }
    // TODO(if-possible): 11.04 While Moving: each model that can end its move within 1" of (and engaged
    // with) a charge target must do so. V1 leaves this to the player (CLAUDE.md "If possible" rules).
    return r;
}

void apply_details(GameState& state, Side, const action::ProposeModelMove& a, DiceSource&, Events& events) {
    MoveInProgress& mv = move_of(state);
    Model* m = find_model(*state.find_unit(mv.unit), a.model);
    ProposedPath path = proposed_path(state, *m, a);
    m->position = path.points.back();
    // D-074 part 2: an oval ends at its last facing (modulo 180). The path's facings are kept only when it turns the
    // model somewhere, so a path without a turn (every round model's) is stored and reported as before.
    std::vector<float> facings;
    if (!path.facings.empty()) {
        m->facing_deg = path.facings.back();
        const float first = path.facings.front();
        if (std::any_of(path.facings.begin(), path.facings.end(), [first](float f) { return f != first; })) {
            facings = path.facings;
        }
    }
    const float length = path_length(path.points);
    auto it = std::find_if(mv.paths.begin(), mv.paths.end(), [&](const ModelPath& p) { return p.model == a.model; });
    if (it == mv.paths.end()) {
        mv.paths.push_back(ModelPath{a.model, path.points, facings});
    } else {
        it->points = path.points;  // proposing again replaces the earlier path
        it->facings = facings;
    }
    events.push_back(event::ModelMoveProposed{mv.unit, a.model, std::move(path.points), length, std::move(facings)});
}

ValidationResult validate_details(const GameState&, Side, const action::CommitUnitMove&) {
    return ValidationResult::success();  // always possible; failing end conditions revert the move (03.01)
}

void apply_details(GameState& state, Side, const action::CommitUnitMove&, DiceSource& dice, Events& events) {
    std::vector<std::string> problems = move_end_problems(state);
    if (!problems.empty()) {
        revert_move(state, std::move(problems), events);  // 03.01: the unit cannot make that move
        return;
    }
    const MoveInProgress mv = move_of(state);
    Unit& u = *state.find_unit(mv.unit);
    for (const ModelPath& p : mv.paths) {
        if (more_than(path_length(p.points), inches_to_mm(3.0f))) {
            u.turn_flags.moved_over_3in = true;  // 24.16 [HEAVY]
        }
    }
    if (mv.type == MoveType::Advance) u.turn_flags.advanced = true;   // 09.06 After Moving
    if (mv.type == MoveType::FallBack) u.turn_flags.fell_back = true;  // 09.07 After Moving
    events.push_back(event::UnitMoveCommitted{u.id, mv.type});
    if (mv.type == MoveType::PileIn || mv.type == MoveType::Consolidation) {
        fight_move_committed(state, mv, events);  // 12.02 / 12.06 / 12.07 / 12.08 After Moving
    }
    if (mv.type == MoveType::Charge) {
        // 11.04 After Moving: every model has Fights First (24.13) until the end of the turn.
        u.turn_flags.made_charge_move = true;
        u.turn_flags.fights_first = true;
        finish_move(state);
        resolve_charge(state, true, "", events);  // 11.02 Attempt Charge: the charge is resolved
        return;
    }
    if (mv.mode == FallBackMode::DesperateEscape && !u.battle_shocked) {
        // 09.07 After Moving: Desperate Escape battle-shock roll (01.07; one roll, D-015).
        const BattleShockRoll roll = make_battle_shock_roll(dice, u);
        apply_battle_shock_roll(u, roll);
        events.push_back(event::BattleShockRolled{u.id, roll});
    }
    finish_move(state);
}

ValidationResult validate_details(const GameState&, Side, const action::ResetUnitMove&) {
    return ValidationResult::success();
}

void apply_details(GameState& state, Side, const action::ResetUnitMove&, DiceSource&, Events& events) {
    revert_move(state, {}, events);
}

}  // namespace detail

// ---- public queries ----

std::optional<float> max_move_distance_mm(const GameState& state, ModelId model) {
    if (!state.flow.move) return std::nullopt;
    const MoveInProgress& mv = *state.flow.move;
    const Unit* u = state.find_unit(mv.unit);
    const Model* m = u == nullptr ? nullptr : find_model(*u, model);
    if (m == nullptr || !m->characteristics.move_mm) return std::nullopt;
    if (mv.type == MoveType::Charge) {
        if (!u->phase_flags.charge_roll) return std::nullopt;
        return inches_to_mm(static_cast<float>(*u->phase_flags.charge_roll));  // 11.04: the charge roll
    }
    if (mv.type == MoveType::PileIn) return inches_to_mm(kPileInDistanceInches);                // 12.03
    if (mv.type == MoveType::Consolidation) return inches_to_mm(kConsolidationDistanceInches);  // 12.08
    float max = *m->characteristics.move_mm;  // 09.05 / 09.07: M
    if (mv.type == MoveType::Advance && u->phase_flags.advance_roll) {
        max += inches_to_mm(static_cast<float>(*u->phase_flags.advance_roll));  // 09.06: advance roll + M
    }
    return max;
}

bool moves_through_non_monster_enemies(MoveType type, const Unit& unit, const Model& model) {
    // 17.01: normal (09.05) and advance (09.06) moves only; not Fall Back, charge, pile-in or consolidation (D-073).
    return (type == MoveType::Normal || type == MoveType::Advance) && is_monster_or_vehicle(unit, model);
}

namespace {

// D-074 part 2: a reach area for the pair (`m` moving at `facing`, other model `o`): today's circle for a round pair,
// a ReachShape with `threshold_mm` otherwise (the same set: a round pair's circle has radius both radii + threshold).
struct ReachArea {
    Base mover;  // the moving model's base
    float facing = 0.0f;
    bool round_mover = true;  // the moving model's base is round
    [[nodiscard]] bool circle_for(const Model& o) const { return round_mover && o.base.shape == BaseShape::Round; }
    [[nodiscard]] ReachShape shape(const Model& o, float threshold_mm, bool engagement) const {
        return ReachShape{mover, facing, pose_of(o), threshold_mm, engagement, o.id};
    }
    // Whether a shape with `threshold_mm` can hold any point: it lies inside the disc of radius circumscribed +
    // circumscribed + threshold round the other model (as a circle is kept only with a positive radius).
    [[nodiscard]] bool may_hold(const Model& o, float threshold_mm) const {
        return circumscribed_radius(mover) + circumscribed_radius(o.base) + threshold_mm > 0.0f;
    }
};

// D-074 part 2: keeps MoveReach::required_any_shapes either empty or one group per required_any group.
void add_required_group(MoveReach& reach, std::vector<ReachCircle> circles, std::vector<ReachShape> shapes) {
    const std::size_t before = reach.required_any.size();
    reach.required_any.push_back(std::move(circles));
    if (shapes.empty() && reach.required_any_shapes.empty()) return;
    reach.required_any_shapes.resize(before);  // the earlier groups had no shape (a no-op once they are in step)
    reach.required_any_shapes.push_back(std::move(shapes));
}

// D-074 part 2: the "closer" threshold's start distance to `target`: from the start position at the start facing,
// and (a turn does not count as moving closer) at the facing the reach is for, whichever is smaller.
std::optional<float> closer_before(const BasePose& start, float facing, const Unit& target) {
    std::optional<float> before = model_distance_to_unit(start, target);
    if (before && start.base.shape != BaseShape::Round && footprint_turn_deg(start.facing_deg, facing) != 0.0) {
        const auto turned = model_distance_to_unit(BasePose{start.position, start.base, facing}, target);
        if (turned) before = std::min(*before, *turned);
    }
    return before;
}

}  // namespace

bool reach_shape_contains(const ReachShape& shape, const Position& centre) {
    return signed_base_distance(BasePose{centre, shape.mover_base, shape.mover_facing_deg}, shape.obstacle) <
           shape.threshold_mm;
}

float reach_shape_inner_radius_mm(const ReachShape& shape) {
    return inscribed_radius(shape.mover_base) + inscribed_radius(shape.obstacle.base) + shape.threshold_mm;
}

float reach_shape_outer_radius_mm(const ReachShape& shape) {
    return circumscribed_radius(shape.mover_base) + circumscribed_radius(shape.obstacle.base) + shape.threshold_mm;
}

std::optional<MoveReach> move_reach(const GameState& state, ModelId model) {
    const Model* m = find_model(state, model);
    return move_reach(state, model, m == nullptr ? 0.0f : m->facing_deg);
}

std::optional<MoveReach> move_reach(const GameState& state, ModelId model, float facing_deg) {
    if (!state.flow.move) return std::nullopt;
    const MoveInProgress& mv = *state.flow.move;
    const Unit* u = state.find_unit(mv.unit);
    const Model* m = u == nullptr ? nullptr : find_model(*u, model);
    const std::optional<float> max = max_move_distance_mm(state, model);
    const std::optional<Position> start = move_start_position(state, model);
    if (m == nullptr || !max || !start) return std::nullopt;
    const bool round_mover = m->base.shape == BaseShape::Round;
    // D-074: the facing the reach is for (the end of the next straight line); a round base has none.
    const float facing = round_mover ? 0.0f : footprint_facing_deg(facing_deg);
    const BasePose start_pose{*start, m->base, round_mover ? 0.0f : move_start_facing_deg(state, model).value_or(0.0f)};
    const ReachArea area{m->base, facing, round_mover};
    MoveReach reach;
    reach.start = *start;
    reach.max_mm = *max;
    reach.from = m->position;
    float moved = 0.0f;
    for (const ModelPath& p : mv.paths) {
        if (p.model == model) moved = path_length(p.points);
    }
    reach.remaining_mm = std::max(0.0f, *max - moved);
    reach.facing_deg = facing;
    // 03.01: the base can't cross the battlefield's edge: its centre stays its half extents inside (D-074: at
    // `facing`; for a round base its radius, as before).
    const BaseExtents ext = base_extents(m->base, facing);
    reach.centre_bounds = Rect{ext.half_x_mm, ext.half_y_mm, state.battlefield.width_mm - ext.half_x_mm,
                               state.battlefield.depth_mm - ext.half_y_mm};
    // 09.05 / 09.06 / 09.07 After Moving: the unit must be unengaged.
    reach.must_end_unengaged =
        mv.type == MoveType::Normal || mv.type == MoveType::Advance || mv.type == MoveType::FallBack;
    // 09.07 While Moving: a Desperate Escape may move through enemy models.
    const bool through_enemies = mv.mode == FallBackMode::DesperateEscape;
    // 17.01 (D-073): a MONSTER/VEHICLE model's Normal or Advance move passes non-MONSTER/VEHICLE enemy models.
    const bool through_non_monsters = moves_through_non_monster_enemies(mv.type, *u, *m);
    // D-074 / 03.01 Rotating a Model: the next straight line starts after turning at `from` to `facing`, which must
    // clear the battlefield edge and every enemy model the line may not pass through.
    const double turn = round_mover ? 0.0 : footprint_turn_deg(m->facing_deg, facing);
    const BasePose turning{m->position, m->base, m->facing_deg};
    const float turn_to = static_cast<float>(static_cast<double>(m->facing_deg) + turn);
    const Rect battlefield{0.0f, 0.0f, state.battlefield.width_mm, state.battlefield.depth_mm};
    if (turn != 0.0) reach.turn_blocked = rotation_against_edge(battlefield, turning, turn_to).blocked;
    const float r = round_mover ? base_radius(m->base) : 0.0f;  // a round mover's radius (round pairs only)
    for (const Unit& other : state.units) {
        for (const Model& o : other.models) {
            if (o.id == m->id) continue;
            const bool circle = area.circle_for(o);
            const float touching = circle ? r + base_radius(o.base) : 0.0f;
            if (other.side != u->side && !through_enemies &&
                !(through_non_monsters && !is_monster_or_vehicle(other, o))) {
                // 03.01: its base cannot be moved through enemy models (check_model_path's limit).
                if (circle) {
                    reach.blocking.push_back(ReachCircle{o.position, touching - EPS_MM, false, o.id});
                } else {
                    reach.blocking_shapes.push_back(area.shape(o, -EPS_MM, false));
                }
                if (turn != 0.0 && !reach.turn_blocked) {
                    reach.turn_blocked = rotation_against_base(turning, turn_to, pose_of(o)).blocked;
                }
            }
            // D-046: 11.04 After Moving: the unit cannot be engaged with enemy units that are not charge
            // targets, so no model may end within their engagement range.
            const bool non_target = mv.type == MoveType::Charge && other.side != u->side &&
                                    std::find(mv.targets.begin(), mv.targets.end(), other.id) == mv.targets.end();
            if (other.side != u->side && (reach.must_end_unengaged || non_target)) {
                // 03.04: within 2" is engaged; ending more than 2" away (EPS policy) is unengaged.
                if (circle) {
                    reach.forbidden.push_back(
                        ReachCircle{o.position, touching + kEngagementRangeMm + EPS_MM, true, o.id});
                } else {
                    reach.forbidden_shapes.push_back(area.shape(o, kEngagementRangeMm + EPS_MM, true));
                }
            } else if (circle) {
                reach.forbidden.push_back(ReachCircle{o.position, touching, false, o.id});  // 03.01: not on it
            } else {
                reach.forbidden_shapes.push_back(area.shape(o, 0.0f, false));
            }
        }
    }
    // The "closer" areas: inside a circle (round pair) or shape round each model of `target`, `before - EPS` closer.
    const auto add_closer = [&](const Unit& target, float before, std::vector<ReachCircle>& closer,
                                std::vector<ReachShape>& closer_shapes) {
        for (const Model& t : target.models) {
            if (area.circle_for(t)) {
                const float radius = r + base_radius(t.base) + before - EPS_MM;
                if (radius > 0.0f) closer.push_back(ReachCircle{t.position, radius, false, t.id});
            } else if (area.may_hold(t, before - EPS_MM)) {
                closer_shapes.push_back(area.shape(t, before - EPS_MM, false));
            }
        }
    };
    if (mv.type == MoveType::Charge) {
        // D-046: 11.04 While Moving: end closer to one or more charge targets, each measured from the
        // start (as ends_closer_to_a_charge_target; D-074: a turn does not count). TODO(if-possible): "within 1" /
        // engaged if it can" is left to the player, and "engaged with all charge targets" is checked when the move
        // ends.
        std::vector<ReachCircle> closer;
        std::vector<ReachShape> closer_shapes;
        for (const UnitId id : mv.targets) {
            const Unit* t = state.find_unit(id);
            if (t == nullptr || is_destroyed(*t)) continue;
            const std::optional<float> before = closer_before(start_pose, facing, *t);
            if (!before) continue;
            add_closer(*t, *before, closer, closer_shapes);
        }
        add_required_group(reach, std::move(closer), std::move(closer_shapes));
        return reach;
    }
    const bool pile_in = mv.type == MoveType::PileIn;
    const bool consolidation = mv.type == MoveType::Consolidation;
    if (!pile_in && !consolidation) return reach;
    const bool ongoing = mv.consolidation_mode == ConsolidationMode::Ongoing;
    // 12.03 / 12.08 Ongoing While Moving: a model in base contact with an enemy model cannot be moved (D-074: nor
    // turned; at its start pose).
    if ((pile_in || ongoing) && in_base_contact_with_enemy(state, *u, start_pose)) {
        reach.immobile = true;
        return reach;
    }
    // While Moving: end closer to the closest target, measured from the start (D-022; ties: any, D-010),
    // as ends_closer_to_closest_target: the end's distance to that unit is "more than" EPS less.
    std::vector<const Unit*> targets;
    std::vector<std::vector<PosedModel>> candidates;
    for (const UnitId id : mv.targets) {
        const Unit* t = state.find_unit(id);
        if (t == nullptr || is_destroyed(*t)) continue;
        targets.push_back(t);
        candidates.push_back(posed_models(*t));
    }
    const PosedModel here{m->id, start_pose.position, m->base, start_pose.facing_deg};
    std::vector<ReachCircle> closer;
    std::vector<ReachShape> closer_shapes;
    for (const std::size_t i : closest_units(std::span<const PosedModel>(&here, 1),
                                             std::span<const std::vector<PosedModel>>(candidates))) {
        const std::optional<float> before = closer_before(start_pose, facing, *targets[i]);
        if (!before) continue;
        add_closer(*targets[i], *before, closer, closer_shapes);
    }
    add_required_group(reach, std::move(closer), std::move(closer_shapes));
    // 12.03 After Moving / 12.08 Ongoing After Moving: still engaged with every enemy unit it started
    // engaged with (as still_engaged_problems, from its start pose).
    if (pile_in || ongoing) {
        for (const UnitId e : enemy_units_engaged_with_model(state, *u, start_pose)) {
            const Unit* enemy = state.find_unit(e);
            if (enemy == nullptr) continue;
            std::vector<ReachCircle> engaged;
            std::vector<ReachShape> engaged_shapes;
            for (const Model& t : enemy->models) {
                if (area.circle_for(t)) {
                    engaged.push_back(
                        ReachCircle{t.position, r + base_radius(t.base) + kEngagementRangeMm + EPS_MM, true, t.id});
                } else {
                    engaged_shapes.push_back(area.shape(t, kEngagementRangeMm + EPS_MM, true));
                }
            }
            add_required_group(reach, std::move(engaged), std::move(engaged_shapes));
        }
    }
    return reach;
}

namespace {

// D-047: starting a pile-in or consolidation rolls nothing, so a roll here is a programmer error.
class NoDice final : public DiceSource {
public:
    int roll_d6() override { throw std::logic_error("preview_fight_move: starting the move rolled a die"); }
};

}  // namespace

std::optional<GameState> preview_fight_move(const GameState& state, Side side, const GameAction& start) {
    if (!std::holds_alternative<action::StartPileIn>(start) &&
        !std::holds_alternative<action::StartConsolidation>(start)) {
        return std::nullopt;
    }
    if (!validate_action(state, side, start).ok()) return std::nullopt;
    GameState copy = state;
    NoDice dice;
    (void)apply_action(copy, side, start, dice);
    if (!copy.flow.move) return std::nullopt;
    return copy;
}

std::optional<Position> move_start_position(const GameState& state, ModelId model) {
    if (!state.flow.move) return std::nullopt;
    for (const ModelStart& s : state.flow.move->starts) {
        if (s.model == model) return s.position;
    }
    return std::nullopt;
}

std::optional<float> move_start_facing_deg(const GameState& state, ModelId model) {
    if (!state.flow.move) return std::nullopt;
    for (const ModelStart& s : state.flow.move->starts) {
        if (s.model == model) return s.facing_deg;
    }
    return std::nullopt;
}

std::vector<std::string> move_end_problems(const GameState& state) {
    std::vector<std::string> out;
    if (!state.flow.move) return out;
    const MoveInProgress& mv = *state.flow.move;
    const Unit& u = *state.find_unit(mv.unit);

    // 03.01 Ending a Move: no model in the unit is on another model.
    for (const Model& m : u.models) {
        for (const Unit& other : state.units) {
            for (const Model& o : other.models) {
                if (o.id == m.id || (other.id == u.id && o.id < m.id)) continue;  // each pair once
                if (bases_overlap(pose_of(m), pose_of(o))) {  // D-074: each base with its facing
                    out.push_back("03.01: " + detail::unit_label(u.id) + "'s model " + std::to_string(m.id.v) +
                                  " is on model " + std::to_string(o.id.v));
                }
            }
        }
    }
    // 03.01 / 03.03: the unit is in coherency.
    const CoherencyResult coherency = unit_coherency(u);
    for (const ModelId id : coherency.isolated) {
        out.push_back("03.03: model " + std::to_string(id.v) + " is more than 2\" from every other model in " +
                      detail::unit_label(u.id));
    }
    for (const ModelId id : coherency.too_far) {
        out.push_back("03.03: model " + std::to_string(id.v) + " is more than 9\" from another model in " +
                      detail::unit_label(u.id));
    }
    if (mv.type == MoveType::Charge) {
        // 11.04 After Moving: engaged with every charge target, and with no other enemy unit.
        for (std::string& p : charge_after_moving_problems(state, u, mv.targets)) {
            out.push_back(std::move(p));
        }
        return out;
    }
    if (mv.type == MoveType::PileIn) {
        // 12.03 After Moving: the unit is engaged, and models that started engaged stay engaged with those units.
        if (!is_engaged(state, u)) {
            out.push_back("12.03: " + detail::unit_label(u.id) + " must be engaged after its pile-in move");
        }
        for (std::string& p : still_engaged_problems(state, u, mv.starts, "12.03")) out.push_back(std::move(p));
        return out;
    }
    if (mv.type == MoveType::Consolidation) {
        if (mv.consolidation_mode == ConsolidationMode::Ongoing) {
            for (std::string& p : still_engaged_problems(state, u, mv.starts, "12.08")) out.push_back(std::move(p));
        } else if (mv.consolidation_mode == ConsolidationMode::Engaging) {
            const std::vector<UnitId> engaged = engaged_enemy_units(state, u);
            for (const UnitId t : mv.targets) {
                if (std::find(engaged.begin(), engaged.end(), t) == engaged.end()) {
                    out.push_back("12.08: " + detail::unit_label(u.id) + " must end its Engaging Consolidation "
                                  "engaged with " + detail::unit_label(t));
                }
            }
        }
        return out;
    }
    // 09.05 / 09.06 / 09.07 After Moving: the unit must be unengaged.
    if (is_engaged(state, u)) {
        out.push_back(std::string(move_type_rule(mv.type)) + ": " + detail::unit_label(u.id) +
                      " must be unengaged after a " + std::string(move_type_name(mv.type)) + " move");
    }
    return out;
}

}  // namespace wh40k::core
