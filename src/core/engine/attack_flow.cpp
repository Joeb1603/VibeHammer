#include "core/engine/attack_flow.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/mortal_wounds.hpp"
#include "core/rules/random_characteristics.hpp"
#include "core/rules/shooting.hpp"
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

// 04.03.03 (D-093, M92; was D-012): a batch whose target is no longer on the battlefield can't be resolved against it.
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

// 04.03.03 (D-093): the selections in batches whose target is gone, in selection order.
std::vector<std::size_t> lost_selections(const GameState& state, const AttackFlow& f) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < f.batches.size(); ++i) {
        if (f.batch_done[i] || !target_gone(state, f.batches[i])) continue;
        out.insert(out.end(), f.batches[i].selections.begin(), f.batches[i].selections.end());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// 04.03.03 (D-093): the eligible new targets for selection `index`, in unit order. Shooting: every enemy unit the
// model could select with that weapon and profile now, with the unit's shooting type (04.02, 06.01, 10.04-10.06,
// 17.03, 24.07: shot_problems). Fighting: the enemy units the model is engaged with (04.02 While Fighting).
std::vector<UnitId> retarget_options(const GameState& state, const AttackFlow& f, std::size_t index) {
    std::vector<UnitId> out;
    const WeaponSelection& sel = f.selections[index];
    const Unit& unit = unit_ref(state, f.attacker);
    const Model* model = find_model(unit, sel.attacker);
    if (model == nullptr) return out;
    if (f.context.type == AttackType::Fighting) {
        for (const UnitId id : enemy_units_engaged_with_model(state, unit, *model, model->position)) {
            const Unit* t = state.find_unit(id);
            if (t != nullptr && !is_destroyed(*t)) out.push_back(id);
        }
        return out;
    }
    for (const Unit& t : state.units) {
        if (t.side == unit.side || is_destroyed(t)) continue;
        const ShotDeclaration shot{sel.attacker, sel.weapon_index, sel.profile_index, t.id};
        if (shot_problems(state, unit, f.shooting_type, std::span<const ShotDeclaration>(&shot, 1)).empty()) {
            out.push_back(t.id);
        }
    }
    return out;
}

bool any_retarget_option(const GameState& state, const AttackFlow& f) {
    for (const std::size_t i : lost_selections(state, f)) {
        if (!retarget_options(state, f, i).empty()) return true;
    }
    return false;
}

// 04.03.03 (D-093): `old` with its new target, the Select Targets facts recorded now (24.05, 24.30, 10.06, 17.03,
// 13.08), as a fresh selection would record them. A melee selection keeps its declared attacks (04.02 splitting).
WeaponSelection retargeted(const GameState& state, const AttackFlow& f, const WeaponSelection& old, UnitId target) {
    if (f.context.type == AttackType::Fighting) {
        return select_weapon_target(state, old.attacker, old.weapon_index, old.profile_index, target,
                                    old.declared_attacks);
    }
    const ShotDeclaration shot{old.attacker, old.weapon_index, old.profile_index, target};
    return make_weapon_selections(state, unit_ref(state, f.attacker), f.shooting_type,
                                  std::span<const ShotDeclaration>(&shot, 1))
        .front();
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

// D-013 / D-051: does any 05.04 save roll inflict damage, so that a model must be selected? The defender is
// asked whenever one does, even when the selection is forced (D-051: no model takes damage without the
// player picking it). Run 05.04 on a copy of the target to find out. D-069: a random D takes its stand-in (its
// maximum), so no dice are rolled; any stand-in gives the same answer.
bool casualty_decision_needed(const GameState& state, const AttackFlow& f) {
    const BatchInProgress& b = current_batch(f);
    const AttackBatch& batch = f.batches[b.batch];
    Unit copy = unit_ref(state, batch.target);
    const ModelChooser first = [](const Unit&, std::span<const ModelId> options) { return options.front(); };
    const InflictDamageResult r = inflict_damage(copy, order_groups(b.groups, *b.order), *b.save_rolls, batch.profile,
                                                 first, false, damage_stand_in);
    return std::any_of(r.steps.begin(), r.steps.end(), [](const SaveStep& s) { return s.damage.has_value(); });
}

// After batch `index`'s wound rolls (05.02), and any re-rolls of them (24.38): the batch is done if nothing wounded,
// otherwise 05.03 starts.
void after_wound_rolls(GameState& state, std::size_t index, const WoundRolls& wounds) {
    AttackFlow& f = flow_of(state);
    if (wounds.wounds() == 0) {
        f.batch_done[index] = true;  // every attack failed: nothing to save
        return;
    }
    // 05.03 Create Groups now; the order and the save rolls follow.
    const Unit& target = unit_ref(state, f.batches[index].target);
    f.current = BatchInProgress{index, wounds.wounds(), create_allocation_groups(target), std::nullopt, std::nullopt};
}

// 04.03 Gather Attack Dice + 05.01 + 05.02 for batch `index`. D-069: the batch's random A are rolled first, one per
// model and weapon, with their bonus dice added after. A [TWIN-LINKED] batch with a failed wound roll waits for its
// attacker's re-roll choice (24.38, D-072); every other batch goes straight on, with the same dice as before.
void start_batch(GameState& state, DiceSource& dice, std::size_t index, Events& events) {
    AttackFlow& f = flow_of(state);
    f.current_target = f.batches[index].target;  // 04.03 Select Enemy Unit
    if (!f.batches[index].random_attacks.empty()) {
        // D-069 (04.03 Gather Attack Dice): each random A for its model and weapon, then the batch is fully gathered.
        AttackBatch& gathering = f.batches[index];
        std::vector<AttackDiceRoll> rolls = roll_random_attacks(dice, gathering.random_attacks);
        for (const AttackDiceRoll& r : rolls) gathering.attack_dice += r.total();
        gathering.random_attacks.clear();
        events.push_back(event::AttackDiceRolled{index, std::move(rolls), gathering.attack_dice});
    }
    const AttackBatch batch = f.batches[index];
    events.push_back(event::AttackBatchSelected{f.attacker, index, batch.target, batch.attack_dice});

    const Unit& target = unit_ref(state, batch.target);
    const HitRolls hits = make_hit_rolls(dice, batch.profile, batch.attack_dice);  // 05.01
    events.push_back(event::HitRollsMade{index, hits});
    const WoundRolls wounds = make_wound_rolls(dice, batch.profile, hits.hits(), target_toughness(target));  // 05.02
    events.push_back(event::WoundRollsMade{index, wounds});

    if (twin_linked(batch.profile) && !failed_wound_rolls(wounds).empty()) {
        f.wound_rerolls = PendingWoundRerolls{index, wounds};  // 24.38: the attacker is asked (attack_decision)
        return;
    }
    after_wound_rolls(state, index, wounds);
}

void declare_order(GameState& state, std::vector<std::size_t> order, Events& events) {
    AttackFlow& f = flow_of(state);
    BatchInProgress& b = *f.current;
    b.order = std::move(order);
    events.push_back(event::AllocationOrderDeclared{f.batches[b.batch].target, order_groups(b.groups, *b.order)});
}

// 05.04 with the casualty priority (D-013), then the batch is done. D-069: a random D is rolled from `dice` for each
// attack that inflicts damage, after the priority has selected its model (the order still answers every step).
void resolve_casualties(GameState& state, DiceSource& dice, const std::vector<ModelId>& priority, Events& events) {
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
    const DamageRoller roll = [&dice](const DiceExpr& d) { return roll_characteristic(dice, d); };
    const InflictDamageResult result =
        inflict_damage(target, order_groups(b.groups, *b.order), *b.save_rolls, batch.profile, by_priority, false, roll);
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
    state.flow.attack.reset();
    state.flow.activation.reset();
}

bool side_has_models(const GameState& state, Side side) {
    return std::any_of(state.units.begin(), state.units.end(),
                       [side](const Unit& u) { return u.side == side && !is_destroyed(u); });
}

// 24.15 [HAZARDOUS]: after the unit has resolved all of its attacks, one hazard roll (06.03) for it per
// [HAZARDOUS] weapon selected, made simultaneously. D-071: none are made if the unit is gone, or if no enemy
// model is left (D-007 ends the game once the attacks are resolved).
void make_attack_hazard_rolls(GameState& state, DiceSource& dice, Events& events) {
    AttackFlow& f = flow_of(state);
    const int count = f.hazard_rolls;
    f.hazard_rolls = 0;
    const Unit& unit = unit_ref(state, f.attacker);
    if (is_destroyed(unit) || !side_has_models(state, opponent(unit.side))) return;
    const HazardRolls rolls = make_hazard_rolls(dice, unit, count);
    events.push_back(event::HazardRollsMade{unit.id, rolls});
    f.pending_mortal_wounds = rolls.mortal_wounds;  // 06.03: 1 per failure, or 3 for MONSTER/VEHICLE units
}

// 06.02 for the attacking unit's hazard mortal wounds, with its player's casualty priority (D-013).
void resolve_attack_mortal_wounds(GameState& state, const std::vector<ModelId>& priority, Events& events) {
    AttackFlow& f = flow_of(state);
    const int count = f.pending_mortal_wounds;
    f.pending_mortal_wounds = 0;
    inflict_mortal_wounds_by_priority(state, unit_ref(state, f.attacker), count, priority, events);
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
    if (f.wound_rerolls) {
        // 24.38 (D-072): the attacking unit's player (the active player, 01.03) chooses the wound rolls to re-roll.
        return PendingDecision{unit_ref(state, f.attacker).side, decision::RerollWoundRolls{f.attacker}};
    }
    if (!f.current && f.pending_mortal_wounds > 0) {
        // 06.02 Select Model for the hazard mortal wounds (24.15), answered once by the attacking unit's player
        // (D-013). Every mortal wound selects a model, so they are always asked (D-051).
        const Unit& attacker = unit_ref(state, f.attacker);
        if (!is_destroyed(attacker)) return PendingDecision{attacker.side, decision::OrderCasualties{attacker.id}};
        return std::nullopt;
    }
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
        if (casualty_decision_needed(state, f)) {
            return PendingDecision{target.side, decision::OrderCasualties{batch.target}};  // 05.04, D-013, D-051
        }
        return std::nullopt;
    }
    if (has_lost_batches(state, f)) {
        // 04.03.03 (D-093): the attacking unit's player can select new targets when one exists; otherwise the attacks
        // are lost (attack_progress).
        if (any_retarget_option(state, f)) {
            return PendingDecision{unit_ref(state, f.attacker).side, decision::RetargetAttacks{f.attacker}};
        }
        return std::nullopt;
    }
    if (batch_options(state, f).size() > 1) {
        return PendingDecision{unit_ref(state, f.attacker).side, decision::SelectAttackBatch{f.attacker}};  // 04.03
    }
    return std::nullopt;
}

void attack_progress(GameState& state, DiceSource& dice, Events& events) {
    AttackFlow& f = flow_of(state);
    if (f.wound_rerolls) {
        throw std::logic_error("attack flow: the wound rolls wait for the attacker's re-roll choice (24.38)");
    }
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
        // No save roll inflicts damage (D-051): nothing to select, any priority gives the same result.
        resolve_casualties(state, dice, model_ids(unit_ref(state, batch.target)), events);
        return;
    }

    if (f.pending_mortal_wounds > 0) {
        // Only reached when the attacking unit is gone (attack_decision asks otherwise): nothing to inflict.
        f.pending_mortal_wounds = 0;
        return;
    }
    // 04.03.03 (D-093): batches against a target that is no longer on the battlefield, none of whose weapons has
    // another eligible target, are lost.
    bool dropped = false;
    for (std::size_t i = 0; i < f.batches.size(); ++i) {
        if (!f.batch_done[i] && target_gone(state, f.batches[i])) {
            f.batch_done[i] = true;
            // D-069: a random A not rolled yet is lost unrolled.
            events.push_back(
                event::AttacksLost{f.batches[i].target, i, f.batches[i].attack_dice, f.batches[i].random_attacks});
            dropped = true;
        }
    }
    if (dropped) {
        return;
    }

    const std::vector<std::size_t> options = batch_options(state, f);
    if (options.empty()) {
        // 04.03: all weapons have been used; the attacks have been resolved. Then 24.15's hazard rolls.
        if (f.hazard_rolls > 0) {
            make_attack_hazard_rolls(state, dice, events);
            return;
        }
        finish_attacks(state);
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
    // M61: 05.04 for an attack's save rolls, 06.02 for mortal wounds (decision_rule(state, ...)).
    const std::string rule(decision_rule(state, *state.pending));
    if (a.target != d.target) {
        return ValidationResult::failure(rule + ": casualties are being allocated in " + unit_label(d.target) +
                                         ", not " + unit_label(a.target));
    }
    if (a.priority.empty()) {
        return ValidationResult::failure(rule + ": the casualty order is empty; list every model in " +
                                         unit_label(d.target) + " (D-013)");
    }
    std::vector<ModelId> expected = model_ids(unit_ref(state, d.target));
    std::vector<ModelId> given = a.priority;
    std::sort(expected.begin(), expected.end());
    std::sort(given.begin(), given.end());
    if (given != expected) {
        return ValidationResult::failure(rule + ": the casualty order must list every model in " +
                                         unit_label(d.target) + " exactly once (D-013)");
    }
    return ValidationResult::success();
}

// ---- 24.38 RerollWoundRolls (D-072) ----

namespace {

const PendingWoundRerolls& pending_rerolls(const GameState& state) {
    const AttackFlow& f = flow_of(state);
    if (!f.wound_rerolls) {
        throw std::logic_error("attack flow: no wound rolls are waiting to be re-rolled");
    }
    return *f.wound_rerolls;
}

}  // namespace

std::vector<GameAction> legal_for(const GameState& state, const decision::RerollWoundRolls&) {
    // D-072 (a D-004 extension): every failed wound roll first, then none. Any other subset is valid too.
    return {action::RerollWoundRolls{failed_wound_rolls(pending_rerolls(state).rolls)}, action::RerollWoundRolls{}};
}

ValidationResult validate_details(const GameState& state, Side, const action::RerollWoundRolls& a) {
    const PendingWoundRerolls& p = pending_rerolls(state);
    const std::size_t n = p.rolls.rolls.size();
    ValidationResult r;
    std::vector<std::size_t> seen;
    for (const std::size_t i : a.indices) {
        if (i >= n) {
            r.reasons.push_back("24.38: there is no wound roll " + std::to_string(i) + "; attack batch " +
                                std::to_string(p.batch) + " made " + std::to_string(n) + " wound roll" +
                                (n == 1 ? "" : "s") + " (0 to " + std::to_string(n - 1) + ")");
        } else if (std::find(seen.begin(), seen.end(), i) != seen.end()) {
            r.reasons.push_back("D-011: wound roll " + std::to_string(i) +
                                " is listed twice; a dice can't be re-rolled more than once");
        }
        seen.push_back(i);
    }
    return r.reasons.empty() ? ValidationResult::success() : r;
}

void apply_details(GameState& state, Side, const action::RerollWoundRolls& a, DiceSource& dice, Events& events) {
    AttackFlow& f = flow_of(state);
    PendingWoundRerolls p = pending_rerolls(state);
    f.wound_rerolls.reset();
    const AttackBatch& batch = f.batches[p.batch];
    std::vector<WoundReroll> rerolls = reroll_wound_rolls(dice, batch.profile, p.rolls, a.indices);  // D-011
    events.push_back(event::WoundRollsRerolled{p.batch, std::move(rerolls), p.rolls});
    after_wound_rolls(state, p.batch, p.rolls);
}

// ---- 04.03.03 RetargetAttacks (D-093) ----

std::vector<GameAction> legal_for(const GameState& state, const decision::RetargetAttacks&) {
    // D-004: one answer per enemy unit that some lost weapon can target (every such weapon given that unit), then
    // none. Any other combination of each weapon's options is valid too.
    const AttackFlow& f = flow_of(state);
    const std::vector<std::size_t> lost = lost_selections(state, f);
    std::vector<std::vector<UnitId>> options;
    for (const std::size_t i : lost) options.push_back(retarget_options(state, f, i));
    std::vector<GameAction> out;
    for (const Unit& u : state.units) {
        action::RetargetAttacks a;
        for (std::size_t k = 0; k < lost.size(); ++k) {
            if (std::find(options[k].begin(), options[k].end(), u.id) != options[k].end()) {
                a.retargets.push_back(AttackRetarget{lost[k], u.id});
            }
        }
        if (!a.retargets.empty()) out.push_back(std::move(a));
    }
    out.push_back(action::RetargetAttacks{});
    return out;
}

ValidationResult validate_details(const GameState& state, Side, const action::RetargetAttacks& a) {
    const AttackFlow& f = flow_of(state);
    const std::vector<std::size_t> lost = lost_selections(state, f);
    ValidationResult r;
    std::vector<std::size_t> seen;
    for (const AttackRetarget& t : a.retargets) {
        const std::string what = "weapon selection " + std::to_string(t.selection);
        if (std::find(lost.begin(), lost.end(), t.selection) == lost.end()) {
            r.reasons.push_back("D-093: " + what + " is not waiting on a target that is gone; only those weapons "
                                "select new targets (04.03.03)");
            continue;
        }
        if (std::find(seen.begin(), seen.end(), t.selection) != seen.end()) {
            r.reasons.push_back("D-093: " + what + " is given a new target more than once");
            continue;
        }
        seen.push_back(t.selection);
        const std::vector<UnitId> options = retarget_options(state, f, t.selection);
        if (std::find(options.begin(), options.end(), t.target) == options.end()) {
            r.reasons.push_back("D-093: " + unit_label(t.target) + " is not an eligible target for " + what +
                                (f.context.type == AttackType::Fighting
                                     ? " (04.02: an enemy unit its model is engaged with)"
                                     : " (04.02: an enemy unit it could select now)"));
        }
    }
    return r.reasons.empty() ? ValidationResult::success() : r;
}

void apply_details(GameState& state, Side, const action::RetargetAttacks& a, DiceSource&, Events& events) {
    AttackFlow& f = flow_of(state);
    std::vector<AttackRetarget> retargets = a.retargets;
    std::sort(retargets.begin(), retargets.end(),
              [](const AttackRetarget& x, const AttackRetarget& y) { return x.selection < y.selection; });
    // The new Select Targets facts, recorded before anything is rolled (04.02).
    std::vector<WeaponSelection> fresh;
    std::vector<std::size_t> moved;
    for (const AttackRetarget& t : retargets) {
        fresh.push_back(retargeted(state, f, f.selections[t.selection], t.target));
        moved.push_back(t.selection);
    }
    // The batches whose target is gone are done; the weapons not given a new target are lost (D-069: a random A
    // unrolled).
    for (std::size_t i = 0; i < f.batches.size(); ++i) {
        if (f.batch_done[i] || !target_gone(state, f.batches[i])) continue;
        f.batch_done[i] = true;
        int dice = 0;
        bool dropped = false;
        std::vector<RandomAttackDice> random;
        for (const std::size_t s : f.batches[i].selections) {
            if (std::find(moved.begin(), moved.end(), s) != moved.end()) continue;
            dropped = true;
            const SelectionDice d = selection_dice(selected_profile(state, f.selections[s]), f.selections[s], f.context);
            dice += d.dice;
            if (d.random) random.push_back(RandomAttackDice{s, f.selections[s].attacker, *d.random, d.bonus});
        }
        if (dropped) {
            events.push_back(event::AttacksLost{f.batches[i].target, i, dice, std::move(random)});
        }
    }
    if (retargets.empty()) return;
    // 04.03 / D-006: the moved weapons gathered into new batches, appended (earlier batch indices stay).
    std::vector<AttackBatch> batches = gather_attack_batches(state, fresh, f.context);
    for (AttackBatch& b : batches) {
        for (std::size_t& s : b.selections) s = moved[s];
        for (RandomAttackDice& r : b.random_attacks) r.selection = moved[r.selection];
    }
    for (std::size_t k = 0; k < moved.size(); ++k) f.selections[moved[k]] = fresh[k];  // fresh is moved into the event
    const std::size_t first = f.batches.size();
    f.batches.insert(f.batches.end(), batches.begin(), batches.end());
    f.batch_done.resize(f.batches.size(), false);
    events.push_back(event::AttacksRetargeted{f.attacker, std::move(retargets), std::move(fresh), first, std::move(batches)});
}

void apply_details(GameState& state, Side, const action::OrderCasualties& a, DiceSource& dice, Events& events) {
    if (state.flow.attack && state.flow.attack->pending_mortal_wounds > 0) {
        resolve_attack_mortal_wounds(state, a.priority, events);  // 06.02 ([HAZARDOUS] hazards, 24.15)
    } else if (state.flow.attack) {
        resolve_casualties(state, dice, a.priority, events);  // 05.04 (D-069: random D rolled here)
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
                   std::vector<WeaponSelection> selections, Events& events, ShootingType shooting_type) {
    check_begin_preconditions(state, attacker, context, selections);
    AttackFlow f;
    f.attacker = attacker;
    f.context = context;
    f.batches = gather_attack_batches(state, selections, context);  // 04.03, D-006
    f.batch_done.assign(f.batches.size(), false);
    f.hazard_rolls = hazardous_weapons_selected(state, selections);  // 24.15: counted at Select Weapons
    f.selections = std::move(selections);
    f.shooting_type = shooting_type;  // D-093: new targets (04.03.03) must fit it too
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
