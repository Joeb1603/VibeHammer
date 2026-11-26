#include "core/state/decision.hpp"

#include <type_traits>

#include "core/state/game_state.hpp"
#include "core/state/stratagems.hpp"

namespace wh40k::core {

namespace {

std::string side_label(Side s) { return s == Side::A ? "Side A" : "Side B"; }

// D-095: what the roll is, as "roll ..." reads it.
std::string_view roll_noun(RollKind roll) {
    switch (roll) {
        case RollKind::Advance: return "the advance roll";
        case RollKind::Charge: return "the charge roll";
        case RollKind::BattleShock: return "the battle-shock roll";
        case RollKind::Hazard: return "the hazard rolls";
        case RollKind::RandomAttacks: return "the random attacks";
        case RollKind::Hit: return "the hit rolls";
        case RollKind::Wound: return "the wound rolls";
        case RollKind::Save: return "the save rolls";
        case RollKind::Damage: return "the damage rolls";
        case RollKind::Explosives: return "the Explosives roll";             // D-099 (15.05)
        case RollKind::CrushingImpact: return "the Crushing Impact roll";    // D-099 (15.06)
    }
    return "the roll";
}

}  // namespace

std::string_view roll_kind_name(RollKind roll) {
    switch (roll) {
        case RollKind::Advance: return "Advance";
        case RollKind::Charge: return "Charge";
        case RollKind::BattleShock: return "BattleShock";
        case RollKind::Hazard: return "Hazard";
        case RollKind::RandomAttacks: return "RandomAttacks";
        case RollKind::Hit: return "Hit";
        case RollKind::Wound: return "Wound";
        case RollKind::Save: return "Save";
        case RollKind::Damage: return "Damage";
        case RollKind::Explosives: return "Explosives";          // D-099
        case RollKind::CrushingImpact: return "CrushingImpact";  // D-099
    }
    return "?";
}

std::optional<RollKind> roll_kind_from_name(std::string_view name) {
    for (const RollKind r : {RollKind::Advance, RollKind::Charge, RollKind::BattleShock, RollKind::Hazard,
                             RollKind::RandomAttacks, RollKind::Hit, RollKind::Wound, RollKind::Save,
                             RollKind::Damage, RollKind::Explosives, RollKind::CrushingImpact}) {
        if (roll_kind_name(r) == name) return r;
    }
    return std::nullopt;
}

std::string_view roll_rule(RollKind roll) {
    switch (roll) {
        case RollKind::Advance: return "09.06";
        case RollKind::Charge: return "11.02";
        case RollKind::BattleShock: return "01.07";
        case RollKind::Hazard: return "06.03";
        case RollKind::RandomAttacks: return "04.03";  // D-069
        case RollKind::Hit: return "05.01";
        case RollKind::Wound: return "05.02";
        case RollKind::Save: return "05.03";
        case RollKind::Damage: return "05.04";  // D-069
        case RollKind::Explosives: return "15.05";      // D-099
        case RollKind::CrushingImpact: return "15.06";  // D-099
    }
    return "?";
}

std::string_view window_timing_name(WindowTiming t) {
    switch (t) {
        case WindowTiming::AfterRoll: return "AfterRoll";
        case WindowTiming::BeforeRoll: return "BeforeRoll";
        case WindowTiming::EndOfPhase: return "EndOfPhase";
        case WindowTiming::AfterAttacks: return "AfterAttacks";
        case WindowTiming::AfterChargeMove: return "AfterChargeMove";  // D-099
    }
    return "?";
}

std::optional<WindowTiming> window_timing_from_name(std::string_view name) {
    for (const WindowTiming t : {WindowTiming::AfterRoll, WindowTiming::BeforeRoll, WindowTiming::EndOfPhase,
                                 WindowTiming::AfterAttacks, WindowTiming::AfterChargeMove}) {
        if (window_timing_name(t) == name) return t;
    }
    return std::nullopt;
}

std::string_view decision_rule(const PendingDecision& d) {
    return std::visit(
        [](const auto& k) -> std::string_view {
            using T = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<T, decision::MoveUnits>) return "09.02";
            else if constexpr (std::is_same_v<T, decision::MoveModels>) return "03.01";
            else if constexpr (std::is_same_v<T, decision::Shoot>) return "10.02";
            else if constexpr (std::is_same_v<T, decision::Charge>) return "11.02";
            else if constexpr (std::is_same_v<T, decision::SelectChargeTargets>) return "11.04";
            else if constexpr (std::is_same_v<T, decision::PileIn>) return "12.02";
            else if constexpr (std::is_same_v<T, decision::OverrunPileIn>) return "12.06";
            else if constexpr (std::is_same_v<T, decision::SelectUnitToFight>) return "12.04";
            else if constexpr (std::is_same_v<T, decision::DeclareMeleeAttacks>) return "04.02";
            else if constexpr (std::is_same_v<T, decision::Consolidate>) return "12.07";
            else if constexpr (std::is_same_v<T, decision::ForcedFight>) return "12.08";
            else if constexpr (std::is_same_v<T, decision::RestoreCoherency>) return "03.03";
            else if constexpr (std::is_same_v<T, decision::SelectAttackBatch>) return "04.03";
            else if constexpr (std::is_same_v<T, decision::DeclareAllocationOrder>) return "05.03";
            else if constexpr (std::is_same_v<T, decision::OrderCasualties>) return "05.04";
            else if constexpr (std::is_same_v<T, decision::RerollWoundRolls>) return "24.38";
            else if constexpr (std::is_same_v<T, decision::DeployUnits>) return "03.02";  // D-081
            else if constexpr (std::is_same_v<T, decision::RetargetAttacks>) return "D-093";  // 04.03.03 (app-only)
            else if constexpr (std::is_same_v<T, decision::RollDice>) return roll_rule(k.roll);  // D-095
            else if constexpr (std::is_same_v<T, decision::UseStratagems>) return "15.01";  // D-096
            else if constexpr (std::is_same_v<T, decision::SnapShoot>) return "15.09";      // D-097
            else if constexpr (std::is_same_v<T, decision::SelectModelAndEnemy>) return stratagem_rule(k.stratagem);
            else static_assert(sizeof(T) == 0, "unhandled decision");
        },
        d.kind);
}

std::string_view decision_name(const DecisionKind& kind) {
    return std::visit(
        [](const auto& k) -> std::string_view {
            using T = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<T, decision::MoveUnits>) return "MoveUnits";
            else if constexpr (std::is_same_v<T, decision::MoveModels>) return "MoveModels";
            else if constexpr (std::is_same_v<T, decision::Shoot>) return "Shoot";
            else if constexpr (std::is_same_v<T, decision::Charge>) return "Charge";
            else if constexpr (std::is_same_v<T, decision::SelectChargeTargets>) return "SelectChargeTargets";
            else if constexpr (std::is_same_v<T, decision::PileIn>) return "PileIn";
            else if constexpr (std::is_same_v<T, decision::OverrunPileIn>) return "OverrunPileIn";
            else if constexpr (std::is_same_v<T, decision::SelectUnitToFight>) return "SelectUnitToFight";
            else if constexpr (std::is_same_v<T, decision::DeclareMeleeAttacks>) return "DeclareMeleeAttacks";
            else if constexpr (std::is_same_v<T, decision::Consolidate>) return "Consolidate";
            else if constexpr (std::is_same_v<T, decision::ForcedFight>) return "ForcedFight";
            else if constexpr (std::is_same_v<T, decision::RestoreCoherency>) return "RestoreCoherency";
            else if constexpr (std::is_same_v<T, decision::SelectAttackBatch>) return "SelectAttackBatch";
            else if constexpr (std::is_same_v<T, decision::DeclareAllocationOrder>) return "DeclareAllocationOrder";
            else if constexpr (std::is_same_v<T, decision::OrderCasualties>) return "OrderCasualties";
            else if constexpr (std::is_same_v<T, decision::RerollWoundRolls>) return "RerollWoundRolls";
            else if constexpr (std::is_same_v<T, decision::DeployUnits>) return "DeployUnits";
            else if constexpr (std::is_same_v<T, decision::RetargetAttacks>) return "RetargetAttacks";
            else if constexpr (std::is_same_v<T, decision::RollDice>) return "RollDice";
            else if constexpr (std::is_same_v<T, decision::UseStratagems>) return "UseStratagems";
            else if constexpr (std::is_same_v<T, decision::SnapShoot>) return "SnapShoot";
            else if constexpr (std::is_same_v<T, decision::SelectModelAndEnemy>) return "SelectModelAndEnemy";
            else static_assert(sizeof(T) == 0, "unhandled decision");
        },
        kind);
}

namespace {

std::string what_to_do(const PendingDecision& d) {
    return std::visit(
        [](const auto& k) -> std::string {
            using T = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<T, decision::MoveUnits>) {
                return "select a unit to move";
            } else if constexpr (std::is_same_v<T, decision::MoveModels>) {
                return "move the models of unit " + std::to_string(k.unit.v) + ", then commit or reset the move";
            } else if constexpr (std::is_same_v<T, decision::Shoot>) {
                return "select a unit to shoot, or end the Shoot step";
            } else if constexpr (std::is_same_v<T, decision::Charge>) {
                return "declare a charge, or end the Charge step";
            } else if constexpr (std::is_same_v<T, decision::SelectChargeTargets>) {
                return "select charge targets for unit " + std::to_string(k.unit.v) + ", or make no charge move";
            } else if constexpr (std::is_same_v<T, decision::PileIn>) {
                return "make a pile-in move, or end pile-in moves";
            } else if constexpr (std::is_same_v<T, decision::OverrunPileIn>) {
                return "make unit " + std::to_string(k.unit.v) + "'s overrun pile-in move, or skip it";
            } else if constexpr (std::is_same_v<T, decision::SelectUnitToFight>) {
                return "select a unit to fight";
            } else if constexpr (std::is_same_v<T, decision::DeclareMeleeAttacks>) {
                return "select melee weapons and targets for unit " + std::to_string(k.unit.v);
            } else if constexpr (std::is_same_v<T, decision::Consolidate>) {
                return "make a consolidation move, or end consolidation moves";
            } else if constexpr (std::is_same_v<T, decision::ForcedFight>) {
                return "select a unit engaged by an engaging consolidation to fight";
            } else if constexpr (std::is_same_v<T, decision::RestoreCoherency>) {
                return "remove a model from unit " + std::to_string(k.unit.v) + " to restore coherency";
            } else if constexpr (std::is_same_v<T, decision::SelectAttackBatch>) {
                return "select the next attacks to resolve for unit " + std::to_string(k.attacker.v);
            } else if constexpr (std::is_same_v<T, decision::DeclareAllocationOrder>) {
                return "declare the allocation order for unit " + std::to_string(k.target.v);
            } else if constexpr (std::is_same_v<T, decision::OrderCasualties>) {
                return "order the casualties for unit " + std::to_string(k.target.v);
            } else if constexpr (std::is_same_v<T, decision::RerollWoundRolls>) {
                return "choose the wound rolls to re-roll for unit " + std::to_string(k.attacker.v) +
                       " ([TWIN-LINKED])";
            } else if constexpr (std::is_same_v<T, decision::DeployUnits>) {
                return "set up one of your units";  // D-081: the sides alternate
            } else if constexpr (std::is_same_v<T, decision::RetargetAttacks>) {
                return "select new targets for unit " + std::to_string(k.attacker.v) +
                       "'s attacks whose target is gone";  // D-093
            } else if constexpr (std::is_same_v<T, decision::RollDice>) {
                return "make " + std::string(roll_noun(k.roll)) + " for unit " + std::to_string(k.unit.v);  // D-095
            } else if constexpr (std::is_same_v<T, decision::UseStratagems>) {
                // D-096: a reaction window, just after or just before a roll for the unit. D-097: at the end of a
                // phase, or just after an enemy unit has fought.
                const std::string unit = k.unit ? std::to_string(k.unit->v) : std::string("?");
                switch (k.timing) {
                    case WindowTiming::AfterRoll:
                        return "use a stratagem after a roll for unit " + unit + ", or decline";
                    case WindowTiming::BeforeRoll:
                        return "use a stratagem before a roll for unit " + unit + ", or decline";
                    case WindowTiming::EndOfPhase:
                        return std::string("use a stratagem at the end of the phase, or decline");
                    case WindowTiming::AfterAttacks:
                        return "use a stratagem now that unit " + unit + " has fought, or decline";
                    case WindowTiming::AfterChargeMove:  // D-099
                        return "use a stratagem now that unit " + unit + " has ended its charge move, or decline";
                }
                return std::string("use a stratagem, or decline");
            } else if constexpr (std::is_same_v<T, decision::SnapShoot>) {
                return "shoot with unit " + std::to_string(k.unit.v) + " using snap shooting";  // D-097
            } else if constexpr (std::is_same_v<T, decision::SelectModelAndEnemy>) {
                // D-099: 15.05 / 15.06 EFFECT steps 1-2.
                return "select a model of unit " + std::to_string(k.unit.v) + " and an enemy unit for " +
                       std::string(stratagem_name(k.stratagem));
            } else {
                static_assert(sizeof(T) == 0, "unhandled decision");
            }
        },
        d.kind);
}

}  // namespace

std::string describe(const PendingDecision& d) {
    return side_label(d.side) + ": " + what_to_do(d) + " (" + std::string(decision_rule(d)) + ")";
}

std::string_view decision_rule(const GameState& state, const PendingDecision& d) {
    if (std::holds_alternative<decision::OrderCasualties>(d.kind)) {
        // 06.02 Select Model for mortal wounds: [HAZARDOUS] after a unit's attacks (24.15), or Desperate Escape (09.07)
        // or a stratagem's effect (D-099: 15.05, 15.06) outside an attack; otherwise 05.04 for an attack's save rolls.
        if (!state.flow.attack || state.flow.attack->pending_mortal_wounds > 0) return "06.02";
        return "05.04";
    }
    if (const auto* r = std::get_if<decision::RollDice>(&d.kind)) {
        // D-095: why the roll is made, where its own rule (roll_rule) doesn't say.
        if (r->roll == RollKind::Hazard) return state.flow.attack ? "24.15" : "09.07";
        if (r->roll == RollKind::BattleShock) return state.flow.stage == Stage::BattleShock ? "08.03" : "09.07";
    }
    if (const auto* w = std::get_if<decision::UseStratagems>(&d.kind)) {
        // D-096: the stratagem the window's timing is for: just after a roll, Command Re-roll; just before the 08.03
        // battle-shock roll, Insane Bravery. D-097: at the end of the Movement phase Fire Overwatch, at the end of the
        // Charge phase Heroic Intervention, after an enemy unit's fight Counteroffensive.
        switch (w->timing) {
            case WindowTiming::AfterRoll: return "15.02";
            case WindowTiming::BeforeRoll: return "15.04";
            case WindowTiming::EndOfPhase: return state.flow.stage == Stage::EndOfChargePhase ? "15.11" : "15.08";
            case WindowTiming::AfterAttacks: return "15.12";
            case WindowTiming::AfterChargeMove: return "15.06";  // D-099: Crushing Impact
        }
    }
    return decision_rule(d);
}

std::string describe(const GameState& state, const PendingDecision& d) {
    return side_label(d.side) + ": " + what_to_do(d) + " (" + std::string(decision_rule(state, d)) + ")";
}

}  // namespace wh40k::core
