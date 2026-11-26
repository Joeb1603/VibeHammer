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
    // D-097 (the user's ruling): 15.09 snap shooting targets one enemy unit, so 04.03.03 gives it no new target.
    if (f.context.snap_shooting) return {};
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

// The batch whose dice are being rolled (D-095).
BatchRolls& rolling_of(AttackFlow& f) {
    if (!f.rolling) throw std::logic_error("attack flow: no batch is being rolled");
    return *f.rolling;
}

// 04.03: the batch's attack dice are gathered: it is announced, with its dice.
void batch_gathered(AttackFlow& f, Events& events) {
    const std::size_t index = rolling_of(f).batch;
    events.push_back(event::AttackBatchSelected{f.attacker, index, f.batches[index].target, f.batches[index].attack_dice});
}

// 04.03 Select Enemy Unit: batch `index` is the next to resolve. D-095 (M94): its dice are rolled as the attacking
// unit's player answers each RollDice (make_attack_roll): its random A (D-069) first, which gathers it, then its hit
// rolls (05.01) and its wound rolls (05.02). A batch with no random A is gathered at once.
void select_batch(GameState& state, std::size_t index, Events& events) {
    AttackFlow& f = flow_of(state);
    f.current_target = f.batches[index].target;
    f.rolling = BatchRolls{index, std::nullopt};
    if (f.batches[index].random_attacks.empty()) batch_gathered(f, events);
}

// D-069 (04.03 Gather Attack Dice): each random A of the batch for its model and weapon, with its bonus dice added
// after; then the batch is fully gathered.
void roll_batch_attacks(GameState& state, DiceSource& dice, Events& events) {
    AttackFlow& f = flow_of(state);
    const std::size_t index = rolling_of(f).batch;
    const AttackBatch& gathering = f.batches[index];
    const std::vector<int> faces = roll_d6s(dice, random_attack_dice_count(gathering.random_attacks));
    std::vector<AttackDiceRoll> rolls = random_attacks_from_faces(gathering.random_attacks, faces);
    int attack_dice = gathering.attack_dice;
    for (const AttackDiceRoll& r : rolls) attack_dice += r.total();
    events.push_back(event::AttackDiceRolled{index, std::move(rolls), attack_dice});
    after_roll(state, RollKind::RandomAttacks, f.attacker, index, faces, {}, events);  // D-096: 15.02's window
}

// 05.01 for the batch being rolled: needs no dice for [TORRENT]'s automatic hits (24.37) or with no attack dice.
bool hit_rolls_need_dice(const AttackBatch& batch) { return !batch.profile.torrent && batch.attack_dice > 0; }

void make_batch_hit_rolls(GameState& state, DiceSource& dice, Events& events) {
    AttackFlow& f = flow_of(state);
    const BatchRolls& r = rolling_of(f);
    const AttackBatch& batch = f.batches[r.batch];
    const std::vector<int> faces = roll_hit_faces(dice, batch.profile, batch.attack_dice);
    events.push_back(event::HitRollsMade{r.batch, judge_hit_rolls(batch.profile, batch.attack_dice, faces)});
    after_roll(state, RollKind::Hit, f.attacker, r.batch, faces, {}, events);  // D-096: 15.02's window
}

// 05.02 for the batch being rolled, one per hit (none with no hits); then the batch is no longer being rolled. A
// [TWIN-LINKED] batch with a failed wound roll waits for its attacker's re-roll choice (24.38, D-072); every other
// batch goes straight on to 05.03.
void make_batch_wound_rolls(GameState& state, DiceSource& dice, Events& events) {
    AttackFlow& f = flow_of(state);
    const BatchRolls r = rolling_of(f);
    const AttackBatch batch = f.batches[r.batch];
    const Unit& target = unit_ref(state, batch.target);
    const int toughness = target_toughness(target);
    const std::vector<int> faces = roll_d6s(dice, r.hits->hits());
    const WoundRolls wounds = judge_wound_rolls(batch.profile, toughness, faces);
    events.push_back(event::WoundRollsMade{r.batch, wounds});
    if (twin_linked(batch.profile) && !failed_wound_rolls(wounds).empty()) {
        f.rolling.reset();
        f.wound_rerolls = PendingWoundRerolls{r.batch, wounds};  // 24.38: the attacker is asked (attack_decision)
        return;
    }
    after_roll(state, RollKind::Wound, f.attacker, r.batch, faces, {}, events);  // D-096: 15.02's window
}

void declare_order(GameState& state, std::vector<std::size_t> order, Events& events) {
    AttackFlow& f = flow_of(state);
    BatchInProgress& b = *f.current;
    b.order = std::move(order);
    events.push_back(event::AllocationOrderDeclared{f.batches[b.batch].target, order_groups(b.groups, *b.order)});
}

// 05.04 Select Model by the casualty priority (D-013): the first legal model in it.
ModelChooser priority_chooser(const std::vector<ModelId>& priority) {
    return [&priority](const Unit&, std::span<const ModelId> options) {
        for (const ModelId id : priority) {
            if (std::find(options.begin(), options.end(), id) != options.end()) {
                return id;
            }
        }
        throw std::logic_error("05.04: the casualty order has no legal model");
    };
}

// 05.04 with the casualty priority (D-013), then the batch is done. D-069: a random D is rolled by `roll` for each
// attack that inflicts damage, after the priority has selected its model (the order still answers every step).
void resolve_casualties(GameState& state, const std::vector<ModelId>& priority, const DamageRoller& roll,
                        Events& events) {
    AttackFlow& f = flow_of(state);
    const BatchInProgress b = current_batch(f);
    const AttackBatch& batch = f.batches[b.batch];
    Unit& target = unit_ref(state, batch.target);
    const InflictDamageResult result = inflict_damage(target, order_groups(b.groups, *b.order), *b.save_rolls,
                                                      batch.profile, priority_chooser(priority), false, roll);
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

// A random D rolled from the engine's dice (D-069), as the attack resolves.
DamageRoller dice_roller(DiceSource& dice) {
    return [&dice](const DiceExpr& d) { return roll_characteristic(dice, d); };
}

// D-096 (M95): a random D is rolled per attack that inflicts damage, each its own Damage roll. Replays 05.04 on a copy
// of the target with the rolls made so far (BatchInProgress::damage_rolls): either every damaging attack has its roll
// (complete), or `model` is the one the next attack's damage goes to (05.04 Select Model, by the casualty priority).
struct DamagePlan {
    bool complete = false;
    ModelId model;
};

DamagePlan plan_damage(const GameState& state, const AttackFlow& f) {
    const BatchInProgress& b = current_batch(f);
    const AttackBatch& batch = f.batches[b.batch];
    const std::vector<ModelId>& priority = b.priority.value();
    const ModelChooser by_priority = priority_chooser(priority);
    std::optional<ModelId> chosen;
    const ModelChooser recording = [&](const Unit& u, std::span<const ModelId> options) {
        chosen = by_priority(u, options);
        return *chosen;
    };
    std::size_t next = 0;
    std::optional<ModelId> needed;
    const DamageRoller roll = [&](const DiceExpr& d) {
        if (next < b.damage_rolls.size()) return b.damage_rolls[next++];
        if (!needed) needed = chosen.value();  // the first attack without a roll yet
        return damage_stand_in(d);             // the rest of the replay is not used
    };
    Unit copy = unit_ref(state, batch.target);
    // ask_when_forced: the chooser hears every damaging step, so `chosen` is that step's model (D-051's preview does
    // the same).
    (void)inflict_damage(copy, order_groups(b.groups, *b.order), *b.save_rolls, batch.profile, recording, true, roll);
    if (needed) return DamagePlan{false, *needed};
    return DamagePlan{true, ModelId{}};
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
bool attack_hazard_rolls_owed(const GameState& state, const AttackFlow& f) {
    if (f.hazard_rolls <= 0) return false;
    const Unit& unit = unit_ref(state, f.attacker);
    return !is_destroyed(unit) && side_has_models(state, opponent(unit.side));
}

void make_attack_hazard_rolls(GameState& state, DiceSource& dice, Events& events) {
    AttackFlow& f = flow_of(state);
    const int count = f.hazard_rolls;
    f.hazard_rolls = 0;
    const Unit& unit = unit_ref(state, f.attacker);
    const std::vector<int> faces = roll_d6s(dice, count);
    events.push_back(event::HazardRollsMade{unit.id, judge_hazard_rolls(unit, faces)});
    after_roll(state, RollKind::Hazard, unit.id, 0, faces, {}, events);  // D-096: 15.02's window
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
    if (f.rolling) {
        // D-095 (M94): the batch's dice wait for the attacking unit's player, roll by roll; one that needs no dice is made
        // at once (attack_progress).
        const AttackBatch& batch = f.batches[f.rolling->batch];
        const Side side = unit_ref(state, f.attacker).side;
        if (!batch.random_attacks.empty()) {
            return PendingDecision{side, decision::RollDice{RollKind::RandomAttacks, f.attacker}};  // D-069
        }
        if (!f.rolling->hits) {
            if (!hit_rolls_need_dice(batch)) return std::nullopt;
            return PendingDecision{side, decision::RollDice{RollKind::Hit, f.attacker}};  // 05.01
        }
        if (f.rolling->hits->hits() == 0) return std::nullopt;
        return PendingDecision{side, decision::RollDice{RollKind::Wound, f.attacker}};  // 05.02
    }
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
            // D-095 (M94): 05.03 Make Save Rolls waits for the opposing player (the target's controlling player).
            return PendingDecision{target.side, decision::RollDice{RollKind::Save, batch.target}};
        }
        if (b.priority) {
            // D-096 (M95, replaces D-095 part 3): with the casualty order given, each attack that inflicts damage with
            // a random D (D-069) waits for the attacking unit's player's Damage roll; with all of them made, the batch
            // resolves (attack_progress).
            if (plan_damage(state, f).complete) return std::nullopt;
            return PendingDecision{unit_ref(state, f.attacker).side, decision::RollDice{RollKind::Damage, f.attacker}};
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
    const std::size_t options = batch_options(state, f).size();
    if (options > 1) {
        return PendingDecision{unit_ref(state, f.attacker).side, decision::SelectAttackBatch{f.attacker}};  // 04.03
    }
    if (options == 0 && attack_hazard_rolls_owed(state, f)) {
        // D-095 (M94): 24.15's hazard rolls (06.03) wait for the attacking unit's player.
        return PendingDecision{unit_ref(state, f.attacker).side, decision::RollDice{RollKind::Hazard, f.attacker}};
    }
    return std::nullopt;
}

void attack_progress(GameState& state, DiceSource& dice, Events& events) {
    AttackFlow& f = flow_of(state);
    if (f.rolling) {
        // D-095: a roll that needs no dice is made at once: [TORRENT]'s automatic hits, or wound rolls with no hits.
        if (!f.rolling->hits) {
            make_batch_hit_rolls(state, dice, events);
        } else {
            make_batch_wound_rolls(state, dice, events);
        }
        return;
    }
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
            throw std::logic_error("attack flow: the batch's save rolls wait for a player (D-095)");
        }
        if (b.priority) {
            // D-096: every damage roll the batch needs is made; 05.04 resolves with them, in the order they were made.
            const std::vector<ModelId> priority = *b.priority;
            const std::vector<CharacteristicRoll> rolls = b.damage_rolls;
            std::size_t next = 0;
            const DamageRoller stored = [&rolls, &next](const DiceExpr&) {
                if (next >= rolls.size()) throw std::logic_error("D-096: a damage roll is missing");
                return rolls[next++];
            };
            resolve_casualties(state, priority, stored, events);
            return;
        }
        // No save roll inflicts damage (D-051): nothing to select, any priority gives the same result.
        resolve_casualties(state, model_ids(unit_ref(state, batch.target)), dice_roller(dice), events);
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
        // 04.03: all weapons have been used; the attacks have been resolved. Then 24.15's hazard rolls, which wait for
        // the attacking unit's player (attack_decision) unless none are made (D-071).
        if (f.hazard_rolls > 0) {
            f.hazard_rolls = 0;
            return;
        }
        finish_attacks(state);
        return;
    }
    select_batch(state, options.front(), events);  // only one option (otherwise a decision)
}

// ---- D-095 (M94): the rolls an attack waits for ----

void make_attack_roll(GameState& state, RollKind roll, DiceSource& dice, Events& events) {
    AttackFlow& f = flow_of(state);
    switch (roll) {
        case RollKind::RandomAttacks:
            roll_batch_attacks(state, dice, events);  // D-069
            return;
        case RollKind::Hit:
            make_batch_hit_rolls(state, dice, events);  // 05.01
            return;
        case RollKind::Wound:
            make_batch_wound_rolls(state, dice, events);  // 05.02
            return;
        case RollKind::Save: {
            const BatchInProgress& b = *f.current;
            const UnitId target = f.batches[b.batch].target;
            const std::vector<int> faces = make_save_rolls(dice, b.wounds);  // 05.03 Make Save Rolls
            events.push_back(event::SaveRollsMade{target, faces});
            after_roll(state, RollKind::Save, target, b.batch, faces, {}, events);  // D-096: the defender's window
            return;
        }
        case RollKind::Damage: {
            // D-096: the next attack's random D (D-069), once its model is selected (05.04): then 15.02's window; the
            // batch resolves when every damaging attack has its roll.
            const DamagePlan plan = plan_damage(state, f);
            if (plan.complete) throw std::logic_error("D-096: no attack waits for a damage roll");
            const std::size_t index = f.current->batch;
            const AttackBatch& batch = f.batches[index];
            const CharacteristicRoll roll = roll_characteristic(dice, batch.profile.damage);
            events.push_back(event::DamageRolled{f.attacker, batch.target, plan.model, batch.profile.damage, roll});
            after_roll(state, RollKind::Damage, f.attacker, index, roll.dice, {}, events);
            return;
        }
        case RollKind::Hazard:
            make_attack_hazard_rolls(state, dice, events);  // 24.15
            return;
        case RollKind::Advance:
        case RollKind::Charge:
        case RollKind::BattleShock:
            break;
    }
    throw std::logic_error("attack flow: an attack makes no " + std::string(roll_kind_name(roll)) + " roll");
}

// D-096 (M95): an attack roll as it stands once its window closes (or at once): M94's consequence, with the typed result
// rebuilt from the dice by the judge the roll used.
void apply_attack_roll(GameState& state, RollKind roll, std::size_t batch, const std::vector<int>& faces,
                       Events& events) {
    AttackFlow& f = flow_of(state);
    switch (roll) {
        case RollKind::RandomAttacks: {
            AttackBatch& gathering = f.batches[batch];
            for (const AttackDiceRoll& r : random_attacks_from_faces(gathering.random_attacks, faces)) {
                gathering.attack_dice += r.total();
            }
            gathering.random_attacks.clear();
            batch_gathered(f, events);  // 04.03: announced with its dice (D-069)
            return;
        }
        case RollKind::Hit: {
            const AttackBatch& b = f.batches[batch];
            rolling_of(f).hits = judge_hit_rolls(b.profile, b.attack_dice, faces);
            return;
        }
        case RollKind::Wound: {
            const AttackBatch b = f.batches[batch];
            const WoundRolls wounds = judge_wound_rolls(b.profile, target_toughness(unit_ref(state, b.target)), faces);
            f.rolling.reset();  // the batch is no longer being rolled (already so after a Twin-linked re-roll)
            after_wound_rolls(state, batch, wounds);
            return;
        }
        case RollKind::Save:
            f.current.value().save_rolls = faces;  // 05.03 (unmodified, roll order)
            return;
        case RollKind::Hazard:
            // 06.03: 1 mortal wound per failure, or 3 for MONSTER/VEHICLE units (24.15).
            f.pending_mortal_wounds = judge_hazard_rolls(unit_ref(state, f.attacker), faces).mortal_wounds;
            return;
        case RollKind::Damage: {
            BatchInProgress& b = f.current.value();
            b.damage_rolls.push_back(characteristic_from_faces(f.batches[b.batch].profile.damage, faces));
            return;
        }
        case RollKind::Advance:
        case RollKind::Charge:
        case RollKind::BattleShock:
            break;
    }
    throw std::logic_error("attack flow: no " + std::string(roll_kind_name(roll)) + " roll waits in a window");
}

event::RerolledRoll attack_roll_result(const GameState& state, RollKind roll, std::size_t batch,
                                       const std::vector<int>& faces) {
    const AttackFlow& f = flow_of(state);
    switch (roll) {
        case RollKind::RandomAttacks: {
            const AttackBatch& b = f.batches[batch];
            std::vector<AttackDiceRoll> rolls = random_attacks_from_faces(b.random_attacks, faces);
            int attack_dice = b.attack_dice;
            for (const AttackDiceRoll& r : rolls) attack_dice += r.total();
            return event::AttackDiceRolled{batch, std::move(rolls), attack_dice};
        }
        case RollKind::Hit: {
            const AttackBatch& b = f.batches[batch];
            return event::HitRollsMade{batch, judge_hit_rolls(b.profile, b.attack_dice, faces)};
        }
        case RollKind::Wound: {
            const AttackBatch& b = f.batches[batch];
            return event::WoundRollsMade{
                batch, judge_wound_rolls(b.profile, target_toughness(unit_ref(state, b.target)), faces)};
        }
        case RollKind::Save:
            return event::SaveRollsMade{f.batches[batch].target, faces};
        case RollKind::Hazard:
            return event::HazardRollsMade{f.attacker, judge_hazard_rolls(unit_ref(state, f.attacker), faces)};
        case RollKind::Damage: {
            const AttackBatch& b = f.batches[batch];
            return event::DamageRolled{f.attacker, b.target, plan_damage(state, f).model, b.profile.damage,
                                       characteristic_from_faces(b.profile.damage, faces)};
        }
        case RollKind::Advance:
        case RollKind::Charge:
        case RollKind::BattleShock:
            break;
    }
    throw std::logic_error("attack flow: no " + std::string(roll_kind_name(roll)) + " roll is re-rolled here");
}

void describe_attack_roll(const GameState& state, RollKind roll, RollDiceInfo& info) {
    const AttackFlow& f = flow_of(state);
    switch (roll) {
        case RollKind::RandomAttacks:
        case RollKind::Hit:
        case RollKind::Wound: {
            const AttackBatch& batch = f.batches[f.rolling.value().batch];
            info.target = batch.target;
            if (roll == RollKind::RandomAttacks) {
                info.dice = 0;
                for (const RandomAttackDice& r : batch.random_attacks) info.dice += r.attacks.num_dice;
                if (!batch.random_attacks.empty()) info.expression = to_string(batch.random_attacks.front().attacks);
            } else if (roll == RollKind::Hit) {
                info.dice = batch.attack_dice;
                info.need = batch.profile.skill;  // 05.01: BS or WS
            } else {
                info.dice = f.rolling->hits.value().hits();
                info.need = required_wound_roll(batch.profile.strength, target_toughness(unit_ref(state, batch.target)));
            }
            return;
        }
        case RollKind::Save: {
            const BatchInProgress& b = current_batch(f);
            info.target = f.batches[b.batch].target;
            info.dice = b.wounds;
            return;
        }
        case RollKind::Damage: {
            // D-096: one attack's random D (D-069), for the model the casualty order selects next.
            const BatchInProgress& b = current_batch(f);
            const AttackBatch& batch = f.batches[b.batch];
            info.target = batch.target;
            info.expression = to_string(batch.profile.damage);
            info.dice = batch.profile.damage.num_dice;
            info.model = plan_damage(state, f).model;
            return;
        }
        case RollKind::Hazard:
            info.dice = f.hazard_rolls;
            return;
        case RollKind::Advance:
        case RollKind::Charge:
        case RollKind::BattleShock:
            return;
    }
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

void apply_details(GameState& state, Side, const action::SelectAttackBatch& a, DiceSource&, Events& events) {
    select_batch(state, a.batch, events);
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
    std::vector<std::size_t> rerolled;
    for (const WoundReroll& r : rerolls) rerolled.push_back(r.index);  // ascending (reroll_wound_rolls' order)
    events.push_back(event::WoundRollsRerolled{p.batch, std::move(rerolls), p.rolls});
    std::vector<int> faces;
    for (const WoundRoll& r : p.rolls.rolls) faces.push_back(r.unmodified);
    // D-096: then 15.02's window, with the dice Twin-linked re-rolled out of reach (01.05.02).
    after_roll(state, RollKind::Wound, f.attacker, p.batch, faces, std::move(rerolled), events);
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
        AttackFlow& f = *state.flow.attack;
        BatchInProgress& b = *f.current;
        if (f.batches[b.batch].profile.damage.is_fixed()) {
            resolve_casualties(state, a.priority, dice_roller(dice), events);  // 05.04
        } else {
            b.priority = a.priority;  // D-096: each attack's random D waits for the attacker's Damage roll (D-069)
        }
    } else {
        resolve_move_mortal_wounds(state, a.priority, events);  // 06.02 (Desperate Escape hazards)
    }
}

namespace {

void check_begin_preconditions(const GameState& state, UnitId attacker, const AttackContext& context,
                               const std::vector<WeaponSelection>& selections) {
    const bool fighting = context.type == AttackType::Fighting;
    const Stage expected = fighting ? Stage::Fight : Stage::Shoot;
    // 12.08: a unit forced to fight by an Engaging Consolidation fights in the Consolidate step. D-097: 15.08 Fire
    // Overwatch's unit shoots using snap shooting at the end of the opponent's Movement phase.
    if (state.flow.stage != expected && !(fighting && state.flow.stage == Stage::Consolidate) && !context.snap_shooting) {
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
    if (context.type == AttackType::Shooting && unit->side != turn_side(state) && !context.snap_shooting) {
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
    detail::roll_through(state, dice, events);  // D-095: every roll made at once, as apply_action
    return events;
}

std::vector<RetargetChoice> retarget_choices(const GameState& state) {
    std::vector<RetargetChoice> out;
    if (!state.flow.attack) return out;
    for (const std::size_t i : detail::lost_selections(state, *state.flow.attack)) {
        out.push_back(RetargetChoice{i, detail::retarget_options(state, *state.flow.attack, i)});
    }
    return out;
}

std::vector<GameEvent> begin_attacks(GameState& state, UnitId attacker, const AttackContext& context,
                                     std::vector<WeaponSelection> selections) {
    SeededDice dice(state.dice);
    return begin_attacks(state, dice, attacker, context, std::move(selections));
}

}  // namespace wh40k::core
