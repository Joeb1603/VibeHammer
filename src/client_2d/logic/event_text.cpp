#include "event_text.hpp"

#include <string>
#include <type_traits>
#include <variant>

#include "core/geometry/units.hpp"
#include "core/model/dice_expr.hpp"
#include "move_plan.hpp"

namespace wh40k::client {

namespace {

namespace ev = core::event;

std::string side(core::Side s) { return s == core::Side::A ? "Side A" : "Side B"; }
std::string unit(core::UnitId id) { return "unit " + std::to_string(id.v); }
std::string model(core::ModelId id) { return "model " + std::to_string(id.v); }

// Dice faces: "!" marks a critical, "x" a failure.
std::string hit_rolls(const core::HitRolls& r) {
    std::string out;
    for (const auto& roll : r.rolls) {
        if (!out.empty()) out += ' ';
        if (roll.outcome == core::HitOutcome::AutoHit) {
            out += "auto";
            continue;
        }
        out += std::to_string(*roll.unmodified);
        if (roll.modified && *roll.modified != *roll.unmodified) out += "(" + std::to_string(*roll.modified) + ")";
        if (roll.outcome == core::HitOutcome::CriticalHit) out += '!';
        if (roll.outcome == core::HitOutcome::Fail) out += 'x';
    }
    return out;
}

std::string wound_rolls(const core::WoundRolls& r) {
    std::string out;
    for (const auto& roll : r.rolls) {
        if (!out.empty()) out += ' ';
        out += std::to_string(roll.unmodified);
        if (roll.modified != roll.unmodified) out += "(" + std::to_string(roll.modified) + ")";
        if (roll.outcome == core::WoundOutcome::CriticalWound) out += '!';
        if (roll.outcome == core::WoundOutcome::Fail) out += 'x';
    }
    return out;
}

// One die's roll: "3", "5(6)" when modified, with "!" for a critical and "x" for a failure.
std::string wound_roll(const core::WoundRoll& roll) {
    std::string out = std::to_string(roll.unmodified);
    if (roll.modified != roll.unmodified) out += "(" + std::to_string(roll.modified) + ")";
    if (roll.outcome == core::WoundOutcome::CriticalWound) out += '!';
    if (roll.outcome == core::WoundOutcome::Fail) out += 'x';
    return out;
}

std::string ints(const std::vector<int>& v) {
    std::string out;
    for (const int x : v) {
        if (!out.empty()) out += ' ';
        out += std::to_string(x);
    }
    return out;
}

std::string body(const core::GameEvent& e) {
    return std::visit(
        [](const auto& x) -> std::string {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, ev::BattleRoundStarted>) {
                return "Battle round " + std::to_string(x.round) + " starts";
            } else if constexpr (std::is_same_v<T, ev::BattleRoundEnded>) {
                return "Battle round " + std::to_string(x.round) + " ends";
            } else if constexpr (std::is_same_v<T, ev::TurnStarted>) {
                return side(x.side) + "'s turn starts";
            } else if constexpr (std::is_same_v<T, ev::TurnEnded>) {
                return side(x.side) + "'s turn ends";
            } else if constexpr (std::is_same_v<T, ev::PhaseStarted>) {
                return std::string(core::phase_name(x.phase)) + " phase starts";
            } else if constexpr (std::is_same_v<T, ev::PhaseEnded>) {
                return std::string(core::phase_name(x.phase)) + " phase ends";
            } else if constexpr (std::is_same_v<T, ev::UnitRemainedStationary>) {
                return unit(x.unit) + " remains stationary";
            } else if constexpr (std::is_same_v<T, ev::MoveStarted>) {
                std::string text = unit(x.unit) + " starts a " + std::string(core::move_type_name(x.move_type)) +
                                   " move";
                if (x.mode) text += " (" + std::string(core::fall_back_mode_name(*x.mode)) + ")";
                return text;
            } else if constexpr (std::is_same_v<T, ev::AdvanceRolled>) {
                return "Advance roll: " + std::to_string(x.roll) + (x.reused ? " (kept from earlier, D-005)" : "");
            } else if constexpr (std::is_same_v<T, ev::HazardRollsMade>) {
                return "Hazard rolls: " + ints(x.rolls.rolls) + " -> " + std::to_string(x.rolls.mortal_wounds) +
                       " mortal wound(s)";
            } else if constexpr (std::is_same_v<T, ev::MortalWoundsInflicted>) {
                return std::to_string(x.result.steps.size()) + " mortal wound(s) on " + unit(x.unit) + ", " +
                       std::to_string(x.result.models_destroyed) + " destroyed";
            } else if constexpr (std::is_same_v<T, ev::ModelMoveProposed>) {
                // D-074 part 3: "turns 45° then moves 3.00\"" when the path turns the model (no facings: as before).
                if (x.facings.empty()) return model(x.model) + " moves " + core::format_inches(x.distance_mm);
                return model(x.model) + " " + describe_path(x.path, x.facings, true);
            } else if constexpr (std::is_same_v<T, ev::UnitMoveCommitted>) {
                return unit(x.unit) + " ends its " + std::string(core::move_type_name(x.move_type)) + " move";
            } else if constexpr (std::is_same_v<T, ev::UnitMoveReverted>) {
                return unit(x.unit) + "'s move is undone" +
                       (x.reasons.empty() ? std::string(" (reset)") : ": " + x.reasons.front());
            } else if constexpr (std::is_same_v<T, ev::BattleShockRolled>) {
                return "Battle-shock roll for " + unit(x.unit) + ": " +
                       std::to_string(x.roll.leadership.roll.total()) + " vs " +
                       std::to_string(x.roll.leadership.target) + "+ -> " +
                       (x.roll.becomes_battle_shocked ? "battle-shocked" : "passed");
            } else if constexpr (std::is_same_v<T, ev::ShootingStarted>) {
                return unit(x.unit) + " shoots (" + std::string(core::shooting_type_name(x.type)) + " shooting, " +
                       std::to_string(x.selections.size()) + " weapon(s))";
            } else if constexpr (std::is_same_v<T, ev::ChargeDeclared>) {
                return unit(x.unit) + " declares a charge";
            } else if constexpr (std::is_same_v<T, ev::ChargeRolled>) {
                std::string targets;
                for (const auto t : x.eligible_targets) targets += (targets.empty() ? "" : ", ") + unit(t);
                return "Charge roll: " + ints({x.roll.dice[0], x.roll.dice[1]}) + " = " +
                       std::to_string(x.roll.total()) + "\" -> " +
                       (targets.empty() ? std::string("no unit can be a charge target") : "can charge " + targets);
            } else if constexpr (std::is_same_v<T, ev::ChargeTargetsSelected>) {
                std::string targets;
                for (const auto t : x.targets) targets += (targets.empty() ? "" : ", ") + unit(t);
                return unit(x.unit) + " charges " + targets;
            } else if constexpr (std::is_same_v<T, ev::ChargeResolved>) {
                return x.charge_move_made ? unit(x.unit) + " completes its charge (Fights First until end of turn)"
                                          : "Charge fails: " + x.reason;
            } else if constexpr (std::is_same_v<T, ev::PileInStarted>) {
                std::string targets;
                for (const auto t : x.targets) targets += (targets.empty() ? "" : ", ") + unit(t);
                return unit(x.unit) + " piles in towards " + targets;
            } else if constexpr (std::is_same_v<T, ev::UnitSelectedToFight>) {
                return unit(x.unit) + " is selected to fight (" + std::string(core::fight_type_name(x.type)) +
                       " fight" + (x.forced ? ", forced by an Engaging Consolidation)" : ")");
            } else if constexpr (std::is_same_v<T, ev::FightPassed>) {
                return side(x.side) + " passes";
            } else if constexpr (std::is_same_v<T, ev::MeleeAttacksDeclared>) {
                return x.selections.empty() ? unit(x.unit) + " has no model that can make melee attacks"
                                            : unit(x.unit) + " fights with " + std::to_string(x.selections.size()) +
                                                  " weapon(s)";
            } else if constexpr (std::is_same_v<T, ev::FightStepEnded>) {
                return "Fight step ends: " + x.reason;
            } else if constexpr (std::is_same_v<T, ev::ConsolidationStarted>) {
                return unit(x.unit) + " consolidates (" + std::string(core::consolidation_mode_name(x.mode)) + ")";
            } else if constexpr (std::is_same_v<T, ev::UnitsForcedToFight>) {
                std::string units;
                for (const auto u : x.units) units += (units.empty() ? "" : ", ") + unit(u);
                return units + " must now fight (Engaging Consolidation)";
            } else if constexpr (std::is_same_v<T, ev::CommandPointGained>) {
                return side(x.side) + " gains 1 CP (" + std::to_string(x.total) + " CP)";
            } else if constexpr (std::is_same_v<T, ev::BattleShockEnded>) {
                return unit(x.unit) + " is no longer battle-shocked";
            } else if constexpr (std::is_same_v<T, ev::ModelRemovedForCoherency>) {
                return model(x.model) + " of " + unit(x.unit) + " is removed to restore coherency (destroyed)";
            } else if constexpr (std::is_same_v<T, ev::AttackBatchesGathered>) {
                int dice = 0;
                std::vector<core::RandomAttackDice> random;  // D-069: rolled when each batch is resolved
                for (const auto& b : x.batches) {
                    dice += b.attack_dice;
                    random.insert(random.end(), b.random_attacks.begin(), b.random_attacks.end());
                }
                return unit(x.attacker) + " attacks: " + std::to_string(x.batches.size()) + " batch(es), " +
                       attack_count_text(dice, random) + " attack dice";
            } else if constexpr (std::is_same_v<T, ev::AttackBatchSelected>) {
                return "Batch " + std::to_string(x.batch) + ": " + std::to_string(x.attack_dice) + " dice at " +
                       unit(x.target);
            } else if constexpr (std::is_same_v<T, ev::HitRollsMade>) {
                std::string text = "Hit rolls: " + hit_rolls(x.rolls) + " -> " + std::to_string(x.rolls.hits()) + " hits";
                if (x.rolls.sustained_hits > 0) {
                    text += " (" + std::to_string(x.rolls.sustained_hits) + " from [SUSTAINED HITS])";  // 24.36
                }
                return text;
            } else if constexpr (std::is_same_v<T, ev::WoundRollsMade>) {
                return "Wound rolls (" + std::to_string(x.rolls.required) + "+): " + wound_rolls(x.rolls) + " -> " +
                       std::to_string(x.rolls.wounds()) + " wounds";
            } else if constexpr (std::is_same_v<T, ev::AllocationOrderDeclared>) {
                return "Allocation order declared for " + unit(x.target) + " (" + std::to_string(x.groups.size()) +
                       " group(s))";
            } else if constexpr (std::is_same_v<T, ev::SaveRollsMade>) {
                return "Save rolls for " + unit(x.target) + ": " + ints(x.rolls);
            } else if constexpr (std::is_same_v<T, ev::SavesResolved>) {
                int failed = 0;
                std::string rolls;  // D-069: each random D, rolled once its model was selected
                for (const auto& s : x.result.steps) {
                    failed += s.damage ? 1 : 0;
                    if (!s.damage || !s.damage_roll) continue;
                    rolls += (rolls.empty() ? "" : ", ") + std::to_string(s.damage->damage) + " (rolled " +
                             ints(s.damage_roll->dice) + ") on " + model(s.damage->model) +
                             (s.damage->destroyed ? " (destroyed)" : "");
                }
                return std::to_string(failed) + " failed save(s), " + std::to_string(x.result.models_destroyed) +
                       " destroyed" +
                       (x.result.attacks_lost > 0 ? ", " + std::to_string(x.result.attacks_lost) + " attacks lost"
                                                  : std::string()) +
                       (rolls.empty() ? std::string() : "; damage rolls: " + rolls);
            } else if constexpr (std::is_same_v<T, ev::ModelDestroyed>) {
                return model(x.model) + " of " + unit(x.unit) + " is destroyed";
            } else if constexpr (std::is_same_v<T, ev::UnitDestroyed>) {
                return unit(x.unit) + " is destroyed";
            } else if constexpr (std::is_same_v<T, ev::AttacksLost>) {
                // D-069: a random A not rolled yet is lost unrolled.
                return attack_count_text(x.attack_dice, x.random_attacks) + " attacks against " + unit(x.target) +
                       " are lost" + (x.random_attacks.empty() ? "" : " (not rolled)");
            } else if constexpr (std::is_same_v<T, ev::GameOver>) {
                if (x.points) {
                    // D-076: "Battle round 5 ends: Side A wins on points destroyed (412.5 to 380)", the winner's first.
                    const core::PointsTally& p = *x.points;
                    const std::string round = "Battle round " + std::to_string(p.battle_round) + " ends: ";
                    if (!x.winner) return round + "a draw on points destroyed (" + p.display[0] + " each)";
                    const std::size_t w = core::side_index(*x.winner);
                    return round + side(*x.winner) + " wins on points destroyed (" + p.display[w] + " to " +
                           p.display[1 - w] + ")";
                }
                return "Game over: " + x.reason;
            } else if constexpr (std::is_same_v<T, ev::WoundRollsRerolled>) {
                // 24.38 (D-072): each re-rolled die, old -> new, then the batch's new total.
                const std::string total = std::to_string(x.rolls.wounds()) + " wound" + (x.rolls.wounds() == 1 ? "" : "s");
                if (x.rerolls.empty()) return "Wound rolls kept, none re-rolled ([TWIN-LINKED]) -> " + total;
                std::string dice;
                for (const core::WoundReroll& r : x.rerolls) {
                    dice += (dice.empty() ? "" : ", ") + wound_roll(r.before) + " -> " + wound_roll(r.after);
                }
                return "Wound re-rolls ([TWIN-LINKED]): " + dice + " -> " + total;
            } else if constexpr (std::is_same_v<T, ev::AttackDiceRolled>) {
                // 04.03, D-069: each model's random A, its bonus dice added after.
                std::string rolls;
                for (const core::AttackDiceRoll& r : x.rolls) {
                    rolls += (rolls.empty() ? "" : "; ") + model(r.model) + " " + characteristic_roll_text(r.attacks, r.roll);
                    if (r.bonus > 0) rolls += ", +" + std::to_string(r.bonus) + " = " + std::to_string(r.total());
                }
                return "Attack dice (batch " + std::to_string(x.batch) + ", random A): " + rolls + " -> " +
                       std::to_string(x.attack_dice) + " attack dice";
            } else {
                static_assert(sizeof(T) == 0, "unhandled event");
            }
        },
        e);
}

}  // namespace

std::string characteristic_roll_text(const core::DiceExpr& expr, const core::CharacteristicRoll& roll) {
    std::string terms;
    for (const int face : roll.dice) {
        if (!terms.empty()) terms += '+';
        // 01.05: a D3 is a D6 halved, rounding up; the D6 is shown in brackets.
        terms += expr.sides == 3 ? std::to_string((face + 1) / 2) + "[" + std::to_string(face) + "]" : std::to_string(face);
    }
    if (expr.modifier > 0) terms += (terms.empty() ? "" : "+") + std::to_string(expr.modifier);
    return core::to_string(expr) + " -> " + terms + " = " + std::to_string(roll.value);
}

std::string attack_count_text(int attack_dice, const std::vector<core::RandomAttackDice>& random) {
    int known = attack_dice;
    for (const core::RandomAttackDice& r : random) known += r.bonus;
    std::string out = random.empty() || known > 0 ? std::to_string(known) : std::string();
    for (const core::RandomAttackDice& r : random) out += (out.empty() ? "" : " + ") + core::to_string(r.attacks);
    return out;
}

bool is_roll_event(const core::GameEvent& event) {
    if (const auto* r = std::get_if<ev::WoundRollsRerolled>(&event)) return !r->rerolls.empty();  // 24.38: dice re-rolled
    if (std::holds_alternative<ev::AttackDiceRolled>(event)) return true;  // D-069: a random A
    if (const auto* s = std::get_if<ev::SavesResolved>(&event)) {
        // D-069: a random D's damage rolls.
        for (const auto& step : s->result.steps) {
            if (step.damage_roll) return true;
        }
        return false;
    }
    return std::holds_alternative<ev::HitRollsMade>(event) || std::holds_alternative<ev::WoundRollsMade>(event) ||
           std::holds_alternative<ev::SaveRollsMade>(event) || std::holds_alternative<ev::AdvanceRolled>(event) ||
           std::holds_alternative<ev::ChargeRolled>(event) || std::holds_alternative<ev::HazardRollsMade>(event) ||
           std::holds_alternative<ev::BattleShockRolled>(event);
}

std::string format_event(const core::GameEvent& event) {
    return "[" + std::string(core::event_rule(event)) + "] " + body(event);
}

}  // namespace wh40k::client
