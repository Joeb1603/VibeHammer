// The battle round -> turn -> phase -> step machine (07.01-07.03) and the D-007 game end.

#include <stdexcept>
#include <string>

#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"
#include "core/model/keywords.hpp"

namespace wh40k::core::detail {

namespace {

bool side_has_models(const GameState& state, Side side) {
    for (const Unit& u : state.units) {
        if (u.side == side && !is_destroyed(u)) {
            return true;
        }
    }
    return false;
}

void end_game(GameState& state, std::optional<Side> winner, std::string reason, Events& events) {
    state.result = GameResult{winner, reason};
    state.flow.stage = Stage::GameOver;
    state.flow.activation.reset();
    state.flow.attack.reset();
    state.flow.move.reset();
    state.flow.charge.reset();
    state.flow.fight_sequence.reset();
    state.flow.fight.reset();
    state.flow.forced_fights.clear();
    state.pending.reset();
    events.push_back(event::GameOver{winner, std::move(reason)});
}

// D-007: if a side has no models on the battlefield, the game ends immediately (checked when no
// move or attack is in progress) and the other side wins.
bool end_game_if_side_wiped_out(GameState& state, Events& events) {
    const bool a = side_has_models(state, Side::A);
    const bool b = side_has_models(state, Side::B);
    if (a && b) {
        return false;
    }
    if (!a && !b) {
        throw std::logic_error("D-007: both sides lost their last models at once (not possible in V1)");
    }
    const Side winner = a ? Side::A : Side::B;
    end_game(state, winner,
             "D-007: " + side_label(opponent(winner)) + " has no models on the battlefield; " +
                 side_label(winner) + " wins",
             events);
    return true;
}

}  // namespace

void enter_stage(GameState& state, Stage stage, DiceSource& dice, Events& events) {
    state.flow.stage = stage;
    on_enter_stage(state, dice, events);
}

void next_stage(GameState& state, DiceSource& dice, Events& events) {
    const Stage current = state.flow.stage;
    on_leave_stage(state, events);
    switch (current) {
        case Stage::NotStarted:
        case Stage::GameOver:
            throw std::logic_error("next_stage: the game is not running");
        case Stage::EndOfTurn:
            if (state.flow.turn_in_round == 0) {
                state.flow.turn_in_round = 1;  // 07.02: the opponent takes their turn
                enter_stage(state, Stage::StartOfTurn, dice, events);
            } else {
                enter_stage(state, Stage::EndOfBattleRound, dice, events);
            }
            return;
        case Stage::EndOfBattleRound:
            if (state.flow.battle_round >= state.battle_rounds) {
                // D-007: the battle ends after battle round N with both sides on the battlefield.
                end_game(state, std::nullopt,
                         "D-007: battle round " + std::to_string(state.flow.battle_round) +
                             " has ended with both sides on the battlefield; the game is a draw",
                         events);
                return;
            }
            state.flow.battle_round += 1;  // 07.03: the next battle round starts
            state.flow.turn_in_round = 0;
            enter_stage(state, Stage::StartOfBattleRound, dice, events);
            return;
        default:
            // Every other stage is followed by the next one in declaration order.
            enter_stage(state, static_cast<Stage>(static_cast<int>(current) + 1), dice, events);
            return;
    }
}

void advance(GameState& state, DiceSource& dice, Events& events) {
    state.pending.reset();
    for (int guard = 0; guard < 100000; ++guard) {
        if (state.flow.stage == Stage::GameOver || state.flow.stage == Stage::NotStarted) {
            return;
        }
        if (state.flow.attack) {
            if (auto d = attack_decision(state)) {
                state.pending = std::move(d);
                return;
            }
            attack_progress(state, dice, events);
            continue;
        }
        if (state.flow.move) {
            if (auto d = move_decision(state)) {
                state.pending = std::move(d);
                return;
            }
            move_progress(state, dice, events);
            continue;
        }
        if (state.flow.charge) {
            state.pending = charge_decision(state);  // 11.04 Before Moving: always the player's choice
            return;
        }
        if (state.flow.fight) {
            if (auto d = fight_decision(state)) {
                state.pending = std::move(d);
                return;
            }
            fight_progress(state, events);
            continue;
        }
        if (!state.flow.forced_fights.empty()) {
            state.pending = forced_fight_decision(state);  // 12.08: the opponent selects each unit
            return;
        }
        if (!state.flow.activation && end_game_if_side_wiped_out(state, events)) {
            return;
        }
        if (auto d = stage_decision(state)) {
            state.pending = std::move(d);
            return;
        }
        if (fight_stage_progress(state, events)) {
            continue;
        }
        next_stage(state, dice, events);
    }
    throw std::logic_error("advance: the engine made no progress");
}

}  // namespace wh40k::core::detail
