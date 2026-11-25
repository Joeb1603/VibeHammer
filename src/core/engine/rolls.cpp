// D-095 (M94): engine-paced dice rolls. At every roll point the engine stops with a RollDice decision for the player
// who makes the roll; its one answer is action::RollDice with the same roll and unit. Each roll is made by the module
// whose action made it before M94 (movement, charge, command, attack flow), with the same dice and events, so answering
// every RollDice at once (roll_through: apply_action) plays exactly as before.

#include <stdexcept>
#include <string>
#include <variant>

#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"

namespace wh40k::core {

namespace detail {

std::vector<GameAction> legal_for(const GameState&, const decision::RollDice& d) {
    return {action::RollDice{d.roll, d.unit}};
}

ValidationResult validate_details(const GameState& state, Side, const action::RollDice& a) {
    const auto& d = std::get<decision::RollDice>(state.pending->kind);
    if (a.roll != d.roll || a.unit != d.unit) {
        return ValidationResult::failure(std::string(decision_rule(state, *state.pending)) + ": waiting for " +
                                         describe(state, *state.pending) + ", not the " +
                                         std::string(roll_kind_name(a.roll)) + " roll for " + unit_label(a.unit));
    }
    return ValidationResult::success();
}

void apply_details(GameState& state, Side, const action::RollDice& a, DiceSource& dice, Events& events) {
    switch (a.roll) {
        case RollKind::Advance:
            make_advance_roll(state, dice, events);
            return;
        case RollKind::Charge:
            make_charge_roll(state, dice, events);
            return;
        case RollKind::BattleShock:
            make_owed_battle_shock_roll(state, dice, events);
            return;
        case RollKind::Hazard:
            // [HAZARDOUS] after a unit's attacks (24.15), or a Desperate Escape before its move (09.07).
            if (state.flow.attack) {
                make_attack_roll(state, a.roll, dice, events);
            } else {
                make_desperate_escape_hazard_rolls(state, dice, events);
            }
            return;
        case RollKind::RandomAttacks:
        case RollKind::Hit:
        case RollKind::Wound:
        case RollKind::Save:
        case RollKind::Damage:
            make_attack_roll(state, a.roll, dice, events);
            return;
    }
    throw std::logic_error("D-095: unknown roll");
}

void roll_through(GameState& state, DiceSource& dice, Events& events) {
    while (state.pending) {
        const auto* d = std::get_if<decision::RollDice>(&state.pending->kind);
        if (d == nullptr) return;
        const action::RollDice a{d->roll, d->unit};
        const Side side = state.pending->side;
        state.pending.reset();
        apply_details(state, side, a, dice, events);  // as apply_in_place, without validating its own answer
        advance(state, dice, events);
    }
}

}  // namespace detail

std::optional<RollDiceInfo> roll_dice_info(const GameState& state) {
    if (!roll_pending(state)) return std::nullopt;
    const auto& d = std::get<decision::RollDice>(state.pending->kind);
    RollDiceInfo info;
    info.roll = d.roll;
    info.unit = d.unit;
    info.roller = state.pending->side;
    info.expression = "D6";
    switch (d.roll) {
        case RollKind::Advance:
            info.dice = 1;  // 09.06: a D6
            break;
        case RollKind::Charge:
        case RollKind::BattleShock:
            info.dice = 2;  // 11.02 / 01.07: 2D6
            info.expression = "2D6";
            break;
        case RollKind::Hazard:
            if (state.flow.attack) {
                detail::describe_attack_roll(state, d.roll, info);
            } else if (const Unit* u = state.find_unit(d.unit)) {
                info.dice = static_cast<int>(u->models.size());  // 09.07: one hazard roll per model
            }
            break;
        case RollKind::RandomAttacks:
        case RollKind::Hit:
        case RollKind::Wound:
        case RollKind::Save:
        case RollKind::Damage:
            detail::describe_attack_roll(state, d.roll, info);
            break;
    }
    return info;
}

}  // namespace wh40k::core
