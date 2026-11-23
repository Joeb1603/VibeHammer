#include "retarget_picks.hpp"

#include <algorithm>
#include <variant>

#include "core/engine/attack_flow.hpp"
#include "core/rules/attacks.hpp"
#include "core/state/queries.hpp"

namespace wh40k::client {

RetargetPicks::RetargetPicks(const core::GameState& state) {
    if (!state.pending || !std::holds_alternative<core::decision::RetargetAttacks>(state.pending->kind) ||
        !state.flow.attack) {
        return;
    }
    active_ = true;
    const core::AttackFlow& f = *state.flow.attack;
    const core::Unit* attacker = state.find_unit(f.attacker);
    for (const core::RetargetChoice& c : core::retarget_choices(state)) {
        RetargetRow row;
        row.selection = c.selection;
        row.options = c.options;
        if (!row.options.empty()) row.chosen = 0;
        const core::WeaponSelection& sel = f.selections[c.selection];
        const core::Model* m = attacker != nullptr ? core::find_model(*attacker, sel.attacker) : nullptr;
        row.weapon = "Model " + std::to_string(sel.attacker.v);
        if (m != nullptr) row.weapon += ": " + core::selected_profile(state, sel).name;
        rows_.push_back(std::move(row));
    }
}

void RetargetPicks::choose(std::size_t row, std::optional<std::size_t> option) {
    if (row >= rows_.size()) return;
    if (option && *option >= rows_[row].options.size()) return;
    rows_[row].chosen = option;
}

void RetargetPicks::all_at(core::UnitId unit) {
    for (RetargetRow& r : rows_) {
        const auto it = std::find(r.options.begin(), r.options.end(), unit);
        r.chosen = it == r.options.end() ? std::nullopt : std::optional<std::size_t>(it - r.options.begin());
    }
}

std::vector<core::UnitId> RetargetPicks::units() const {
    std::vector<core::UnitId> out;
    for (const RetargetRow& r : rows_) {
        for (const core::UnitId u : r.options) {
            if (std::find(out.begin(), out.end(), u) == out.end()) out.push_back(u);
        }
    }
    return out;
}

core::action::RetargetAttacks RetargetPicks::action() const {
    core::action::RetargetAttacks a;
    for (const RetargetRow& r : rows_) {
        if (r.chosen) a.retargets.push_back(core::AttackRetarget{r.selection, r.options[*r.chosen]});
    }
    return a;
}

}  // namespace wh40k::client
