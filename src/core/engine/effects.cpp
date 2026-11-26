// D-099 (M97): the EFFECT of 15.05 Explosives and 15.06 Crushing Impact, once the stratagem is used and its CP are paid
// (15.01 step 3). It runs as FlowState::effect, each step a decision:
//   1-2. SelectModelAndEnemy: a model of the unit and an enemy unit, together (core/rules/stratagem_effects.hpp);
//   3.   RollDice{Explosives} (six D6) or RollDice{CrushingImpact} (D6 per the model's T), the unit's player's; no
//        window follows (15.02 lists neither roll);
//   then the enemy unit's casualties (06.02), its player's OrderCasualties, and Crushing Impact's 1s on the unit, its
//   player's OrderCasualties, only while the enemy's army still has models on the battlefield (D-099 section 6: the
//   enemy's mortal wounds first; if they leave it no models, the game ends at once, D-007).

#include <algorithm>
#include <stdexcept>
#include <string>

#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/stratagem_effects.hpp"

namespace wh40k::core::detail {

namespace {

StratagemEffect& effect_of(GameState& state) {
    if (!state.flow.effect) throw std::logic_error("D-099: no stratagem effect is in progress");
    return *state.flow.effect;
}

const StratagemEffect& effect_of(const GameState& state) {
    if (!state.flow.effect) throw std::logic_error("D-099: no stratagem effect is in progress");
    return *state.flow.effect;
}

RollKind roll_of(Stratagem s) {
    if (s == Stratagem::Explosives) return RollKind::Explosives;
    if (s == Stratagem::CrushingImpact) return RollKind::CrushingImpact;
    throw std::logic_error("D-099: " + std::string(stratagem_name(s)) + " has no effect roll");
}

bool side_has_models(const GameState& state, Side side) {
    return std::any_of(state.units.begin(), state.units.end(),
                       [side](const Unit& u) { return u.side == side && !is_destroyed(u); });
}

const Model& selected_model(const GameState& state, const StratagemEffect& e) {
    const Unit& u = *state.find_unit(e.unit);
    const auto it = std::find_if(u.models.begin(), u.models.end(), [&e](const Model& m) { return m.id == e.model; });
    if (it == u.models.end()) throw std::logic_error("D-099: the effect's model is not in its unit");
    return *it;
}

}  // namespace

void start_effect(GameState& state, Stratagem stratagem, UnitId unit) {
    state.flow.effect = StratagemEffect{stratagem, unit, std::nullopt, std::nullopt, false, 0, 0};
}

std::optional<PendingDecision> effect_decision(const GameState& state) {
    const StratagemEffect& e = effect_of(state);
    const Unit& u = *state.find_unit(e.unit);
    if (!e.model || !e.enemy) {
        // Steps 1-2 (D-099 section 5.1). The stratagem is used only when a pair exists (D-097's ruling).
        return PendingDecision{u.side, decision::SelectModelAndEnemy{e.stratagem, e.unit}};
    }
    if (!e.rolled) return PendingDecision{u.side, decision::RollDice{roll_of(e.stratagem), e.unit}};  // step 3
    if (e.enemy_mortal_wounds > 0) {
        // 06.02: the enemy unit's controlling player selects the models (D-013), always asked (D-051).
        const Unit* enemy = state.find_unit(*e.enemy);
        if (enemy != nullptr && !is_destroyed(*enemy)) {
            return PendingDecision{enemy->side, decision::OrderCasualties{enemy->id}};
        }
        return std::nullopt;
    }
    if (e.unit_mortal_wounds > 0 && !is_destroyed(u) && side_has_models(state, opponent(u.side))) {
        // 15.06's 1s, after the enemy's mortal wounds, only while the enemy still has models (D-099 section 6).
        return PendingDecision{u.side, decision::OrderCasualties{u.id}};
    }
    return std::nullopt;
}

void effect_progress(GameState& state, Events&) {
    // Every decision of the effect is answered (or the 1s lapse: D-099 section 6). Play returns to the Shoot or Charge
    // decision, D-007 checked first (advance).
    state.flow.effect.reset();
}

void make_effect_roll(GameState& state, DiceSource& dice, Events& events) {
    StratagemEffect& e = effect_of(state);
    const Model& m = selected_model(state, e);
    const std::vector<int> faces = roll_d6s(dice, effect_dice(e.stratagem, m));
    const EffectMortalWounds mw = judge_effect_roll(e.stratagem, faces);
    e.rolled = true;
    e.enemy_mortal_wounds = mw.enemy;
    e.unit_mortal_wounds = mw.unit;
    events.push_back(event::StratagemDiceRolled{e.stratagem, e.unit, m.id, *e.enemy, faces, mw.enemy, mw.unit});
}

void describe_effect_roll(const GameState& state, RollDiceInfo& info) {
    const StratagemEffect& e = effect_of(state);
    info.dice = effect_dice(e.stratagem, selected_model(state, e));
    info.target = e.enemy;
    info.model = e.model;
    info.need = e.stratagem == Stratagem::Explosives ? kExplosivesSuccess : kCrushingImpactSuccess;
}

void resolve_effect_casualties(GameState& state, UnitId target, const std::vector<ModelId>& priority, Events& events) {
    StratagemEffect& e = effect_of(state);
    int count = 0;
    if (e.enemy && target == *e.enemy && e.enemy_mortal_wounds > 0) {
        count = e.enemy_mortal_wounds;
        e.enemy_mortal_wounds = 0;
    } else if (target == e.unit && e.unit_mortal_wounds > 0) {
        count = e.unit_mortal_wounds;
        e.unit_mortal_wounds = 0;
    } else {
        throw std::logic_error("D-099: no mortal wounds are owed by " + unit_label(target));
    }
    inflict_mortal_wounds_by_priority(state, *state.find_unit(target), count, priority, events);  // 06.02
}

// ---- 15.05 / 15.06 steps 1-2: SelectModelAndEnemy ----

std::vector<GameAction> legal_for(const GameState& state, const decision::SelectModelAndEnemy& d) {
    std::vector<GameAction> out;
    for (const EffectPair& p : effect_pairs(state, d.stratagem, *state.find_unit(d.unit))) {
        out.push_back(action::SelectModelAndEnemy{p.model, p.enemy});
    }
    return out;
}

ValidationResult validate_details(const GameState& state, Side, const action::SelectModelAndEnemy& a) {
    const StratagemEffect& e = effect_of(state);
    if (auto p = effect_pair_problem(state, e.stratagem, *state.find_unit(e.unit), a.model, a.enemy)) {
        return ValidationResult::failure(std::move(*p));
    }
    return ValidationResult::success();
}

void apply_details(GameState& state, Side, const action::SelectModelAndEnemy& a, DiceSource&, Events& events) {
    StratagemEffect& e = effect_of(state);
    e.model = a.model;
    e.enemy = a.enemy;
    events.push_back(event::ModelAndEnemySelected{e.stratagem, e.unit, a.model, a.enemy});
}

}  // namespace wh40k::core::detail
