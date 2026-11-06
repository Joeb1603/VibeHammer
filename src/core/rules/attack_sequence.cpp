#include "core/rules/attack_sequence.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <variant>

#include "core/dice/modifiers.hpp"
#include "core/model/keywords.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core {

namespace {

// Known core abilities whose effects (re-rolls, critical-wound handling) are not implemented yet.
// Refuse rather than resolve them wrongly. Unknown{text} abilities are display-only by design (M2).
void reject_unimplemented(const AttackProfile& profile) {
    for (const WeaponAbility& a : profile.attack_time_abilities) {
        if (std::holds_alternative<ability::TwinLinked>(a)) {
            throw std::logic_error("24.38 [TWIN-LINKED] is not implemented (needs Re-rolls, D-011)");
        }
        if (std::holds_alternative<ability::DevastatingWounds>(a)) {
            throw std::logic_error("24.10 [DEVASTATING WOUNDS] is not implemented");
        }
    }
}

bool contains(std::span<const ModelId> ids, ModelId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

}  // namespace

// ---- 05.01 ----

int HitRolls::hits() const {
    return static_cast<int>(std::count_if(rolls.begin(), rolls.end(), [](const HitRoll& r) {
        return r.outcome != HitOutcome::Fail;
    }));
}

int HitRolls::critical_hits() const {
    return static_cast<int>(std::count_if(rolls.begin(), rolls.end(), [](const HitRoll& r) {
        return r.outcome == HitOutcome::CriticalHit;
    }));
}

HitRolls make_hit_rolls(DiceSource& dice, const AttackProfile& profile, int attack_dice) {
    reject_unimplemented(profile);
    HitRolls out;
    const int modifier = cap_hit_wound_modifier(profile.hit_modifier);  // D-011
    for (int i = 0; i < attack_dice; ++i) {
        HitRoll r;
        if (profile.torrent) {
            r.outcome = HitOutcome::AutoHit;  // 24.37: automatically hits; no hit roll
            out.rolls.push_back(r);
            continue;
        }
        const int u = dice.roll_d6();
        const int m = modified_result(u, modifier);
        r.unmodified = u;
        r.modified = m;
        if (u == 1) {
            r.outcome = HitOutcome::Fail;  // unmodified 1
        } else if (u == 6) {
            r.outcome = HitOutcome::CriticalHit;  // unmodified 6
        } else if (m >= profile.skill.value()) {
            r.outcome = HitOutcome::Hit;
        } else {
            r.outcome = HitOutcome::Fail;
        }
        out.rolls.push_back(r);
    }
    return out;
}

// ---- 05.02 ----

int required_wound_roll(int strength, int toughness) {
    if (strength >= 2 * toughness) return 2;  // twice (or more than twice)
    if (strength > toughness) return 3;
    if (strength == toughness) return 4;
    if (2 * strength <= toughness) return 6;  // half (or less than half)
    return 5;                                 // less than
}

int target_toughness(const Unit& target) {
    if (target.models.empty()) {
        throw std::logic_error("05.02: target unit '" + target.name + "' has no models");
    }
    const int t = target.models.front().characteristics.toughness;
    for (const Model& m : target.models) {
        if (m.characteristics.toughness != t) {
            throw std::logic_error("05.02: units with mixed Toughness are out of scope in V1");
        }
    }
    return t;
}

int WoundRolls::wounds() const {
    return static_cast<int>(std::count_if(rolls.begin(), rolls.end(), [](const WoundRoll& r) {
        return r.outcome != WoundOutcome::Fail;
    }));
}

int WoundRolls::critical_wounds() const {
    return static_cast<int>(std::count_if(rolls.begin(), rolls.end(), [](const WoundRoll& r) {
        return r.outcome == WoundOutcome::CriticalWound;
    }));
}

WoundRolls make_wound_rolls(DiceSource& dice, const AttackProfile& profile, int hits, int toughness) {
    reject_unimplemented(profile);
    WoundRolls out;
    out.required = required_wound_roll(profile.strength, toughness);
    const int modifier = cap_hit_wound_modifier(profile.wound_modifier);  // D-011
    for (int i = 0; i < hits; ++i) {
        WoundRoll r;
        r.unmodified = dice.roll_d6();
        r.modified = modified_result(r.unmodified, modifier);
        if (r.unmodified == 1) {
            r.outcome = WoundOutcome::Fail;
        } else if (r.unmodified == 6) {
            r.outcome = WoundOutcome::CriticalWound;
        } else if (r.modified >= out.required) {
            r.outcome = WoundOutcome::Wound;
        } else {
            r.outcome = WoundOutcome::Fail;
        }
        out.rolls.push_back(r);
    }
    return out;
}

// ---- 05.03 ----

std::vector<AllocationGroup> create_allocation_groups(const Unit& target) {
    std::vector<AllocationGroup> groups;
    for (const Model& m : target.models) {
        const Characteristics& c = m.characteristics;
        if (is_character(target, m)) {
            groups.push_back(AllocationGroup{{m.id}, true, c.wounds, c.save, c.invuln_save});
            continue;
        }
        auto it = std::find_if(groups.begin(), groups.end(), [&](const AllocationGroup& g) {
            return !g.character && g.wounds == c.wounds && g.save == c.save && g.invuln_save == c.invuln_save;
        });
        if (it == groups.end()) {
            groups.push_back(AllocationGroup{{m.id}, false, c.wounds, c.save, c.invuln_save});
        } else {
            it->models.push_back(m.id);
        }
    }
    return groups;
}

namespace {

bool group_has_wounded_model(const Unit& target, const AllocationGroup& g) {
    for (const ModelId id : g.models) {
        const Model* m = find_model(target, id);
        if (m != nullptr && is_wounded(*m)) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::optional<std::string> check_allocation_order(const Unit& target, std::span<const AllocationGroup> groups,
                                                  std::span<const std::size_t> order) {
    // The order must declare every group exactly once.
    std::vector<std::size_t> sorted(order.begin(), order.end());
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (sorted[i] != i) {
            return "05.03: the allocation order must list every group exactly once";
        }
    }
    if (sorted.size() != groups.size()) {
        return "05.03: the allocation order must list every group exactly once";
    }

    // A non-CHARACTER group containing a wounded model must be first.
    bool any_wounded_non_character = false;
    for (const AllocationGroup& g : groups) {
        any_wounded_non_character |= !g.character && group_has_wounded_model(target, g);
    }
    if (any_wounded_non_character) {
        const AllocationGroup& first = groups[order.front()];
        if (first.character || !group_has_wounded_model(target, first)) {
            return "05.03: a non-CHARACTER group containing a model that has lost wounds must be first";
        }
    }

    // No CHARACTER group earlier than a non-CHARACTER group; wounded CHARACTER groups before unwounded
    // ones.
    bool seen_character = false;
    bool seen_unwounded_character = false;
    for (const std::size_t gi : order) {
        const AllocationGroup& g = groups[gi];
        if (!g.character) {
            if (seen_character) {
                return "05.03: no CHARACTER group can be earlier than a non-CHARACTER group";
            }
            continue;
        }
        seen_character = true;
        const bool wounded = group_has_wounded_model(target, g);
        if (wounded && seen_unwounded_character) {
            return "05.03: CHARACTER groups with a wounded model must be earlier than those without";
        }
        seen_unwounded_character |= !wounded;
    }
    return std::nullopt;
}

std::vector<std::vector<std::size_t>> legal_allocation_orders(const Unit& target,
                                                              std::span<const AllocationGroup> groups) {
    std::vector<std::size_t> order(groups.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::vector<std::vector<std::size_t>> out;
    do {
        if (!check_allocation_order(target, groups, order)) {
            out.push_back(order);
        }
    } while (std::next_permutation(order.begin(), order.end()));
    return out;
}

std::vector<AllocationGroup> order_groups(std::span<const AllocationGroup> groups,
                                          std::span<const std::size_t> order) {
    std::vector<AllocationGroup> out;
    out.reserve(order.size());
    for (const std::size_t i : order) {
        out.push_back(groups[i]);
    }
    return out;
}

std::vector<int> make_save_rolls(DiceSource& dice, int wounds) {
    std::vector<int> rolls;
    for (int i = 0; i < wounds; ++i) {
        rolls.push_back(dice.roll_d6());
    }
    return rolls;
}

// ---- 05.04 ----

SaveCheck check_save_roll(int unmodified, const AllocationGroup& group, int armour_penetration) {
    SaveCheck c;
    c.unmodified = unmodified;
    c.modified = modified_result(unmodified, armour_penetration);  // D-011: AP uncapped, floor 1
    if (unmodified == 1) {
        c.outcome = SaveOutcome::InflictsDamage;
    } else if (group.invuln_save && unmodified >= *group.invuln_save) {
        c.invulnerable_save = true;  // InSv is not modified by AP
        c.outcome = SaveOutcome::AttackFails;
    } else if (group.save && c.modified >= *group.save) {
        c.outcome = SaveOutcome::AttackFails;
    } else {
        c.outcome = SaveOutcome::InflictsDamage;
    }
    return c;
}

std::optional<std::size_t> current_allocation_group(const Unit& target,
                                                    std::span<const AllocationGroup> ordered) {
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        for (const ModelId id : ordered[i].models) {
            if (find_model(target, id) != nullptr) {
                return i;
            }
        }
    }
    return std::nullopt;
}

std::vector<ModelId> selectable_models(const Unit& target, const AllocationGroup& group) {
    std::vector<ModelId> alive;
    std::vector<ModelId> wounded;
    for (const ModelId id : group.models) {
        if (const Model* m = find_model(target, id)) {
            alive.push_back(id);
            if (is_wounded(*m)) {
                wounded.push_back(id);
            }
        }
    }
    // "this must be a model that has lost one or more wounds if possible"
    return wounded.empty() ? alive : wounded;
}

DamageResult resolve_damage(Unit& target, ModelId model, int damage) {
    Model* m = find_model(target, model);
    if (m == nullptr) {
        throw std::logic_error("resolve_damage: model " + std::to_string(model.v) + " is not in unit '" +
                               target.name + "'");
    }
    DamageResult r;
    r.model = model;
    r.damage = damage;
    r.wounds_lost = std::min(damage, m->wounds_remaining);
    m->wounds_remaining -= damage;
    if (m->wounds_remaining <= 0) {
        r.destroyed = true;
        // Appendix, Destroyed: removed from the battlefield. Excess damage is lost with it.
        std::erase_if(target.models, [&](const Model& x) { return x.id == model; });
    }
    return r;
}

InflictDamageResult inflict_damage(Unit& target, std::span<const AllocationGroup> ordered_groups,
                                   std::span<const int> save_rolls, const AttackProfile& profile,
                                   const ModelChooser& choose_model, bool ask_when_forced) {
    InflictDamageResult out;
    std::vector<int> rolls(save_rolls.begin(), save_rolls.end());
    std::sort(rolls.begin(), rolls.end());  // lowest result(s) to highest result(s)
    const int damage = profile.damage.fixed_value();  // D-002

    for (const int roll : rolls) {
        SaveStep step;
        const std::optional<std::size_t> gi = current_allocation_group(target, ordered_groups);
        if (!gi) {
            ++out.attacks_lost;  // all models in the target unit are destroyed: excess attacks are lost
            out.steps.push_back(step);
            continue;
        }
        step.group = gi;
        const AllocationGroup& group = ordered_groups[*gi];
        step.check = check_save_roll(roll, group, profile.armour_penetration);
        if (step.check->outcome == SaveOutcome::InflictsDamage) {
            const std::vector<ModelId> options = selectable_models(target, group);
            ModelId chosen = options.front();
            if (options.size() > 1 || ask_when_forced) {
                chosen = choose_model(target, options);
                if (!contains(options, chosen)) {
                    throw std::logic_error("05.04: selected model " + std::to_string(chosen.v) +
                                           " is not a legal choice");
                }
            }
            step.damage = resolve_damage(target, chosen, damage);
            out.models_destroyed += step.damage->destroyed ? 1 : 0;
        }
        out.steps.push_back(step);
    }
    return out;
}

BatchResult resolve_attack_batch(DiceSource& dice, Unit& target, const AttackBatch& batch,
                                 const AllocationOrderChooser& choose_order, const ModelChooser& choose_model) {
    if (target.models.empty()) {
        throw std::logic_error("resolve_attack_batch: target unit '" + target.name +
                               "' is destroyed (Target No Longer Eligible or Viable is not implemented)");
    }
    BatchResult r;
    r.hits = make_hit_rolls(dice, batch.profile, batch.attack_dice);                           // 05.01
    r.wounds = make_wound_rolls(dice, batch.profile, r.hits.hits(), target_toughness(target));  // 05.02
    const int wounds = r.wounds.wounds();
    if (wounds == 0) {
        return r;
    }

    // 05.03: the opposing player creates groups, declares the order, then makes the save rolls.
    const std::vector<AllocationGroup> groups = create_allocation_groups(target);
    const auto legal = legal_allocation_orders(target, groups);
    if (legal.empty()) {
        throw std::logic_error("05.03: no legal allocation order");
    }
    std::vector<std::size_t> order = legal.front();
    if (legal.size() > 1) {
        order = choose_order(target, groups, legal);
        if (const auto reason = check_allocation_order(target, groups, order)) {
            throw std::logic_error(*reason);
        }
    }
    r.allocation_order = order_groups(groups, order);
    r.save_rolls = make_save_rolls(dice, wounds);

    // 05.04
    r.damage = inflict_damage(target, r.allocation_order, r.save_rolls, batch.profile, choose_model);
    return r;
}

}  // namespace wh40k::core
