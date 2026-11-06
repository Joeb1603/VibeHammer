#pragma once

// Undo by the player's inputs (D-039, M26). One drag of a model, one drag of the whole unit (a
// ProposeModelMove per model), or one button is one group, together with the steps the client then took by
// itself (D-036 skips, D-038 allocation). Undo removes the last group. Client only: each undo is
// GameSession::undo, a rebuild from the initial state (M13).
// D-054 (M39): each group records whose input it was. Against the bot, the human's Undo removes the bot's
// groups after the human's last input as well as that input, so the game goes back to the human's decision.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "session.hpp"

namespace wh40k::client {

enum class InputOwner : std::uint8_t { Human, Bot };

class UndoGroups {
public:
    // Forget every group (a new game, a load, a replay).
    void clear() noexcept { groups_.clear(); }
    // A player's input starts at this history size.
    void begin(std::size_t history_size, InputOwner owner = InputOwner::Human);
    // The input is over: a group that added no step (everything was rejected) is dropped.
    void end(std::size_t history_size);

    // Undo the last group (steps from before any group, e.g. after a load, go one at a time). False if
    // there was nothing to undo.
    bool undo(GameSession& session);
    // D-054: the human's Undo against a bot: every bot group after the human's last group, then that group.
    // False (nothing undone) if the human has no group to undo.
    bool undo_human(GameSession& session);

    // The last group is part of the move in progress: every step in it moves a model of the unit now
    // making a move (so "Undo last step" can offer it).
    [[nodiscard]] bool last_group_is_move_step(const GameSession& session) const;

    [[nodiscard]] std::vector<std::size_t> starts() const;
    [[nodiscard]] bool has_human_group(const GameSession& session) const;

private:
    struct Group {
        std::size_t start = 0;  // the history size where it starts (ascending)
        InputOwner owner = InputOwner::Human;
    };
    void drop_undone(std::size_t history_size);  // groups starting at or past the history's end
    std::vector<Group> groups_;
};

}  // namespace wh40k::client
