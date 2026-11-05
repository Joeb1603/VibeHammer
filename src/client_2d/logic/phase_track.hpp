#pragma once

// What the phase bar shows (D-028): the battle round, whose turn it is, where the turn is in the phase
// sequence, the current step, and who the game is waiting for. Display only (D-024).

#include <array>
#include <optional>
#include <string>

#include "core/state/game_state.hpp"

namespace wh40k::client {

// The five phases of a turn, in order (07.02).
inline constexpr std::array<core::Phase, 5> kTurnPhases{core::Phase::Command, core::Phase::Movement,
                                                        core::Phase::Shooting, core::Phase::Charge,
                                                        core::Phase::Fight};

struct PhaseTrack {
    int round = 0;
    int rounds = 0;
    std::optional<core::Side> turn;           // none before the battle and after it ends
    std::optional<std::size_t> phase_index;   // into kTurnPhases; none between turns
    std::string step;                         // e.g. "Move Units (09.02)"
    std::optional<core::Side> waiting_for;    // the deciding side, when it is not the turn side
    std::optional<std::string> result;        // the game is over
};

[[nodiscard]] PhaseTrack phase_track(const core::GameState& state);

// D-038: the victory pop-up: "Side A (Space Marines) wins!" or "Draw", and the engine's rule-cited reason.
struct GameOverText {
    std::string title;
    std::string reason;
    std::optional<core::Side> winner;  // nullopt: a draw
};
[[nodiscard]] std::optional<GameOverText> game_over_text(const core::GameState& state);

// "MoveUnits" -> "Move Units".
[[nodiscard]] std::string spaced(std::string_view camel_case);

}  // namespace wh40k::client
