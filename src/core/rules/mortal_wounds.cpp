#include "core/rules/mortal_wounds.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

#include "core/model/keywords.hpp"

namespace wh40k::core {

std::vector<ModelId> mortal_wound_candidates(const Unit& unit) {
    std::vector<ModelId> wounded_other;      // tier 1
    std::vector<ModelId> other;              // tier 2: all non-CHARACTER models
    std::vector<ModelId> wounded_character;  // tier 3
    std::vector<ModelId> character;          // tier 4: all CHARACTER models
    for (const Model& m : unit.models) {
        if (is_character(unit, m)) {
            character.push_back(m.id);
            if (is_wounded(m)) wounded_character.push_back(m.id);
        } else {
            other.push_back(m.id);
            if (is_wounded(m)) wounded_other.push_back(m.id);
        }
    }
    if (!wounded_other.empty()) return wounded_other;
    if (!other.empty()) return other;
    if (!wounded_character.empty()) return wounded_character;
    return character;
}

MortalWoundsResult inflict_mortal_wounds(Unit& unit, int count, const ModelChooser& choose, bool ask_when_forced) {
    MortalWoundsResult out;
    for (int i = 0; i < count; ++i) {
        const std::vector<ModelId> options = mortal_wound_candidates(unit);
        if (options.empty()) {
            out.not_inflicted = count - i;  // the unit is destroyed
            break;
        }
        ModelId chosen = options.front();
        if (options.size() > 1 || ask_when_forced) {
            chosen = choose(unit, options);
            if (std::find(options.begin(), options.end(), chosen) == options.end()) {
                throw std::logic_error("06.02: selected model " + std::to_string(chosen.v) +
                                       " is not a legal choice");
            }
        }
        const DamageResult d = resolve_damage(unit, chosen, 1);  // 06.02 Resolve Damage: loses 1 wound
        out.models_destroyed += d.destroyed ? 1 : 0;
        out.steps.push_back(d);
    }
    return out;
}

HazardRolls make_hazard_rolls(DiceSource& dice, const Unit& unit, int count) {
    HazardRolls out;
    bool all_monster_vehicle = !unit.models.empty();
    for (const Model& m : unit.models) {
        all_monster_vehicle = all_monster_vehicle && is_monster_or_vehicle(unit, m);
    }
    const int per_failure = all_monster_vehicle ? 3 : 1;
    for (int i = 0; i < count; ++i) {
        const int r = dice.roll_d6();
        out.rolls.push_back(r);
        if (r <= 2) {
            ++out.failures;
            out.mortal_wounds += per_failure;
        }
    }
    return out;
}

}  // namespace wh40k::core
