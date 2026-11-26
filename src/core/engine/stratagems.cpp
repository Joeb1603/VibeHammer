// D-096 (M95): reaction windows (01.03.02 Rules Sequencing, app-only) and the stratagems used in them (15): spending
// CP (15.01), Command Re-roll (15.02, just after a roll) and Insane Bravery (15.04, just before a battle-shock roll).
//
// Every roll point rolls its dice, emits its event (as in M94), then calls after_roll: a window opens there if a side
// can use a stratagem (the roll waits in FlowState::window), otherwise the roll is applied at once. Closing the window
// applies the roll (apply_roll), with the dice as they stand after any re-roll. Declining changes nothing, so a game in
// which every window is declined is exactly the M94 game (the closure, D-096 part 6).

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"
#include "core/model/keywords.hpp"

namespace wh40k::core {

namespace detail {

namespace {

constexpr std::array<Stratagem, 2> kStratagems{Stratagem::CommandReroll, Stratagem::InsaneBravery};

// TEMPORARY (M95 step 3, removed in step 5): windows stay closed while the roll points are refactored, so the whole
// suite checks the refactor alone.
constexpr bool kOpenWindows = false;

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

// 15.02 / 01.05.02: the dice a pick of die `die` re-rolls, as [first, last): a charge roll in full; a characteristic
// roll made by adding several dice together (a random A or D) whole; any other die alone.
std::pair<std::size_t, std::size_t> reroll_group(const GameState& state, const StratagemWindow& w, std::size_t die) {
    switch (w.roll) {
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

// 15.01 and the stratagem's WHEN / TARGET / RESTRICTIONS: why `side` can't use `s` in the window now, or nullopt.
std::optional<std::string> problem(const GameState& state, Side side, Stratagem s, const StratagemWindow& w) {
    const std::string name(stratagem_name(s));
    const std::string rule(stratagem_rule(s));
    const std::size_t i = side_index(side);
    const StratagemRecord& rec = state.stratagems[i];
    const Unit* u = state.find_unit(w.unit);
    if (u == nullptr || u->side != side || is_destroyed(*u)) {
        return rule + ": " + name + " targets " + side_label(side) + "'s own unit (" + unit_label(w.unit) + " is not)";
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
    if (contains(rec.targeted_this_phase, w.unit)) {
        return "15.01: " + unit_label(w.unit) + " has already been targeted by a stratagem of " + side_label(side) +
               " this phase";
    }
    if (u->battle_shocked) {
        return "01.07: " + unit_label(w.unit) + " is battle-shocked, so its player can't target it with stratagems";
    }
    switch (s) {
        case Stratagem::CommandReroll:
            // 15.02 WHEN: just after one of its rolls (never a battle-shock roll).
            if (w.timing != WindowTiming::AfterRoll || w.roll == RollKind::BattleShock) {
                return "15.02: Command Re-roll is used just after an advance, charge, damage, hazard, hit, save or "
                       "wound roll, or a roll for the number of attacks";
            }
            if (pickable_dice(state, w).empty()) {
                return "01.05.02: every die of this roll has already been re-rolled (a die is re-rolled only once)";
            }
            return std::nullopt;
        case Stratagem::InsaneBravery:
            // 15.04 WHEN: the Battle-shock step of your Command phase, just before a battle-shock roll.
            if (w.timing != WindowTiming::BeforeRoll || w.roll != RollKind::BattleShock ||
                state.flow.stage != Stage::BattleShock || side != turn_side(state)) {
                return "15.04: Insane Bravery is used in the Battle-shock step of your Command phase, just before a "
                       "battle-shock roll";
            }
            return std::nullopt;
    }
    return "15.01: unknown stratagem";
}

bool can_use_any(const GameState& state, Side side, const StratagemWindow& w) {
    return std::any_of(kStratagems.begin(), kStratagems.end(),
                       [&](Stratagem s) { return !problem(state, side, s, w); });
}

// 01.03.02: the sides with a stratagem to use in `w`, the active player first.
std::vector<Side> window_sides(const GameState& state, const StratagemWindow& w) {
    return sequence_sides(active_side(state), {can_use_any(state, Side::A, w), can_use_any(state, Side::B, w)});
}

// The roll's consequence, with its dice as they now stand (each roll module's own; M94's code).
void apply_roll(GameState& state, const StratagemWindow& w, Events& events) {
    switch (w.roll) {
        case RollKind::Advance:
            apply_move_roll(state, w.roll, w.faces);
            return;
        case RollKind::Charge:
            apply_charge_roll(state, w.faces, events);
            return;
        case RollKind::Hazard:
            if (state.flow.attack) {
                apply_attack_roll(state, w.roll, w.batch, w.faces, events);  // [HAZARDOUS] (24.15)
            } else {
                apply_move_roll(state, w.roll, w.faces);  // Desperate Escape (09.07)
            }
            return;
        case RollKind::RandomAttacks:
        case RollKind::Hit:
        case RollKind::Wound:
        case RollKind::Save:
        case RollKind::Damage:
            apply_attack_roll(state, w.roll, w.batch, w.faces, events);
            return;
        case RollKind::BattleShock:
            break;
    }
    throw std::logic_error("D-096: a " + std::string(roll_kind_name(w.roll)) + " roll has no window after it");
}

// The roll's event, rebuilt from its dice (after a re-roll).
event::RerolledRoll rerolled_result(const GameState& state, const StratagemWindow& w) {
    switch (w.roll) {
        case RollKind::Advance:
            return event::AdvanceRolled{w.unit, w.faces.at(0), false};
        case RollKind::Charge:
            return charge_roll_result(state, w.unit, w.faces);
        case RollKind::Hazard:
            return event::HazardRollsMade{w.unit, judge_hazard_rolls(*state.find_unit(w.unit), w.faces)};
        case RollKind::RandomAttacks:
        case RollKind::Hit:
        case RollKind::Wound:
        case RollKind::Save:
        case RollKind::Damage:
            return attack_roll_result(state, w.roll, w.batch, w.faces);
        case RollKind::BattleShock:
            break;
    }
    throw std::logic_error("D-096: a battle-shock roll is not re-rolled");
}

// Closes the window: an AfterRoll window applies its roll; a BeforeRoll window offers the next battle-shock test's
// window when Insane Bravery has taken the test (otherwise the test's RollDice follows).
void close_window(GameState& state, Events& events) {
    const StratagemWindow w = window_of(state);
    state.flow.window.reset();
    if (w.timing == WindowTiming::AfterRoll) {
        apply_roll(state, w, events);
        return;
    }
    const std::vector<UnitId>& owed = state.flow.battle_shock_tests;
    if (!owed.empty() && owed.front() != w.unit) offer_battle_shock_window(state);
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
    event::DiceRerolled e{w.roll, w.unit, die, before, w.faces, rerolled_result(state, w)};
    events.push_back(std::move(e));
}

}  // namespace

void after_roll(GameState& state, RollKind roll, UnitId unit, std::size_t batch, std::vector<int> faces,
                std::vector<std::size_t> rerolled, Events& events) {
    StratagemWindow w{WindowTiming::AfterRoll, roll, unit, batch, {}, std::move(faces), std::move(rerolled)};
    if (kOpenWindows) w.sides = window_sides(state, w);
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
    if (kOpenWindows) w.sides = window_sides(state, w);
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
        if (problem(state, side, s, w)) continue;
        if (s == Stratagem::CommandReroll && w.roll != RollKind::Charge) {
            for (const std::size_t die : pickable_dice(state, w)) out.push_back(action::UseStratagem{s, w.unit, die});
        } else {
            out.push_back(action::UseStratagem{s, w.unit, std::nullopt});
        }
    }
    return out;
}

ValidationResult validate_details(const GameState& state, Side side, const action::UseStratagem& a) {
    const StratagemWindow& w = window_of(state);
    const std::string name(stratagem_name(a.stratagem));
    if (a.unit != w.unit) {
        return ValidationResult::failure("15.01: this window is for " + unit_label(w.unit) + "'s roll, not " +
                                         unit_label(a.unit) + "'s");
    }
    if (auto p = problem(state, side, a.stratagem, w)) return ValidationResult::failure(std::move(*p));
    if (a.stratagem == Stratagem::CommandReroll) {
        if (w.roll == RollKind::Charge) {
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
                return ValidationResult::failure("01.05.02: die " + std::to_string(k) +
                                                 " has already been re-rolled (a die is re-rolled only once)");
            }
        }
        return ValidationResult::success();
    }
    if (a.die) return ValidationResult::failure(std::string(stratagem_rule(a.stratagem)) + ": " + name + " picks no die");
    return ValidationResult::success();
}

ValidationResult validate_details(const GameState&, Side, const action::DeclineStratagems&) {
    return ValidationResult::success();
}

void apply_details(GameState& state, Side side, const action::UseStratagem& a, DiceSource& dice, Events& events) {
    // 15.01: 1. select the target (the window's unit); 2. pay the CP; 3. resolve the effect.
    const std::size_t i = side_index(side);
    StratagemRecord& rec = state.stratagems[i];
    const int cost = stratagem_cost(a.stratagem);
    state.command_points[i] -= cost;
    rec.used_this_phase.push_back(a.stratagem);
    rec.targeted_this_phase.push_back(a.unit);
    if (once_per_battle(a.stratagem)) rec.used_this_battle.push_back(a.stratagem);
    events.push_back(event::StratagemUsed{side, a.stratagem, a.unit, cost, state.command_points[i]});
    switch (a.stratagem) {
        case Stratagem::CommandReroll:
            command_reroll(state, a.die, dice, events);
            break;
        case Stratagem::InsaneBravery:
            pass_battle_shock_automatically(state, a.unit, events);
            break;
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
    for (const Stratagem s : detail::kStratagems) {
        if (!detail::problem(state, info.side, s, w)) {
            info.options.push_back(StratagemOption{s, std::string(stratagem_name(s)), std::string(stratagem_rule(s)),
                                                   stratagem_cost(s)});
        }
    }
    info.pickable.assign(w.faces.size(), false);
    if (w.roll != RollKind::Charge) {
        for (const std::size_t die : detail::pickable_dice(state, w)) {
            const auto [first, last] = detail::reroll_group(state, w, die);
            for (std::size_t k = first; k < last; ++k) info.pickable[k] = true;
        }
    }
    info.whole_roll = w.roll == RollKind::Charge;
    return info;
}

}  // namespace wh40k::core
