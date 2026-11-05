#include "board_interaction.hpp"

#include <algorithm>
#include <type_traits>
#include <variant>

#include "action_text.hpp"

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
    const core::Unit* u = state.find_unit(id);
    return u != nullptr ? u->name : "unit " + std::to_string(id.v);
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
    } else if (const auto* m = std::get_if<d::MoveModels>(&kind)) {
        mode_ = Mode::Moving;
        actor_ = m->unit;
        actor_fixed_ = true;
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
            return unit == actor_;
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
            return true;
        case Mode::Inactive:
        case Mode::Moving:
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
            return "Click a target for the " + name + ", then pick a weapon. Click the " + name +
                   " again to pick another unit.";
        }
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
    }
    return "";
}

ClickResult BoardInteraction::click(const core::GameState& state, core::UnitId unit) {
    if (!clickable(unit)) return {};
    switch (mode_) {
        case Mode::Inactive:
        case Mode::Moving:
            return {};
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
                label += ": " + std::to_string(shoot.shots.size()) + (shoot.shots.size() == 1 ? " model" : " models");
                result.menu.push_back(MenuEntry{label, shoot});
            }
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
        for (const core::GameAction& action : actions) result.menu.push_back({action_label(state, action), action});
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
    if (actions.size() == 1) return ClickResult{actions.front(), {}};
    ClickResult result;
    for (const core::GameAction& action : actions) result.menu.push_back({action_label(state, action), action});
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

void BoardInteraction::cancel() {
    targets_.clear();
    shot_.reset();
    if (!actor_fixed_) actor_.reset();
    mode_ = base_mode_;
}

std::optional<EndPhase> BoardInteraction::end_phase() const {
    if (end_label_.empty()) return std::nullopt;
    EndPhase end{end_label_, {}};
    for (const core::GameAction& action : legal_) {
        if (is_end_action(action)) end.actions.push_back(action);
    }
    if (end.actions.empty()) return std::nullopt;
    return end;
}

std::vector<core::GameAction> BoardInteraction::step_buttons() const {
    std::vector<core::GameAction> out;
    for (const core::GameAction& action : legal_) {
        if (core::is_template(action) || board_unit(action)) continue;
        if (!end_label_.empty() && is_end_action(action)) continue;  // the end-phase button covers it
        out.push_back(action);
    }
    return out;
}

std::optional<core::GameAction> BoardInteraction::confirm() const {
    if (mode_ != Mode::Targets || targets_.empty() || !actor_) return std::nullopt;
    for (const core::GameAction& action : actions_of(*actor_)) {
        if (const auto* t = targets_of(action); t != nullptr && same_set(*t, targets_)) return action;
    }
    return std::nullopt;
}

}  // namespace wh40k::client
