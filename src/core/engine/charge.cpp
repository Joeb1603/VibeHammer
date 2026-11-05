// 11.02 Charge: declare a charge, make the charge roll, then select charge targets and make the charge
// move (11.04, run by the movement sub-machine) or make no charge move. D-018 covers reverted moves.

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"
#include "core/rules/charge.hpp"

namespace wh40k::core::detail {

namespace {

const ChargeInProgress& charge_of(const GameState& state) {
    if (!state.flow.charge) {
        throw std::logic_error("charge: no charge in progress");
    }
    return *state.flow.charge;
}

int charge_roll_of(const Unit& u) {
    if (!u.phase_flags.charge_roll) {
        throw std::logic_error("charge: the unit has no charge roll");
    }
    return *u.phase_flags.charge_roll;
}

}  // namespace

void resolve_charge(GameState& state, bool charge_move_made, std::string reason, Events& events) {
    const UnitId unit = charge_of(state).unit;
    events.push_back(event::ChargeResolved{unit, charge_move_made, std::move(reason)});
    state.flow.charge.reset();
    state.flow.activation.reset();
}

std::optional<PendingDecision> charge_decision(const GameState& state) {
    const ChargeInProgress& c = charge_of(state);
    const Unit& u = *state.find_unit(c.unit);
    return PendingDecision{u.side, decision::SelectChargeTargets{c.unit}};
}

// ---- 11.02 Charge: declare a charge, or end the step ----

std::vector<GameAction> legal_for(const GameState& state, const decision::Charge&) {
    std::vector<GameAction> out{action::EndChargeStep{}};  // 11.02: charging is optional
    const Side side = state.pending ? state.pending->side : turn_side(state);
    for (const Unit& u : state.units) {
        if (u.side == side && !declare_charge_problem(state, u)) {
            out.push_back(action::DeclareCharge{u.id});
        }
    }
    return out;
}

ValidationResult validate_details(const GameState& state, Side side, const action::DeclareCharge& a) {
    const Unit* u = state.find_unit(a.unit);
    if (u == nullptr) {
        return ValidationResult::failure("11.02: " + unit_label(a.unit) + " does not exist");
    }
    if (u->side != side) {
        return ValidationResult::failure("11.02: " + unit_label(a.unit) + " is not a friendly unit");
    }
    if (auto problem = declare_charge_problem(state, *u)) {
        return ValidationResult::failure(std::move(*problem));
    }
    return ValidationResult::success();
}

void apply_details(GameState& state, Side, const action::DeclareCharge& a, DiceSource& dice, Events& events) {
    Unit& u = *state.find_unit(a.unit);
    u.phase_flags.declared_charge = true;  // 11.02 Declare Charge
    events.push_back(event::ChargeDeclared{u.id});
    const TwoD6 roll = roll_2d6(dice);  // 11.02 Make Charge Roll: the maximum distance of the charge move
    u.phase_flags.charge_roll = roll.total();
    const std::vector<UnitId> targets = eligible_charge_targets(state, u, roll.total());
    events.push_back(event::ChargeRolled{u.id, roll, targets});

    state.flow.activation = Activation{u.id, ActivationKind::Move};  // 01.03: selected to move
    state.flow.charge = ChargeInProgress{u.id};
    if (targets.empty()) {
        // 11.02 Attempt Charge: no charge move is possible without a charge target (11.04 Before Moving).
        resolve_charge(state, false,
                       "11.02: no enemy unit is within 12\" and within the charge roll (" +
                           std::to_string(roll.total()) + "\") of " + unit_label(u.id) + "; the charge fails",
                       events);
    }
}

ValidationResult validate_details(const GameState&, Side, const action::EndChargeStep&) {
    return ValidationResult::success();
}

void apply_details(GameState& state, Side, const action::EndChargeStep&, DiceSource& dice, Events& events) {
    next_stage(state, dice, events);  // 11.03 End of Charge Phase
}

// ---- 11.04 Before Moving: select charge targets, or make no charge move ----

std::vector<GameAction> legal_for(const GameState& state, const decision::SelectChargeTargets& d) {
    const Unit& u = *state.find_unit(d.unit);
    const std::vector<UnitId> eligible = eligible_charge_targets(state, u, charge_roll_of(u));
    std::vector<GameAction> out;
    for (auto& targets : nonempty_subsets(eligible)) out.push_back(action::StartChargeMove{d.unit, std::move(targets)});
    out.push_back(action::SkipChargeMove{d.unit});
    return out;
}

ValidationResult validate_details(const GameState& state, Side, const action::StartChargeMove& a) {
    const auto& d = std::get<decision::SelectChargeTargets>(state.pending->kind);
    if (a.unit != d.unit) {
        return ValidationResult::failure("11.04: the charge being resolved is " + unit_label(d.unit) + "'s, not " +
                                         unit_label(a.unit) + "'s");
    }
    if (a.targets.empty()) {
        return ValidationResult::failure("11.04: select one or more enemy units as charge targets");
    }
    const Unit& u = *state.find_unit(d.unit);
    const int roll = charge_roll_of(u);
    const std::vector<UnitId> eligible = eligible_charge_targets(state, u, roll);
    ValidationResult r;
    for (std::size_t i = 0; i < a.targets.size(); ++i) {
        const UnitId t = a.targets[i];
        if (std::find(a.targets.begin(), a.targets.begin() + static_cast<std::ptrdiff_t>(i), t) !=
            a.targets.begin() + static_cast<std::ptrdiff_t>(i)) {
            r.reasons.push_back("11.04: " + unit_label(t) + " is selected as a charge target more than once");
        } else if (std::find(eligible.begin(), eligible.end(), t) == eligible.end()) {
            r.reasons.push_back("11.04: " + unit_label(t) + " is not an enemy unit within 12\" and within the charge "
                                "roll (" + std::to_string(roll) + "\") of " + unit_label(d.unit));
        }
    }
    return r;
}

void apply_details(GameState& state, Side, const action::StartChargeMove& a, DiceSource&, Events& events) {
    const Unit& u = *state.find_unit(a.unit);
    events.push_back(event::ChargeTargetsSelected{u.id, a.targets});
    events.push_back(event::MoveStarted{u.id, MoveType::Charge, std::nullopt});
    MoveInProgress mv;
    mv.unit = u.id;
    mv.type = MoveType::Charge;
    mv.targets = a.targets;  // until the end of this move, each is a charge target (11.04)
    for (const Model& m : u.models) {
        mv.starts.push_back(ModelStart{m.id, m.position});
    }
    state.flow.move = std::move(mv);
}

ValidationResult validate_details(const GameState& state, Side, const action::SkipChargeMove& a) {
    const auto& d = std::get<decision::SelectChargeTargets>(state.pending->kind);
    if (a.unit != d.unit) {
        return ValidationResult::failure("11.02: the charge being resolved is " + unit_label(d.unit) + "'s, not " +
                                         unit_label(a.unit) + "'s");
    }
    return ValidationResult::success();
}

void apply_details(GameState& state, Side, const action::SkipChargeMove& a, DiceSource&, Events& events) {
    // 11.02 Attempt Charge: the unit does not make a charge move; the charge is resolved.
    resolve_charge(state, false, "11.02: " + unit_label(a.unit) + " does not make a charge move; the charge fails",
                   events);
}

}  // namespace wh40k::core::detail
