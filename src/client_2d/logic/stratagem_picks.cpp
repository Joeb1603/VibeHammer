#include "stratagem_picks.hpp"

#include <algorithm>

#include "core/model/keywords.hpp"
#include "core/rules/stratagem_effects.hpp"
#include "core/state/queries.hpp"
#include "unit_style.hpp"

namespace wh40k::client {

namespace {

const core::StratagemEffect* pending_selection(const core::GameState& state) {
    if (!state.pending || !std::holds_alternative<core::decision::SelectModelAndEnemy>(state.pending->kind)) {
        return nullptr;
    }
    return state.flow.effect ? &*state.flow.effect : nullptr;
}

}  // namespace

std::vector<EffectChoice> effect_choices(const core::GameState& state, std::optional<core::ModelId> model) {
    std::vector<EffectChoice> out;
    const core::StratagemEffect* e = pending_selection(state);
    if (e == nullptr) return out;
    const std::vector<core::GameAction> legal = core::legal_actions(state, state.pending->side);
    const core::Unit* unit = state.find_unit(e->unit);
    for (const core::Unit& enemy : state.units) {  // unit order
        for (const core::GameAction& a : legal) {  // model order: the first legal model for this enemy unit
            const auto* s = std::get_if<core::action::SelectModelAndEnemy>(&a);
            if (s == nullptr || s->enemy != enemy.id) continue;
            if (model && s->model != *model) continue;  // D-100 (M98): the model chosen on the board
            std::string label = unit_display_name(state, enemy.id) + " (model " + std::to_string(s->model.v);
            if (e->stratagem == core::Stratagem::CrushingImpact && unit != nullptr) {
                for (const core::Model& m : unit->models) {
                    if (m.id != s->model) continue;
                    const int dice = core::effect_dice(e->stratagem, m);
                    label += ", T" + std::to_string(m.characteristics.toughness) + ": " + std::to_string(dice) + " dice";
                }
            }
            out.push_back(EffectChoice{a, label + ")", enemy.id, s->model});
            break;
        }
    }
    return out;
}

std::optional<std::string> effect_refusal_text(const core::GameState& state, core::UnitId enemy,
                                               std::optional<core::ModelId> model) {
    const core::StratagemEffect* e = pending_selection(state);
    if (e == nullptr) return std::nullopt;
    const core::Unit* unit = state.find_unit(e->unit);
    const core::Unit* target = state.find_unit(enemy);
    if (unit == nullptr || unit->models.empty() || target == nullptr || target->side == unit->side ||
        core::is_destroyed(*target)) {
        return std::nullopt;
    }
    std::optional<core::ModelId> m = model;
    if (!m) {
        // The model closest to the enemy unit (01.04, edge to edge; ties keep the first in unit order).
        std::optional<float> closest;
        for (const core::Model& x : unit->models) {
            const std::optional<float> d = core::model_distance_to_unit(core::pose_of(x), *target);
            if (d && (!closest || *d < *closest)) {
                closest = d;
                m = x.id;
            }
        }
        if (!m) m = unit->models.front().id;
    }
    const core::ValidationResult v =
        core::validate_action(state, state.pending->side, core::action::SelectModelAndEnemy{*m, enemy});
    if (v.ok() || v.reasons.empty()) return std::nullopt;
    return v.reasons.front();
}

std::string effect_headline(const core::GameState& state) {
    const core::StratagemEffect* e = pending_selection(state);
    if (e == nullptr) return "";
    return std::string(core::stratagem_name(e->stratagem)) + ": pick the enemy unit (" +
           std::string(core::stratagem_rule(e->stratagem)) + ")";
}

std::string effect_hint(const core::GameState& state) {
    const core::StratagemEffect* e = pending_selection(state);
    if (e == nullptr) return "";
    if (e->stratagem == core::Stratagem::Explosives) {
        return "One model of " + unit_display_name(state, e->unit) +
               " throws at an unengaged enemy unit within 8\" that it can see: six D6, each 4+ is a mortal wound.";
    }
    return "A model of " + unit_display_name(state, e->unit) +
           " engaged with the enemy unit rolls a D6 per point of its T: each 5+ is a mortal wound on the enemy unit, "
           "each 1 one on your unit (at most 6 each).";
}

StratagemPicks::StratagemPicks(const core::GameState& state) : info_(core::window_info(state)) {
    if (!info_ || info_->whole_roll) return;
    // The only pickable die (a damage or advance roll) is picked already: one click less.
    const auto n = std::count(info_->pickable.begin(), info_->pickable.end(), true);
    if (n == 1) picked_ = static_cast<std::size_t>(std::find(info_->pickable.begin(), info_->pickable.end(), true) -
                                                   info_->pickable.begin());
}

bool StratagemPicks::offers(core::Stratagem s) const {
    if (!info_) return false;
    return std::any_of(info_->options.begin(), info_->options.end(),
                       [s](const core::StratagemOption& o) { return o.stratagem == s; });
}

int StratagemPicks::cost(core::Stratagem s) const {
    if (!info_) return 0;
    for (const core::StratagemOption& o : info_->options) {
        if (o.stratagem == s) return o.cost;
    }
    return 0;
}

bool StratagemPicks::pickable(std::size_t die) const {
    return info_ && !info_->whole_roll && die < info_->pickable.size() && info_->pickable[die];
}

void StratagemPicks::pick(std::size_t die) {
    if (!pickable(die)) return;
    picked_ = picked_ == die ? std::nullopt : std::optional<std::size_t>(die);
}

bool StratagemPicks::reroll_ready() const {
    return offers(core::Stratagem::CommandReroll) && (info_->whole_roll || picked_.has_value());
}

std::optional<core::GameAction> StratagemPicks::command_reroll() const {
    if (!reroll_ready()) return std::nullopt;
    return core::action::UseStratagem{core::Stratagem::CommandReroll, info_->unit.value(),
                                      info_->whole_roll ? std::nullopt : picked_};
}

std::optional<core::GameAction> StratagemPicks::insane_bravery() const {
    if (!offers(core::Stratagem::InsaneBravery)) return std::nullopt;
    return core::action::UseStratagem{core::Stratagem::InsaneBravery, info_->unit.value(), std::nullopt};
}

std::string StratagemPicks::reroll_label() const {
    return "Command Re-roll (" + std::to_string(cost(core::Stratagem::CommandReroll)) + " CP)";
}

std::string StratagemPicks::insane_bravery_label() const {
    return "Insane Bravery (" + std::to_string(cost(core::Stratagem::InsaneBravery)) + " CP)";
}

std::vector<ReactionChoice> StratagemPicks::reactions(const core::GameState& state) const {
    std::vector<ReactionChoice> out;
    if (!reaction_window()) return out;
    for (const core::StratagemOption& o : info_->options) {
        // "Fire Overwatch (1 CP)", or Heroic Intervention's mode: "Leap to Defend (1 CP)".
        const std::string head = (o.mode ? std::string(core::stratagem_mode_name(*o.mode)) : o.name) + " (" +
                                 std::to_string(o.cost) + " CP): ";
        for (const core::UnitId u : o.targets) {
            out.push_back(ReactionChoice{core::action::UseStratagem{o.stratagem, u, std::nullopt, o.mode},
                                         head + unit_display_name(state, u), u});
        }
    }
    return out;
}

std::string StratagemPicks::decline_label() const {
    if (reaction_window()) return "No stratagem";  // D-097
    return info_ && info_->timing == core::WindowTiming::BeforeRoll ? "Roll normally" : "Keep the roll";
}

std::string StratagemPicks::headline() const {
    if (!info_) return "";
    // D-097 (M96): the windows that follow no roll, by what they offer.
    if (offers(core::Stratagem::CrushingImpact)) return "Crushing Impact? (15.06)";  // D-099 (M97)
    if (offers(core::Stratagem::FireOverwatch)) return "Fire Overwatch? (15.08)";
    if (offers(core::Stratagem::HeroicIntervention)) return "Heroic Intervention? (15.11)";
    if (offers(core::Stratagem::Counteroffensive)) return "Counteroffensive? (15.12)";
    if (info_->timing == core::WindowTiming::BeforeRoll) return "Insane Bravery? (15.04)";
    return "Command Re-roll? (15.02)";
}

std::string StratagemPicks::hint() const {
    if (!info_) return "";
    const std::string cp = std::to_string(info_->command_points) + " CP";
    if (offers(core::Stratagem::CrushingImpact)) {
        return "You have " + cp + ". Crushing Impact: a model of your unit engaged with an enemy unit rolls a D6 per "
               "point of its T: each 5+ is a mortal wound on that enemy unit, each 1 one on your unit (at most 6 each).";
    }
    if (offers(core::Stratagem::FireOverwatch)) {
        return "You have " + cp + ". Fire Overwatch: one of your unengaged units shoots now at one visible enemy unit "
               "within 24\" (snap shooting: only a 6 hits, and it is not critical).";
    }
    if (offers(core::Stratagem::HeroicIntervention)) {
        return "You have " + cp + ". Heroic Intervention: one of your units charges now. Leap to Defend: only at an "
               "enemy unit that made a charge move this phase. Into the Fray (+1 CP): at any enemy unit within 6\", "
               "the roll at most 6.";
    }
    if (offers(core::Stratagem::Counteroffensive)) {
        return "You have " + cp + ". Counteroffensive: one of your units eligible to fight gains Fights First until "
               "the end of the phase and must be the next unit you select to fight.";
    }
    if (info_->timing == core::WindowTiming::BeforeRoll) {
        return "You have " + cp + ". Insane Bravery passes this battle-shock test with no dice rolled, once per battle.";
    }
    if (info_->whole_roll) return "You have " + cp + ". Command Re-roll re-rolls the whole charge roll.";
    return "You have " + cp + ". Click a die to pick the one to re-roll (a die is re-rolled only once).";
}

}  // namespace wh40k::client
