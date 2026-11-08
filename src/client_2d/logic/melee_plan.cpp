#include "melee_plan.hpp"

#include <algorithm>

#include "core/rules/attacks.hpp"
#include "core/rules/fight.hpp"

namespace wh40k::client {

namespace {

// D-071: the first option of `options` that isn't [HAZARDOUS], else the first; nullopt if there are none.
std::optional<MeleeOption> prefer_safe(const std::vector<MeleeOption>& options) {
    for (const MeleeOption& o : options) {
        if (!o.hazardous) return o;
    }
    if (options.empty()) return std::nullopt;
    return options.front();
}

}  // namespace

MeleePlan::MeleePlan(const core::GameState& state, core::UnitId unit) : unit_(unit) {
    const core::Unit& u = *state.find_unit(unit);
    for (const core::Model& m : u.models) models_.push_back(m.id);
    for (const core::MeleeDeclaration& d : core::melee_options(state, u)) {
        const core::Model& m = *std::find_if(u.models.begin(), u.models.end(),
                                             [&](const core::Model& x) { return x.id == d.model; });
        const core::Weapon& w = m.weapons[d.weapon_index];
        const core::WeaponProfile& p = w.profiles[d.profile_index];
        options_.push_back(
            MeleeOption{d, w.name, p.name, core::is_extra_attacks_weapon(w), p.has<core::ability::Hazardous>()});
    }
    // The first main option; D-071: if it is [HAZARDOUS], the same weapon's other profile at the same target, else
    // the first main option that isn't, else the first.
    std::vector<MeleeOption> mains;
    for (const core::ModelId m : models_) {
        for (MeleeOption& o : main_options_for(m)) mains.push_back(std::move(o));
    }
    if (!mains.empty()) {
        const MeleeOption& first = mains.front();
        std::optional<MeleeOption> pick;
        if (!first.hazardous) pick = first;
        for (const MeleeOption& o : mains) {
            if (pick) break;
            if (!o.hazardous && o.declaration.model == first.declaration.model &&
                o.declaration.weapon_index == first.declaration.weapon_index &&
                o.declaration.targets.front().unit == first.declaration.targets.front().unit) {
                pick = o;
            }
        }
        if (!pick) pick = prefer_safe(mains);
        default_ = MeleeChoice{pick->weapon, pick->declaration.profile_index, pick->declaration.targets.front().unit};
    }
}

std::vector<MeleeOption> MeleePlan::options_for(core::ModelId model) const {
    std::vector<MeleeOption> out;
    for (const MeleeOption& o : options_) {
        if (o.declaration.model == model) out.push_back(o);
    }
    return out;
}

std::vector<MeleeOption> MeleePlan::main_options_for(core::ModelId model) const {
    std::vector<MeleeOption> others;
    std::vector<MeleeOption> extras;
    for (const MeleeOption& o : options_) {
        if (o.declaration.model == model) (o.extra_attacks ? extras : others).push_back(o);
    }
    return others.empty() ? extras : others;
}

std::vector<MeleeChoice> MeleePlan::choices() const {
    std::vector<MeleeChoice> out;
    for (const core::ModelId m : models_) {
        for (const MeleeOption& o : main_options_for(m)) {
            const MeleeChoice c{o.weapon, o.declaration.profile_index, o.declaration.targets.front().unit};
            if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
        }
    }
    return out;
}

void MeleePlan::override_model(core::ModelId model, core::MeleeDeclaration declaration) {
    overrides_[model] = std::move(declaration);
}

std::optional<core::MeleeDeclaration> MeleePlan::declaration_for(core::ModelId model) const {
    if (const auto it = overrides_.find(model); it != overrides_.end()) return it->second;
    const std::vector<MeleeOption> mine = main_options_for(model);
    if (mine.empty()) return std::nullopt;  // no enemy unit is engaged with this model
    if (default_) {
        std::vector<MeleeOption> at_target;
        for (const MeleeOption& o : mine) {
            if (o.weapon == default_->weapon && o.declaration.profile_index == default_->profile_index &&
                o.declaration.targets.front().unit == default_->target) {
                return o.declaration;
            }
            if (o.declaration.targets.front().unit == default_->target) at_target.push_back(o);
        }
        if (const auto pick = prefer_safe(at_target)) return pick->declaration;  // D-071: the same target
    }
    return prefer_safe(mine)->declaration;  // D-021: every model that can fight must
}

std::vector<core::MeleeDeclaration> MeleePlan::extra_declarations_for(core::ModelId model) const {
    std::vector<core::MeleeDeclaration> out;
    const std::optional<core::MeleeDeclaration> main = declaration_for(model);
    if (!main || main->targets.empty()) return out;
    const core::UnitId target = main->targets.front().unit;
    std::vector<std::size_t> weapons;  // the model's [EXTRA ATTACKS] weapons, in weapon order
    for (const MeleeOption& o : options_) {
        if (o.declaration.model != model || !o.extra_attacks || o.declaration.weapon_index == main->weapon_index) {
            continue;
        }
        if (std::find(weapons.begin(), weapons.end(), o.declaration.weapon_index) == weapons.end()) {
            weapons.push_back(o.declaration.weapon_index);
        }
    }
    std::sort(weapons.begin(), weapons.end());
    for (const std::size_t w : weapons) {
        std::vector<MeleeOption> at_target;
        std::vector<MeleeOption> any;
        for (const MeleeOption& o : options_) {
            if (o.declaration.model != model || o.declaration.weapon_index != w) continue;
            any.push_back(o);
            if (o.declaration.targets.front().unit == target) at_target.push_back(o);
        }
        const auto pick = prefer_safe(at_target.empty() ? any : at_target);
        if (pick) out.push_back(pick->declaration);
    }
    return out;
}

std::vector<core::MeleeDeclaration> MeleePlan::declarations_for(core::ModelId model) const {
    std::vector<core::MeleeDeclaration> out;
    if (const auto main = declaration_for(model)) out.push_back(*main);
    for (core::MeleeDeclaration& d : extra_declarations_for(model)) out.push_back(std::move(d));
    return out;
}

core::action::Fight MeleePlan::expand() const {
    core::action::Fight a{unit_, {}};
    for (const core::ModelId m : models_) {
        for (core::MeleeDeclaration& d : declarations_for(m)) a.declarations.push_back(std::move(d));
    }
    return a;
}

}  // namespace wh40k::client
