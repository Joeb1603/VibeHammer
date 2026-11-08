#include "shot_plan.hpp"

#include <algorithm>

#include "core/model/keywords.hpp"
#include "core/rules/shooting.hpp"

namespace wh40k::client {

namespace {

// The names of a model's ranged weapons, in order (D-071: models with the same list share a loadout).
std::vector<std::string> ranged_weapon_names(const core::Model& m) {
    std::vector<std::string> out;
    for (const core::Weapon& w : m.weapons) {
        const bool ranged = std::any_of(w.profiles.begin(), w.profiles.end(),
                                        [](const core::WeaponProfile& p) { return p.kind == core::WeaponKind::Ranged; });
        if (ranged) out.push_back(w.name);
    }
    return out;
}

}  // namespace

ShotPlan::ShotPlan(const core::GameState& state, core::UnitId unit, core::ShootingType type)
    : unit_(unit), type_(type) {
    const core::Unit& u = *state.find_unit(unit);
    for (const core::Model& m : u.models) {
        models_.push_back(m.id);
        // 24.07: outside close-quarters shooting, a non-MONSTER/VEHICLE model fires either its [CLOSE-QUARTERS]
        // weapons or its other ranged weapons.
        if (type != core::ShootingType::CloseQuarters && !core::is_monster_or_vehicle(u, m)) {
            one_set_models_.push_back(m.id);
        }
        if (!u.models.empty() && ranged_weapon_names(m) != ranged_weapon_names(u.models.front())) mixed_ = true;
    }
    for (const core::ShotDeclaration& d : core::shot_options(state, u, type)) {
        const core::Model& m = *std::find_if(u.models.begin(), u.models.end(),
                                             [&](const core::Model& x) { return x.id == d.model; });
        const core::Weapon& w = m.weapons[d.weapon_index];
        const core::WeaponProfile& p = w.profiles[d.profile_index];
        options_.push_back(ShotOption{d, w.name, p.name, p.has<core::ability::Hazardous>(),
                                      p.has<core::ability::CloseQuarters>()});
    }
    if (!options_.empty()) {
        // The first option; D-071: if it is [HAZARDOUS], the same weapon's first other profile at the same
        // target, else the first option that isn't [HAZARDOUS] (none: no default).
        const ShotOption& first = options_.front();
        const ShotOption* pick = first.hazardous ? nullptr : &first;
        for (const ShotOption& o : options_) {
            if (pick != nullptr) break;
            if (!o.hazardous && o.declaration.model == first.declaration.model &&
                o.declaration.weapon_index == first.declaration.weapon_index &&
                o.declaration.target == first.declaration.target) {
                pick = &o;
            }
        }
        for (const ShotOption& o : options_) {
            if (pick != nullptr) break;
            if (!o.hazardous) pick = &o;
        }
        if (pick != nullptr) default_ = ShotChoice{pick->weapon, pick->declaration.profile_index, pick->declaration.target};
    }
    own_weapons_ = mixed_ && default_.has_value();
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

std::vector<core::ShotDeclaration> ShotPlan::own_weapons(core::ModelId model, core::UnitId target) const {
    std::vector<const ShotOption*> mine;
    for (const ShotOption& o : options_) {
        if (o.declaration.model == model && !o.hazardous) mine.push_back(&o);
    }
    if (mine.empty()) return {};
    // The model's target: the unit default's if any of its weapons reaches it, else the first it can reach.
    core::UnitId at = mine.front()->declaration.target;
    for (const ShotOption* o : mine) {
        if (o->declaration.target == target) at = target;
    }
    // One option per weapon, in weapon order (options are listed in that order): its first profile at the model's
    // target if it reaches it, else at its first target.
    std::vector<const ShotOption*> picks;
    for (const ShotOption* o : mine) {
        const std::size_t w = o->declaration.weapon_index;
        if (std::any_of(picks.begin(), picks.end(), [w](const ShotOption* p) { return p->declaration.weapon_index == w; })) {
            continue;
        }
        const ShotOption* pick = o;
        for (const ShotOption* x : mine) {
            if (x->declaration.weapon_index == w && x->declaration.target == at) {
                pick = x;
                break;
            }
        }
        picks.push_back(pick);
    }
    // 24.07: one set only. The set with more weapons reaching the model's target; a tie, its other ranged weapons.
    const bool one_set = std::find(one_set_models_.begin(), one_set_models_.end(), model) != one_set_models_.end();
    bool use_cq = false;
    if (one_set) {
        int cq = 0;
        int other = 0;
        bool any_other = false;
        for (const ShotOption* p : picks) {
            any_other = any_other || !p->close_quarters;
            if (p->declaration.target == at) (p->close_quarters ? cq : other) += 1;
        }
        use_cq = !any_other || cq > other;
    }
    std::vector<core::ShotDeclaration> out;
    for (const ShotOption* p : picks) {
        if (!one_set || p->close_quarters == use_cq) out.push_back(p->declaration);
    }
    return out;
}

std::vector<core::ShotDeclaration> ShotPlan::declarations_for(core::ModelId model) const {
    if (const auto it = overrides_.find(model); it != overrides_.end()) {
        if (it->second) return {*it->second};
        return {};
    }
    if (!default_) return {};
    if (own_weapons_) return own_weapons(model, default_->target);  // D-071: a mixed loadout's default
    for (const ShotOption& o : options_) {
        if (o.declaration.model == model && o.weapon == default_->weapon &&
            o.declaration.profile_index == default_->profile_index && o.declaration.target == default_->target) {
            return {o.declaration};
        }
    }
    if (mixed_) return own_weapons(model, default_->target);  // D-071: a model without the chosen weapon
    return {};  // e.g. out of range: this model doesn't fire
}

std::optional<core::ShotDeclaration> ShotPlan::declaration_for(core::ModelId model) const {
    const std::vector<core::ShotDeclaration> all = declarations_for(model);
    if (all.empty()) return std::nullopt;
    return all.front();
}

core::action::Shoot ShotPlan::expand() const {
    core::action::Shoot a{unit_, type_, {}};
    for (const core::ModelId m : models_) {
        for (const core::ShotDeclaration& d : declarations_for(m)) a.shots.push_back(d);
    }
    return a;
}

}  // namespace wh40k::client
