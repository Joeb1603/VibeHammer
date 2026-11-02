#include "melee_plan.hpp"

#include <algorithm>

#include "core/rules/fight.hpp"

namespace wh40k::client {

MeleePlan::MeleePlan(const core::GameState& state, core::UnitId unit) : unit_(unit) {
    const core::Unit& u = *state.find_unit(unit);
    for (const core::Model& m : u.models) models_.push_back(m.id);
    for (const core::MeleeDeclaration& d : core::melee_options(state, u)) {
        const core::Model& m = *std::find_if(u.models.begin(), u.models.end(),
                                             [&](const core::Model& x) { return x.id == d.model; });
        const core::Weapon& w = m.weapons[d.weapon_index];
        options_.push_back(MeleeOption{d, w.name, w.profiles[d.profile_index].name});
    }
    if (!options_.empty()) {
        const MeleeOption& first = options_.front();
        default_ = MeleeChoice{first.weapon, first.declaration.profile_index, first.declaration.targets.front().unit};
    }
}

std::vector<MeleeOption> MeleePlan::options_for(core::ModelId model) const {
    std::vector<MeleeOption> out;
    for (const MeleeOption& o : options_) {
        if (o.declaration.model == model) out.push_back(o);
    }
    return out;
}

std::vector<MeleeChoice> MeleePlan::choices() const {
    std::vector<MeleeChoice> out;
    for (const MeleeOption& o : options_) {
        const MeleeChoice c{o.weapon, o.declaration.profile_index, o.declaration.targets.front().unit};
        if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
    }
    return out;
}

void MeleePlan::override_model(core::ModelId model, core::MeleeDeclaration declaration) {
    overrides_[model.v] = std::move(declaration);
}

std::optional<core::MeleeDeclaration> MeleePlan::declaration_for(core::ModelId model) const {
    if (const auto it = overrides_.find(model.v); it != overrides_.end()) return it->second;
    const std::vector<MeleeOption> mine = options_for(model);
    if (mine.empty()) return std::nullopt;  // no enemy unit is engaged with this model
    if (default_) {
        for (const MeleeOption& o : mine) {
            if (o.weapon == default_->weapon && o.declaration.profile_index == default_->profile_index &&
                o.declaration.targets.front().unit == default_->target) {
                return o.declaration;
            }
        }
    }
    return mine.front().declaration;  // D-021: every model that can fight must
}

core::action::Fight MeleePlan::expand() const {
    core::action::Fight a{unit_, {}};
    for (const core::ModelId m : models_) {
        if (const auto d = declaration_for(m)) a.declarations.push_back(*d);
    }
    return a;
}

}  // namespace wh40k::client
