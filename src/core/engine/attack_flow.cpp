#include "core/engine/attack_flow.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/attacks.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core {

namespace detail {

namespace {

const AttackFlow& flow_of(const GameState& state) {
    if (!state.flow.attack) {
        throw std::logic_error("attack flow: no attack in progress");
    }
    return *state.flow.attack;
}

AttackFlow& flow_of(GameState& state) {
    if (!state.flow.attack) {
        throw std::logic_error("attack flow: no attack in progress");
    }
    return *state.flow.attack;
}

const Unit& unit_ref(const GameState& state, UnitId id) {
    const Unit* u = state.find_unit(id);
    if (u == nullptr) {
        throw std::logic_error("attack flow: unknown " + unit_label(id));
    }
    return *u;
}

Unit& unit_ref(GameState& state, UnitId id) {
    Unit* u = state.find_unit(id);
    if (u == nullptr) {
        throw std::logic_error("attack flow: unknown " + unit_label(id));
    }
    return *u;
}

// D-012: a batch whose target is no longer on the battlefield can't be resolved against it.
bool target_gone(const GameState& state, const AttackBatch& batch) {
    const Unit* u = state.find_unit(batch.target);
    return u == nullptr || is_destroyed(*u);
}

bool has_lost_batches(const GameState& state, const AttackFlow& f) {
    for (std::size_t i = 0; i < f.batches.size(); ++i) {
        if (!f.batch_done[i] && target_gone(state, f.batches[i])) {
            return true;
        }
    }
    return false;
}

// 04.03: the batches the attacker may resolve next. All batches against the current target are
// resolved before another enemy unit is selected.
std::vector<std::size_t> batch_options(const GameState& state, const AttackFlow& f) {
    std::vector<std::size_t> same_target;
    std::vector<std::size_t> all;
    for (std::size_t i = 0; i < f.batches.size(); ++i) {
        if (f.batch_done[i] || target_gone(state, f.batches[i])) {
            continue;
        }
        all.push_back(i);
        if (f.current_target && f.batches[i].target == *f.current_target) {
            same_target.push_back(i);
        }
    }
    return same_target.empty() ? all : same_target;
}

const BatchInProgress& current_batch(const AttackFlow& f) {
    if (!f.current) {
        throw std::logic_error("attack flow: no batch in progress");
    }
    return *f.current;
}

// D-013: does any 05.04 Select Model step have more than one legal model? inflict_damage calls its
// chooser only in that case, so run it on a copy of the target with a probe chooser.
bool casualty_choice_needed(const GameState& state, const AttackFlow& f) {
    const BatchInProgress& b = current_batch(f);
    const AttackBatch& batch = f.batches[b.batch];
    Unit copy = unit_ref(state, batch.target);
    bool asked = false;
    const ModelChooser probe = [&asked](const Unit&, std::span<const ModelId> options) {
        asked = true;
        return options.front();
    };
    (void)inflict_damage(copy, order_groups(b.groups, *b.order), *b.save_rolls, batch.profile, probe);
    return asked;
}

// 04.03 Gather Attack Dice + 05.01 + 05.02 for batch `index`.
void start_batch(GameState& state, DiceSource& dice, std::size_t index, Events& events) {
    AttackFlow& f = flow_of(state);
    const AttackBatch batch = f.batches[index];
    f.current_target = batch.target;  // 04.03 Select Enemy Unit
    events.push_back(event::AttackBatchSelected{f.attacker, index, batch.target, batch.attack_dice});

    const Unit& target = unit_ref(state, batch.target);
    const HitRolls hits = make_hit_rolls(dice, batch.profile, batch.attack_dice);  // 05.01
    events.push_back(event::HitRollsMade{index, hits});
    const WoundRolls wounds = make_wound_rolls(dice, batch.profile, hits.hits(), target_toughness(target));  // 05.02
    events.push_back(event::WoundRollsMade{index, wounds});

    if (wounds.wounds() == 0) {
        f.batch_done[index] = true;  // every attack failed: nothing to save
        return;
    }
    // 05.03 Create Groups now; the order and the save rolls follow.
    f.current = BatchInProgress{index, wounds.wounds(), create_allocation_groups(target), std::nullopt, std::nullopt};
}

void declare_order(GameState& state, std::vector<std::size_t> order, Events& events) {
    AttackFlow& f = flow_of(state);
    BatchInProgress& b = *f.current;
    b.order = std::move(order);
    events.push_back(event::AllocationOrderDeclared{f.batches[b.batch].target, order_groups(b.groups, *b.order)});
}

// 05.04 with the casualty priority (D-013), then the batch is done.
void resolve_casualties(GameState& state, const std::vector<ModelId>& priority, Events& events) {
    AttackFlow& f = flow_of(state);
    const BatchInProgress b = current_batch(f);
    const AttackBatch& batch = f.batches[b.batch];
    Unit& target = unit_ref(state, batch.target);

    const ModelChooser by_priority = [&priority](const Unit&, std::span<const ModelId> options) {
        for (const ModelId id : priority) {
            if (std::find(options.begin(), options.end(), id) != options.end()) {
                return id;
            }
        }
        throw std::logic_error("05.04: the casualty order has no legal model");
    };
    const InflictDamageResult result =
        inflict_damage(target, order_groups(b.groups, *b.order), *b.save_rolls, batch.profile, by_priority);
    events.push_back(event::SavesResolved{target.id, result});
    for (const SaveStep& step : result.steps) {
        if (step.damage && step.damage->destroyed) {
            events.push_back(event::ModelDestroyed{target.id, step.damage->model});
        }
    }
    if (is_destroyed(target)) {
        events.push_back(event::UnitDestroyed{target.id});
    }
    f.batch_done[b.batch] = true;
    f.current.reset();
}

void finish_attacks(GameState& state) {
    // TODO(24.15): [HAZARDOUS] hazard rolls after the unit's attacks (no V1 loadout has a [HAZARDOUS] weapon;
    // not in the M8 rule list).
    state.flow.attack.reset();
    state.flow.activation.reset();
}

std::vector<ModelId> model_ids(const Unit& unit) {
    std::vector<ModelId> out;
    for (const Model& m : unit.models) {
        out.push_back(m.id);
    }
    return out;
}

}  // namespace

std::optional<PendingDecision> attack_decision(const GameState& state) {
    const AttackFlow& f = flow_of(state);
    if (f.current) {
        const BatchInProgress& b = *f.current;
        const AttackBatch& batch = f.batches[b.batch];
        const Unit& target = unit_ref(state, batch.target);
        if (!b.order) {
            if (legal_allocation_orders(target, b.groups).size() > 1) {
                return PendingDecision{target.side, decision::DeclareAllocationOrder{batch.target}};  // 05.03
            }
            return std::nullopt;
        }
        if (!b.save_rolls) {
            return std::nullopt;
        }
        if (casualty_choice_needed(state, f)) {
            return PendingDecision{target.side, decision::OrderCasualties{batch.target}};  // 05.04, D-013
        }
        return std::nullopt;
    }
    if (has_lost_batches(state, f)) {
        return std::nullopt;  // D-012 drops them first
    }
    if (batch_options(state, f).size() > 1) {
        return PendingDecision{unit_ref(state, f.attacker).side, decision::SelectAttackBatch{f.attacker}};  // 04.03
    }
    return std::nullopt;
}

void attack_progress(GameState& state, DiceSource& dice, Events& events) {
    AttackFlow& f = flow_of(state);
    if (f.current) {
        BatchInProgress& b = *f.current;
        const AttackBatch& batch = f.batches[b.batch];
        if (!b.order) {
            // Only one legal allocation order: declared without asking.
            declare_order(state, legal_allocation_orders(unit_ref(state, batch.target), b.groups).front(), events);
            return;
        }
        if (!b.save_rolls) {
            b.save_rolls = make_save_rolls(dice, b.wounds);  // 05.03 Make Save Rolls
            events.push_back(event::SaveRollsMade{batch.target, *b.save_rolls});
            return;
        }
        // No Select Model step has a real choice (D-013): any priority gives the same result.
        resolve_casualties(state, model_ids(unit_ref(state, batch.target)), events);
        return;
    }

    // D-012: batches against a target that is no longer on the battlefield are lost.
    bool dropped = false;
    for (std::size_t i = 0; i < f.batches.size(); ++i) {
        if (!f.batch_done[i] && target_gone(state, f.batches[i])) {
            f.batch_done[i] = true;
            events.push_back(event::AttacksLost{f.batches[i].target, i, f.batches[i].attack_dice});
            dropped = true;
        }
    }
    if (dropped) {
        return;
    }

    const std::vector<std::size_t> options = batch_options(state, f);
    if (options.empty()) {
        finish_attacks(state);  // 04.03: all weapons have been used; the attacks have been resolved
        return;
    }
    start_batch(state, dice, options.front(), events);  // only one option (otherwise a decision)
}

// ---- 04.03 SelectAttackBatch ----

std::vector<GameAction> legal_for(const GameState& state, const decision::SelectAttackBatch&) {
    std::vector<GameAction> out;
    for (const std::size_t i : batch_options(state, flow_of(state))) {
        out.push_back(action::SelectAttackBatch{i});
    }
    return out;
}

ValidationResult validate_details(const GameState& state, Side, const action::SelectAttackBatch& a) {
    const AttackFlow& f = flow_of(state);
    if (a.batch >= f.batches.size()) {
        return ValidationResult::failure("04.03: there is no attack batch " + std::to_string(a.batch));
    }
    if (f.batch_done[a.batch]) {
        return ValidationResult::failure("04.03: attack batch " + std::to_string(a.batch) +
                                         " has already been used to make attacks");
    }
    const auto options = batch_options(state, f);
    if (std::find(options.begin(), options.end(), a.batch) == options.end()) {
        if (f.current_target) {
            return ValidationResult::failure("04.03: resolve the remaining attacks against " +
                                             unit_label(*f.current_target) + " before selecting another enemy unit");
        }
        return ValidationResult::failure("04.03: attack batch " + std::to_string(a.batch) +
                                         " cannot be selected now");
    }
    return ValidationResult::success();
}

void apply_details(GameState& state, Side, const action::SelectAttackBatch& a, DiceSource& dice, Events& events) {
    start_batch(state, dice, a.batch, events);
}

// ---- 05.03 DeclareAllocationOrder ----

std::vector<GameAction> legal_for(const GameState& state, const decision::DeclareAllocationOrder& d) {
    const AttackFlow& f = flow_of(state);
    const BatchInProgress& b = current_batch(f);
    std::vector<GameAction> out;
    for (auto& order : legal_allocation_orders(unit_ref(state, d.target), b.groups)) {
        out.push_back(action::DeclareAllocationOrder{d.target, std::move(order)});
    }
    return out;
}

ValidationResult validate_details(const GameState& state, Side, const action::DeclareAllocationOrder& a) {
    const auto& d = std::get<decision::DeclareAllocationOrder>(state.pending->kind);
    if (a.target != d.target) {
        return ValidationResult::failure("05.03: the allocation order being declared is for " + unit_label(d.target) +
                                         ", not " + unit_label(a.target));
    }
    const BatchInProgress& b = current_batch(flow_of(state));
    if (auto reason = check_allocation_order(unit_ref(state, d.target), b.groups, a.order)) {
        return ValidationResult::failure(*reason);
    }
    return ValidationResult::success();
}

void apply_details(GameState& state, Side, const action::DeclareAllocationOrder& a, DiceSource&, Events& events) {
    declare_order(state, a.order, events);
}

// ---- 05.04 OrderCasualties (D-013) ----

std::vector<GameAction> legal_for(const GameState&, const decision::OrderCasualties& d) {
    return {action::OrderCasualties{d.target, {}}};  // template: the priority is open
}

ValidationResult validate_details(const GameState& state, Side, const action::OrderCasualties& a) {
    const auto& d = std::get<decision::OrderCasualties>(state.pending->kind);
    if (a.target != d.target) {
        return ValidationResult::failure("05.04: casualties are being allocated in " + unit_label(d.target) +
                                         ", not " + unit_label(a.target));
    }
    if (a.priority.empty()) {
        return ValidationResult::failure("05.04: the casualty order is empty; list every model in " +
                                         unit_label(d.target) + " (D-013)");
    }
    std::vector<ModelId> expected = model_ids(unit_ref(state, d.target));
    std::vector<ModelId> given = a.priority;
    std::sort(expected.begin(), expected.end());
    std::sort(given.begin(), given.end());
    if (given != expected) {
        return ValidationResult::failure("05.04: the casualty order must list every model in " +
                                         unit_label(d.target) + " exactly once (D-013)");
    }
    return ValidationResult::success();
}

void apply_details(GameState& state, Side, const action::OrderCasualties& a, DiceSource&, Events& events) {
    if (state.flow.attack) {
        resolve_casualties(state, a.priority, events);  // 05.04
    } else {
        resolve_move_mortal_wounds(state, a.priority, events);  // 06.02 (Desperate Escape hazards)
    }
}

namespace {

void check_begin_preconditions(const GameState& state, UnitId attacker, const AttackContext& context,
                               const std::vector<WeaponSelection>& selections) {
    const bool fighting = context.type == AttackType::Fighting;
    const Stage expected = fighting ? Stage::Fight : Stage::Shoot;
    // 12.08: a unit forced to fight by an Engaging Consolidation fights in the Consolidate step.
    if (state.flow.stage != expected && !(fighting && state.flow.stage == Stage::Consolidate)) {
        throw std::logic_error("begin_attacks: attacks are made in the " + std::string(stage_name(expected)) +
                               " step");
    }
    const bool selected_to_fight = fighting && state.flow.activation &&
                                   state.flow.activation->unit == attacker &&
                                   state.flow.activation->kind == ActivationKind::Fight;
    if (state.flow.attack || (state.flow.activation && !selected_to_fight)) {
        throw std::logic_error("begin_attacks: another unit is already selected");
    }
    const Unit* unit = state.find_unit(attacker);
    if (unit == nullptr || is_destroyed(*unit)) {
        throw std::logic_error("begin_attacks: the attacking unit is not on the battlefield");
    }
    if (context.type == AttackType::Shooting && unit->side != turn_side(state)) {
        throw std::logic_error("begin_attacks: 10.02 the active player shoots with their own units");
    }
    for (const WeaponSelection& s : selections) {
        if (find_model(*unit, s.attacker) == nullptr) {
            throw std::logic_error("begin_attacks: model " + std::to_string(s.attacker.v) +
                                   " is not in the attacking unit");
        }
        const Unit* target = state.find_unit(s.target);
        if (target == nullptr || target->side == unit->side) {
            throw std::logic_error("begin_attacks: the target of every weapon must be an enemy unit");
        }
    }
}

}  // namespace

void start_attacks(GameState& state, UnitId attacker, const AttackContext& context,
                   std::vector<WeaponSelection> selections, Events& events) {
    check_begin_preconditions(state, attacker, context, selections);
    AttackFlow f;
    f.attacker = attacker;
    f.context = context;
    f.batches = gather_attack_batches(state, selections, context);  // 04.03, D-006
    f.batch_done.assign(f.batches.size(), false);
    f.selections = std::move(selections);
    events.push_back(event::AttackBatchesGathered{attacker, f.batches});

    // 01.03: the attacking unit's controlling player is active until its attacks are resolved.
    state.flow.activation =
        Activation{attacker, context.type == AttackType::Shooting ? ActivationKind::Shoot : ActivationKind::Fight};
    state.flow.attack = std::move(f);
}

}  // namespace detail

std::vector<GameEvent> begin_attacks(GameState& state, DiceSource& dice, UnitId attacker,
                                     const AttackContext& context, std::vector<WeaponSelection> selections) {
    detail::Events events;
    state.pending.reset();
    detail::start_attacks(state, attacker, context, std::move(selections), events);
    detail::advance(state, dice, events);
    return events;
}

std::vector<GameEvent> begin_attacks(GameState& state, UnitId attacker, const AttackContext& context,
                                     std::vector<WeaponSelection> selections) {
    SeededDice dice(state.dice);
    return begin_attacks(state, dice, attacker, context, std::move(selections));
}

}  // namespace wh40k::core
