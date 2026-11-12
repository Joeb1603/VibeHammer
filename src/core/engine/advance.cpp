// The battle round -> turn -> phase -> step machine (07.01-07.03) and the D-007 game end.

#include <stdexcept>
#include <string>

#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"
#include "core/model/keywords.hpp"
#include "core/state/objectives.hpp"
#include "core/state/points_destroyed.hpp"

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

void end_game(GameState& state, std::optional<Side> winner, std::string reason, Events& events,
              std::optional<PointsTally> points = std::nullopt, std::optional<VpTally> vp = std::nullopt) {
    state.result = GameResult{winner, reason, points, vp};
    state.flow.stage = Stage::GameOver;
    state.flow.activation.reset();
    state.flow.attack.reset();
    state.flow.move.reset();
    state.flow.charge.reset();
    state.flow.fight_sequence.reset();
    state.flow.fight.reset();
    state.flow.forced_fights.clear();
    state.pending.reset();
    events.push_back(event::GameOver{winner, std::move(reason), std::move(points), vp});
}

// D-076 (amends D-007): after the End of Battle Round step of the last battle round, with both sides on the
// battlefield, the side that destroyed more points wins; equal points is a draw. The totals compare exactly (one
// common denominator, so as integers).
void end_game_on_points(GameState& state, Events& events) {
    PointsTally tally = points_destroyed(state);
    const std::int64_t a = tally.destroyed[side_index(Side::A)];
    const std::int64_t b = tally.destroyed[side_index(Side::B)];
    const std::optional<Side> winner = a > b ? std::optional<Side>(Side::A)
                                     : b > a ? std::optional<Side>(Side::B)
                                             : std::nullopt;
    std::string reason = "D-076: battle round " + std::to_string(state.flow.battle_round) +
                         " has ended with both sides on the battlefield; points destroyed: Side A " +
                         tally.display[side_index(Side::A)] + ", Side B " + tally.display[side_index(Side::B)] + "; ";
    reason += winner ? side_label(*winner) + " wins on points destroyed" : "equal points destroyed: the game is a draw";
    end_game(state, winner, std::move(reason), events, std::move(tally));
}

// D-084 (M86, amends D-076): after the End of Battle Round step of the last battle round, with both sides on the
// battlefield, the side with more victory points (objective VP + kill VP) wins; equal VP is decided by points
// destroyed (D-076, exactly); equal again is a draw.
void end_game_on_mission(GameState& state, Events& events) {
    PointsTally tally = points_destroyed(state);
    const VpTally vp = vp_tally(state);
    const std::size_t ia = side_index(Side::A);
    const std::size_t ib = side_index(Side::B);
    const auto side_vp = [&vp](Side s) {
        const std::size_t i = side_index(s);
        return side_label(s) + " " + std::to_string(vp.total[i]) + " (" + std::to_string(vp.objectives[i]) +
               " objectives + " + std::to_string(vp.kills[i]) + " kills)";
    };
    std::string reason = "D-084: battle round " + std::to_string(state.flow.battle_round) +
                         " has ended with both sides on the battlefield; victory points: " + side_vp(Side::A) + ", " +
                         side_vp(Side::B) + "; ";
    std::optional<Side> winner;
    if (vp.total[ia] != vp.total[ib]) {
        winner = vp.total[ia] > vp.total[ib] ? Side::A : Side::B;
        reason += side_label(*winner) + " wins on victory points";
    } else {
        const std::int64_t a = tally.destroyed[ia];
        const std::int64_t b = tally.destroyed[ib];
        reason += "equal victory points (" + std::to_string(vp.total[ia]) + " each); points destroyed: Side A " +
                  tally.display[ia] + ", Side B " + tally.display[ib] + "; ";
        if (a != b) {
            winner = a > b ? Side::A : Side::B;
            reason += side_label(*winner) + " wins on points destroyed";
        } else {
            reason += "equal points destroyed: the game is a draw";
        }
    }
    end_game(state, winner, std::move(reason), events, std::move(tally), vp);
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
        case Stage::Deploy:
            // D-081: every unit is handled; battle round 1 starts as D-003's start (Deploy is appended after GameOver,
            // so it has no "next stage in declaration order").
            start_first_battle_round(state, dice, events);
            return;
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
                if (state.result_rule == ResultRule::PointsDestroyed) {
                    end_game_on_points(state, events);
                    return;
                }
                if (state.result_rule == ResultRule::Mission) {
                    end_game_on_mission(state, events);
                    return;
                }
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
        if (state.flow.stage == Stage::Deploy) {
            // D-081 (amends D-007): no wipe-out check while the armies are set up (no side has all of its units on the
            // battlefield yet); it applies again from battle round 1.
            if (auto d = deploy_decision(state)) {
                state.pending = std::move(d);
                return;
            }
            next_stage(state, dice, events);
            continue;
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
        // D-007 first: a wipe-out ends the game at once, even with forced fights (12.08) still to select.
        if (!state.flow.activation && end_game_if_side_wiped_out(state, events)) {
            return;
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
