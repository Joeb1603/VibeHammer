#include "normal/normal_bot.hpp"

#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "analysis/odds.hpp"
#include "normal/charge.hpp"
#include "normal/defaults.hpp"
#include "normal/defense.hpp"
#include "normal/fight.hpp"
#include "normal/movement.hpp"
#include "normal/shooting.hpp"
#include "normal/stratagems.hpp"
#include "unit_style.hpp"

namespace wh40k::ai {

namespace {

using core::GameAction;
using core::GameState;

PolicyResult single(GameAction a, std::string reason) { return PolicyResult{std::move(a), std::move(reason), {}}; }

bool is_fight_move(const GameState& gs) {
    return gs.flow.move &&
           (gs.flow.move->type == core::MoveType::PileIn || gs.flow.move->type == core::MoveType::Consolidation);
}

// The bot's own answer, or nullopt for the defaults'.
std::optional<PolicyResult> normal_answer(const GameState& gs, core::Side side, const std::vector<GameAction>& legal,
                                          std::uint64_t seed, const normal::MoveWeights& weights) {
    using namespace core;
    analysis::OddsCache cache;  // per decision: the bot keeps no state between decisions but its plan memo
    const DecisionKind& kind = gs.pending->kind;
    // M95 (D-096): a reaction window, only with a stratagem key on (otherwise the session declines it).
    if (std::holds_alternative<decision::UseStratagems>(kind)) return normal::decide_stratagems(cache, gs, side, weights);
    if (std::holds_alternative<decision::Shoot>(kind)) {
        // M87 (D-086): the objective part of the aim, under a mission.
        const double fire = normal::objectives_on(gs, weights) ? weights.objective_fire : 0.0;
        // M91 (D-092): the decision's terrain (its sight, shared by the aim, the plan and the hold-fire score).
        std::optional<normal::ShootTerrain> terrain;
        if (normal::cover_on(gs, weights)) terrain = normal::ShootTerrain{std::make_shared<analysis::TerrainSight>(gs), weights};
        std::vector<UnitId> held;
        if (const auto c = normal::choose_shooting(cache, gs, legal, fire, terrain ? &*terrain : nullptr, &held)) {
            return single(action::Shoot{c->unit, c->type, c->shots}, normal::describe_shooting(gs, *c));
        }
        if (!held.empty()) {
            std::string names;
            for (const UnitId id : held) names += (names.empty() ? "" : ", ") + client::unit_display_name(gs, id);
            return single(action::EndShootStep{}, "End shooting: " + names + " hold fire to stay hidden (13.09, M91)");
        }
        return single(action::EndShootStep{}, "End shooting: no shot is worth anything");
    }
    if (std::holds_alternative<decision::SelectAttackBatch>(kind)) {
        const std::size_t b = normal::choose_attack_batch(cache, gs, legal);
        return single(action::SelectAttackBatch{b}, "Resolve attack batch " + std::to_string(b) +
                                                        " next: the order with the most expected damage (04.03)");
    }
    if (std::holds_alternative<decision::RetargetAttacks>(kind)) {
        // 04.03.03 (M92, D-093): the new target with the most expected damage.
        double value = 0.0;
        action::RetargetAttacks r = normal::choose_retarget(cache, gs, legal, &value);
        char buf[96];
        std::snprintf(buf, sizeof buf, " (expected value %.2f, 04.03.03)", value);
        if (r.retargets.empty()) return single(std::move(r), "Select no new targets: none is worth anything (04.03.03)");
        return single(std::move(r), "New targets for the weapons whose target is gone" + std::string(buf));
    }
    if (std::holds_alternative<decision::DeclareAllocationOrder>(kind)) {
        if (auto o = normal::allocation_order(cache, gs, legal)) {
            return single(*o, "Allocation order: the least valuable models first (05.03)");
        }
        return std::nullopt;
    }
    if (std::holds_alternative<decision::OrderCasualties>(kind)) {
        if (auto o = normal::casualty_order(cache, gs)) {
            const auto& d = std::get<decision::OrderCasualties>(kind);
            return single(action::OrderCasualties{d.target, o->priority}, o->reason);
        }
        return std::nullopt;
    }
    if (const auto* d = std::get_if<decision::RestoreCoherency>(&kind)) {
        std::string why;
        if (auto m = normal::coherency_removal(cache, gs, d->unit, &why)) {
            return single(action::RemoveModelForCoherency{d->unit, *m}, why);
        }
        return std::nullopt;
    }
    if (std::holds_alternative<decision::SelectUnitToFight>(kind) || std::holds_alternative<decision::ForcedFight>(kind)) {
        return normal::decide_fight_selection(cache, gs, side, legal, seed, weights);
    }
    if (const auto* d = std::get_if<decision::DeclareMeleeAttacks>(&kind)) return normal::decide_melee(cache, gs, d->unit);
    if (std::holds_alternative<decision::PileIn>(kind)) return normal::decide_pile_in(cache, gs, side, legal, seed, weights);
    if (std::holds_alternative<decision::OverrunPileIn>(kind)) {
        return normal::decide_overrun_pile_in(cache, gs, side, legal, seed, weights);
    }
    if (std::holds_alternative<decision::Consolidate>(kind)) {
        return normal::decide_consolidation(cache, gs, side, legal, seed, weights);
    }
    if (std::holds_alternative<decision::MoveModels>(kind) && is_fight_move(gs)) {
        return normal::continue_fight_move(gs, side, seed);
    }
    // M42: charges (11.02, 11.04).
    if (std::holds_alternative<decision::Charge>(kind)) return normal::decide_charge(cache, gs, side, legal, seed, weights);
    if (std::holds_alternative<decision::SelectChargeTargets>(kind)) {
        return normal::decide_charge_targets(cache, gs, side, legal, seed, weights);
    }
    if (std::holds_alternative<decision::MoveModels>(kind) && gs.flow.move &&
        gs.flow.move->type == MoveType::Charge) {
        return normal::continue_charge_move(gs, side, seed);
    }
    // M43: the Movement phase (09.02-09.07): the move type and the whole planned move, and the MoveModels of
    // Normal, Advance and Fall-back moves (replanned from the move's start).
    if (std::holds_alternative<decision::MoveUnits>(kind)) {
        return normal::decide_move_units(cache, gs, side, legal, seed, nullptr, nullptr, weights);
    }
    if (std::holds_alternative<decision::MoveModels>(kind) && gs.flow.move &&
        (gs.flow.move->type == MoveType::Normal || gs.flow.move->type == MoveType::Advance ||
         gs.flow.move->type == MoveType::FallBack)) {
        return normal::continue_unit_move(cache, gs, side, weights);
    }
    return std::nullopt;
}

}  // namespace

PolicyResult normal_policy(const GameState& gs, core::Side side, const std::vector<GameAction>& legal,
                           std::uint64_t seed, const normal::MoveWeights& weights) {
    if (std::optional<PolicyResult> r = normal_answer(gs, side, legal, seed, weights)) return std::move(*r);
    return normal::default_answer(gs, side, legal, seed);
}

}  // namespace wh40k::ai
