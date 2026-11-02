#include "core/engine/engine.hpp"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

#include "core/engine/internal.hpp"

namespace wh40k::core {

namespace detail {

std::string side_label(Side side) { return side == Side::A ? "Side A" : "Side B"; }
std::string unit_label(UnitId id) { return "unit " + std::to_string(id.v); }

}  // namespace detail

namespace {

using detail::Events;

// Which decision each action answers.
bool answers(const DecisionKind& d, const GameAction& a) {
    return std::visit(
        [&d](const auto& act) {
            using T = std::decay_t<decltype(act)>;
            if constexpr (std::is_same_v<T, action::StartMove>) return std::holds_alternative<decision::MoveUnits>(d);
            else if constexpr (std::is_same_v<T, action::ProposeModelMove> || std::is_same_v<T, action::CommitUnitMove> ||
                               std::is_same_v<T, action::ResetUnitMove>)
                return std::holds_alternative<decision::MoveModels>(d);
            else if constexpr (std::is_same_v<T, action::Shoot> || std::is_same_v<T, action::EndShootStep>)
                return std::holds_alternative<decision::Shoot>(d);
            else if constexpr (std::is_same_v<T, action::EndChargeStep>) return std::holds_alternative<decision::Charge>(d);
            else if constexpr (std::is_same_v<T, action::SelectAttackBatch>)
                return std::holds_alternative<decision::SelectAttackBatch>(d);
            else if constexpr (std::is_same_v<T, action::DeclareAllocationOrder>)
                return std::holds_alternative<decision::DeclareAllocationOrder>(d);
            else if constexpr (std::is_same_v<T, action::OrderCasualties>)
                return std::holds_alternative<decision::OrderCasualties>(d);
            else static_assert(sizeof(T) == 0, "unhandled action");
        },
        a);
}

std::vector<GameEvent> start_game_impl(GameState& state, DiceSource& dice) {
    if (state.flow.stage != Stage::NotStarted) {
        throw std::logic_error("start_game: the game has already started");
    }
    Events events;
    state.flow.battle_round = 1;
    state.flow.turn_in_round = 0;
    detail::enter_stage(state, Stage::StartOfBattleRound, events);  // D-003: straight into round 1
    detail::advance(state, dice, events);
    return events;
}

}  // namespace

std::vector<GameEvent> start_game(GameState& state) {
    SeededDice dice(state.dice);
    return start_game_impl(state, dice);
}

std::vector<GameEvent> start_game(GameState& state, DiceSource& dice) { return start_game_impl(state, dice); }

Side turn_side(const GameState& state) {
    return state.flow.turn_in_round == 0 ? state.first_turn : opponent(state.first_turn);
}

Side active_side(const GameState& state) {
    switch (state.flow.stage) {
        case Stage::NotStarted:
        case Stage::StartOfBattleRound:
        case Stage::EndOfBattleRound:
        case Stage::GameOver:
            // 01.03: while it is neither player's turn, the player who takes the first turn.
            return state.first_turn;
        default:
            break;
    }
    if (state.flow.activation) {
        // 01.03: while a unit is selected to move/shoot/fight, its controlling player.
        if (const Unit* u = state.find_unit(state.flow.activation->unit)) {
            return u->side;
        }
    }
    return turn_side(state);
}

std::optional<PendingDecision> compute_decision(const GameState& state) {
    if (state.flow.stage == Stage::NotStarted || state.flow.stage == Stage::GameOver) {
        return std::nullopt;
    }
    if (state.flow.attack) {
        return detail::attack_decision(state);
    }
    if (state.flow.move) {
        return detail::move_decision(state);
    }
    return detail::stage_decision(state);
}

std::vector<GameAction> legal_actions(const GameState& state, Side side) {
    if (!state.pending || state.pending->side != side) {
        return {};
    }
    return std::visit([&state](const auto& d) { return detail::legal_for(state, d); }, state.pending->kind);
}

ValidationResult validate_action(const GameState& state, Side side, const GameAction& action) {
    if (state.flow.stage == Stage::NotStarted) {
        return ValidationResult::failure("07.01: the battle has not started (start_game first)");
    }
    if (state.flow.stage == Stage::GameOver || state.result) {
        return ValidationResult::failure("D-007: the game is over");
    }
    if (!state.pending) {
        throw std::logic_error("validate_action: the game is running but no decision is pending");
    }
    const PendingDecision& pending = *state.pending;
    if (side != pending.side) {
        return ValidationResult::failure(std::string(decision_rule(pending)) + ": waiting for " + describe(pending) +
                                         "; " + detail::side_label(side) + " cannot act now");
    }
    if (!answers(pending.kind, action)) {
        return ValidationResult::failure(std::string(action_rule(action)) + ": " + std::string(action_name(action)) +
                                         " is not possible now; waiting for " + describe(pending));
    }
    return std::visit([&](const auto& a) { return detail::validate_details(state, side, a); }, action);
}

std::vector<GameEvent> apply_action(GameState& state, Side side, const GameAction& action, DiceSource& dice) {
    const ValidationResult v = validate_action(state, side, action);
    if (!v.ok()) {
        throw std::logic_error("apply_action: invalid action: " + v.reasons.front());
    }
    Events events;
    state.pending.reset();
    std::visit([&](const auto& a) { detail::apply_details(state, side, a, dice, events); }, action);
    detail::advance(state, dice, events);
    return events;
}

std::vector<GameEvent> apply_action(GameState& state, Side side, const GameAction& action) {
    SeededDice dice(state.dice);
    return apply_action(state, side, action, dice);
}

}  // namespace wh40k::core
