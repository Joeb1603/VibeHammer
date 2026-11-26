#include "normal/stratagems.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <variant>
#include <vector>

#include "analysis/charge_odds.hpp"
#include "core/engine/engine.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/mortal_wounds.hpp"
#include "core/rules/random_characteristics.hpp"
#include "core/state/queries.hpp"
#include "unit_style.hpp"

namespace wh40k::ai::normal {

namespace {

using core::GameState;

// The roll window's unit (M95's windows each have one; D-097's EndOfPhase windows have none).
const core::Unit* window_unit(const GameState& gs, const core::WindowInfo& w) {
    return w.unit ? gs.find_unit(*w.unit) : nullptr;
}

bool offered(const core::WindowInfo& w, core::Stratagem s) {
    return std::any_of(w.options.begin(), w.options.end(), [s](const core::StratagemOption& o) { return o.stratagem == s; });
}

// The window's attack batch (every attack roll's window records its batch).
const core::AttackBatch* window_batch(const GameState& gs) {
    if (!gs.flow.attack || !gs.flow.window) return nullptr;
    const std::size_t b = gs.flow.window->batch;
    return b < gs.flow.attack->batches.size() ? &gs.flow.attack->batches[b] : nullptr;
}

// Expected model equivalents of one attack die of `profile` at `target` now (a random D mixed over its values).
double one_attack_die(analysis::OddsCache& cache, const core::AttackProfile& profile, const core::Unit& target) {
    const std::vector<analysis::VolleyBatch> volley{analysis::VolleyBatch{profile, 1, {}}};
    return analysis::expected_outcome(cache, volley, analysis::snapshot_of(target)).model_equivalents;
}

// Hits one attack die scores on average (24.36: [SUSTAINED HITS X] adds X per critical hit).
double hits_per_die(analysis::OddsCache& cache, const core::AttackProfile& profile, int toughness) {
    const analysis::HitWoundOdds& o = cache.hit_wound(profile, toughness);
    return o.hit + o.sustained * o.critical_hit;
}

// The lowest pickable die among those `failed` says failed.
template <typename Failed>
std::optional<std::size_t> lowest_failed(const core::WindowInfo& w, Failed failed) {
    std::optional<std::size_t> best;
    for (std::size_t i = 0; i < w.faces.size(); ++i) {
        if (!w.pickable[i] || !failed(i)) continue;
        if (!best || w.faces[i] < w.faces[*best]) best = i;
    }
    return best;
}

// The expected value of `value(roll)` over a new roll of `expr` (each D6 face equally likely; 2 dice enumerated).
template <typename Value>
double expected_over(const core::DiceExpr& expr, Value value) {
    if (expr.is_fixed()) return value(expr.modifier);
    double sum = 0.0;
    int n = 0;
    if (expr.num_dice == 1) {
        for (int f = 1; f <= 6; ++f, ++n) {
            const std::array<int, 1> faces{f};
            sum += value(core::characteristic_from_faces(expr, faces).value);
        }
    } else {
        for (int a = 1; a <= 6; ++a) {
            for (int b = 1; b <= 6; ++b, ++n) {
                const std::array<int, 2> faces{a, b};
                sum += value(core::characteristic_from_faces(expr, faces).value);
            }
        }
    }
    return n > 0 ? sum / n : 0.0;
}

std::optional<RerollPick> hit_or_wound(analysis::OddsCache& cache, const GameState& gs, const core::WindowInfo& w) {
    const core::AttackBatch* batch = window_batch(gs);
    const core::Unit* target = batch != nullptr ? gs.find_unit(batch->target) : nullptr;
    if (target == nullptr || target->models.empty() || !w.result) return std::nullopt;
    const double die_value = one_attack_die(cache, batch->profile, *target);
    std::optional<std::size_t> die;
    double gain = 0.0;
    if (const auto* h = std::get_if<core::event::HitRollsMade>(&*w.result)) {
        die = lowest_failed(w, [&](std::size_t i) { return h->rolls.rolls[i].outcome == core::HitOutcome::Fail; });
        gain = die_value;  // a missed hit re-rolled is worth a fresh attack die
    } else if (const auto* wo = std::get_if<core::event::WoundRollsMade>(&*w.result)) {
        die = lowest_failed(w, [&](std::size_t i) { return wo->rolls.rolls[i].outcome == core::WoundOutcome::Fail; });
        const double hits = hits_per_die(cache, batch->profile, analysis::snapshot_of(*target).toughness);
        gain = hits > 0.0 ? die_value / hits : 0.0;  // a failed wound re-rolled is worth a hit
    }
    if (!die) return std::nullopt;
    return RerollPick{die, gain, "re-rolls a failed " + std::string(w.roll == core::RollKind::Hit ? "hit" : "wound") + " roll"};
}

std::optional<RerollPick> save(analysis::OddsCache& cache, const GameState& gs, const core::WindowInfo& w) {
    const core::AttackBatch* batch = window_batch(gs);
    const core::Unit* target = window_unit(gs, w);
    if (batch == nullptr || target == nullptr || target->models.empty() || !gs.flow.attack->current) return std::nullopt;
    const core::BatchInProgress& b = *gs.flow.attack->current;
    if (b.groups.empty()) return std::nullopt;
    const std::size_t g = b.order && !b.order->empty() ? b.order->front() : 0;
    const core::AllocationGroup& group = b.groups[std::min(g, b.groups.size() - 1)];
    const int ap = batch->profile.armour_penetration;
    const auto fails = [&](int face) {
        return core::check_save_roll(face, group, ap).outcome != core::SaveOutcome::AttackFails;
    };
    const std::optional<std::size_t> die = lowest_failed(w, [&](std::size_t i) { return fails(w.faces[i]); });
    if (!die) return std::nullopt;
    double p_save = 0.0;
    for (int f = 1; f <= 6; ++f) p_save += fails(f) ? 0.0 : 1.0 / 6.0;
    // One damaging attack's worth to our unit: a fresh attack die's value over the chance it gets through to damage.
    const analysis::HitWoundOdds& o = cache.hit_wound(batch->profile, analysis::snapshot_of(*target).toughness);
    const double through = hits_per_die(cache, batch->profile, analysis::snapshot_of(*target).toughness) * o.wound *
                           cache.unsaved(group, ap);
    const double unsaved_value = through > 1e-9 ? one_attack_die(cache, batch->profile, *target) / through : 0.0;
    return RerollPick{die, p_save * unsaved_value, "re-rolls a failed save"};
}

std::optional<RerollPick> damage(const GameState& gs, const core::WindowInfo& w) {
    if (!w.result || !w.pickable.at(0)) return std::nullopt;
    const auto* d = std::get_if<core::event::DamageRolled>(&*w.result);
    const core::Unit* target = d != nullptr ? gs.find_unit(d->target) : nullptr;
    const core::Model* model = target != nullptr ? core::find_model(*target, d->model) : nullptr;
    if (model == nullptr || model->characteristics.wounds <= 0) return std::nullopt;
    const int left = model->wounds_remaining;
    const int w_char = model->characteristics.wounds;
    const auto worth = [&](int dealt) { return static_cast<double>(std::min(dealt, left)) / w_char; };
    const double gain = expected_over(d->damage, worth) - worth(d->roll.value);
    return RerollPick{std::size_t{0}, gain, "re-rolls a damage roll of " + std::to_string(d->roll.value)};
}

std::optional<RerollPick> random_attacks(analysis::OddsCache& cache, const GameState& gs, const core::WindowInfo& w) {
    const core::AttackBatch* batch = window_batch(gs);
    const core::Unit* target = batch != nullptr ? gs.find_unit(batch->target) : nullptr;
    const auto* r = w.result ? std::get_if<core::event::AttackDiceRolled>(&*w.result) : nullptr;
    if (target == nullptr || target->models.empty() || r == nullptr) return std::nullopt;
    const double die_value = one_attack_die(cache, batch->profile, *target);
    std::optional<RerollPick> best;
    std::size_t first = 0;  // each selection's dice, in order (the window's faces are flattened)
    for (const core::AttackDiceRoll& roll : r->rolls) {
        const std::size_t n = roll.roll.dice.size();
        if (n > 0 && first < w.pickable.size() && w.pickable[first]) {
            const double gain =
                (expected_over(roll.attacks, [](int a) { return static_cast<double>(a); }) - roll.roll.value) * die_value;
            if (!best || gain > best->gain) best = RerollPick{first, gain, "re-rolls a random A of " + std::to_string(roll.roll.value)};
        }
        first += n;
    }
    return best;
}

std::optional<RerollPick> hazard(const GameState& gs, const core::WindowInfo& w) {
    const core::Unit* unit = window_unit(gs, w);
    if (unit == nullptr || unit->models.empty()) return std::nullopt;
    const auto fails = [&](int face) {
        const std::array<int, 1> one{face};
        return core::judge_hazard_rolls(*unit, one).failures > 0;
    };
    const std::optional<std::size_t> die = lowest_failed(w, [&](std::size_t i) { return fails(w.faces[i]); });
    if (!die) return std::nullopt;
    int failing = 0;
    for (int f = 1; f <= 6; ++f) failing += fails(f) ? 1 : 0;
    if (failing == 0 || failing == 6) return std::nullopt;
    // One failure's loss: one roll's expected loss over the chance it fails (06.03 via hazard_model_equivalents).
    const double loss = analysis::hazard_model_equivalents(*unit, 1) * 6.0 / failing;
    return RerollPick{die, loss * (6 - failing) / 6.0, "re-rolls a failed hazard roll"};
}

std::optional<RerollPick> charge(const GameState& gs, const core::WindowInfo& w) {
    const auto* c = w.result ? std::get_if<core::event::ChargeRolled>(&*w.result) : nullptr;
    const core::Unit* unit = window_unit(gs, w);
    if (c == nullptr || unit == nullptr || !c->eligible_targets.empty()) return std::nullopt;  // it reaches a target
    std::optional<float> gap;
    for (const core::Unit& enemy : gs.units) {
        if (enemy.side == unit->side || enemy.models.empty()) continue;
        if (const std::optional<float> d = core::unit_distance(*unit, enemy); d && (!gap || *d < *gap)) gap = d;
    }
    if (!gap) return std::nullopt;
    return RerollPick{std::nullopt, analysis::p_charge_roll_covers(*gap), "re-rolls a charge that reaches no target"};
}

}  // namespace

std::optional<RerollPick> best_command_reroll(analysis::OddsCache& cache, const GameState& state) {
    const std::optional<core::WindowInfo> w = core::window_info(state);
    if (!w || w->timing != core::WindowTiming::AfterRoll || !w->roll || !w->unit ||
        !offered(*w, core::Stratagem::CommandReroll)) {
        return std::nullopt;
    }
    switch (*w->roll) {
        case core::RollKind::Hit:
        case core::RollKind::Wound:
            return hit_or_wound(cache, state, *w);
        case core::RollKind::Save:
            return save(cache, state, *w);
        case core::RollKind::Damage:
            return damage(state, *w);
        case core::RollKind::RandomAttacks:
            return random_attacks(cache, state, *w);
        case core::RollKind::Hazard:
            return hazard(state, *w);
        case core::RollKind::Charge:
            return charge(state, *w);
        case core::RollKind::Advance:
        case core::RollKind::BattleShock:
            break;
    }
    return std::nullopt;
}

PolicyResult decide_stratagems(analysis::OddsCache& cache, const GameState& state, core::Side,
                               const MoveWeights& weights) {
    const core::GameAction decline = core::action::DeclineStratagems{};
    const std::optional<core::WindowInfo> w = core::window_info(state);
    if (!w) return PolicyResult{decline, "No stratagem", {}};
    if (!w->unit) return PolicyResult{decline, "No stratagem", {}};  // D-097's EndOfPhase windows: not played yet
    const std::string unit = client::unit_display_name(state, *w->unit);
    if (weights.insane_bravery > 0.0 && offered(*w, core::Stratagem::InsaneBravery) && w->next_roll &&
        w->next_roll->need) {
        const double risk = 1.0 - analysis::p_2d6_at_least(*w->next_roll->need);
        if (risk >= kBraveryRisk) {
            return PolicyResult{core::action::UseStratagem{core::Stratagem::InsaneBravery, *w->unit, std::nullopt},
                                unit + ": Insane Bravery (the test would fail " +
                                    std::to_string(static_cast<int>(std::lround(risk * 100))) + "% of the time)",
                                {}};
        }
    }
    if (weights.command_reroll > 0.0) {
        if (const std::optional<RerollPick> pick = best_command_reroll(cache, state)) {
            const bool whole = !pick->die.has_value();
            if ((whole && pick->gain >= kChargeRerollChance) || (!whole && pick->gain >= kRerollMinGain)) {
                return PolicyResult{core::action::UseStratagem{core::Stratagem::CommandReroll, *w->unit, pick->die},
                                    unit + ": Command Re-roll " + pick->why, {}};
            }
        }
    }
    return PolicyResult{decline, unit + ": no stratagem", {}};
}

}  // namespace wh40k::ai::normal
