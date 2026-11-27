// D-096 (M95): reaction windows (01.03.02 Rules Sequencing, app-only) and the stratagems used in them (15): spending
// CP (15.01), Command Re-roll (15.02, just after a roll) and Insane Bravery (15.04, just before a battle-shock roll).
// D-097 (M96): windows that follow no roll: at the end of the opponent's Movement phase (15.08 Fire Overwatch) and
// Charge phase (15.11 Heroic Intervention), and just after an enemy unit has fought (15.12 Counteroffensive).
// D-099 (M97): 15.05 Explosives is used at the Shoot decision (no window) and 15.06 Crushing Impact in a window just
// after a friendly unit ends a charge move; both start an effect (effects.cpp) once their CP are paid.
//
// Every roll point rolls its dice, emits its event (as in M94), then calls after_roll: a window opens there if a side
// can use a stratagem (the roll waits in FlowState::window), otherwise the roll is applied at once. Closing the window
// applies the roll (apply_roll), with the dice as they stand after any re-roll. Declining changes nothing, so a game in
// which every window is declined is exactly the M94 game (the closure, D-096 part 6). The windows that follow no roll
// open with open_window and change nothing either when declined.

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"
#include "core/geometry/distance.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/charge.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "core/rules/stratagem_effects.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core {

namespace detail {

namespace {

constexpr std::array<Stratagem, 7> kStratagems{Stratagem::CommandReroll,      Stratagem::InsaneBravery,
                                               Stratagem::FireOverwatch,      Stratagem::HeroicIntervention,
                                               Stratagem::Counteroffensive,   Stratagem::Explosives,
                                               Stratagem::CrushingImpact};

template <typename T>
bool contains(const std::vector<T>& v, const T& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

StratagemWindow& window_of(GameState& state) {
    if (!state.flow.window) throw std::logic_error("D-096: no window is open");
    return *state.flow.window;
}

const StratagemWindow& window_of(const GameState& state) {
    if (!state.flow.window) throw std::logic_error("D-096: no window is open");
    return *state.flow.window;
}

// D-007's test (as advance.cpp's): a side with no models on the battlefield has lost.
bool side_has_models(const GameState& state, Side side) {
    return std::any_of(state.units.begin(), state.units.end(),
                       [side](const Unit& u) { return u.side == side && !is_destroyed(u); });
}

bool is_roll_window(const StratagemWindow& w) {
    return w.timing == WindowTiming::AfterRoll || w.timing == WindowTiming::BeforeRoll;
}

bool is_roll_stratagem(Stratagem s) { return s == Stratagem::CommandReroll || s == Stratagem::InsaneBravery; }

RollKind roll_of(const StratagemWindow& w) {
    if (!w.roll) throw std::logic_error("D-097: this window follows no roll");
    return *w.roll;
}

UnitId unit_of(const StratagemWindow& w) {
    if (!w.unit) throw std::logic_error("D-097: this window is for no unit");
    return *w.unit;
}

// D-097: the modes a stratagem is used in (Heroic Intervention's two), or the one "no mode".
std::vector<std::optional<StratagemMode>> modes_of(Stratagem s) {
    if (has_modes(s)) return {StratagemMode::LeapToDefend, StratagemMode::IntoTheFray};
    return {std::nullopt};
}

std::string named(Stratagem s, std::optional<StratagemMode> mode) {
    std::string name(stratagem_name(s));
    if (mode) name += " (" + std::string(stratagem_mode_name(*mode)) + ")";
    return name;
}

// 15.02 / 01.05.02: the dice a pick of die `die` re-rolls, as [first, last): a charge roll in full; a characteristic
// roll made by adding several dice together (a random A or D) whole; any other die alone.
std::pair<std::size_t, std::size_t> reroll_group(const GameState& state, const StratagemWindow& w, std::size_t die) {
    switch (roll_of(w)) {
        case RollKind::Charge:
        case RollKind::Damage:  // one attack's D (D-069): its dice are added together
            return {0, w.faces.size()};
        case RollKind::RandomAttacks: {
            // One roll per selection (D-069), each its expression's dice, in order.
            const AttackBatch& batch = state.flow.attack.value().batches.at(w.batch);
            std::size_t first = 0;
            for (const RandomAttackDice& r : batch.random_attacks) {
                const std::size_t last = first + static_cast<std::size_t>(r.attacks.num_dice);
                if (die < last) return {first, last};
                first = last;
            }
            return {die, die + 1};
        }
        default:
            return {die, die + 1};
    }
}

// The dice a player can pick for Command Re-roll: the first die of each roll none of whose dice has been re-rolled.
std::vector<std::size_t> pickable_dice(const GameState& state, const StratagemWindow& w) {
    std::vector<std::size_t> out;
    std::size_t i = 0;
    while (i < w.faces.size()) {
        const auto [first, last] = reroll_group(state, w, i);
        bool fresh = true;
        for (std::size_t k = first; k < last; ++k) fresh = fresh && !contains(w.rerolled, k);
        if (fresh) out.push_back(first);
        i = std::max(last, i + 1);
    }
    return out;
}

// Each stratagem's WHEN, as its refusal.
std::string when_text(Stratagem s) {
    switch (s) {
        case Stratagem::CommandReroll:
            return "15.02: Command Re-roll is used just after an advance, charge, damage, hazard, hit, save or wound "
                   "roll, or a roll for the number of attacks";
        case Stratagem::InsaneBravery:
            return "15.04: Insane Bravery is used in the Battle-shock step of your Command phase, just before a "
                   "battle-shock roll";
        case Stratagem::FireOverwatch:
            return "15.08: Fire Overwatch is used at the end of your opponent's Movement phase";
        case Stratagem::HeroicIntervention:
            return "15.11: Heroic Intervention is used at the end of your opponent's Charge phase";
        case Stratagem::Counteroffensive:
            return "15.12: Counteroffensive is used in the Fight step of your opponent's Fight phase, just after an "
                   "enemy unit has resolved its attacks";
        case Stratagem::Explosives:
            return "15.05: Explosives is used in your Shooting phase, when you select the next unit to shoot (10.02)";
        case Stratagem::CrushingImpact:
            return "15.06: Crushing Impact is used in your Charge phase, just after a friendly MONSTER/VEHICLE unit "
                   "ends a charge move";
    }
    return "15.01: unknown stratagem";
}

// M95: a roll window's stratagems (15.02, 15.04), their target the window's unit. The reasons and their order are
// M95's (tests pin the first reason).
std::optional<std::string> roll_problem(const GameState& state, Side side, Stratagem s, const StratagemWindow& w) {
    const std::string name(stratagem_name(s));
    const std::string rule(stratagem_rule(s));
    const std::size_t i = side_index(side);
    const StratagemRecord& rec = state.stratagems[i];
    const UnitId target = unit_of(w);
    const Unit* u = state.find_unit(target);
    if (u == nullptr || u->side != side || is_destroyed(*u)) {
        return rule + ": " + name + " targets " + side_label(side) + "'s own unit (" + unit_label(target) + " is not)";
    }
    if (state.command_points[i] < stratagem_cost(s)) {
        return "15.01: " + side_label(side) + " has " + std::to_string(state.command_points[i]) + " CP; " + name +
               " costs " + std::to_string(stratagem_cost(s)) + "CP";
    }
    if (contains(rec.used_this_phase, s)) {
        return "15.01: " + side_label(side) + " has already used " + name + " this phase";
    }
    if (once_per_battle(s) && contains(rec.used_this_battle, s)) {
        return rule + ": " + name + " can be used only once per battle, and " + side_label(side) + " has used it";
    }
    if (contains(rec.targeted_this_phase, target)) {
        return "15.01: " + unit_label(target) + " has already been targeted by a stratagem of " + side_label(side) +
               " this phase";
    }
    if (u->battle_shocked) {
        return "01.07: " + unit_label(target) + " is battle-shocked, so its player can't target it with stratagems";
    }
    switch (s) {
        case Stratagem::CommandReroll:
            // 15.02 WHEN: just after one of its rolls (never a battle-shock roll).
            if (w.timing != WindowTiming::AfterRoll || roll_of(w) == RollKind::BattleShock) return when_text(s);
            // 15.09 (D-097): snap shooting's hit rolls are never re-rolled.
            if (roll_of(w) == RollKind::Hit && state.flow.attack && state.flow.attack->context.snap_shooting) {
                return "15.09: you cannot re-roll hit rolls when snap shooting";
            }
            // 10.07 (M101, D-103): "You cannot re-roll hit rolls" of an [INDIRECT FIRE] weapon in indirect shooting.
            if (roll_of(w) == RollKind::Hit && state.flow.attack && w.batch < state.flow.attack->batches.size() &&
                state.flow.attack->batches[w.batch].profile.unmodified_hit_fails_to > 1) {
                return "10.07: you cannot re-roll the hit rolls of an [INDIRECT FIRE] weapon in indirect shooting";
            }
            if (pickable_dice(state, w).empty()) {
                return "D-011: every die of this roll has already been re-rolled (a die is re-rolled only once)";
            }
            return std::nullopt;
        case Stratagem::InsaneBravery:
            // 15.04 WHEN: the Battle-shock step of your Command phase, just before a battle-shock roll.
            if (w.timing != WindowTiming::BeforeRoll || roll_of(w) != RollKind::BattleShock ||
                state.flow.stage != Stage::BattleShock || side != turn_side(state)) {
                return when_text(s);
            }
            return std::nullopt;
        default:
            return when_text(s);
    }
}

// D-097: the WHEN of the stratagems used at the windows that follow no roll (no unit needed).
std::optional<std::string> reaction_when(const GameState& state, Side side, Stratagem s, const StratagemWindow& w) {
    const bool opponents_turn = side != turn_side(state);
    switch (s) {
        case Stratagem::FireOverwatch:
            if (w.timing == WindowTiming::EndOfPhase && state.flow.stage == Stage::EndOfMovementPhase && opponents_turn) {
                return std::nullopt;
            }
            break;
        case Stratagem::HeroicIntervention:
            if (w.timing == WindowTiming::EndOfPhase && state.flow.stage == Stage::EndOfChargePhase && opponents_turn) {
                return std::nullopt;
            }
            break;
        case Stratagem::Counteroffensive:
            if (w.timing == WindowTiming::AfterAttacks && state.flow.stage == Stage::Fight && opponents_turn && w.unit) {
                const Unit* fought = state.find_unit(*w.unit);
                if (fought != nullptr && fought->side != side) return std::nullopt;  // "an enemy unit"
            }
            break;
        case Stratagem::CrushingImpact:
            // D-099: your Charge phase (its Charge step: a heroic charge comes in the opponent's), just after a
            // friendly unit ends a charge move.
            if (w.timing == WindowTiming::AfterChargeMove && state.flow.stage == Stage::Charge && !opponents_turn &&
                w.unit) {
                const Unit* charged = state.find_unit(*w.unit);
                if (charged != nullptr && charged->side == side) return std::nullopt;
            }
            break;
        default:
            break;
    }
    return when_text(s);
}

// D-097: 15.08 TARGET (one friendly unengaged unit, excluding TITANIC units) and what the user's ruling adds: it must be
// able to shoot using snap shooting at something (15.09: one visible eligible enemy unit within 24").
std::optional<std::string> fire_overwatch_problem(const GameState& state, const Unit& u) {
    const std::string who = unit_label(u.id);
    if (is_engaged(state, u)) return "15.08: Fire Overwatch targets an unengaged unit, and " + who + " is engaged";
    if (has_keyword(u, "TITANIC")) return "15.08: Fire Overwatch can't target a TITANIC unit (" + who + ")";
    if (auto p = unit_shoot_problem(state, u)) return p;
    if (!has_shot_option(state, u, ShootingType::Snap)) {
        return "15.09: " + who + " has no visible, eligible enemy unit within 24\" to snap shoot at (D-097)";
    }
    return std::nullopt;
}

// D-097: 15.11 TARGET (one friendly unengaged unit within 12" of one or more enemy units; a VEHICLE only if it is a
// CHARACTER/WALKER), 11.02's eligibility to declare a charge (FAQ Q71), and the user's ruling: the mode can select a
// charge target (Leap to Defend: an enemy unit that made a charge move this phase within 12"; Into the Fray: an enemy
// unit within 6").
std::optional<std::string> heroic_intervention_problem(const GameState& state, const Unit& u, StratagemMode mode) {
    const std::string who = unit_label(u.id);
    if (has_keyword(u, "VEHICLE") && !has_keyword(u, "CHARACTER") && !has_keyword(u, "WALKER")) {
        return "15.11: Heroic Intervention can select a VEHICLE unit only if it is a CHARACTER or WALKER unit (" + who +
               " is not)";
    }
    if (auto p = declare_charge_problem(state, u)) return p;  // 11.02: unengaged, within 12", ...
    const float reach = inches_to_mm(mode == StratagemMode::IntoTheFray ? 6.0f : 12.0f);
    for (const Unit& enemy : state.units) {
        if (enemy.side == u.side || is_destroyed(enemy)) continue;
        if (mode == StratagemMode::LeapToDefend && !enemy.turn_flags.made_charge_move) continue;
        if (const auto d = unit_distance(u, enemy); d && within(*d, reach)) return std::nullopt;
    }
    if (mode == StratagemMode::LeapToDefend) {
        return "15.11: Leap to Defend: no enemy unit that made a charge move this phase is within 12\" of " + who;
    }
    return "15.11: Into the Fray: no enemy unit is within 6\" of " + who;
}

// D-099 (M97): 15.06 TARGET (that MONSTER/VEHICLE unit: the window's) and what D-097's ruling adds: an enemy unit is
// engaged with it, and a model of it engaged with that unit (crushing_impact_pairs).
std::optional<std::string> crushing_impact_problem(const GameState& state, const Unit& u, const StratagemWindow& w) {
    const std::string who = unit_label(u.id);
    if (!w.unit || u.id != *w.unit) {
        return "15.06: Crushing Impact targets the unit that has just ended its charge move (" +
               (w.unit ? unit_label(*w.unit) : std::string("none")) + "), not " + who;
    }
    const bool monster_or_vehicle =
        std::any_of(u.models.begin(), u.models.end(), [&u](const Model& m) { return is_monster_or_vehicle(u, m); });
    if (!monster_or_vehicle) return "15.06: Crushing Impact targets a MONSTER/VEHICLE unit (" + who + " is not)";
    if (crushing_impact_pairs(state, u, true).empty()) {
        return "15.06: no model of " + who + " is engaged with an enemy unit (D-099)";
    }
    return std::nullopt;
}

// D-097: the stratagems used at the windows that follow no roll (15.08, 15.11, 15.12; D-099: 15.06), their target
// `target` (the player's pick) in `mode`.
std::optional<std::string> reaction_problem(const GameState& state, Side side, Stratagem s, const StratagemWindow& w,
                                            UnitId target, std::optional<StratagemMode> mode) {
    if (auto p = reaction_when(state, side, s, w)) return p;
    const std::string name = named(s, mode);
    const std::string rule(stratagem_rule(s));
    const std::size_t i = side_index(side);
    const StratagemRecord& rec = state.stratagems[i];
    const int cost = stratagem_use_cost(s, mode);
    if (state.command_points[i] < cost) {
        return "15.01: " + side_label(side) + " has " + std::to_string(state.command_points[i]) + " CP; " + name +
               " costs " + std::to_string(cost) + "CP";
    }
    if (contains(rec.used_this_phase, s)) {
        return "15.01: " + side_label(side) + " has already used " + std::string(stratagem_name(s)) + " this phase";
    }
    const Unit* u = state.find_unit(target);
    if (u == nullptr || u->side != side || is_destroyed(*u)) {
        return rule + ": " + std::string(stratagem_name(s)) + " targets one of " + side_label(side) + "'s units (" +
               unit_label(target) + " is not)";
    }
    if (contains(rec.targeted_this_phase, target)) {
        return "15.01: " + unit_label(target) + " has already been targeted by a stratagem of " + side_label(side) +
               " this phase";
    }
    if (u->battle_shocked) {
        return "01.07: " + unit_label(target) + " is battle-shocked, so its player can't target it with stratagems";
    }
    switch (s) {
        case Stratagem::FireOverwatch:
            return fire_overwatch_problem(state, *u);
        case Stratagem::HeroicIntervention:
            if (!mode) return std::string("15.11: Heroic Intervention needs its mode: Leap to Defend or Into the Fray");
            return heroic_intervention_problem(state, *u, *mode);
        case Stratagem::Counteroffensive:
            if (!eligible_to_fight(state, *u)) {
                return "15.12: Counteroffensive targets a unit that is eligible to fight (12.04); " + unit_label(target) +
                       " is not";
            }
            return std::nullopt;
        case Stratagem::CrushingImpact:
            return crushing_impact_problem(state, *u, w);
        default:
            return when_text(s);
    }
}

// 15.01 and the stratagem's WHEN / TARGET / RESTRICTIONS: why `side` can't use `s` in the window now (on `target`, in
// `mode`), or nullopt. A roll window's target is its unit; the other windows' is the player's pick.
std::optional<std::string> problem(const GameState& state, Side side, Stratagem s, const StratagemWindow& w,
                                   std::optional<UnitId> target = std::nullopt,
                                   std::optional<StratagemMode> mode = std::nullopt) {
    if (is_roll_stratagem(s) != is_roll_window(w)) return when_text(s);
    if (is_roll_stratagem(s)) return roll_problem(state, side, s, w);
    if (!target) return std::string(stratagem_rule(s)) + ": select the friendly unit " + std::string(stratagem_name(s)) +
                        " targets";
    return reaction_problem(state, side, s, w, *target, mode);
}

// D-097: the friendly units `side` can target with `s` in `mode` in the window, in unit order.
std::vector<UnitId> targets_of(const GameState& state, Side side, Stratagem s, const StratagemWindow& w,
                               std::optional<StratagemMode> mode) {
    std::vector<UnitId> out;
    if (reaction_when(state, side, s, w)) return out;  // cheap: no unit is looked at outside the WHEN
    if (state.command_points[side_index(side)] < stratagem_use_cost(s, mode)) return out;
    for (const Unit& u : state.units) {
        if (u.side == side && !reaction_problem(state, side, s, w, u.id, mode)) out.push_back(u.id);
    }
    return out;
}

bool can_use_any(const GameState& state, Side side, const StratagemWindow& w) {
    for (const Stratagem s : kStratagems) {
        if (is_roll_stratagem(s) != is_roll_window(w)) continue;
        if (is_roll_stratagem(s)) {
            if (!roll_problem(state, side, s, w)) return true;
            continue;
        }
        if (reaction_when(state, side, s, w)) continue;
        for (const std::optional<StratagemMode> mode : modes_of(s)) {
            if (state.command_points[side_index(side)] < stratagem_use_cost(s, mode)) continue;
            for (const Unit& u : state.units) {
                if (u.side == side && !reaction_problem(state, side, s, w, u.id, mode)) return true;
            }
        }
    }
    return false;
}

// 01.03.02: the sides with a stratagem to use in `w`, the active player first.
std::vector<Side> window_sides(const GameState& state, const StratagemWindow& w) {
    return sequence_sides(active_side(state), {can_use_any(state, Side::A, w), can_use_any(state, Side::B, w)});
}

// The roll's consequence, with its dice as they now stand (each roll module's own; M94's code).
void apply_roll(GameState& state, const StratagemWindow& w, Events& events) {
    const RollKind roll = roll_of(w);
    switch (roll) {
        case RollKind::Advance:
            apply_move_roll(state, roll, w.faces);
            return;
        case RollKind::Charge:
            apply_charge_roll(state, w.faces, events);
            return;
        case RollKind::Hazard:
            if (state.flow.attack) {
                apply_attack_roll(state, roll, w.batch, w.faces, events);  // [HAZARDOUS] (24.15)
            } else {
                apply_move_roll(state, roll, w.faces);  // Desperate Escape (09.07)
            }
            return;
        case RollKind::RandomAttacks:
        case RollKind::Hit:
        case RollKind::Wound:
        case RollKind::Save:
        case RollKind::Damage:
            apply_attack_roll(state, roll, w.batch, w.faces, events);
            return;
        case RollKind::BattleShock:
        case RollKind::Explosives:      // D-099: 15.02 lists neither stratagem's roll
        case RollKind::CrushingImpact:
            break;
    }
    throw std::logic_error("D-096: a " + std::string(roll_kind_name(roll)) + " roll has no window after it");
}

// The roll's event, rebuilt from its dice (after a re-roll).
event::RerolledRoll rerolled_result(const GameState& state, const StratagemWindow& w) {
    const UnitId unit = unit_of(w);
    switch (roll_of(w)) {
        case RollKind::Advance:
            return event::AdvanceRolled{unit, w.faces.at(0), false};
        case RollKind::Charge:
            return charge_roll_result(state, unit, w.faces);
        case RollKind::Hazard:
            return event::HazardRollsMade{unit, judge_hazard_rolls(*state.find_unit(unit), w.faces)};
        case RollKind::RandomAttacks:
        case RollKind::Hit:
        case RollKind::Wound:
        case RollKind::Save:
        case RollKind::Damage:
            return attack_roll_result(state, roll_of(w), w.batch, w.faces);
        case RollKind::BattleShock:
        case RollKind::Explosives:  // D-099: never re-rolled (15.02)
        case RollKind::CrushingImpact:
            break;
    }
    throw std::logic_error("D-096: a battle-shock, Explosives or Crushing Impact roll is not re-rolled");
}

// Closes the window: an AfterRoll window applies its roll; a BeforeRoll window offers the next battle-shock test's
// window when Insane Bravery has taken the test (otherwise the test's RollDice follows). D-097: a window that follows
// no roll has nothing to apply.
void close_window(GameState& state, Events& events) {
    const StratagemWindow w = std::move(window_of(state));  // moved out: the window is gone from the state now
    state.flow.window.reset();
    switch (w.timing) {
        case WindowTiming::AfterRoll:
            apply_roll(state, w, events);
            return;
        case WindowTiming::BeforeRoll: {
            const std::vector<UnitId>& owed = state.flow.battle_shock_tests;
            if (!owed.empty() && owed.front() != unit_of(w)) offer_battle_shock_window(state);
            return;
        }
        case WindowTiming::EndOfPhase:
        case WindowTiming::AfterAttacks:
        case WindowTiming::AfterChargeMove:  // D-099
            return;
    }
}

// After an answer: the side keeps the window while it still has a stratagem to use there; then the next side with one
// answers; with none left, the window closes.
void continue_window(GameState& state, bool side_done, Events& events) {
    StratagemWindow& w = window_of(state);
    if (side_done || !can_use_any(state, w.sides.front(), w)) w.sides.erase(w.sides.begin());
    while (!w.sides.empty() && !can_use_any(state, w.sides.front(), w)) w.sides.erase(w.sides.begin());
    if (w.sides.empty()) close_window(state, events);
}

// 15.02 EFFECT: the die picked is rolled again (a charge roll, or a roll of dice added together, in full), before any
// modifier (D-011); the roll's event is rebuilt from the new dice.
void command_reroll(GameState& state, std::optional<std::size_t> die, DiceSource& dice, Events& events) {
    StratagemWindow& w = window_of(state);
    const std::vector<int> before = w.faces;
    const auto [first, last] = reroll_group(state, w, die.value_or(0));
    for (std::size_t k = first; k < last; ++k) {
        w.faces[k] = dice.roll_d6();
        w.rerolled.insert(std::upper_bound(w.rerolled.begin(), w.rerolled.end(), k), k);
    }
    event::DiceRerolled e{roll_of(w), unit_of(w), die, before, w.faces, rerolled_result(state, w)};
    events.push_back(std::move(e));
}

// 15.12 EFFECT: Fights First until the end of the phase, and the next unit its player selects to fight. Used in the
// Resolve Remaining Combats step, play returns to Resolve Fights First Combats at once (D-097, the user's ruling).
void counteroffensive(GameState& state, UnitId unit) {
    state.find_unit(unit)->phase_flags.fights_first = true;
    if (!state.flow.fight_sequence) return;
    FightSequence& seq = *state.flow.fight_sequence;
    seq.next_to_fight = unit;
    if (seq.sub_step == FightSubStep::Remaining) seq.sub_step = FightSubStep::FightsFirst;
}

}  // namespace

// D-099 (M97): 15.05 Explosives, used at the Shoot decision (10.02) of the player's own turn: its WHEN, 15.01's cost and
// records, 01.07, its TARGET (one friendly unengaged EXPLOSIVES/GRENADES unit that is eligible to shoot and did not make
// an advance move this turn) and D-097's ruling (a pair exists). The reasons in that order.
std::optional<std::string> explosives_problem(const GameState& state, Side side, UnitId target) {
    const Stratagem s = Stratagem::Explosives;
    // WHEN: your Shooting phase, at the Shoot step's decision (no unit selected, no attack, window or effect).
    if (state.flow.stage != Stage::Shoot || side != turn_side(state) || state.flow.window || state.flow.effect ||
        state.flow.attack || state.flow.activation || state.flow.snap_shooter) {
        return when_text(s);
    }
    const std::size_t i = side_index(side);
    const StratagemRecord& rec = state.stratagems[i];
    if (state.command_points[i] < stratagem_cost(s)) {
        return "15.01: " + side_label(side) + " has " + std::to_string(state.command_points[i]) + " CP; Explosives costs " +
               std::to_string(stratagem_cost(s)) + "CP";
    }
    if (contains(rec.used_this_phase, s)) return "15.01: " + side_label(side) + " has already used Explosives this phase";
    const Unit* u = state.find_unit(target);
    if (u == nullptr || u->side != side || is_destroyed(*u)) {
        return "15.05: Explosives targets one of " + side_label(side) + "'s units (" + unit_label(target) + " is not)";
    }
    const std::string who = unit_label(target);
    if (contains(rec.targeted_this_phase, target)) {
        return "15.01: " + who + " has already been targeted by a stratagem of " + side_label(side) + " this phase";
    }
    if (u->battle_shocked) return "01.07: " + who + " is battle-shocked, so its player can't target it with stratagems";
    const bool keyword =
        std::any_of(u->models.begin(), u->models.end(), [u](const Model& m) { return is_explosives_or_grenades(*u, m); });
    if (!keyword) return "15.05: Explosives targets an EXPLOSIVES/GRENADES unit (" + who + " is not)";
    if (is_engaged(state, *u)) return "15.05: Explosives targets an unengaged unit, and " + who + " is engaged";
    if (auto p = unit_shoot_problem(state, *u)) return p;  // 10.02 eligible to shoot; 09.07 not after falling back
    if (u->turn_flags.advanced) return "15.05: " + who + " made an advance move this turn";
    if (explosives_pairs(state, *u, true).empty()) {
        return "15.05: no model of " + who + " has an unengaged enemy unit within 8\" and visible to it (D-099)";
    }
    return std::nullopt;
}

std::vector<UnitId> explosives_targets(const GameState& state, Side side) {
    std::vector<UnitId> out;
    // Cheap refusals first: outside the WHEN, without the CP or once used, no unit is looked at.
    const std::size_t i = side_index(side);
    if (state.flow.stage != Stage::Shoot || side != turn_side(state) ||
        state.command_points[i] < stratagem_cost(Stratagem::Explosives) ||
        contains(state.stratagems[i].used_this_phase, Stratagem::Explosives)) {
        return out;
    }
    for (const Unit& u : state.units) {
        if (u.side != side || is_destroyed(u) || u.models.empty() || !is_explosives_or_grenades(u, u.models.front())) {
            continue;
        }
        if (!explosives_problem(state, side, u.id)) out.push_back(u.id);
    }
    return out;
}

// D-096: the open window's roll as its event, judged from its dice now (window_info's result).
event::RerolledRoll window_roll_result(const GameState& state) { return rerolled_result(state, *state.flow.window); }

void after_roll(GameState& state, RollKind roll, UnitId unit, std::size_t batch, std::vector<int> faces,
                std::vector<std::size_t> rerolled, Events& events) {
    StratagemWindow w{WindowTiming::AfterRoll, roll, unit, batch, {}, std::move(faces), std::move(rerolled)};
    w.sides = window_sides(state, w);
    if (w.sides.empty()) {
        apply_roll(state, w, events);  // no side can use a stratagem here: as in M94
        return;
    }
    state.flow.window = std::move(w);
}

void offer_battle_shock_window(GameState& state) {
    if (state.flow.stage != Stage::BattleShock || state.flow.battle_shock_tests.empty()) return;
    StratagemWindow w{WindowTiming::BeforeRoll, RollKind::BattleShock, state.flow.battle_shock_tests.front(), 0, {},
                      {}, {}};
    w.sides = window_sides(state, w);
    if (!w.sides.empty()) state.flow.window = std::move(w);
}

void open_window(GameState& state, WindowTiming timing, std::optional<UnitId> unit) {
    // D-007 comes first: no window once the battle is over, or when a side has no models left (the game then ends).
    if (state.result || !side_has_models(state, Side::A) || !side_has_models(state, Side::B)) return;
    StratagemWindow w{timing, std::nullopt, unit, 0, {}, {}, {}};
    w.sides = window_sides(state, w);
    if (!w.sides.empty()) state.flow.window = std::move(w);
}

std::optional<PendingDecision> window_decision(const GameState& state) {
    if (!state.flow.window) return std::nullopt;
    const StratagemWindow& w = *state.flow.window;
    if (w.sides.empty()) throw std::logic_error("D-096: an open window has no side to answer");
    return PendingDecision{w.sides.front(), decision::UseStratagems{w.timing, w.unit}};
}

void end_phase_stratagems(GameState& state) {
    for (StratagemRecord& r : state.stratagems) {
        r.used_this_phase.clear();  // 15.01: "in the same phase"
        r.targeted_this_phase.clear();
    }
}

std::vector<GameAction> legal_for(const GameState& state, const decision::UseStratagems&) {
    const StratagemWindow& w = window_of(state);
    std::vector<GameAction> out{action::DeclineStratagems{}};  // D-096: the decline first (the rules are optional)
    const Side side = w.sides.front();
    for (const Stratagem s : kStratagems) {
        if (is_roll_stratagem(s) != is_roll_window(w)) continue;
        if (is_roll_stratagem(s)) {
            if (roll_problem(state, side, s, w)) continue;
            if (s == Stratagem::CommandReroll && roll_of(w) != RollKind::Charge) {
                for (const std::size_t die : pickable_dice(state, w)) {
                    out.push_back(action::UseStratagem{s, unit_of(w), die});
                }
            } else {
                out.push_back(action::UseStratagem{s, unit_of(w), std::nullopt});
            }
            continue;
        }
        // D-097: by stratagem, by mode, by friendly unit.
        for (const std::optional<StratagemMode> mode : modes_of(s)) {
            for (const UnitId u : targets_of(state, side, s, w, mode)) {
                out.push_back(action::UseStratagem{s, u, std::nullopt, mode});
            }
        }
    }
    return out;
}

ValidationResult validate_details(const GameState& state, Side side, const action::UseStratagem& a) {
    const std::string name(stratagem_name(a.stratagem));
    const std::string rule(stratagem_rule(a.stratagem));
    if (a.mode && !has_modes(a.stratagem)) return ValidationResult::failure(rule + ": " + name + " has no modes");
    if (!state.flow.window) {
        // D-099 (M97): the Shoot decision (10.02), where only 15.05 Explosives is used.
        if (a.stratagem != Stratagem::Explosives) return ValidationResult::failure(when_text(a.stratagem));
        if (a.die) return ValidationResult::failure("15.05: Explosives picks no die");
        if (auto p = explosives_problem(state, side, a.unit)) return ValidationResult::failure(std::move(*p));
        return ValidationResult::success();
    }
    const StratagemWindow& w = window_of(state);
    if (!is_roll_window(w)) {
        // D-097: a window that follows no roll: the player picks the target, and Heroic Intervention its mode.
        if (a.die) return ValidationResult::failure(rule + ": " + name + " picks no die");
        if (has_modes(a.stratagem) && !a.mode) {
            return ValidationResult::failure("15.11: select Heroic Intervention's mode (Leap to Defend or Into the "
                                             "Fray) before the charge roll");
        }
        if (auto p = problem(state, side, a.stratagem, w, a.unit, a.mode)) return ValidationResult::failure(std::move(*p));
        return ValidationResult::success();
    }
    if (a.unit != unit_of(w)) {
        return ValidationResult::failure("15.01: this window is for " + unit_label(unit_of(w)) + "'s roll, not " +
                                         unit_label(a.unit) + "'s");
    }
    if (auto p = problem(state, side, a.stratagem, w)) return ValidationResult::failure(std::move(*p));
    if (a.stratagem == Stratagem::CommandReroll) {
        if (roll_of(w) == RollKind::Charge) {
            if (a.die) return ValidationResult::failure("15.02: a charge roll is re-rolled in full; no die is picked");
            return ValidationResult::success();
        }
        if (!a.die) return ValidationResult::failure("15.02: pick the die of the roll to re-roll");
        if (*a.die >= w.faces.size()) {
            return ValidationResult::failure("15.02: die " + std::to_string(*a.die) + " is not one of the roll's " +
                                             std::to_string(w.faces.size()) + " dice");
        }
        const auto [first, last] = reroll_group(state, w, *a.die);
        for (std::size_t k = first; k < last; ++k) {
            if (contains(w.rerolled, k)) {
                return ValidationResult::failure("D-011: die " + std::to_string(k) +
                                                 " has already been re-rolled (a die is re-rolled only once)");
            }
        }
        return ValidationResult::success();
    }
    if (a.die) return ValidationResult::failure(rule + ": " + name + " picks no die");
    return ValidationResult::success();
}

ValidationResult validate_details(const GameState&, Side, const action::DeclineStratagems&) {
    return ValidationResult::success();
}

void apply_details(GameState& state, Side side, const action::UseStratagem& a, DiceSource& dice, Events& events) {
    // 15.01: 1. select the target; 2. pay the CP; 3. resolve the effect.
    const std::size_t i = side_index(side);
    StratagemRecord& rec = state.stratagems[i];
    const int cost = stratagem_use_cost(a.stratagem, a.mode);
    state.command_points[i] -= cost;
    rec.used_this_phase.push_back(a.stratagem);
    rec.targeted_this_phase.push_back(a.unit);
    if (once_per_battle(a.stratagem)) rec.used_this_battle.push_back(a.stratagem);
    events.push_back(event::StratagemUsed{side, a.stratagem, a.unit, cost, state.command_points[i], a.mode});
    switch (a.stratagem) {
        case Stratagem::CommandReroll:
            command_reroll(state, a.die, dice, events);
            break;
        case Stratagem::InsaneBravery:
            pass_battle_shock_automatically(state, a.unit, events);
            break;
        case Stratagem::FireOverwatch:
            // 15.08 EFFECT: the unit shoots using snap shooting (the SnapShoot decision follows).
            close_window(state, events);
            state.flow.snap_shooter = a.unit;
            return;
        case Stratagem::HeroicIntervention:
            // 15.11 EFFECT: resolve a charge (11.02) in the mode chosen before the charge roll.
            close_window(state, events);
            declare_charge(state, a.unit, a.mode, events);
            return;
        case Stratagem::Counteroffensive:
            counteroffensive(state, a.unit);
            break;
        case Stratagem::Explosives:
            // 15.05 EFFECT (D-099): the selection, the roll and the mortal wounds follow; then the Shoot decision again.
            // It answers the Shoot decision, so there is no window to continue.
            start_effect(state, Stratagem::Explosives, a.unit);
            return;
        case Stratagem::CrushingImpact:
            // 15.06 EFFECT (D-099): the window closes, as Fire Overwatch's and Heroic Intervention's do; the effect follows.
            close_window(state, events);
            start_effect(state, Stratagem::CrushingImpact, a.unit);
            return;
    }
    continue_window(state, false, events);
}

void apply_details(GameState& state, Side, const action::DeclineStratagems&, DiceSource&, Events& events) {
    continue_window(state, true, events);
}

}  // namespace detail

bool window_pending(const GameState& state) {
    return state.pending.has_value() && std::holds_alternative<decision::UseStratagems>(state.pending->kind);
}

std::optional<WindowInfo> window_info(const GameState& state) {
    if (!window_pending(state) || !state.flow.window) return std::nullopt;
    const StratagemWindow& w = *state.flow.window;
    WindowInfo info;
    info.timing = w.timing;
    info.roll = w.roll;
    info.unit = w.unit;
    info.side = state.pending->side;
    info.command_points = state.command_points[side_index(info.side)];
    info.faces = w.faces;
    if (!detail::is_roll_window(w)) {
        // D-097: one option per stratagem and mode with a target, its use cost, and the units it can target.
        for (const Stratagem s : detail::kStratagems) {
            if (detail::is_roll_stratagem(s)) continue;
            for (const std::optional<StratagemMode> mode : detail::modes_of(s)) {
                std::vector<UnitId> targets = detail::targets_of(state, info.side, s, w, mode);
                if (targets.empty()) continue;
                info.options.push_back(StratagemOption{s, detail::named(s, mode), std::string(stratagem_rule(s)),
                                                       stratagem_use_cost(s, mode), mode, std::move(targets)});
            }
        }
        return info;
    }
    for (const Stratagem s : detail::kStratagems) {
        if (detail::is_roll_stratagem(s) && !detail::roll_problem(state, info.side, s, w)) {
            info.options.push_back(StratagemOption{s, std::string(stratagem_name(s)), std::string(stratagem_rule(s)),
                                                   stratagem_cost(s)});
        }
    }
    const RollKind roll = detail::roll_of(w);
    info.pickable.assign(w.faces.size(), false);
    if (roll != RollKind::Charge) {
        for (const std::size_t die : detail::pickable_dice(state, w)) {
            const auto [first, last] = detail::reroll_group(state, w, die);
            for (std::size_t k = first; k < last; ++k) info.pickable[k] = true;
        }
    }
    info.whole_roll = roll == RollKind::Charge;
    if (w.timing == WindowTiming::AfterRoll) info.result = detail::window_roll_result(state);
    if (w.timing == WindowTiming::BeforeRoll) {
        // The unit's player makes the roll (08.03: the active player's units).
        const Unit* u = state.find_unit(detail::unit_of(w));
        info.next_roll =
            detail::describe_roll(state, decision::RollDice{roll, detail::unit_of(w)}, u != nullptr ? u->side : info.side);
    }
    return info;
}

std::optional<std::string> forecast_stratagem_problem(const GameState& state, Side side, Stratagem stratagem,
                                                      WindowTiming timing, std::optional<UnitId> window_unit,
                                                      UnitId target, std::optional<StratagemMode> mode) {
    // D-100 (M98): the window as open_window would build it (no roll, no dice), and the check its offers come from.
    const StratagemWindow w{timing, std::nullopt, window_unit, 0, {}, {}, {}};
    if (detail::is_roll_stratagem(stratagem) || detail::is_roll_window(w)) return detail::when_text(stratagem);
    // D-007 comes first, as in open_window: no window once the battle is over or a side has no models left.
    if (state.result || !detail::side_has_models(state, Side::A) || !detail::side_has_models(state, Side::B)) {
        return std::string("D-007: the battle is over, so no window opens");
    }
    return detail::reaction_problem(state, side, stratagem, w, target, mode);
}

}  // namespace wh40k::core
