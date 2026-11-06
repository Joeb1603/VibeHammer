#include "undo_groups.hpp"

#include <variant>

namespace wh40k::client {

void UndoGroups::drop_undone(std::size_t history_size) {
    while (!groups_.empty() && groups_.back().start >= history_size) groups_.pop_back();
}

void UndoGroups::begin(std::size_t history_size, InputOwner owner) {
    while (!groups_.empty() && groups_.back().start > history_size) groups_.pop_back();  // undone elsewhere
    if (!groups_.empty() && groups_.back().start == history_size) {  // an empty group is still open
        groups_.back().owner = owner;
        return;
    }
    groups_.push_back(Group{history_size, owner});
}

void UndoGroups::end(std::size_t history_size) {
    if (!groups_.empty() && groups_.back().start >= history_size) groups_.pop_back();
}

bool UndoGroups::undo(GameSession& session) {
    drop_undone(session.history().size());
    if (groups_.empty()) return session.undo();
    const std::size_t start = groups_.back().start;
    groups_.pop_back();
    bool any = false;
    while (session.history().size() > start && session.undo()) any = true;
    return any;
}

bool UndoGroups::undo_human(GameSession& session) {
    drop_undone(session.history().size());
    std::size_t i = groups_.size();
    while (i > 0 && groups_[i - 1].owner != InputOwner::Human) --i;
    if (i == 0) return false;
    const std::size_t start = groups_[i - 1].start;
    groups_.resize(i - 1);
    bool any = false;
    while (session.history().size() > start && session.undo()) any = true;
    return any;
}

std::vector<std::size_t> UndoGroups::starts() const {
    std::vector<std::size_t> out;
    for (const Group& g : groups_) out.push_back(g.start);
    return out;
}

bool UndoGroups::has_human_group(const GameSession& session) const {
    for (const Group& g : groups_) {
        if (g.owner == InputOwner::Human && g.start < session.history().size()) return true;
    }
    return false;
}

bool UndoGroups::last_group_is_move_step(const GameSession& session) const {
    const auto& history = session.history();
    if (groups_.empty() || groups_.back().start >= history.size() || !session.pending()) return false;
    if (groups_.back().owner != InputOwner::Human) return false;  // D-054: not the bot's placements
    const auto* moving = std::get_if<core::decision::MoveModels>(&session.pending()->kind);
    if (moving == nullptr) return false;
    for (std::size_t i = groups_.back().start; i < history.size(); ++i) {
        const auto* p = std::get_if<core::action::ProposeModelMove>(&history[i].action);
        if (p == nullptr) return false;
        const core::Unit* u = session.state().find_unit(moving->unit);
        if (u == nullptr) return false;
        bool in_unit = false;
        for (const core::Model& m : u->models) in_unit = in_unit || m.id == p->model;
        if (!in_unit) return false;
    }
    return true;
}

}  // namespace wh40k::client
