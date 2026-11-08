#include "board_interaction.hpp"

#include <algorithm>
#include <type_traits>
#include <variant>

#include "action_text.hpp"
#include "casualty_plan.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "core/state/queries.hpp"
#include "unit_style.hpp"

namespace wh40k::client {

namespace {

namespace a = core::action;

// The targets of an action that picks enemy units (charge, pile-in, consolidation), if it has any.
const std::vector<core::UnitId>* targets_of(const core::GameAction& action) {
    if (const auto* x = std::get_if<a::StartChargeMove>(&action)) return &x->targets;
    if (const auto* x = std::get_if<a::StartPileIn>(&action)) return &x->targets;
    if (const auto* x = std::get_if<a::StartConsolidation>(&action)) return &x->targets;
    return nullptr;
}

// The unit's models, in unit order.
std::vector<core::ModelId> unit_models(const core::GameState& state, core::UnitId unit) {
    std::vector<core::ModelId> out;
    if (const core::Unit* u = state.find_unit(unit)) {
        for (const core::Model& m : u->models) out.push_back(m.id);
    }
    return out;
}

const core::WeaponProfile* profile_at(const core::GameState& state, core::ModelId model, std::size_t weapon,
                                      std::size_t profile) {
    const core::Model* m = core::find_model(state, model);
    if (m == nullptr || weapon >= m->weapons.size() || profile >= m->weapons[weapon].profiles.size()) return nullptr;
    return &m->weapons[weapon].profiles[profile];
}

void add_help(const core::WeaponProfile& p, MenuEntry& entry) {
    entry.detail = profile_stats(p);
    for (const core::WeaponAbility& ab : p.abilities) entry.help.push_back(weapon_ability_help(ab));
    // D-069: what a random A or D means.
    if (auto h = random_attacks_help(p)) entry.help.push_back(std::move(*h));
    if (auto h = random_damage_help(p)) entry.help.push_back(std::move(*h));
}

// D-037: a whole-unit weapon choice: its stats, and what its abilities do to this volley (core's
// ability_effects through effect_lines, for the selections the engine would make).
void describe(const core::GameState& state, const a::Shoot& shoot, MenuEntry& entry) {
    const core::Unit* u = state.find_unit(shoot.unit);
    if (u == nullptr || shoot.shots.empty()) return;
    const core::ShotDeclaration& first = shoot.shots.front();
    const core::WeaponProfile* p = profile_at(state, first.model, first.weapon_index, first.profile_index);
    if (p == nullptr) return;
    add_help(*p, entry);
    // The shots with this profile (D-071: a mixed loadout's other models fire other weapons).
    std::vector<core::ShotDeclaration> same;
    for (const core::ShotDeclaration& d : shoot.shots) {
        const core::WeaponProfile* q = profile_at(state, d.model, d.weapon_index, d.profile_index);
        if (q != nullptr && q->name == p->name) same.push_back(d);
    }
    const auto selections = core::make_weapon_selections(state, *u, shoot.type, same);
    entry.effects = effect_lines(*p, selections, core::shooting_context(state, *u), shoot.type);
    for (EffectLine& line : random_characteristic_lines(*p)) entry.effects.push_back(std::move(line));  // D-069
}

void describe(const core::GameState& state, const a::Fight& fight, core::UnitId target, MenuEntry& entry) {
    std::vector<core::MeleeDeclaration> at_target;
    for (const core::MeleeDeclaration& d : fight.declarations) {
        if (!d.targets.empty() && d.targets.front().unit == target) at_target.push_back(d);
    }
    if (at_target.empty()) return;
    const core::MeleeDeclaration& first = at_target.front();
    const core::WeaponProfile* p = profile_at(state, first.model, first.weapon_index, first.profile_index);
    if (p == nullptr) return;
    add_help(*p, entry);
    std::vector<core::MeleeDeclaration> same;  // the declarations with this profile
    for (const core::MeleeDeclaration& d : at_target) {
        if (d.weapon_index == first.weapon_index && d.profile_index == first.profile_index) same.push_back(d);
    }
    const auto selections = core::make_melee_selections(state, same);
    entry.effects = effect_lines(*p, selections, core::AttackContext{core::AttackType::Fighting, false});
    for (EffectLine& line : random_characteristic_lines(*p)) entry.effects.push_back(std::move(line));  // D-069
}

// D-038 / D-045: every click menu ends with Cancel. Its action is never submitted (choose() handles the
// entry first); ResetUnitMove answers none of the decisions a click menu is for.
void add_cancel(std::vector<MenuEntry>& menu) {
    if (menu.empty()) return;
    MenuEntry cancel_entry{"Cancel", a::ResetUnitMove{}};
    cancel_entry.cancel = true;
    menu.push_back(std::move(cancel_entry));
}

// A menu entry for an action; a shooting type gets its help (D-037).
MenuEntry action_entry(const core::GameState& state, const core::GameAction& action) {
    MenuEntry entry{action_label(state, action), action};
    if (const auto* s = std::get_if<a::Shoot>(&action)) entry.help.push_back(shooting_type_help(s->type));
    return entry;
}

bool contains(const std::vector<core::UnitId>& ids, core::UnitId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// Same units, in any order.
bool same_set(const std::vector<core::UnitId>& x, const std::vector<core::UnitId>& y) {
    return x.size() == y.size() && std::all_of(x.begin(), x.end(), [&](core::UnitId id) { return contains(y, id); });
}

// The actions the end-phase button submits.
bool is_end_action(const core::GameAction& action) {
    if (const auto* m = std::get_if<a::StartMove>(&action)) return m->move_type == core::MoveType::RemainStationary;
    return std::holds_alternative<a::EndShootStep>(action) || std::holds_alternative<a::EndChargeStep>(action) ||
           std::holds_alternative<a::EndPileIns>(action) || std::holds_alternative<a::EndConsolidations>(action) ||
           std::holds_alternative<a::PassFight>(action);
}

std::string unit_name(const core::GameState& state, core::UnitId id) {
    return unit_display_name(state, id);  // D-065: "Termagants 2" when its side has several units
}

}  // namespace

std::optional<core::UnitId> board_unit(const core::GameAction& action) {
    return std::visit(
        [](const auto& x) -> std::optional<core::UnitId> {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, a::StartMove>) {
                if (x.move_type == core::MoveType::RemainStationary) return std::nullopt;
                return x.unit;
            } else if constexpr (std::is_same_v<T, a::Shoot> ||
                          std::is_same_v<T, a::DeclareCharge> || std::is_same_v<T, a::StartChargeMove> ||
                          std::is_same_v<T, a::StartPileIn> || std::is_same_v<T, a::StartConsolidation> ||
                          std::is_same_v<T, a::SelectToFight>) {
                return x.unit;
            } else {
                return std::nullopt;
            }
        },
        action);
}

BoardInteraction::BoardInteraction(const core::GameState& state, std::vector<core::GameAction> legal)
    : legal_(std::move(legal)) {
    if (!state.pending) return;
    const auto& kind = state.pending->kind;
    namespace d = core::decision;
    if (std::holds_alternative<d::MoveUnits>(kind)) {
        mode_ = Mode::PickActor;
        verb_ = "move";
        end_label_ = "End Movement phase (the rest remain stationary)";
    } else if (std::holds_alternative<d::Shoot>(kind)) {
        mode_ = Mode::PickActor;
        verb_ = "shoot with";
        end_label_ = "End Shooting phase";
    } else if (std::holds_alternative<d::Charge>(kind)) {
        mode_ = Mode::PickActor;
        verb_ = "declare a charge with";
        end_label_ = "End Charge phase";
    } else if (std::holds_alternative<d::SelectUnitToFight>(kind) || std::holds_alternative<d::ForcedFight>(kind)) {
        mode_ = Mode::PickActor;
        verb_ = "fight with";
        end_label_ = "Pass";
    } else if (std::holds_alternative<d::PileIn>(kind)) {
        mode_ = Mode::PickActor;
        verb_ = "pile in with";
        end_label_ = "End pile-in moves";
    } else if (std::holds_alternative<d::Consolidate>(kind)) {
        mode_ = Mode::PickActor;
        verb_ = "consolidate with";
        end_label_ = "End consolidation moves";
    } else if (const auto* c = std::get_if<d::SelectChargeTargets>(&kind)) {
        mode_ = Mode::Targets;
        actor_ = c->unit;
        actor_fixed_ = true;
        target_verb_ = "charge";
    } else if (const auto* o = std::get_if<d::OverrunPileIn>(&kind)) {
        mode_ = Mode::Targets;
        actor_ = o->unit;
        actor_fixed_ = true;
        target_verb_ = "pile in towards";
    } else if (const auto* rc = std::get_if<d::RestoreCoherency>(&kind)) {
        mode_ = Mode::RemoveModel;  // D-042
        actor_ = rc->unit;
        actor_fixed_ = true;
    } else if (const auto* oc = std::get_if<d::OrderCasualties>(&kind)) {
        mode_ = Mode::PickCasualties;  // D-051 (reached only with automatic allocation off, D-038)
        actor_ = oc->target;
        actor_fixed_ = true;
        picks_.emplace(state, oc->target);
        default_order_ = CasualtyPlan(state, oc->target).action();
    } else if (const auto* m = std::get_if<d::MoveModels>(&kind)) {
        mode_ = Mode::Moving;
        actor_ = m->unit;
        actor_fixed_ = true;
    } else if (const auto* f = std::get_if<d::DeclareMeleeAttacks>(&kind)) {
        // D-032: click the enemy unit to attack, then a melee weapon (or Custom...).
        mode_ = Mode::Melee;
        actor_ = f->unit;
        actor_fixed_ = true;
        melee_.emplace(state, f->unit);
    }
    base_mode_ = mode_;
}

std::vector<core::GameAction> BoardInteraction::actions_of(core::UnitId unit) const {
    std::vector<core::GameAction> out;
    for (const core::GameAction& action : legal_) {
        if (board_unit(action) == unit) out.push_back(action);
    }
    return out;
}

std::vector<core::UnitId> BoardInteraction::target_options() const {
    std::vector<core::UnitId> out;
    if (!actor_) return out;
    for (const core::GameAction& action : actions_of(*actor_)) {
        if (const auto* t = targets_of(action)) {
            for (const core::UnitId id : *t) {
                if (!contains(out, id)) out.push_back(id);
            }
        }
    }
    return out;
}

bool BoardInteraction::clickable(core::UnitId unit) const {
    switch (mode_) {
        case Mode::Inactive:
            return false;
        case Mode::PickActor:
            return !actions_of(unit).empty();
        case Mode::Shoot: {
            if (unit == actor_ || !actions_of(unit).empty()) return true;
            const auto choices = shot_->choices();
            return std::any_of(choices.begin(), choices.end(), [&](const ShotChoice& c) { return c.target == unit; });
        }
        case Mode::Targets:
            return (unit == actor_ && !actor_fixed_) || contains(target_options(), unit);
        case Mode::Moving:
        case Mode::CustomShot:
        case Mode::CustomMelee:
        case Mode::RemoveModel:
            return unit == actor_;
        case Mode::PickCasualties:
            return unit == actor_ && picks_ && !picks_->done();
        case Mode::Melee: {
            const auto choices = melee_->choices();
            return std::any_of(choices.begin(), choices.end(), [&](const MeleeChoice& c) { return c.target == unit; });
        }
    }
    return false;
}

bool BoardInteraction::waiting_for_click() const {
    switch (mode_) {
        case Mode::PickActor:
            return std::any_of(legal_.begin(), legal_.end(),
                               [](const core::GameAction& action) { return board_unit(action).has_value(); });
        case Mode::Shoot:
        case Mode::Targets:
        case Mode::Melee:
        case Mode::RemoveModel:
            return true;
        case Mode::PickCasualties:
            return picks_ && !picks_->done();
        case Mode::Inactive:
        case Mode::Moving:
        case Mode::CustomShot:
        case Mode::CustomMelee:
            return false;
    }
    return false;
}

bool BoardInteraction::greyed(core::UnitId unit) const {
    if (waiting_for_click()) return !clickable(unit);
    return mode_ == Mode::PickActor;  // nothing left to pick: everything is grey
}

std::string BoardInteraction::prompt(const core::GameState& state) const {
    if (mode_ == Mode::Shoot && actor_) return "Pick a target for the " + unit_name(state, *actor_);
    if (mode_ == Mode::Targets) return "Pick the enemy units to " + target_verb_;
    if (mode_ == Mode::Melee && actor_) return "Pick a target for the " + unit_name(state, *actor_) + "'s attacks";
    if ((mode_ == Mode::CustomShot || mode_ == Mode::CustomMelee) && actor_) {
        std::string text = (mode_ == Mode::CustomShot ? "Custom shot: " : "Custom fight: ") + unit_name(state, *actor_);
        if (custom_target_) text += " at " + unit_name(state, *custom_target_);
        return text;
    }
    return "";
}

std::string BoardInteraction::hint(const core::GameState& state) const {
    switch (mode_) {
        case Mode::Inactive:
            return "";
        case Mode::PickActor:
            if (!waiting_for_click()) return "No units left to " + verb_ + ".";
            return "Click a unit to " + verb_ + ".";
        case Mode::Shoot: {
            const std::string name = unit_name(state, *actor_);
            const auto choices = shot_->choices();
            if (choices.empty()) return name + " has no target in range. Click it again to pick another unit.";
            return "Click a target for the " + name + ", then pick a weapon (or Custom... to choose per model). "
                   "Click the " + name + " again to pick another unit.";
        }
        case Mode::CustomShot:
            return "Choose each model's weapon and target, then Shoot. Esc or Back returns to picking a target.";
        case Mode::Melee:
            return "Click an engaged enemy unit, then pick a melee weapon (or Custom... to choose per model).";
        case Mode::CustomMelee:
            return "Choose each model's melee weapon and target (every model that can fight must), then Fight. "
                   "Esc or Back returns to picking a target.";
        case Mode::Targets: {
            std::string text = "Click the enemy units to " + target_verb_ + ".";
            if (!targets_.empty()) {
                text += " Chosen:";
                for (const core::UnitId id : targets_) text += " " + unit_name(state, id);
                text += ".";
            }
            return text;
        }
        case Mode::Moving:
            return "Drag the models (or the yellow diamond, for the whole unit) to move them, then commit the move.";
        case Mode::RemoveModel: {
            const std::string name = unit_name(state, *actor_);
            if (chosen_model_) {
                return "Model " + std::to_string(chosen_model_->v) + " of the " + name +
                       " will be removed. Click another model to change it, or confirm.";
            }
            return "The " + name + " is out of coherency (03.03 Regaining Coherency): click a model to remove, "
                   "then confirm.";
        }
        case Mode::PickCasualties: {
            const std::string name = unit_name(state, *actor_);
            if (!picks_ || !picks_->preview()) return "";
            const core::CasualtyPreview& p = *picks_->preview();
            // D-069: a random D is rolled when the casualties are confirmed; the preview shows the most it can do.
            const std::string random = p.random_damage ? core::to_string(*p.random_damage) : std::string();
            if (picks_->done()) {
                std::string text = "Every casualty of the " + name + " is picked. Confirm them, or Undo pick to change.";
                if (p.random_damage) text += " The " + random + " damage is rolled when you confirm.";
                return text;
            }
            const int d = p.next_damage.value_or(1);
            std::string damage = d == 1 ? std::string("damage") : std::to_string(d) + " damage";
            if (p.random_damage) damage = random + " damage (up to " + std::to_string(d) + ")";
            // D-058 (M47): or the default order, which keeps the picks so far.
            std::string text = "Click the model of the " + name + " that takes the next " + damage +
                               " (05.04 / 06.02), or use the default order" +
                               (picks_->picks().empty() ? "" : " for the rest") +
                               ". A picked model keeps taking the damage forced onto it.";
            if (p.options.size() == 1) text += " Only one model can take it.";
            // D-069: after a lower roll the model before may survive and take this attack instead.
            if (p.next_provisional()) text += " Thin rings are provisional.";
            return text;
        }
    }
    return "";
}

bool BoardInteraction::model_dimmed(core::UnitId unit, core::ModelId model) const {
    if (mode_ != Mode::PickCasualties || unit != actor_ || !picks_ || picks_->done()) return false;
    return !picks_->can_pick(model);
}

void BoardInteraction::undo_pick(const core::GameState& state) {
    if (picks_) picks_->undo_pick(state);
}

bool keep_selection(std::optional<core::UnitId> selected, std::optional<core::UnitId> actor,
                    std::optional<core::UnitId> moving, std::optional<core::UnitId> menu_unit) {
    if (!selected) return false;
    return selected == actor || selected == moving || selected == menu_unit;
}

ClickResult BoardInteraction::click_model(const core::GameState& state, core::UnitId unit, core::ModelId model) {
    if (mode_ == Mode::PickCasualties) {
        if (unit == actor_ && picks_) picks_->pick(state, model);  // D-051: ignored unless it may be picked
        return {};
    }
    if (mode_ != Mode::RemoveModel) return click(state, unit);
    if (!clickable(unit)) return {};
    for (const core::GameAction& action : legal_) {
        const auto* r = std::get_if<a::RemoveModelForCoherency>(&action);
        if (r != nullptr && r->unit == unit && r->model == model) chosen_model_ = model;
    }
    return {};
}

ClickResult BoardInteraction::click(const core::GameState& state, core::UnitId unit) {
    if (!clickable(unit)) return {};
    switch (mode_) {
        case Mode::Inactive:
        case Mode::Moving:
        case Mode::CustomShot:
        case Mode::CustomMelee:
        case Mode::RemoveModel:  // models are clicked one at a time (click_model)
        case Mode::PickCasualties:
            return {};
        case Mode::Melee:
            return melee_menu(state, unit);
        case Mode::PickActor:
            return pick_actor(state, unit);
        case Mode::Shoot: {
            if (unit == actor_) {
                cancel();  // click the shooter again: pick another unit
                return {};
            }
            if (!actions_of(unit).empty()) return pick_actor(state, unit);  // another friendly shooter
            // A target: one entry per weapon choice that can reach it (D-001: the whole unit by default).
            ClickResult result;
            for (const ShotChoice& c : shot_->choices()) {
                if (c.target != unit) continue;
                ShotPlan plan = *shot_;
                plan.set_default(c);
                const a::Shoot shoot = plan.expand();
                if (shoot.shots.empty()) continue;
                std::string profile;
                for (const ShotOption& o : plan.options()) {
                    if (o.weapon == c.weapon && o.declaration.profile_index == c.profile_index) profile = o.profile;
                }
                // Profile names usually include the weapon ("Bolt Rifle - Focused Fire").
                std::string label = c.weapon;
                if (profile.rfind(c.weapon, 0) == 0) {
                    label = profile;
                } else if (!profile.empty()) {
                    label += " (" + profile + ")";
                }
                // Models, not declarations: in a mixed loadout a model can fire several weapons (D-071).
                std::size_t at = 0;
                std::size_t own = 0;
                for (const core::ModelId m : unit_models(state, plan.unit())) {
                    const std::vector<core::ShotDeclaration> ds = plan.declarations_for(m);
                    if (ds.empty()) continue;
                    const bool chosen = std::any_of(ds.begin(), ds.end(), [&](const core::ShotDeclaration& d) {
                        for (const ShotOption& o : plan.options()) {
                            if (o.declaration == d) {
                                return o.weapon == c.weapon && d.profile_index == c.profile_index && d.target == c.target;
                            }
                        }
                        return false;
                    });
                    (chosen ? at : own) += 1;
                }
                label += ": " + std::to_string(at) + (at == 1 ? " model" : " models");
                if (own > 0) label += " (+" + std::to_string(own) + " with their own weapons)";
                MenuEntry entry{label, shoot};
                describe(state, shoot, entry);
                result.menu.push_back(std::move(entry));
            }
            // D-031: per-model weapons and targets are chosen after the target, starting from the first weapon.
            if (!result.menu.empty()) {
                result.menu.push_back(MenuEntry{"Custom...", result.menu.front().action, true});
            }
            add_cancel(result.menu);
            return result;
        }
        case Mode::Targets: {
            if (unit == actor_) {
                cancel();
                return {};
            }
            if (contains(targets_, unit)) {
                targets_.erase(std::find(targets_.begin(), targets_.end(), unit));
                return {};
            }
            targets_.push_back(unit);
            // Submit once the choice matches an option that no bigger option extends.
            const auto match = confirm();
            const auto options = actions_of(*actor_);
            const bool bigger = std::any_of(options.begin(), options.end(), [&](const core::GameAction& o) {
                const auto* t = targets_of(o);
                return t != nullptr && t->size() > targets_.size() &&
                       std::all_of(targets_.begin(), targets_.end(), [&](core::UnitId id) { return contains(*t, id); });
            });
            if (match && !bigger) return ClickResult{match, {}};
            return {};
        }
    }
    return {};
}

ClickResult BoardInteraction::pick_actor(const core::GameState& state, core::UnitId unit) {
    const std::vector<core::GameAction> actions = actions_of(unit);
    if (actions.empty()) return {};
    if (std::holds_alternative<a::Shoot>(actions.front())) {
        if (actions.size() == 1) return choose(state, actions.front());
        ClickResult result;  // several shooting types: pick one first
        for (const core::GameAction& action : actions) result.menu.push_back(action_entry(state, action));
        add_cancel(result.menu);
        return result;
    }
    if (targets_of(actions.front()) != nullptr) {
        // Pile-in / consolidation: an engaged unit has one set of targets, so it starts at once.
        const bool one_set = std::all_of(actions.begin(), actions.end(), [&](const core::GameAction& action) {
            return same_set(*targets_of(action), *targets_of(actions.front()));
        });
        if (one_set) return ClickResult{actions.front(), {}};
        target_verb_ =
            std::holds_alternative<a::StartPileIn>(actions.front()) ? "pile in towards" : "consolidate towards";
        return start_targets(unit);
    }
    if (std::holds_alternative<a::StartMove>(actions.front())) {
        // D-038: always a menu for a move, even with one move type (an engaged unit's Fall Back makes a
        // Desperate Escape roll as soon as it is chosen), with Cancel at the end.
        ClickResult result;
        for (const core::GameAction& action : actions) result.menu.push_back(action_entry(state, action));
        add_cancel(result.menu);
        return result;
    }
    // D-045: declaring a charge makes the charge roll at once (11.02), so it is asked, as a move is.
    const bool rolls = std::holds_alternative<a::DeclareCharge>(actions.front());
    if (actions.size() == 1 && !rolls) return ClickResult{actions.front(), {}};
    ClickResult result;
    for (const core::GameAction& action : actions) result.menu.push_back(action_entry(state, action));
    add_cancel(result.menu);
    return result;
}

ClickResult BoardInteraction::start_targets(core::UnitId unit) {
    actor_ = unit;
    targets_.clear();
    mode_ = Mode::Targets;
    return {};
}

ClickResult BoardInteraction::choose(const core::GameState& state, const core::GameAction& action) {
    if (const auto* s = std::get_if<a::Shoot>(&action); s != nullptr && s->shots.empty()) {
        actor_ = s->unit;  // a shooting template: this unit shoots; now pick a target
        shot_.emplace(state, s->unit, s->type);
        targets_.clear();
        mode_ = Mode::Shoot;
        return {};
    }
    return ClickResult{action, {}};
}

ClickResult BoardInteraction::melee_menu(const core::GameState& state, core::UnitId unit) const {
    // D-032: one entry per melee weapon choice at the clicked target, then "Custom...". A model that can't
    // reach that target still fights with its first option (D-021), which the label counts separately.
    ClickResult result;
    for (const MeleeChoice& c : melee_->choices()) {
        if (c.target != unit) continue;
        MeleePlan plan = *melee_;
        plan.set_default(c);
        const a::Fight fight = plan.expand();
        if (fight.declarations.empty()) continue;
        std::size_t at = 0;
        std::string profile;
        for (const MeleeOption& o : plan.options()) {
            if (o.weapon != c.weapon || o.declaration.profile_index != c.profile_index ||
                o.declaration.targets.front().unit != unit) {
                continue;
            }
            profile = o.profile;
            if (plan.declaration_for(o.declaration.model) == o.declaration) ++at;
        }
        std::string label = c.weapon;
        if (profile.rfind(c.weapon, 0) == 0) {
            label = profile;
        } else if (!profile.empty()) {
            label += " (" + profile + ")";
        }
        label += ": " + std::to_string(at) + (at == 1 ? " model" : " models");
        // Models, not declarations: a model with [EXTRA ATTACKS] weapons declares those too (24.11, D-071).
        std::size_t fighting = 0;
        for (const core::ModelId m : unit_models(state, plan.unit())) fighting += plan.declaration_for(m) ? 1 : 0;
        if (const std::size_t other = fighting - at; other > 0) {
            label += " (+" + std::to_string(other) + " at another target)";
        }
        MenuEntry entry{label, fight};
        describe(state, fight, unit, entry);
        result.menu.push_back(std::move(entry));
    }
    if (!result.menu.empty()) {
        // The custom entry's action starts the editor: the first entry's declarations at the clicked target.
        a::Fight seed = std::get<a::Fight>(result.menu.front().action);
        std::erase_if(seed.declarations,
                      [&](const core::MeleeDeclaration& d) { return d.targets.front().unit != unit; });
        result.menu.push_back(MenuEntry{"Custom...", seed, true});
    }
    add_cancel(result.menu);
    return result;
}

ClickResult BoardInteraction::choose(const core::GameState& state, const MenuEntry& entry) {
    if (entry.cancel) {
        // D-045: on a target's weapon menu, Cancel only closes it (still picking a target for the same
        // shooter or fighter); on a unit's menu it drops the unit.
        if (mode_ == Mode::PickActor) cancel();
        return {};
    }
    if (const auto* f = std::get_if<a::Fight>(&entry.action);
        entry.custom && f != nullptr && !f->declarations.empty() && mode_ == Mode::Melee && melee_) {
        // D-032: a copy of the fighter's plan starting from the entry's weapon at its target.
        MeleePlan plan = *melee_;
        const core::MeleeDeclaration& first = f->declarations.front();
        for (const MeleeOption& o : plan.options()) {
            if (o.declaration == first) {
                plan.set_default(MeleeChoice{o.weapon, first.profile_index, first.targets.front().unit});
                break;
            }
        }
        custom_target_ = first.targets.front().unit;
        custom_melee_ = std::move(plan);
        mode_ = Mode::CustomMelee;
        return {};
    }
    const auto* s = std::get_if<a::Shoot>(&entry.action);
    if (!entry.custom || s == nullptr || s->shots.empty() || mode_ != Mode::Shoot || !shot_) {
        return choose(state, entry.action);
    }
    // D-031: a copy of the shooter's plan whose unit-wide choice is the entry's weapon at its target; the
    // panel edits each model's choice on it. shot_ stays as it was, for going back.
    const core::ShotDeclaration& first = s->shots.front();
    ShotPlan plan = *shot_;
    for (const ShotOption& o : plan.options()) {
        if (o.declaration == first) {
            plan.set_default(ShotChoice{o.weapon, o.declaration.profile_index, o.declaration.target});
            break;
        }
    }
    custom_ = std::move(plan);
    custom_target_ = first.target;
    mode_ = Mode::CustomShot;
    return {};
}

void BoardInteraction::cancel() {
    targets_.clear();
    chosen_model_.reset();
    custom_.reset();
    custom_melee_.reset();
    custom_target_.reset();
    if (mode_ == Mode::CustomShot) {
        mode_ = Mode::Shoot;  // back to picking a target for the same shooter, with nothing customised
        return;
    }
    if (mode_ == Mode::CustomMelee) {
        mode_ = Mode::Melee;
        return;
    }
    shot_.reset();
    if (!actor_fixed_) actor_.reset();
    mode_ = base_mode_;
}

std::optional<EndPhase> BoardInteraction::end_phase() const {
    if (end_label_.empty() || custom_editor()) return std::nullopt;
    EndPhase end{end_label_, {}};
    for (const core::GameAction& action : legal_) {
        if (is_end_action(action)) end.actions.push_back(action);
    }
    if (end.actions.empty()) return std::nullopt;
    return end;
}

std::vector<core::GameAction> BoardInteraction::step_buttons() const {
    std::vector<core::GameAction> out;
    if (custom_editor()) return out;  // the panel's Back and Shoot / Fight only (D-031, D-032)
    if (mode_ == Mode::RemoveModel) return out;  // D-042: a model is clicked, then confirmed
    if (mode_ == Mode::PickCasualties) {  // D-051: or skip the picking with the default order (D-058)
        if (picks_ && !picks_->picks().empty()) {
            // M47: finish from the picks so far: the picks, then the default order for the rest.
            if (!picks_->done()) out.push_back(picks_->action());
        } else if (default_order_) {
            out.push_back(*default_order_);
        }
        return out;
    }
    for (const core::GameAction& action : legal_) {
        if (core::is_template(action) || board_unit(action)) continue;
        if (!end_label_.empty() && is_end_action(action)) continue;  // the end-phase button covers it
        out.push_back(action);
    }
    return out;
}

std::string BoardInteraction::default_order_label() const {
    return picks_ && !picks_->picks().empty() ? "Default order for the rest" : "Use default order";
}

std::optional<core::GameAction> BoardInteraction::confirm() const {
    if (mode_ == Mode::RemoveModel) {
        if (!chosen_model_ || !actor_) return std::nullopt;
        return a::RemoveModelForCoherency{*actor_, *chosen_model_};  // D-042
    }
    if (mode_ == Mode::PickCasualties) {
        if (!picks_ || !picks_->done()) return std::nullopt;
        return picks_->action();  // D-051: the picks, then the rest of the unit
    }
    if (mode_ != Mode::Targets || targets_.empty() || !actor_) return std::nullopt;
    for (const core::GameAction& action : actions_of(*actor_)) {
        if (const auto* t = targets_of(action); t != nullptr && same_set(*t, targets_)) return action;
    }
    return std::nullopt;
}

}  // namespace wh40k::client
