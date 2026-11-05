#include "shot_plan.hpp"

#include <algorithm>

#include "core/rules/shooting.hpp"

namespace wh40k::client {

ShotPlan::ShotPlan(const core::GameState& state, core::UnitId unit, core::ShootingType type)
    : unit_(unit), type_(type) {
    const core::Unit& u = *state.find_unit(unit);
    for (const core::Model& m : u.models) models_.push_back(m.id);
    for (const core::ShotDeclaration& d : core::shot_options(state, u, type)) {
        const core::Model& m = *std::find_if(u.models.begin(), u.models.end(),
                                             [&](const core::Model& x) { return x.id == d.model; });
        const core::Weapon& w = m.weapons[d.weapon_index];
        options_.push_back(ShotOption{d, w.name, w.profiles[d.profile_index].name});
    }
    if (!options_.empty()) {
        const ShotOption& first = options_.front();
        default_ = ShotChoice{first.weapon, first.declaration.profile_index, first.declaration.target};
    }
}

std::vector<ShotOption> ShotPlan::options_for(core::ModelId model) const {
    std::vector<ShotOption> out;
    for (const ShotOption& o : options_) {
        if (o.declaration.model == model) out.push_back(o);
    }
    return out;
}

std::vector<ShotChoice> ShotPlan::choices() const {
    std::vector<ShotChoice> out;
    for (const ShotOption& o : options_) {
        const ShotChoice c{o.weapon, o.declaration.profile_index, o.declaration.target};
        if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
    }
    return out;
}

void ShotPlan::override_model(core::ModelId model, std::optional<core::ShotDeclaration> choice) {
    overrides_[model] = choice;
}

std::optional<core::ShotDeclaration> ShotPlan::declaration_for(core::ModelId model) const {
    if (const auto it = overrides_.find(model); it != overrides_.end()) return it->second;
    if (!default_) return std::nullopt;
    for (const ShotOption& o : options_) {
        if (o.declaration.model == model && o.weapon == default_->weapon &&
            o.declaration.profile_index == default_->profile_index && o.declaration.target == default_->target) {
            return o.declaration;
        }
    }
    return std::nullopt;  // e.g. out of range: this model doesn't fire
}

core::action::Shoot ShotPlan::expand() const {
    core::action::Shoot a{unit_, type_, {}};
    for (const core::ModelId m : models_) {
        if (const auto d = declaration_for(m)) a.shots.push_back(*d);
    }
    return a;
}

}  // namespace wh40k::client
