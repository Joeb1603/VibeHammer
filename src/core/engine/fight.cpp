// 12 Fight phase: the Pile In step (12.02), the Fight step's alternation (12.04, D-020) with passing
// (Appendix: Eligible to Fight, but Unable to Fight), units selected to fight (12.04-12.06, melee
// declarations D-021, resolved by the attack flow), the Consolidate step (12.07) and the units an
// Engaging Consolidation forces to fight (12.08). Pile-in and consolidation moves run through the
// movement sub-machine (engine/movement.cpp).

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/charge.hpp"
#include "core/rules/fight.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core::detail {

namespace {

// Every non-empty subset of `ids` (at most 2^10 - 1), in bit order.
std::vector<std::vector<UnitId>> nonempty_subsets(const std::vector<UnitId>& ids) {
    std::vector<std::vector<UnitId>> out;
    const std::size_t n = std::min<std::size_t>(ids.size(), 10);
    for (std::size_t mask = 1; mask < (std::size_t{1} << n); ++mask) {
        std::vector<UnitId> s;
        for (std::size_t i = 0; i < n; ++i) {
            if ((mask >> i) & 1u) s.push_back(ids[i]);
        }
        out.push_back(std::move(s));
    }
    return out;
}

bool has_pile_in_option(const GameState& state, const Unit& u) {
    return eligible_to_pile_in(state, u) && !u.phase_flags.piled_in && !pile_in_target_options(state, u).empty();
}

template <typename Pred>
bool side_has(const GameState& state, Side side, Pred pred) {
    return std::any_of(state.units.begin(), state.units.end(),
                       [&](const Unit& u) { return u.side == side && pred(u); });
}

// 12.02 / 12.07: the player whose turn it is resolves all of their moves first, followed by their
// opponent. `has_option` says whether a unit can make the step's move now.
template <typename Pred>
std::optional<Side> step_side(const GameState& state, Pred has_option) {
    const Side turn = turn_side(state);
    for (const Side s : {turn, opponent(turn)}) {
        if (!state.flow.step_done[side_index(s)] && side_has(state, s, has_option)) return s;
    }
    return std::nullopt;
}

// Once the opponent acts in a Pile In or Consolidate step, the turn player's moves are over.
void note_step_actor(GameState& state, Side side) {
    const Side turn = turn_side(state);
    if (side != turn) state.flow.step_done[side_index(turn)] = true;
}

bool can_select_to_fight(const GameState& state, Side side, FightSubStep sub_step) {
    return side_has(state, side, [&](const Unit& u) {
        return eligible_to_fight(state, u) && (sub_step == FightSubStep::Remaining || is_fights_first_unit(u));
    });
}

bool any_fights_first_eligible(const GameState& state) {
    return std::any_of(state.units.begin(), state.units.end(),
                       [&](const Unit& u) { return eligible_to_fight(state, u) && is_fights_first_unit(u); });
}

bool any_eligible(const GameState& state, std::optional<Side> side = std::nullopt) {
    return std::any_of(state.units.begin(), state.units.end(), [&](const Unit& u) {
        return (!side || u.side == *side) && eligible_to_fight(state, u);
    });
}

// 12.08 Eligible If: remember every unit that is eligible to fight at some point this phase.
void refresh_fight_eligibility(GameState& state) {
    for (Unit& u : state.units) {
        if (eligible_to_fight(state, u)) u.phase_flags.eligible_to_fight_this_phase = true;
    }
}

void end_fight_step(GameState& state, std::string reason, Events& events) {
    const bool anything = std::any_of(state.units.begin(), state.units.end(),
                                      [](const Unit& u) { return u.phase_flags.eligible_to_fight_this_phase; });
    if (anything) events.push_back(event::FightStepEnded{std::move(reason)});
    state.flow.fight_sequence.reset();
}

std::string side_name(Side s) { return side_label(s); }

void finish_fight(GameState& state) {
    const FightInProgress f = *state.flow.fight;
    const Side side = state.find_unit(f.unit)->side;
    state.flow.fight.reset();
    state.flow.activation.reset();
    if (!f.forced && state.flow.fight_sequence) {
        FightSequence& seq = *state.flow.fight_sequence;
        seq.selecting = opponent(side);  // 12.04: players alternate
        seq.last_pass.reset();
        if (seq.sub_step == FightSubStep::Remaining && any_fights_first_eligible(state)) {
            seq.sub_step = FightSubStep::FightsFirst;  // 12.04; D-020: the alternation continues
        }
    }
    if (state.flow.stage == Stage::Fight) refresh_fight_eligibility(state);
}

}  // namespace

// ---- stage entry, decisions and automatic progress ----

void on_enter_fight_stage(GameState& state, Events&) {
    switch (state.flow.stage) {
        case Stage::StartOfFightPhase:
            for (Unit& u : state.units) {
                UnitPhaseFlags& f = u.phase_flags;
                f.piled_in = f.engaged_at_fight_step_start = f.selected_to_fight = false;
                f.eligible_to_fight_this_phase = f.selected_for_overrun = f.forced_to_fight = false;
                f.consolidated = false;
            }
            state.flow.fight_sequence.reset();
            state.flow.fight.reset();
            state.flow.forced_fights.clear();
            break;
        case Stage::PileIn:
        case Stage::Consolidate:
            state.flow.step_done = {false, false};
            break;
        case Stage::Fight:
            for (Unit& u : state.units) {
                u.phase_flags.engaged_at_fight_step_start = !is_destroyed(u) && is_engaged(state, u);
            }
            refresh_fight_eligibility(state);
            // 12.04 Resolve Fights First Combats, starting with the player whose turn it is.
            state.flow.fight_sequence = FightSequence{FightSubStep::FightsFirst, turn_side(state), std::nullopt};
            break;
        default:
            break;
    }
}

std::optional<PendingDecision> fight_stage_decision(const GameState& state) {
    switch (state.flow.stage) {
        case Stage::PileIn:
            if (const auto s = step_side(state, [&](const Unit& u) { return has_pile_in_option(state, u); })) {
                return PendingDecision{*s, decision::PileIn{}};
            }
            return std::nullopt;
        case Stage::Fight: {
            if (!state.flow.fight_sequence) return std::nullopt;
            const FightSequence& seq = *state.flow.fight_sequence;
            if (can_select_to_fight(state, seq.selecting, seq.sub_step)) {
                return PendingDecision{seq.selecting, decision::SelectUnitToFight{}};
            }
            return std::nullopt;  // fight_stage_progress applies 12.04's "if this is not possible"
        }
        case Stage::Consolidate:
            if (const auto s = step_side(state, [&](const Unit& u) { return eligible_to_consolidate(state, u); })) {
                return PendingDecision{*s, decision::Consolidate{}};
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

bool fight_stage_progress(GameState& state, Events& events) {
    if (state.flow.stage != Stage::Fight || !state.flow.fight_sequence) return false;
    FightSequence& seq = *state.flow.fight_sequence;
    const Side s = seq.selecting;
    const Side other = opponent(s);
    const bool other_just_passed = seq.last_pass == other;
    if (seq.sub_step == FightSubStep::FightsFirst) {
        if (!any_fights_first_eligible(state)) {
            seq.sub_step = FightSubStep::Remaining;  // 12.04: this player selects their next unit there
        } else if (other_just_passed) {
            seq.sub_step = FightSubStep::Remaining;  // D-020: a pass can't stall the Fights First step
        } else {
            seq.selecting = other;
        }
        return true;
    }
    if (!any_eligible(state)) {
        end_fight_step(state, "12.04: no units are eligible to fight", events);
    } else if (other_just_passed) {
        end_fight_step(state,
                       "Appendix: " + side_name(other) + " passed and " + side_name(s) +
                           " has no units eligible to fight",
                       events);
    } else {
        seq.selecting = other;
    }
    return true;
}

// ---- 12.02 Pile In ----

std::vector<GameAction> legal_for(const GameState& state, const decision::PileIn&) {
    const Side side = state.pending ? state.pending->side : turn_side(state);
    std::vector<GameAction> out{action::EndPileIns{}};
    for (const Unit& u : state.units) {
        if (u.side != side || !has_pile_in_option(state, u)) continue;
        const std::vector<UnitId> options = pile_in_target_options(state, u);
        if (is_engaged(state, u)) {
            out.push_back(action::StartPileIn{u.id, options});
        } else {
            for (auto& s : nonempty_subsets(options)) out.push_back(action::StartPileIn{u.id, std::move(s)});
        }
    }
    return out;
}

std::vector<GameAction> legal_for(const GameState& state, const decision::OverrunPileIn& d) {
    const Unit& u = *state.find_unit(d.unit);
    std::vector<GameAction> out;
    const std::vector<UnitId> options = pile_in_target_options(state, u);
    if (is_engaged(state, u)) {
        out.push_back(action::StartPileIn{u.id, options});
    } else {
        for (auto& s : nonempty_subsets(options)) out.push_back(action::StartPileIn{u.id, std::move(s)});
    }
    out.push_back(action::SkipPileIn{u.id});
    return out;
}

ValidationResult validate_details(const GameState& state, Side side, const action::StartPileIn& a) {
    const Unit* u = state.find_unit(a.unit);
    if (u == nullptr || u->side != side || is_destroyed(*u)) {
        return ValidationResult::failure("12.02: " + unit_label(a.unit) + " is not a friendly unit on the battlefield");
    }
    if (const auto* d = std::get_if<decision::OverrunPileIn>(&state.pending->kind)) {
        if (a.unit != d->unit) {
            return ValidationResult::failure("12.06: the overrunning unit is " + unit_label(d->unit));
        }
    } else {
        if (!eligible_to_pile_in(state, *u)) {
            return ValidationResult::failure("12.03: " + unit_label(a.unit) +
                                             " is not eligible to pile in (it is unengaged and did not make a charge "
                                             "move this turn)");
        }
        if (u->phase_flags.piled_in) {
            return ValidationResult::failure("12.02: " + unit_label(a.unit) +
                                             " has already made a pile-in move this step");
        }
    }
    if (auto p = pile_in_targets_problem(state, *u, a.targets)) return ValidationResult::failure(std::move(*p));
    return ValidationResult::success();
}

void apply_details(GameState& state, Side side, const action::StartPileIn& a, DiceSource&, Events& events) {
    const Unit& u = *state.find_unit(a.unit);
    if (!state.flow.fight) {
        note_step_actor(state, side);
        state.flow.activation = Activation{u.id, ActivationKind::Move};  // 01.03: selected to move
    }
    events.push_back(event::PileInStarted{u.id, a.targets});
    events.push_back(event::MoveStarted{u.id, MoveType::PileIn, std::nullopt});
    MoveInProgress mv;
    mv.unit = u.id;
    mv.type = MoveType::PileIn;
    mv.targets = a.targets;  // 12.03 Before Moving
    for (const Model& m : u.models) mv.starts.push_back(ModelStart{m.id, m.position});
    state.flow.move = std::move(mv);
}

ValidationResult validate_details(const GameState& state, Side, const action::SkipPileIn& a) {
    const auto& d = std::get<decision::OverrunPileIn>(state.pending->kind);
    if (a.unit != d.unit) return ValidationResult::failure("12.06: the overrunning unit is " + unit_label(d.unit));
    return ValidationResult::success();
}

void apply_details(GameState& state, Side, const action::SkipPileIn&, DiceSource&, Events&) {
    state.flow.fight->overrun_pile_in_pending = false;  // 12.06: "can make" the additional pile-in move
}

ValidationResult validate_details(const GameState&, Side, const action::EndPileIns&) {
    return ValidationResult::success();
}

void apply_details(GameState& state, Side side, const action::EndPileIns&, DiceSource&, Events&) {
    note_step_actor(state, side);
    state.flow.step_done[side_index(side)] = true;
}

// ---- 12.04 Fight: select a unit to fight, or pass ----

std::vector<GameAction> legal_for(const GameState& state, const decision::SelectUnitToFight&) {
    const FightSequence& seq = *state.flow.fight_sequence;
    std::vector<GameAction> out;
    for (const Unit& u : state.units) {
        if (u.side != seq.selecting || !eligible_to_fight(state, u)) continue;
        if (seq.sub_step == FightSubStep::FightsFirst && !is_fights_first_unit(u)) continue;
        for (const FightType t : {FightType::Normal, FightType::Overrun}) {
            if (!fight_type_problem(state, u, t)) out.push_back(action::SelectToFight{u.id, t});
        }
    }
    if (may_pass_fight(state, seq.selecting)) out.push_back(action::PassFight{});
    return out;
}

std::vector<GameAction> legal_for(const GameState& state, const decision::ForcedFight&) {
    std::vector<GameAction> out;
    for (const UnitId id : state.flow.forced_fights) {
        const Unit& u = *state.find_unit(id);
        for (const FightType t : {FightType::Normal, FightType::Overrun}) {
            if (!fight_type_problem(state, u, t)) out.push_back(action::SelectToFight{u.id, t});
        }
    }
    return out;
}

ValidationResult validate_details(const GameState& state, Side side, const action::SelectToFight& a) {
    const Unit* u = state.find_unit(a.unit);
    if (u == nullptr || u->side != side) {
        return ValidationResult::failure("12.04: " + unit_label(a.unit) + " is not a friendly unit");
    }
    if (std::holds_alternative<decision::ForcedFight>(state.pending->kind)) {
        const auto& forced = state.flow.forced_fights;
        if (std::find(forced.begin(), forced.end(), a.unit) == forced.end()) {
            return ValidationResult::failure("12.08: " + unit_label(a.unit) +
                                             " is not one of the units that must now be selected to fight");
        }
    } else if (state.flow.fight_sequence->sub_step == FightSubStep::FightsFirst && !is_fights_first_unit(*u)) {
        return ValidationResult::failure("12.04: only Fights First units can be selected in the Resolve Fights First "
                                         "Combats step");
    }
    if (auto p = fight_type_problem(state, *u, a.type)) return ValidationResult::failure(std::move(*p));
    return ValidationResult::success();
}

void apply_details(GameState& state, Side, const action::SelectToFight& a, DiceSource&, Events& events) {
    const bool forced = std::holds_alternative<decision::ForcedFight>(state.pending->kind);
    Unit& u = *state.find_unit(a.unit);
    u.phase_flags.selected_to_fight = true;  // 12.04: that unit is selected to fight
    u.phase_flags.eligible_to_fight_this_phase = true;
    if (a.type == FightType::Overrun) u.phase_flags.selected_for_overrun = true;  // 12.03 Eligible If
    if (forced) {
        auto& list = state.flow.forced_fights;
        list.erase(std::find(list.begin(), list.end(), a.unit));
    } else {
        state.flow.fight_sequence->last_pass.reset();
    }
    state.flow.activation = Activation{u.id, ActivationKind::Fight};  // 01.03
    state.flow.fight = FightInProgress{u.id, a.type, a.type == FightType::Overrun, false, forced};
    events.push_back(event::UnitSelectedToFight{u.id, a.type, forced});
}

ValidationResult validate_details(const GameState& state, Side side, const action::PassFight&) {
    if (!may_pass_fight(state, side)) {
        return ValidationResult::failure("12.04: " + side_label(side) +
                                         " cannot pass while a unit of theirs that is eligible to fight is within 5\" "
                                         "of an enemy unit (Appendix: Eligible to Fight, but Unable to Fight)");
    }
    return ValidationResult::success();
}

void apply_details(GameState& state, Side side, const action::PassFight&, DiceSource&, Events& events) {
    events.push_back(event::FightPassed{side});
    FightSequence& seq = *state.flow.fight_sequence;
    const Side other = opponent(side);
    if (seq.last_pass == other) {
        end_fight_step(state, "Appendix: both players passed in succession", events);
    } else if (!any_eligible(state, other)) {
        end_fight_step(state,
                       "Appendix: " + side_label(side) + " passed and " + side_label(other) +
                           " has no units eligible to fight",
                       events);
    } else {
        seq.selecting = other;  // the sequence returns to the opponent
        seq.last_pass = side;
    }
}

// ---- a unit selected to fight (12.04-12.06) ----

std::optional<PendingDecision> fight_decision(const GameState& state) {
    const FightInProgress& f = *state.flow.fight;
    const Unit& u = *state.find_unit(f.unit);
    if (f.attacks_started) return std::nullopt;
    if (f.overrun_pile_in_pending) {
        if (eligible_to_pile_in(state, u) && !pile_in_target_options(state, u).empty()) {
            return PendingDecision{u.side, decision::OverrunPileIn{u.id}};  // 12.06
        }
        return std::nullopt;
    }
    if (can_make_melee_attacks(state, u)) return PendingDecision{u.side, decision::DeclareMeleeAttacks{u.id}};
    return std::nullopt;
}

void fight_progress(GameState& state, Events& events) {
    FightInProgress& f = *state.flow.fight;
    if (f.attacks_started) {
        finish_fight(state);  // 04.03: its attacks have been resolved
    } else if (f.overrun_pile_in_pending) {
        f.overrun_pile_in_pending = false;  // no pile-in move is possible
    } else {
        events.push_back(event::MeleeAttacksDeclared{f.unit, {}});  // no model can make a melee attack
        finish_fight(state);
    }
}

std::vector<GameAction> legal_for(const GameState&, const decision::DeclareMeleeAttacks& d) {
    return {action::Fight{d.unit, {}}};  // template: the declarations are open (D-021)
}

ValidationResult validate_details(const GameState& state, Side, const action::Fight& a) {
    const auto& d = std::get<decision::DeclareMeleeAttacks>(state.pending->kind);
    if (a.unit != d.unit) {
        return ValidationResult::failure("04.02: the unit fighting is " + unit_label(d.unit) + ", not " +
                                         unit_label(a.unit));
    }
    ValidationResult r;
    r.reasons = melee_problems(state, *state.find_unit(a.unit), a.declarations);
    return r;
}

void apply_details(GameState& state, Side, const action::Fight& a, DiceSource&, Events& events) {
    std::vector<WeaponSelection> selections = make_melee_selections(state, a.declarations);
    events.push_back(event::MeleeAttacksDeclared{a.unit, selections});
    state.flow.fight->attacks_started = true;
    start_attacks(state, a.unit, AttackContext{AttackType::Fighting, false}, std::move(selections), events);
}

// ---- 12.07 Consolidate ----

std::vector<GameAction> legal_for(const GameState& state, const decision::Consolidate&) {
    const Side side = state.pending ? state.pending->side : turn_side(state);
    std::vector<GameAction> out{action::EndConsolidations{}};
    for (const Unit& u : state.units) {
        if (u.side != side || !eligible_to_consolidate(state, u)) continue;
        const std::vector<UnitId> options = consolidation_target_options(state, u);
        if (consolidation_mode(state, u) == ConsolidationMode::Ongoing) {
            out.push_back(action::StartConsolidation{u.id, options});
        } else {
            for (auto& s : nonempty_subsets(options)) out.push_back(action::StartConsolidation{u.id, std::move(s)});
        }
    }
    return out;
}

ValidationResult validate_details(const GameState& state, Side side, const action::StartConsolidation& a) {
    const Unit* u = state.find_unit(a.unit);
    if (u == nullptr || u->side != side || is_destroyed(*u)) {
        return ValidationResult::failure("12.07: " + unit_label(a.unit) + " is not a friendly unit on the battlefield");
    }
    if (u->phase_flags.consolidated) {
        return ValidationResult::failure("12.07: " + unit_label(a.unit) +
                                         " has already made a consolidation move this step");
    }
    if (!u->phase_flags.eligible_to_fight_this_phase) {
        return ValidationResult::failure("12.08: " + unit_label(a.unit) + " was not eligible to fight this phase");
    }
    if (auto p = consolidation_targets_problem(state, *u, a.targets)) return ValidationResult::failure(std::move(*p));
    return ValidationResult::success();
}

void apply_details(GameState& state, Side side, const action::StartConsolidation& a, DiceSource&, Events& events) {
    note_step_actor(state, side);
    const Unit& u = *state.find_unit(a.unit);
    const ConsolidationMode mode = *consolidation_mode(state, u);  // 12.08: the first mode that applies
    state.flow.activation = Activation{u.id, ActivationKind::Move};
    events.push_back(event::ConsolidationStarted{u.id, mode, a.targets});
    events.push_back(event::MoveStarted{u.id, MoveType::Consolidation, std::nullopt});
    MoveInProgress mv;
    mv.unit = u.id;
    mv.type = MoveType::Consolidation;
    mv.targets = a.targets;
    mv.consolidation_mode = mode;
    for (const Model& m : u.models) mv.starts.push_back(ModelStart{m.id, m.position});
    state.flow.move = std::move(mv);
}

ValidationResult validate_details(const GameState&, Side, const action::EndConsolidations&) {
    return ValidationResult::success();
}

void apply_details(GameState& state, Side side, const action::EndConsolidations&, DiceSource&, Events&) {
    note_step_actor(state, side);
    state.flow.step_done[side_index(side)] = true;
}

// ---- 12.08 forced fights ----

std::optional<PendingDecision> forced_fight_decision(const GameState& state) {
    const Unit& u = *state.find_unit(state.flow.forced_fights.front());
    return PendingDecision{u.side, decision::ForcedFight{}};  // "your opponent must select each of those units"
}

// ---- pile-in and consolidation moves that ended (called by the movement sub-machine) ----

void fight_move_committed(GameState& state, const MoveInProgress& mv, Events& events) {
    Unit& u = *state.find_unit(mv.unit);
    if (mv.type == MoveType::PileIn) {
        if (state.flow.fight && state.flow.fight->unit == u.id && state.flow.fight->overrun_pile_in_pending) {
            state.flow.fight->overrun_pile_in_pending = false;  // 12.06: the additional pile-in move
        } else {
            u.phase_flags.piled_in = true;  // 12.02: one pile-in move per unit this step
        }
    } else if (mv.type == MoveType::Consolidation) {
        u.phase_flags.consolidated = true;
        if (mv.consolidation_mode == ConsolidationMode::Engaging) {
            // 12.08 Engaging Consolidation After Moving: each engaged enemy unit that has not been selected to
            // fight this phase becomes eligible to fight and must be selected to fight, one at a time.
            std::vector<UnitId> forced;
            for (const UnitId e : engaged_enemy_units(state, u)) {
                Unit& enemy = *state.find_unit(e);
                if (enemy.phase_flags.selected_to_fight) continue;
                enemy.phase_flags.forced_to_fight = true;
                enemy.phase_flags.eligible_to_fight_this_phase = true;
                forced.push_back(e);
            }
            if (!forced.empty()) {
                events.push_back(event::UnitsForcedToFight{forced});
                state.flow.forced_fights.insert(state.flow.forced_fights.end(), forced.begin(), forced.end());
            }
        }
    }
    if (state.flow.stage == Stage::Fight) refresh_fight_eligibility(state);
}

}  // namespace wh40k::core::detail
