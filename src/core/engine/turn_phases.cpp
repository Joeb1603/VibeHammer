// Stage entry/exit work, stage decisions and the turn-structure actions (07-12).
// Movement (M7) is in movement.cpp, shooting (M8) in shooting.cpp, charges (M9) in charge.cpp and the
// Fight phase (M10) in fight.cpp.

#include <stdexcept>
#include <string>

#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"
#include "core/model/keywords.hpp"
#include "core/state/deployment.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core::detail {

namespace {

bool is_phase_start(Stage s) {
    return s == Stage::StartOfCommandPhase || s == Stage::StartOfMovementPhase || s == Stage::StartOfShootingPhase ||
           s == Stage::StartOfChargePhase || s == Stage::StartOfFightPhase;
}

bool is_phase_end(Stage s) {
    return s == Stage::EndOfCommandPhase || s == Stage::EndOfMovementPhase || s == Stage::EndOfShootingPhase ||
           s == Stage::EndOfChargePhase || s == Stage::EndOfFightPhase;
}

}  // namespace

void on_enter_stage(GameState& state, DiceSource& dice, Events& events) {
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
            gain_core_cp(state, events);  // 08.02
            break;
        case Stage::BattleShock:
            battle_shock_step(state, dice, events);  // 08.03
            break;
        case Stage::StartOfMovementPhase:
            for (Unit& u : state.units) {
                u.phase_flags = UnitPhaseFlags{};  // 09.02 "not been selected to move this phase"; D-005 rolls
            }
            break;
        case Stage::StartOfShootingPhase:
            for (Unit& u : state.units) {
                u.phase_flags.selected_to_shoot = false;  // 10.02 "not already been selected to shoot this phase"
            }
            break;
        case Stage::StartOfChargePhase:
            for (Unit& u : state.units) {
                u.phase_flags.declared_charge = false;  // 11.02 "has not declared a charge this phase"
                u.phase_flags.charge_roll.reset();
            }
            break;
        case Stage::StartOfFightPhase:
        case Stage::PileIn:
        case Stage::Fight:
        case Stage::Consolidate:
            on_enter_fight_stage(state, events);  // fight.cpp
            break;
        case Stage::EndOfTurn:
            break;  // 03.03 Regaining Coherency is a decision (stage_decision)
        case Stage::Deploy:
            // D-081: the side that sets up first (the one taking the second turn, unless it has no units).
            events.push_back(event::DeploymentStarted{side_to_deploy(state).value_or(opponent(state.first_turn))});
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
        case Stage::PileIn:
        case Stage::Fight:
        case Stage::Consolidate:
            return fight_stage_decision(state);
        case Stage::EndOfTurn:
            return coherency_decision(state);  // 03.03 Regaining Coherency
        case Stage::Deploy:
            return deploy_decision(state);  // D-081: the next side sets up a unit
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
            const auto moves = legal_moves_for_unit(state, u);
            out.insert(out.end(), moves.begin(), moves.end());
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
    return validate_move_type(state, *u, a);
}

void apply_details(GameState& state, Side, const action::StartMove& a, DiceSource& dice, Events& events) {
    Unit* u = state.find_unit(a.unit);
    u->phase_flags.selected_to_move = true;  // 09.02: that unit is selected to move
    if (a.move_type == MoveType::RemainStationary) {
        // 09.04: no models are moved; no rules triggered by starting or ending a move.
        events.push_back(event::UnitRemainedStationary{a.unit});
        return;
    }
    begin_move(state, a, dice, events);
}

}  // namespace wh40k::core::detail
