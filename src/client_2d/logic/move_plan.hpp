#pragma once

// Building ProposeModelMove actions from board drags (03.01: a path of straight lines). The engine checks
// every proposal; these helpers only assemble paths and measure them for the on-screen ruler.

#include <optional>
#include <string>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

// The model's path in the move in progress: its start position, then each proposed point. Just the start
// if it hasn't been moved yet; empty if the model isn't moving.
[[nodiscard]] std::vector<core::Position> model_path(const core::GameState& state, core::ModelId model);

// D-074 part 3 (M66): the model's facing at each point of that path (one per point, the start's first): the path's
// facings when it has turned, otherwise its start facing at every point (0 for a round base). Empty if the model
// isn't moving.
[[nodiscard]] std::vector<float> model_path_facings(const core::GameState& state, core::ModelId model);

// The model's path so far plus one more straight line ending at `to`. D-074 part 3: it keeps the path's facings, so a
// model that has turned keeps its turns (and its facing on the new line).
[[nodiscard]] core::action::ProposeModelMove extend_path(const core::GameState& state, core::ModelId model,
                                                         core::Position to);

// D-074 part 3 (03.01 Rotating a Model): the path so far plus one more straight line ending at `to` with the model
// turned to `facing_deg`: turning at the line's start, then moving (`turn_at_end` false), or moving keeping its facing
// and then turning at `to` (the end point repeated with the new facing). With `to` where the model is, a turn in
// place (one vertex there with the new facing; for a model not yet moved, path {start}).
[[nodiscard]] core::action::ProposeModelMove extend_path_turned(const core::GameState& state, core::ModelId model,
                                                                core::Position to, float facing_deg, bool turn_at_end);

// D-074 part 3: the drop of a dragged model at `to` with the dragged facing `facing_deg`. With no turn (the same
// footprint, or a round base) it is extend_path. Otherwise turn-then-move is preferred; if core's validation refuses
// it but accepts move-then-turn, that; if it refuses both, turn-then-move (so its reasons show).
struct TurnedDrop {
    enum class How { NoTurn, TurnThenMove, MoveThenTurn, TurnInPlace };
    core::action::ProposeModelMove action;
    How how = How::NoTurn;
};
[[nodiscard]] TurnedDrop turned_drop(const core::GameState& state, core::ModelId model, core::Position to,
                                     float facing_deg);

// D-074 part 3: the turn (degrees, summed the shorter way between footprints, 03.01) along a path's facings.
[[nodiscard]] float facings_turn_deg(const std::vector<float>& facings);

// D-074 part 3: what a path does, for the log and the drag's ruler: "moved 3.00\"", "turned 45° then moved 3.00\"",
// "moved 3.00\" then turned 45°", "turned 45°", or "turned 90° and moved 3.00\"" (turns at several points). `points`
// start at the move's start; `facings` empty (no turn) or one per point. Present tense ("moves", "turns") when
// `present`.
[[nodiscard]] std::string describe_path(const std::vector<core::Position>& points, const std::vector<float>& facings,
                                        bool present = false);

// D-075: where a dragged model's centre goes: it keeps the offset from the cursor it was grabbed with (it doesn't jump
// so its centre is under the cursor). `grab` is the cursor's world position at the press, `model_at_grab` the model's
// centre then.
[[nodiscard]] core::Position drag_drop(const core::Position& model_at_grab, const core::Position& grab,
                                       const core::Position& cursor);

// The "Move unit" handle: every model of `unit` moves by (dx, dy) from where it is now, appended to its
// path. One action per model, furthest along the move first (D-027), so submitting them in order never
// ends a model on one that has yet to move.
[[nodiscard]] std::vector<core::action::ProposeModelMove> translate_unit(const core::GameState& state,
                                                                         core::UnitId unit, float dx_mm,
                                                                         float dy_mm);

// The ruler: distance moved along the path (plus a line to `to`, while dragging) and the move's maximum
// distance (core's max_move_distance_mm).
// D-074 part 3: `turned_deg`, how far it has turned (with the drag's facing, `facing_deg`, when given; a turn doesn't
// count towards the distance, 03.01).
struct Ruler {
    float moved_mm = 0.0f;
    float max_mm = 0.0f;
    bool over = false;  // "more than" the maximum, with the engine's tolerance
    float turned_deg = 0.0f;
};
[[nodiscard]] std::optional<Ruler> ruler(const core::GameState& state, core::ModelId model,
                                         std::optional<core::Position> to = std::nullopt,
                                         std::optional<float> facing_deg = std::nullopt);

}  // namespace wh40k::client
