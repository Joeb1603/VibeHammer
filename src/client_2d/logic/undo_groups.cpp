#include "undo_groups.hpp"

#include <variant>

namespace wh40k::client {

void UndoGroups::begin(std::size_t history_size) {
    while (!starts_.empty() && starts_.back() > history_size) starts_.pop_back();  // undone elsewhere
    if (!starts_.empty() && starts_.back() == history_size) return;  // an empty group is still open
    starts_.push_back(history_size);
}

void UndoGroups::end(std::size_t history_size) {
    if (!starts_.empty() && starts_.back() >= history_size) starts_.pop_back();
}

bool UndoGroups::undo(GameSession& session) {
    while (!starts_.empty() && starts_.back() >= session.history().size()) starts_.pop_back();
    if (starts_.empty()) return session.undo();
    const std::size_t start = starts_.back();
    starts_.pop_back();
    bool any = false;
    while (session.history().size() > start && session.undo()) any = true;
    return any;
}

bool UndoGroups::last_group_is_move_step(const GameSession& session) const {
    const auto& history = session.history();
    if (starts_.empty() || starts_.back() >= history.size() || !session.pending()) return false;
    const auto* moving = std::get_if<core::decision::MoveModels>(&session.pending()->kind);
    if (moving == nullptr) return false;
    for (std::size_t i = starts_.back(); i < history.size(); ++i) {
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
