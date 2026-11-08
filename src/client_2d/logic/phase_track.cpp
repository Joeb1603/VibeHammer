#include "phase_track.hpp"

#include <cctype>

#include "core/engine/engine.hpp"
#include "core/state/points_destroyed.hpp"

namespace wh40k::client {

std::string spaced(std::string_view camel_case) {
    std::string out;
    for (std::size_t i = 0; i < camel_case.size(); ++i) {
        const char c = camel_case[i];
        if (i > 0 && std::isupper(static_cast<unsigned char>(c)) != 0) out += ' ';
        out += c;
    }
    return out;
}

PhaseTrack phase_track(const core::GameState& state) {
    PhaseTrack t;
    t.round = state.flow.battle_round;
    t.rounds = state.battle_rounds;
    const core::Stage stage = state.flow.stage;
    if (state.result) {
        t.result = state.result->reason;
        return t;
    }
    const bool between_turns = stage == core::Stage::NotStarted || stage == core::Stage::StartOfBattleRound ||
                               stage == core::Stage::EndOfBattleRound || stage == core::Stage::GameOver;
    if (!between_turns) t.turn = core::turn_side(state);
    const core::Phase phase = core::phase_of(stage);
    for (std::size_t i = 0; i < kTurnPhases.size(); ++i) {
        if (kTurnPhases[i] == phase) t.phase_index = i;
    }
    t.step = spaced(core::stage_name(stage)) + " (" + std::string(core::stage_rule(stage)) + ")";
    if (state.pending && (!t.turn || state.pending->side != *t.turn)) t.waiting_for = state.pending->side;
    return t;
}

std::optional<GameOverText> game_over_text(const core::GameState& state) {
    if (!state.result) return std::nullopt;
    GameOverText t;
    t.reason = state.result->reason;
    t.winner = state.result->winner;
    if (t.winner) {
        t.title = std::string(*t.winner == core::Side::A ? "Side A" : "Side B") + " (" + state.side(*t.winner).name +
                  ") wins!";
    } else {
        t.title = "Draw";
    }
    if (state.result_rule == core::ResultRule::PointsDestroyed) {
        // D-076: the totals the result was decided on; after a wipe-out (D-007) the totals as the game ended.
        const core::PointsTally tally = state.result->points ? *state.result->points : core::points_destroyed(state);
        t.points = "Points destroyed: Side A (" + state.side(core::Side::A).name + ") " + tally.display[0] +
                   ", Side B (" + state.side(core::Side::B).name + ") " + tally.display[1];
    }
    return t;
}

std::optional<std::string> points_destroyed_text(const core::GameState& state) {
    if (state.result_rule != core::ResultRule::PointsDestroyed) return std::nullopt;
    const core::PointsTally tally = state.result && state.result->points ? *state.result->points
                                                                         : core::points_destroyed(state);
    return "Points destroyed: Side A " + tally.display[0] + ", Side B " + tally.display[1];
}

}  // namespace wh40k::client
