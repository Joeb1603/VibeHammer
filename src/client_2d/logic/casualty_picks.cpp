#include "casualty_picks.hpp"

#include <algorithm>

namespace wh40k::client {

namespace {

bool contains(const std::vector<core::ModelId>& v, core::ModelId id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

}  // namespace

CasualtyPicks::CasualtyPicks(const core::GameState& state, core::UnitId target) : target_(target) {
    if (const core::Unit* u = state.find_unit(target)) {
        for (const core::Model& m : u->models) unit_order_.push_back(m.id);
    }
    preview_ = core::preview_casualties(state, picks_);
}

std::vector<core::ModelId> CasualtyPicks::options() const {
    return preview_ ? preview_->options : std::vector<core::ModelId>{};
}

bool CasualtyPicks::can_pick(core::ModelId model) const { return preview_ && contains(preview_->options, model); }

bool CasualtyPicks::pick(const core::GameState& state, core::ModelId model) {
    if (!can_pick(model)) return false;
    picks_.push_back(model);
    std::optional<core::CasualtyPreview> next = core::preview_casualties(state, picks_);
    if (!next) {  // can't happen for a model core offered; keep the picks consistent anyway
        picks_.pop_back();
        return false;
    }
    preview_ = std::move(next);
    return true;
}

bool CasualtyPicks::undo_pick(const core::GameState& state) {
    if (picks_.empty()) return false;
    picks_.pop_back();
    preview_ = core::preview_casualties(state, picks_);
    return true;
}

std::vector<CasualtyMark> CasualtyPicks::marks() const {
    std::vector<CasualtyMark> out;
    if (!preview_) return out;
    for (const core::DamageResult& d : preview_->results) {
        auto it = std::find_if(out.begin(), out.end(), [&](const CasualtyMark& m) { return m.model == d.model; });
        if (it == out.end()) {
            out.push_back(CasualtyMark{d.model, 0, false});
            it = out.end() - 1;
        }
        it->wounds_lost += d.wounds_lost;
        it->destroyed = it->destroyed || d.destroyed;
    }
    return out;
}

core::action::OrderCasualties CasualtyPicks::action() const {
    std::vector<core::ModelId> priority = picks_;
    for (const core::ModelId id : unit_order_) {
        if (!contains(priority, id)) priority.push_back(id);
    }
    return core::action::OrderCasualties{target_, priority};
}

}  // namespace wh40k::client
