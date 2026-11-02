#include "event_text.hpp"

#include <string>
#include <type_traits>
#include <variant>

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
            } else if constexpr (std::is_same_v<T, ev::AttackBatchesGathered>) {
                int dice = 0;
                for (const auto& b : x.batches) dice += b.attack_dice;
                return unit(x.attacker) + " attacks: " + std::to_string(x.batches.size()) + " batch(es), " +
                       std::to_string(dice) + " attack dice";
            } else if constexpr (std::is_same_v<T, ev::AttackBatchSelected>) {
                return "Batch " + std::to_string(x.batch) + ": " + std::to_string(x.attack_dice) + " dice at " +
                       unit(x.target);
            } else if constexpr (std::is_same_v<T, ev::HitRollsMade>) {
                return "Hit rolls: " + hit_rolls(x.rolls) + " -> " + std::to_string(x.rolls.hits()) + " hits";
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
                for (const auto& s : x.result.steps) failed += s.damage ? 1 : 0;
                return std::to_string(failed) + " failed save(s), " + std::to_string(x.result.models_destroyed) +
                       " destroyed" +
                       (x.result.attacks_lost > 0 ? ", " + std::to_string(x.result.attacks_lost) + " attacks lost"
                                                  : std::string());
            } else if constexpr (std::is_same_v<T, ev::ModelDestroyed>) {
                return model(x.model) + " of " + unit(x.unit) + " is destroyed";
            } else if constexpr (std::is_same_v<T, ev::UnitDestroyed>) {
                return unit(x.unit) + " is destroyed";
            } else if constexpr (std::is_same_v<T, ev::AttacksLost>) {
                return std::to_string(x.attack_dice) + " attacks against " + unit(x.target) + " are lost";
            } else if constexpr (std::is_same_v<T, ev::GameOver>) {
                return "Game over: " + x.reason;
            } else {
                static_assert(sizeof(T) == 0, "unhandled event");
            }
        },
        e);
}

}  // namespace

std::string format_event(const core::GameEvent& event) {
    return "[" + std::string(core::event_rule(event)) + "] " + body(event);
}

}  // namespace wh40k::client
