// 09 Movement phase moves (09.04-09.07) and 03.01 Moving Units, as an engine sub-machine:
// StartMove -> [hazard casualties] -> ProposeModelMove* -> CommitUnitMove | ResetUnitMove.

#include "core/engine/movement.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"
#include "core/geometry/closest.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
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
        mv.starts.push_back(ModelStart{m.id, m.position});
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
        // 06.02 Select Model, answered once (D-013) when there is a real choice.
        Unit copy = u;
        bool asked = false;
        const ModelChooser probe = [&asked](const Unit&, std::span<const ModelId> options) {
            asked = true;
            return options.front();
        };
        (void)inflict_mortal_wounds(copy, mv.pending_mortal_wounds, probe);
        if (asked) {
            return PendingDecision{u.side, decision::OrderCasualties{u.id}};
        }
        return std::nullopt;
    }
    return PendingDecision{u.side, decision::MoveModels{u.id}};
}

void resolve_move_mortal_wounds(GameState& state, const std::vector<ModelId>& priority, Events& events) {
    MoveInProgress& mv = move_of(state);
    Unit& u = *state.find_unit(mv.unit);
    const ModelChooser by_priority = [&priority](const Unit&, std::span<const ModelId> options) {
        for (const ModelId id : priority) {
            if (std::find(options.begin(), options.end(), id) != options.end()) return id;
        }
        throw std::logic_error("06.02: the casualty order has no legal model");
    };
    const MortalWoundsResult result = inflict_mortal_wounds(u, mv.pending_mortal_wounds, by_priority);
    mv.pending_mortal_wounds = 0;
    events.push_back(event::MortalWoundsInflicted{u.id, result});
    for (const DamageResult& d : result.steps) {
        if (d.destroyed) events.push_back(event::ModelDestroyed{u.id, d.model});
    }
    if (is_destroyed(u)) {
        events.push_back(event::UnitDestroyed{u.id});
    }
}

void move_progress(GameState& state, DiceSource&, Events& events) {
    const MoveInProgress& mv = move_of(state);
    const Unit& u = *state.find_unit(mv.unit);
    if (is_destroyed(u)) {
        finish_move(state);  // every model was destroyed before it could move
        return;
    }
    if (mv.pending_mortal_wounds > 0) {
        resolve_move_mortal_wounds(state, ids_of(u), events);  // no real choice (D-013)
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
    std::vector<Position> points{*move_start_position(state, a.model)};
    points.insert(points.end(), a.path.begin(), a.path.end());

    // 02.02: a model with M '-' can be set up but cannot otherwise be moved (any move type, including
    // pile-in, consolidation and charge moves).
    const std::optional<float> max_mm = max_move_distance_mm(state, a.model);
    if (!m->characteristics.move_mm || !max_mm) {
        return ValidationResult::failure("02.02: " + model_label(a.model) + " has M '-' and cannot be moved");
    }
    ValidationResult r;
    // 03.01 Moving a Model in a Straight Line: total distance <= the move type's maximum distance.
    const float length = path_length(points);
    const float max = *max_mm;
    if (more_than(length, max)) {
        r.reasons.push_back(std::string(move_type_rule(mv.type)) + ": " + model_label(a.model) + " would move " +
                            format_inches(length) + " but the maximum distance is " + format_inches(max));
    }
    // 03.01: enemy models and battlefield edges. D-027: the path may pass through friendly models but not
    // end on any model at its current position (including models of this unit already moved); the
    // commit still checks 03.01 Ending a Move for the whole unit.
    const std::vector<PlacedModel> enemies = enemy_models(state, u.side);
    const std::vector<PlacedModel> friends = friendly_models(state, u.side);
    const PathContext context{Rect{0.0f, 0.0f, state.battlefield.width_mm, state.battlefield.depth_mm}, friends,
                              enemies, mv.mode == FallBackMode::DesperateEscape};  // 09.07 While Moving
    for (const PathViolation& v : check_model_path(a.model, m->base, points, context)) {
        r.reasons.push_back(v.reason);
    }
    if (mv.type == MoveType::Charge &&
        !ends_closer_to_a_charge_target(state, *m, points.front(), points.back(), mv.targets)) {
        // 11.04 While Moving (03.01: checked for each model that is moved).
        r.reasons.push_back("11.04: " + model_label(a.model) + " must end its move closer to one or more charge "
                            "targets");
    }
    const bool pile_in = mv.type == MoveType::PileIn;
    const bool ongoing = mv.consolidation_mode == ConsolidationMode::Ongoing;
    if (pile_in || mv.type == MoveType::Consolidation) {
        const std::string rule = pile_in ? "12.03" : "12.08";
        // 12.03 / 12.08 Ongoing While Moving: models in base contact with an enemy model cannot be moved.
        if ((pile_in || ongoing) && in_base_contact_with_enemy(state, u, *m, points.front())) {
            r.reasons.push_back(rule + ": " + model_label(a.model) +
                                " is in base contact with an enemy model and cannot be moved");
        }
        // Each model that is moved must end its move closer to the closest target (D-022).
        if (!ends_closer_to_closest_target(state, *m, points.front(), points.back(), mv.targets)) {
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
    std::vector<Position> points{*move_start_position(state, a.model)};
    points.insert(points.end(), a.path.begin(), a.path.end());
    m->position = points.back();
    const float length = path_length(points);
    auto it = std::find_if(mv.paths.begin(), mv.paths.end(), [&](const ModelPath& p) { return p.model == a.model; });
    if (it == mv.paths.end()) {
        mv.paths.push_back(ModelPath{a.model, points});
    } else {
        it->points = points;  // proposing again replaces the earlier path
    }
    events.push_back(event::ModelMoveProposed{mv.unit, a.model, std::move(points), length});
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

std::optional<MoveReach> move_reach(const GameState& state, ModelId model) {
    if (!state.flow.move || state.flow.move->type == MoveType::Charge) return std::nullopt;
    const MoveInProgress& mv = *state.flow.move;
    const Unit* u = state.find_unit(mv.unit);
    const Model* m = u == nullptr ? nullptr : find_model(*u, model);
    const std::optional<float> max = max_move_distance_mm(state, model);
    const std::optional<Position> start = move_start_position(state, model);
    if (m == nullptr || !max || !start) return std::nullopt;
    MoveReach reach;
    reach.start = *start;
    reach.max_mm = *max;
    reach.from = m->position;
    float moved = 0.0f;
    for (const ModelPath& p : mv.paths) {
        if (p.model == model) moved = path_length(p.points);
    }
    reach.remaining_mm = std::max(0.0f, *max - moved);
    const float r = base_radius(m->base);
    reach.centre_bounds = Rect{r, r, state.battlefield.width_mm - r, state.battlefield.depth_mm - r};
    // 09.05 / 09.06 / 09.07 After Moving: the unit must be unengaged.
    reach.must_end_unengaged =
        mv.type == MoveType::Normal || mv.type == MoveType::Advance || mv.type == MoveType::FallBack;
    // 09.07 While Moving: a Desperate Escape may move through enemy models.
    const bool through_enemies = mv.mode == FallBackMode::DesperateEscape;
    for (const Unit& other : state.units) {
        for (const Model& o : other.models) {
            if (o.id == m->id) continue;
            const float touching = r + base_radius(o.base);
            if (other.side != u->side && !through_enemies) {
                // 03.01: its base cannot be moved through enemy models (check_model_path's limit).
                reach.blocking.push_back(ReachCircle{o.position, touching - EPS_MM, false, o.id});
            }
            if (other.side != u->side && reach.must_end_unengaged) {
                // 03.04: within 2" is engaged; ending more than 2" away (EPS policy) is unengaged.
                reach.forbidden.push_back(ReachCircle{o.position, touching + kEngagementRangeMm + EPS_MM, true, o.id});
            } else {
                reach.forbidden.push_back(ReachCircle{o.position, touching, false, o.id});  // 03.01: not on it
            }
        }
    }
    const bool pile_in = mv.type == MoveType::PileIn;
    const bool consolidation = mv.type == MoveType::Consolidation;
    if (!pile_in && !consolidation) return reach;
    const bool ongoing = mv.consolidation_mode == ConsolidationMode::Ongoing;
    // 12.03 / 12.08 Ongoing While Moving: a model in base contact with an enemy model cannot be moved.
    if ((pile_in || ongoing) && in_base_contact_with_enemy(state, *u, *m, *start)) {
        reach.immobile = true;
        return reach;
    }
    // While Moving: end closer to the closest target, measured from the start (D-022; ties: any, D-010),
    // as ends_closer_to_closest_target: the end's distance to that unit is "more than" EPS less.
    std::vector<const Unit*> targets;
    std::vector<std::vector<PlacedModel>> candidates;
    for (const UnitId id : mv.targets) {
        const Unit* t = state.find_unit(id);
        if (t == nullptr || is_destroyed(*t)) continue;
        targets.push_back(t);
        candidates.push_back(placed_models(*t));
    }
    const PlacedModel here{m->id, *start, m->base};
    std::vector<ReachCircle> closer;
    for (const std::size_t i : closest_units(std::span<const PlacedModel>(&here, 1), candidates)) {
        const std::optional<float> before = model_distance_to_unit(*m, *start, *targets[i]);
        if (!before) continue;
        for (const Model& t : targets[i]->models) {
            const float radius = r + base_radius(t.base) + *before - EPS_MM;
            if (radius > 0.0f) closer.push_back(ReachCircle{t.position, radius, false, t.id});
        }
    }
    reach.required_any.push_back(std::move(closer));
    // 12.03 After Moving / 12.08 Ongoing After Moving: still engaged with every enemy unit it started
    // engaged with (as still_engaged_problems).
    if (pile_in || ongoing) {
        for (const UnitId e : enemy_units_engaged_with_model(state, *u, *m, *start)) {
            const Unit* enemy = state.find_unit(e);
            if (enemy == nullptr) continue;
            std::vector<ReachCircle> engaged;
            for (const Model& t : enemy->models) {
                engaged.push_back(ReachCircle{t.position, r + base_radius(t.base) + kEngagementRangeMm + EPS_MM, true, t.id});
            }
            reach.required_any.push_back(std::move(engaged));
        }
    }
    return reach;
}

std::optional<Position> move_start_position(const GameState& state, ModelId model) {
    if (!state.flow.move) return std::nullopt;
    for (const ModelStart& s : state.flow.move->starts) {
        if (s.model == model) return s.position;
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
                if (bases_overlap(m.position, m.base, o.position, o.base)) {
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
