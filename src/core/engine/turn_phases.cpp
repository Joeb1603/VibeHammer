// Stage entry/exit work, stage decisions and the turn-structure actions (07-12).
// M5: phases are stubs (remain stationary, end shooting, end charges). Later milestones fill them in.

#include <stdexcept>
#include <string>

#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"
#include "core/model/keywords.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core::detail {

namespace {

bool any_unit_engaged(const GameState& state) {
    for (const Unit& u : state.units) {
        if (!is_destroyed(u) && is_engaged(state, u)) {
            return true;
        }
    }
    return false;
}

// Fight-phase steps are M10. Until then they must never have work to do.
void require_no_fights(const GameState& state, const char* rule) {
    if (any_unit_engaged(state)) {
        throw std::logic_error(std::string(rule) + ": units are eligible, but the Fight phase is not implemented "
                                                   "until M10");
    }
}

bool is_phase_start(Stage s) {
    return s == Stage::StartOfCommandPhase || s == Stage::StartOfMovementPhase || s == Stage::StartOfShootingPhase ||
           s == Stage::StartOfChargePhase || s == Stage::StartOfFightPhase;
}

bool is_phase_end(Stage s) {
    return s == Stage::EndOfCommandPhase || s == Stage::EndOfMovementPhase || s == Stage::EndOfShootingPhase ||
           s == Stage::EndOfChargePhase || s == Stage::EndOfFightPhase;
}

}  // namespace

void on_enter_stage(GameState& state, Events& events) {
    const Stage stage = state.flow.stage;
    const Side side = turn_side(state);
    if (is_phase_start(stage)) {
        events.push_back(event::PhaseStarted{phase_of(stage), side});
    }
    switch (stage) {
        case Stage::StartOfBattleRound:
            events.push_back(event::BattleRoundStarted{state.flow.battle_round});  // 07.01
            break;
        case Stage::StartOfTurn:
            events.push_back(event::TurnStarted{state.flow.battle_round, side});  // 07.02
            break;
        case Stage::GainCoreCP:
            // TODO(M11): 08.02 both players gain 1 CP.
            break;
        case Stage::BattleShock:
            // TODO(M11): 08.03 battle-shock rolls for units below half-strength or battle-shocked.
            break;
        case Stage::StartOfMovementPhase:
            for (Unit& u : state.units) {
                u.phase_flags.selected_to_move = false;  // 09.02: "not been selected to move this phase"
            }
            break;
        case Stage::PileIn:
            require_no_fights(state, "12.02");
            break;
        case Stage::Fight:
            require_no_fights(state, "12.04");
            break;
        case Stage::Consolidate:
            require_no_fights(state, "12.07");
            break;
        case Stage::EndOfTurn:
            // TODO(M11): 03.03 Regaining Coherency.
            break;
        default:
            break;
    }
}

void on_leave_stage(GameState& state, Events& events) {
    const Stage stage = state.flow.stage;
    const Side side = turn_side(state);
    if (is_phase_end(stage)) {
        events.push_back(event::PhaseEnded{phase_of(stage), side});
    }
    switch (stage) {
        case Stage::EndOfTurn:
            for (Unit& u : state.units) {
                u.turn_flags = UnitTurnFlags{};  // effects that last "until the end of the turn" end
            }
            events.push_back(event::TurnEnded{state.flow.battle_round, side});  // 07.02
            break;
        case Stage::EndOfBattleRound:
            events.push_back(event::BattleRoundEnded{state.flow.battle_round});  // 07.03
            break;
        default:
            break;
    }
}

std::optional<PendingDecision> stage_decision(const GameState& state) {
    const Side side = turn_side(state);
    switch (state.flow.stage) {
        case Stage::MoveUnits:
            // 09.02: the active player must select every unit before the step ends.
            for (const Unit& u : state.units) {
                if (u.side == side && !is_destroyed(u) && !u.phase_flags.selected_to_move) {
                    return PendingDecision{side, decision::MoveUnits{}};
                }
            }
            return std::nullopt;
        case Stage::Shoot:
            return PendingDecision{side, decision::Shoot{}};  // 10.02: shooting is optional
        case Stage::Charge:
            return PendingDecision{side, decision::Charge{}};  // 11.02: charging is optional
        default:
            return std::nullopt;
    }
}

// ---- 09.02 Move Units ----

std::vector<GameAction> legal_for(const GameState& state, const decision::MoveUnits&) {
    std::vector<GameAction> out;
    const Side side = state.pending ? state.pending->side : turn_side(state);
    for (const Unit& u : state.units) {
        if (u.side == side && !is_destroyed(u) && !u.phase_flags.selected_to_move) {
            out.push_back(action::StartMove{u.id, MoveType::RemainStationary});  // 09.04: any unit
        }
    }
    return out;
}

ValidationResult validate_details(const GameState& state, Side side, const action::StartMove& a) {
    const Unit* u = state.find_unit(a.unit);
    if (u == nullptr) {
        return ValidationResult::failure("09.02: " + unit_label(a.unit) + " does not exist");
    }
    if (u->side != side) {
        return ValidationResult::failure("09.02: " + unit_label(a.unit) + " is not a friendly unit");
    }
    if (is_destroyed(*u)) {
        return ValidationResult::failure("09.02: " + unit_label(a.unit) +
                                         " is destroyed and cannot be selected (Rules Appendix: Destroyed)");
    }
    if (u->phase_flags.selected_to_move) {
        return ValidationResult::failure("09.02: " + unit_label(a.unit) +
                                         " has already been selected to move this phase");
    }
    switch (a.move_type) {
        case MoveType::RemainStationary:
            return ValidationResult::success();  // 09.04: eligible if any unit
    }
    return ValidationResult::failure("09.02: unknown move type");
}

void apply_details(GameState& state, Side, const action::StartMove& a, DiceSource&, Events& events) {
    Unit* u = state.find_unit(a.unit);
    u->phase_flags.selected_to_move = true;
    switch (a.move_type) {
        case MoveType::RemainStationary:
            // 09.04: no models are moved; no rules triggered by starting or ending a move.
            events.push_back(event::UnitRemainedStationary{a.unit});
            return;
    }
}

// ---- 10.02 Shoot (M5 stub: the only option is to end the step) ----

std::vector<GameAction> legal_for(const GameState&, const decision::Shoot&) {
    return {action::EndShootStep{}};
}

ValidationResult validate_details(const GameState&, Side, const action::EndShootStep&) {
    return ValidationResult::success();
}

void apply_details(GameState& state, Side, const action::EndShootStep&, DiceSource&, Events& events) {
    next_stage(state, events);  // 10.03 End of Shooting Phase
}

// ---- 11.02 Charge (M5 stub: the only option is to end the step) ----

std::vector<GameAction> legal_for(const GameState&, const decision::Charge&) {
    return {action::EndChargeStep{}};
}

ValidationResult validate_details(const GameState&, Side, const action::EndChargeStep&) {
    return ValidationResult::success();
}

void apply_details(GameState& state, Side, const action::EndChargeStep&, DiceSource&, Events& events) {
    next_stage(state, events);  // 11.03 End of Charge Phase
}

}  // namespace wh40k::core::detail
