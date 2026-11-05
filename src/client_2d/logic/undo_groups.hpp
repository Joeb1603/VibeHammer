#pragma once

// Undo by the player's inputs (D-039, M26). One drag of a model, one drag of the whole unit (a
// ProposeModelMove per model), or one button is one group, together with the steps the client then took by
// itself (D-036 skips, D-038 allocation). Undo removes the last group. Client only: each undo is
// GameSession::undo, a rebuild from the initial state (M13).

#include <cstddef>
#include <vector>

#include "session.hpp"

namespace wh40k::client {

class UndoGroups {
public:
    // Forget every group (a new game, a load, a replay).
    void clear() noexcept { starts_.clear(); }
    // A player's input starts at this history size.
    void begin(std::size_t history_size);
    // The input is over: a group that added no step (everything was rejected) is dropped.
    void end(std::size_t history_size);

    // Undo the last group (steps from before any group, e.g. after a load, go one at a time). False if
    // there was nothing to undo.
    bool undo(GameSession& session);

    // The last group is part of the move in progress: every step in it moves a model of the unit now
    // making a move (so "Undo last step" can offer it).
    [[nodiscard]] bool last_group_is_move_step(const GameSession& session) const;

    [[nodiscard]] const std::vector<std::size_t>& starts() const noexcept { return starts_; }

private:
    std::vector<std::size_t> starts_;  // ascending history sizes where each group starts
};

}  // namespace wh40k::client
