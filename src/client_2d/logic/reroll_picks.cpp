#include "reroll_picks.hpp"

#include <algorithm>
#include <variant>

#include "core/state/queries.hpp"
#include "unit_style.hpp"

namespace wh40k::client {

namespace {

// "<attacker> shooting|fighting <target> (<weapon profile>)": the batch's first selection names the weapon.
std::string purpose_of(const core::GameState& state, const core::AttackFlow& f, std::size_t batch_index) {
    if (batch_index >= f.batches.size()) return "";
    const core::AttackBatch& batch = f.batches[batch_index];
    std::string text = unit_display_name(state, f.attacker) +
                       (f.context.type == core::AttackType::Fighting ? " fighting " : " shooting ") +
                       unit_display_name(state, batch.target);
    if (!batch.selections.empty() && batch.selections.front() < f.selections.size()) {
        const core::WeaponSelection& s = f.selections[batch.selections.front()];
        if (const core::Model* m = core::find_model(state, s.attacker); m != nullptr && s.weapon_index < m->weapons.size()) {
            const core::Weapon& w = m->weapons[s.weapon_index];
            text += " (" + (s.profile_index < w.profiles.size() ? w.profiles[s.profile_index].name : w.name) + ")";
        }
    }
    return text;
}

}  // namespace

RerollPicks::RerollPicks(const core::GameState& state) {
    if (!state.pending || !state.flow.attack || !state.flow.attack->wound_rerolls) return;
    const auto* d = std::get_if<core::decision::RerollWoundRolls>(&state.pending->kind);
    if (d == nullptr) return;
    active_ = true;
    attacker_ = d->attacker;
    purpose_ = purpose_of(state, *state.flow.attack, state.flow.attack->wound_rerolls->batch);
    const core::WoundRolls& rolls = state.flow.attack->wound_rerolls->rolls;
    required_ = rolls.required;
    for (const core::WoundRoll& r : rolls.rolls) {
        dice_.push_back(RerollDie{r.unmodified, r.modified, r.outcome == core::WoundOutcome::Fail,
                                  r.outcome == core::WoundOutcome::CriticalWound, false});
    }
}

std::size_t RerollPicks::failed_count() const {
    return static_cast<std::size_t>(std::count_if(dice_.begin(), dice_.end(), [](const RerollDie& d) { return d.failed; }));
}

void RerollPicks::toggle(std::size_t index) {
    if (index < dice_.size()) dice_[index].selected = !dice_[index].selected;
}

void RerollPicks::select_failed() {
    for (RerollDie& d : dice_) d.selected = d.failed;
}

void RerollPicks::clear() {
    for (RerollDie& d : dice_) d.selected = false;
}

std::vector<std::size_t> RerollPicks::selected() const {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < dice_.size(); ++i) {
        if (dice_[i].selected) out.push_back(i);
    }
    return out;
}

core::action::RerollWoundRolls RerollPicks::action() const { return core::action::RerollWoundRolls{selected()}; }

core::action::RerollWoundRolls RerollPicks::all_failed() const {
    core::action::RerollWoundRolls a;
    for (std::size_t i = 0; i < dice_.size(); ++i) {
        if (dice_[i].failed) a.indices.push_back(i);
    }
    return a;
}

bool RerollPicks::custom() const {
    const std::vector<std::size_t> picks = selected();
    return !picks.empty() && picks != all_failed().indices;
}

}  // namespace wh40k::client
