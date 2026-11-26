#include "core/rules/stratagem_effects.hpp"

#include <algorithm>

#include "core/geometry/distance.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core {

namespace {

std::string unit_text(UnitId id) { return "unit " + std::to_string(id.v); }
std::string model_text(ModelId id) { return "model " + std::to_string(id.v); }

const Model* model_in(const Unit& unit, ModelId id) {
    const auto it = std::find_if(unit.models.begin(), unit.models.end(), [id](const Model& m) { return m.id == id; });
    return it == unit.models.end() ? nullptr : &*it;
}

// An enemy unit of `unit`'s on the battlefield, or nullptr.
const Unit* enemy_on_battlefield(const GameState& state, const Unit& unit, UnitId enemy) {
    const Unit* e = state.find_unit(enemy);
    return e != nullptr && e->side != unit.side && !is_destroyed(*e) ? e : nullptr;
}

bool within_explosives_range(const Model& model, const Unit& enemy) {
    const std::optional<float> d = model_distance_to_unit(pose_of(model), enemy);
    return d.has_value() && within(*d, inches_to_mm(kExplosivesRangeInches));  // 01.04 edge to edge, D-009
}

bool engaged_with(const GameState& state, const Unit& unit, const Model& model, UnitId enemy) {
    const std::vector<UnitId> engaged = enemy_units_engaged_with_model(state, unit, pose_of(model));
    return std::find(engaged.begin(), engaged.end(), enemy) != engaged.end();
}

}  // namespace

std::optional<std::string> explosives_pair_problem(const GameState& state, const Unit& unit, ModelId model,
                                                   UnitId enemy) {
    const Model* m = model_in(unit, model);
    if (m == nullptr) return "15.05: " + model_text(model) + " is not in " + unit_text(unit.id);
    if (!is_explosives_or_grenades(unit, *m)) {
        return "15.05: " + model_text(model) + " is not an EXPLOSIVES/GRENADES model";
    }
    const Unit* e = enemy_on_battlefield(state, unit, enemy);
    if (e == nullptr) return "15.05: " + unit_text(enemy) + " is not an enemy unit on the battlefield";
    if (is_engaged(state, *e)) {
        return "15.05: " + unit_text(enemy) + " is engaged; Explosives selects an unengaged enemy unit";
    }
    if (!within_explosives_range(*m, *e)) {
        return "15.05: " + unit_text(enemy) + " is not within 8\" of " + model_text(model);
    }
    if (!unit_visible_to_model(state, *m, *e)) {
        return "15.05: " + unit_text(enemy) + " is not visible to " + model_text(model) + " (06.01)";
    }
    return std::nullopt;
}

std::vector<EffectPair> explosives_pairs(const GameState& state, const Unit& unit, bool first_only) {
    std::vector<EffectPair> out;
    // The enemy units step 2 can select from at all (unengaged), once.
    std::vector<const Unit*> enemies;
    for (const Unit& e : state.units) {
        if (e.side != unit.side && !is_destroyed(e) && !is_engaged(state, e)) enemies.push_back(&e);
    }
    if (enemies.empty()) return out;
    for (const Model& m : unit.models) {
        if (!is_explosives_or_grenades(unit, m)) continue;
        for (const Unit* e : enemies) {
            // The distance first: a visibility check costs more.
            if (!within_explosives_range(m, *e) || !unit_visible_to_model(state, m, *e)) continue;
            out.push_back(EffectPair{m.id, e->id});
            if (first_only) return out;
        }
    }
    return out;
}

std::optional<std::string> crushing_impact_pair_problem(const GameState& state, const Unit& unit, ModelId model,
                                                        UnitId enemy) {
    const Model* m = model_in(unit, model);
    if (m == nullptr) return "15.06: " + model_text(model) + " is not in " + unit_text(unit.id);
    if (enemy_on_battlefield(state, unit, enemy) == nullptr) {
        return "15.06: " + unit_text(enemy) + " is not an enemy unit on the battlefield";
    }
    const std::vector<UnitId> engaged = engaged_enemy_units(state, unit);
    if (std::find(engaged.begin(), engaged.end(), enemy) == engaged.end()) {
        return "15.06: " + unit_text(enemy) + " is not engaged with " + unit_text(unit.id);
    }
    if (!engaged_with(state, unit, *m, enemy)) {
        return "15.06: " + model_text(model) + " is not engaged with " + unit_text(enemy) + " (03.04)";
    }
    return std::nullopt;
}

std::vector<EffectPair> crushing_impact_pairs(const GameState& state, const Unit& unit, bool first_only) {
    std::vector<EffectPair> out;
    const std::vector<UnitId> engaged = engaged_enemy_units(state, unit);  // step 1, in GameState order
    if (engaged.empty()) return out;
    for (const Model& m : unit.models) {
        const std::vector<UnitId> with_model = enemy_units_engaged_with_model(state, unit, pose_of(m));
        for (const UnitId e : engaged) {
            if (std::find(with_model.begin(), with_model.end(), e) == with_model.end()) continue;
            out.push_back(EffectPair{m.id, e});
            if (first_only) return out;
        }
    }
    return out;
}

std::vector<EffectPair> effect_pairs(const GameState& state, Stratagem s, const Unit& unit, bool first_only) {
    if (s == Stratagem::Explosives) return explosives_pairs(state, unit, first_only);
    if (s == Stratagem::CrushingImpact) return crushing_impact_pairs(state, unit, first_only);
    return {};
}

std::optional<std::string> effect_pair_problem(const GameState& state, Stratagem s, const Unit& unit, ModelId model,
                                               UnitId enemy) {
    if (s == Stratagem::Explosives) return explosives_pair_problem(state, unit, model, enemy);
    if (s == Stratagem::CrushingImpact) return crushing_impact_pair_problem(state, unit, model, enemy);
    return std::string(stratagem_rule(s)) + ": " + std::string(stratagem_name(s)) +
           " selects no model and enemy unit";
}

EffectMortalWounds judge_explosives(std::span<const int> dice) {
    EffectMortalWounds out;
    out.enemy = static_cast<int>(std::count_if(dice.begin(), dice.end(), [](int d) { return d >= kExplosivesSuccess; }));
    return out;
}

EffectMortalWounds judge_crushing_impact(std::span<const int> dice) {
    EffectMortalWounds out;
    const int fives = static_cast<int>(
        std::count_if(dice.begin(), dice.end(), [](int d) { return d >= kCrushingImpactSuccess; }));
    const int ones = static_cast<int>(
        std::count_if(dice.begin(), dice.end(), [](int d) { return d == kCrushingImpactFailure; }));
    out.enemy = std::min(fives, kCrushingImpactCap);  // "to a maximum of 6 mortal wounds per unit" (D-099)
    out.unit = std::min(ones, kCrushingImpactCap);
    return out;
}

EffectMortalWounds judge_effect_roll(Stratagem s, std::span<const int> dice) {
    if (s == Stratagem::Explosives) return judge_explosives(dice);
    if (s == Stratagem::CrushingImpact) return judge_crushing_impact(dice);
    return {};
}

int effect_dice(Stratagem s, const Model& model) {
    if (s == Stratagem::Explosives) return kExplosivesDice;
    if (s == Stratagem::CrushingImpact) return model.characteristics.toughness;  // 15.06: T D6
    return 0;
}

}  // namespace wh40k::core
