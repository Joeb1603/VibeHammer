// 08 Command phase (08.02 Gain Core CP, 08.03 Battle-shock) and the End of Turn step's Regaining
// Coherency (03.03).

#include <algorithm>
#include <string>
#include <vector>

#include "core/engine/engine.hpp"
#include "core/engine/internal.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/leadership.hpp"
#include "core/rules/unit_strength.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core::detail {

namespace {

// 03.03 Regaining Coherency: the first unit on the battlefield that is not in coherency, the turn
// player's units first, then the opponent's, each in unit order (the order cannot change the outcome).
const Unit* first_incoherent_unit(const GameState& state) {
    const Side turn = turn_side(state);
    for (const Side side : {turn, opponent(turn)}) {
        for (const Unit& u : state.units) {
            if (u.side == side && !is_destroyed(u) && !unit_coherency(u).ok()) return &u;
        }
    }
    return nullptr;
}

}  // namespace

void gain_core_cp(GameState& state, Events& events) {
    for (const Side side : {Side::A, Side::B}) {
        int& cp = state.command_points[side_index(side)];
        cp += 1;  // 08.02: both players gain 1 CP
        events.push_back(event::CommandPointGained{side, cp});
    }
}

void battle_shock_step(GameState& state, DiceSource& dice, Events& events) {
    const Side active = turn_side(state);  // 01.03: no unit is selected in the Command phase
    for (Unit& u : state.units) {
        if (u.side != active || is_destroyed(u)) continue;
        // 08.03: units that are battle-shocked, or at or below half-strength (Appendix).
        const bool was_shocked = u.battle_shocked;
        if (!was_shocked && !at_or_below_half_strength(u)) continue;
        const BattleShockRoll roll = make_battle_shock_roll(dice, u);  // one roll per unit (D-015)
        apply_battle_shock_roll(u, roll);
        events.push_back(event::BattleShockRolled{u.id, roll});
        if (was_shocked && roll.leadership.success) {
            u.battle_shocked = false;  // 08.03: it is no longer battle-shocked
            events.push_back(event::BattleShockEnded{u.id});
        }
    }
}

std::optional<PendingDecision> coherency_decision(const GameState& state) {
    if (const Unit* u = first_incoherent_unit(state)) {
        return PendingDecision{u->side, decision::RestoreCoherency{u->id}};  // its controlling player
    }
    return std::nullopt;
}

std::vector<GameAction> legal_for(const GameState& state, const decision::RestoreCoherency& d) {
    std::vector<GameAction> out;
    for (const Model& m : state.find_unit(d.unit)->models) {
        out.push_back(action::RemoveModelForCoherency{d.unit, m.id});
    }
    return out;
}

ValidationResult validate_details(const GameState& state, Side, const action::RemoveModelForCoherency& a) {
    const auto& d = std::get<decision::RestoreCoherency>(state.pending->kind);
    if (a.unit != d.unit) {
        return ValidationResult::failure("03.03: models are being removed from " + unit_label(d.unit) + ", not " +
                                         unit_label(a.unit));
    }
    if (find_model(*state.find_unit(a.unit), a.model) == nullptr) {
        return ValidationResult::failure("03.03: model " + std::to_string(a.model.v) + " is not in " +
                                         unit_label(a.unit));
    }
    return ValidationResult::success();
}

void apply_details(GameState& state, Side, const action::RemoveModelForCoherency& a, DiceSource&, Events& events) {
    // 03.03: removed models are destroyed, but they do not trigger rules that apply when a model is
    // destroyed. A unit of one model is always in coherency, so the unit is never emptied.
    Unit& u = *state.find_unit(a.unit);
    u.models.erase(std::find_if(u.models.begin(), u.models.end(), [&](const Model& m) { return m.id == a.model; }));
    events.push_back(event::ModelRemovedForCoherency{a.unit, a.model});
}

}  // namespace wh40k::core::detail
