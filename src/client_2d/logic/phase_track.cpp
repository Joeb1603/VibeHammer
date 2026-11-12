#include "phase_track.hpp"

#include <cctype>

#include "core/engine/engine.hpp"
#include "core/state/objectives.hpp"
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
    // D-081: the deployment step is nobody's turn either (01.03); it comes before battle round 1.
    t.deploying = stage == core::Stage::Deploy;
    const bool between_turns = stage == core::Stage::NotStarted || stage == core::Stage::StartOfBattleRound ||
                               stage == core::Stage::EndOfBattleRound || stage == core::Stage::GameOver ||
                               t.deploying;
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
    if (state.mission) {
        // D-084: the VP the result was decided on; after a wipe-out (D-007) the VP as the game ended.
        const core::VpTally vp = state.result->vp ? *state.result->vp : core::vp_tally(state);
        const auto one = [&](core::Side s) {
            const std::size_t i = core::side_index(s);
            return std::string(s == core::Side::A ? "Side A (" : "Side B (") + state.side(s).name + ") " +
                   std::to_string(vp.total[i]) + " (" + std::to_string(vp.objectives[i]) + " objectives + " +
                   std::to_string(vp.kills[i]) + " kills)";
        };
        t.vp = "Victory points: " + one(core::Side::A) + ", " + one(core::Side::B);
    }
    if (state.result_rule == core::ResultRule::PointsDestroyed || state.result_rule == core::ResultRule::Mission) {
        // D-076: the totals the result was decided on; after a wipe-out (D-007) the totals as the game ended.
        const core::PointsTally tally = state.result->points ? *state.result->points : core::points_destroyed(state);
        t.points = "Points destroyed: Side A (" + state.side(core::Side::A).name + ") " + tally.display[0] +
                   ", Side B (" + state.side(core::Side::B).name + ") " + tally.display[1];
    }
    return t;
}

std::optional<std::string> points_destroyed_text(const core::GameState& state) {
    if (state.result_rule != core::ResultRule::PointsDestroyed && state.result_rule != core::ResultRule::Mission) {
        return std::nullopt;
    }
    const core::PointsTally tally = state.result && state.result->points ? *state.result->points
                                                                         : core::points_destroyed(state);
    return "Points destroyed: Side A " + tally.display[0] + ", Side B " + tally.display[1];
}

std::optional<std::string> victory_points_text(const core::GameState& state) {
    if (!state.mission) return std::nullopt;
    const core::VpTally vp = state.result && state.result->vp ? *state.result->vp : core::vp_tally(state);
    const auto one = [&vp](core::Side s) {
        const std::size_t i = core::side_index(s);
        return std::string(s == core::Side::A ? "Side A " : "Side B ") + std::to_string(vp.total[i]) + " (" +
               std::to_string(vp.objectives[i]) + " + " + std::to_string(vp.kills[i]) + " kills)";
    };
    return "Victory points: " + one(core::Side::A) + ", " + one(core::Side::B);
}

std::vector<ObjectiveMark> objective_marks(const core::GameState& state) {
    std::vector<ObjectiveMark> out;
    if (!state.mission) return out;
    for (const core::ObjectiveArea& a : state.mission->objectives) {
        ObjectiveMark m;
        m.centre = a.centre;
        m.radius_mm = a.diameter_mm * 0.5f;
        m.controller = a.controller;
        m.secured = a.secured_by.has_value();
        m.label = a.name + " " + std::to_string(a.vp[0]) + "/" + std::to_string(a.vp[1]);
        out.push_back(std::move(m));
    }
    return out;
}

}  // namespace wh40k::client
