#include "normal/stratagems.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "analysis/charge_odds.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/charge.hpp"
#include "core/rules/fight.hpp"
#include "normal/charge.hpp"
#include "normal/shooting.hpp"
#include "normal/value.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/mortal_wounds.hpp"
#include "core/rules/random_characteristics.hpp"
#include "core/rules/stratagem_effects.hpp"
#include "core/state/queries.hpp"
#include "unit_style.hpp"

namespace wh40k::ai::normal {

namespace {

using core::GameState;

constexpr double kBetter = 1e-9;  // D-097: a choice must beat the current one by more than this (ties keep it)

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

std::string percent(double p) { return std::to_string(static_cast<int>(std::lround(p * 100))) + "%"; }

std::string shares(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

// D-097: one heroic charge's gain over holding, per 2D6 total (the mode's distance: Into the Fray caps it at 6).
struct HeroicGain {
    double gain = 0.0;
    double p_success = 0.0;
    std::vector<core::UnitId> targets;
};
std::optional<HeroicGain> heroic_gain(analysis::OddsCache& cache, const GameState& gs, core::UnitId unit,
                                      core::StratagemMode mode, std::uint64_t seed) {
    const core::Unit* u = gs.find_unit(unit);
    if (u == nullptr || u->models.empty()) return std::nullopt;
    const core::Side side = u->side;
    const core::GameAction declare = core::action::UseStratagem{core::Stratagem::HeroicIntervention, unit, std::nullopt, mode};
    if (!core::validate_action(gs, side, declare).ok()) return std::nullopt;
    const int turns = turns_left(gs);
    const double hold = hold_value(cache, gs, unit);
    // The mode's possible targets, closest first (at most three, as M42's charges), then every non-empty set of them.
    std::vector<core::UnitId> enemies =
        core::eligible_charge_targets(gs, *u, static_cast<int>(core::kChargeRangeInches), mode);
    std::stable_sort(enemies.begin(), enemies.end(), [&](core::UnitId a, core::UnitId b) {
        return core::unit_distance(*u, *gs.find_unit(a)).value_or(0.0f) <
               core::unit_distance(*u, *gs.find_unit(b)).value_or(0.0f);
    });
    if (enemies.size() > 3) enemies.resize(3);
    std::sort(enemies.begin(), enemies.end(), [](core::UnitId a, core::UnitId b) { return a.v < b.v; });
    const auto distance_of = [mode](std::size_t total) {
        return mode == core::StratagemMode::IntoTheFray ? std::min<std::size_t>(total, 6) : total;
    };
    std::array<bool, 13> any{};
    std::array<double, 13> best{};  // by distance
    std::optional<HeroicGain> out;
    double best_set = -1.0;
    const auto& pmf = analysis::two_d6_pmf();
    for (std::uint32_t mask = 1; mask < (1u << enemies.size()); ++mask) {
        std::vector<core::UnitId> set;
        for (std::size_t i = 0; i < enemies.size(); ++i) {
            if ((mask >> i) & 1u) set.push_back(enemies[i]);
        }
        std::array<bool, 13> planned{};
        std::array<double, 13> value{};  // by distance (the state after the roll depends only on it)
        const auto check = [&](const GameState& after_roll, core::UnitId id, std::span<const core::UnitId> t) {
            const std::optional<int> roll = after_roll.find_unit(id)->phase_flags.charge_roll;
            if (!roll || *roll < 0 || *roll > 12) return false;
            const auto d = static_cast<std::size_t>(*roll);
            if (planned[d]) return true;
            const std::optional<ChargePlan> move =
                plan_charge_move(after_roll, side, id, std::vector<core::UnitId>(t.begin(), t.end()), seed, {});
            if (!move) return false;
            planned[d] = true;
            value[d] = charged_value(cache, move->after, id, t, turns);
            return true;
        };
        const std::optional<analysis::ChargeOdds> odds = analysis::charge_odds(gs, unit, set, check, declare);
        if (!odds) continue;
        double gain = 0.0;
        for (std::size_t r = 0; r <= 12; ++r) {
            if (!odds->success_by_total[r]) continue;
            const std::size_t d = distance_of(r);
            gain += pmf[r] * std::max(0.0, value[d] - hold);
            if (!any[d] || value[d] > best[d]) {
                any[d] = true;
                best[d] = value[d];
            }
        }
        if (gain > best_set + kBetter) {
            best_set = gain;
            if (!out) out = HeroicGain{};
            out->targets = set;
        }
    }
    if (!out) return std::nullopt;
    for (std::size_t r = 0; r <= 12; ++r) {
        const std::size_t d = distance_of(r);
        if (!any[d]) continue;
        out->gain += pmf[r] * std::max(0.0, best[d] - hold);
        out->p_success += pmf[r];
    }
    return out;
}

const core::Unit* engaged_enemy_fights_first(const GameState& gs, const core::Unit& ours) {
    // The enemy's eligible Fights First unit engaged with `ours` whose fight on it is worth the most (first found on a tie).
    const core::Unit* pick = nullptr;
    double most = -1.0;
    analysis::OddsCache cache;
    for (const core::Unit& e : gs.units) {
        if (e.side == ours.side || core::is_destroyed(e)) continue;
        if (!core::eligible_to_fight(gs, e) || !core::is_fights_first_unit(e)) continue;
        const std::vector<core::UnitId> engaged = core::engaged_enemy_units(gs, e);
        if (std::find(engaged.begin(), engaged.end(), ours.id) == engaged.end()) continue;
        const std::optional<analysis::MeleePlanValue> theirs = analysis::best_melee(cache, gs, e.id, ours.id);
        const double v = theirs ? theirs->outcome.model_equivalents : 0.0;
        if (v > most) {
            most = v;
            pick = &e;
        }
    }
    return pick;
}

// ---- D-099 (M97): Explosives and Crushing Impact ----

constexpr double kExplosivesHit = 1.0 / 2.0;  // 15.05: a 4+ on a D6
constexpr double kCrushFive = 1.0 / 3.0;      // 15.06: a 5+
constexpr double kCrushOne = 1.0 / 6.0;       // 15.06: a 1

std::string two_places(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

// 15.05: six D6 at 4+ as a share of `enemy`.
double explosives_value(const GameState& state, const core::Unit& enemy) {
    const std::vector<double> pmf =
        analysis::capped_binomial(core::kExplosivesDice, kExplosivesHit, core::kExplosivesDice);
    return unit_share(state, enemy, analysis::mortal_wound_model_equivalents(enemy, pmf));
}

// 15.06: `model`'s T D6: the 5+s as a share of `enemy`, less the 1s as a share of `unit`, each capped at 6.
double crush_gain(const GameState& state, const core::Unit& unit, const core::Model& model, const core::Unit& enemy) {
    const int t = model.characteristics.toughness;
    const double them = unit_share(state, enemy, analysis::mortal_wound_model_equivalents(
                                                     enemy, analysis::capped_binomial(t, kCrushFive, core::kCrushingImpactCap)));
    const double us = unit_share(state, unit, analysis::mortal_wound_model_equivalents(
                                                  unit, analysis::capped_binomial(t, kCrushOne, core::kCrushingImpactCap)));
    return them - us;
}

struct PairValue {
    core::EffectPair pair;
    double value = 0.0;
};

// Explosives' enemy unit worth the most for `unit`, with the first legal model for it (ties keep the first).
std::optional<PairValue> best_explosives_pair(const GameState& state, const core::Unit& unit) {
    std::optional<PairValue> best;
    std::vector<core::UnitId> seen;
    for (const core::EffectPair& p : core::explosives_pairs(state, unit)) {
        if (std::find(seen.begin(), seen.end(), p.enemy) != seen.end()) continue;
        seen.push_back(p.enemy);
        const core::Unit* enemy = state.find_unit(p.enemy);
        if (enemy == nullptr) continue;
        const double v = explosives_value(state, *enemy);
        if (!best || v > best->value + kBetter) best = PairValue{p, v};
    }
    return best;
}

// Crushing Impact's pair with the best gain for `unit` (ties keep the first).
std::optional<PairValue> best_crush_pair(const GameState& state, const core::Unit& unit) {
    std::optional<PairValue> best;
    for (const core::EffectPair& p : core::crushing_impact_pairs(state, unit)) {
        const core::Unit* enemy = state.find_unit(p.enemy);
        const core::Model* model = core::find_model(unit, p.model);
        if (enemy == nullptr || model == nullptr) continue;
        const double v = crush_gain(state, unit, *model, *enemy);
        if (!best || v > best->value + kBetter) best = PairValue{p, v};
    }
    return best;
}

}  // namespace

std::optional<ReactionPick> best_explosives(const GameState& state, const std::vector<core::GameAction>& legal) {
    std::optional<ReactionPick> best;
    for (const core::GameAction& a : legal) {
        const auto* use = std::get_if<core::action::UseStratagem>(&a);
        if (use == nullptr || use->stratagem != core::Stratagem::Explosives) continue;
        const core::Unit* unit = state.find_unit(use->unit);
        if (unit == nullptr) continue;
        const std::optional<PairValue> p = best_explosives_pair(state, *unit);
        if (!p || (best && !(p->value > best->value + kBetter))) continue;
        best = ReactionPick{a, p->value,
                            client::unit_display_name(state, use->unit) + ": Explosives at " +
                                client::unit_display_name(state, p->pair.enemy) + " (" + two_places(p->value) +
                                " of it for 1 CP, 15.05)"};
    }
    return best;
}

std::optional<ReactionPick> best_crushing_impact(const GameState& state) {
    const std::optional<core::WindowInfo> w = core::window_info(state);
    if (!w || w->timing != core::WindowTiming::AfterChargeMove || !w->unit ||
        !offered(*w, core::Stratagem::CrushingImpact)) {
        return std::nullopt;
    }
    const core::Unit* unit = state.find_unit(*w->unit);
    if (unit == nullptr) return std::nullopt;
    const std::optional<PairValue> p = best_crush_pair(state, *unit);
    if (!p) return std::nullopt;
    return ReactionPick{core::action::UseStratagem{core::Stratagem::CrushingImpact, unit->id, std::nullopt}, p->value,
                        client::unit_display_name(state, unit->id) + ": Crushing Impact on " +
                            client::unit_display_name(state, p->pair.enemy) + " (a gain of " + two_places(p->value) +
                            " for 1 CP, 15.06)"};
}

std::optional<ReactionPick> best_effect_pair(const GameState& state) {
    if (!state.pending || !state.flow.effect) return std::nullopt;
    const auto* d = std::get_if<core::decision::SelectModelAndEnemy>(&state.pending->kind);
    if (d == nullptr) return std::nullopt;
    const core::Unit* unit = state.find_unit(d->unit);
    if (unit == nullptr) return std::nullopt;
    const std::optional<PairValue> p = d->stratagem == core::Stratagem::Explosives ? best_explosives_pair(state, *unit)
                                                                                    : best_crush_pair(state, *unit);
    if (!p) return std::nullopt;
    return ReactionPick{core::action::SelectModelAndEnemy{p->pair.model, p->pair.enemy}, p->value,
                        std::string(core::stratagem_name(d->stratagem)) + ": model " +
                            std::to_string(p->pair.model.v) + " and " +
                            client::unit_display_name(state, p->pair.enemy) + " (" + two_places(p->value) + ")"};
}

double crushing_impact_value(const GameState& after, core::UnitId unit) {
    const core::Unit* u = after.find_unit(unit);
    if (u == nullptr || u->models.empty()) return 0.0;
    if (core::forecast_stratagem_problem(after, u->side, core::Stratagem::CrushingImpact,
                                         core::WindowTiming::AfterChargeMove, unit, unit)) {
        return 0.0;
    }
    const std::optional<PairValue> p = best_crush_pair(after, *u);
    if (!p || !(p->value >= kCrushMinGain)) return 0.0;
    return p->value - kCrushMinGain;
}

std::optional<ReactionPick> best_fire_overwatch(analysis::OddsCache& cache, const GameState& state) {
    const std::optional<core::WindowInfo> w = core::window_info(state);
    if (!w || w->timing != core::WindowTiming::EndOfPhase) return std::nullopt;
    std::optional<ReactionPick> best;
    for (const core::StratagemOption& o : w->options) {
        if (o.stratagem != core::Stratagem::FireOverwatch) continue;
        for (const core::UnitId u : o.targets) {
            const std::optional<ShootingChoice> c = plan_snap_shooting(cache, state, u);
            if (!c) continue;
            if (!best || c->value > best->value + kBetter) {
                best = ReactionPick{core::action::UseStratagem{core::Stratagem::FireOverwatch, u, std::nullopt},
                                    c->value,
                                    client::unit_display_name(state, u) + ": Fire Overwatch, a snap shot worth " +
                                        shares(c->value) + " (" + describe_shooting(state, *c) + ")"};
            }
        }
    }
    return best;
}

std::optional<ReactionPick> best_heroic_intervention(analysis::OddsCache& cache, const GameState& state,
                                                     std::uint64_t seed) {
    const std::optional<core::WindowInfo> w = core::window_info(state);
    if (!w || w->timing != core::WindowTiming::EndOfPhase) return std::nullopt;
    std::optional<ReactionPick> best;
    for (const core::StratagemOption& o : w->options) {  // Leap to Defend first (the cheaper wins a tie)
        if (o.stratagem != core::Stratagem::HeroicIntervention || !o.mode || o.cost <= 0) continue;
        for (const core::UnitId u : o.targets) {
            const std::optional<HeroicGain> g = heroic_gain(cache, state, u, *o.mode, seed);
            if (!g) continue;
            const double per_cp = g->gain / o.cost;
            if (!best || per_cp > best->value + kBetter) {
                best = ReactionPick{
                    core::action::UseStratagem{core::Stratagem::HeroicIntervention, u, std::nullopt, *o.mode}, per_cp,
                    client::unit_display_name(state, u) + ": Heroic Intervention (" +
                        std::string(core::stratagem_mode_name(*o.mode)) + "), " + percent(g->p_success) +
                        " to charge, " + shares(g->gain) + " over holding (" + std::to_string(o.cost) + " CP)"};
            }
        }
    }
    return best;
}

std::optional<ReactionPick> best_counteroffensive(analysis::OddsCache& cache, const GameState& state) {
    const std::optional<core::WindowInfo> w = core::window_info(state);
    if (!w || w->timing != core::WindowTiming::AfterAttacks || !state.flow.fight_sequence) return std::nullopt;
    const core::Side side = w->side;
    // Only when it changes who fights next: in a Fights First step (finish_fight has already returned to it for any
    // eligible Fights First unit) the enemy selects next when we have no eligible Fights First unit and it has one.
    if (state.flow.fight_sequence->sub_step != core::FightSubStep::FightsFirst) return std::nullopt;
    for (const core::Unit& u : state.units) {
        if (u.side == side && core::eligible_to_fight(state, u) && core::is_fights_first_unit(u)) return std::nullopt;
    }
    std::optional<ReactionPick> best;
    for (const core::StratagemOption& o : w->options) {
        if (o.stratagem != core::Stratagem::Counteroffensive || o.cost <= 0) continue;
        for (const core::UnitId id : o.targets) {
            const core::Unit& ours = *state.find_unit(id);
            const core::Unit* enemy = engaged_enemy_fights_first(state, ours);
            if (enemy == nullptr) continue;  // the enemy's next fights don't touch it
            const std::optional<analysis::MeleePlanValue> mine = analysis::best_melee(cache, state, id, enemy->id);
            const std::optional<analysis::MeleePlanValue> theirs = analysis::best_melee(cache, state, enemy->id, id);
            if (!mine || !theirs) continue;
            const double our_size = static_cast<double>(ours.models.size());
            const double their_size = static_cast<double>(enemy->models.size());
            // Fighting first, our unit keeps the share of its fight the enemy's strike would have taken, and takes away
            // the share of the enemy's strike its own kills remove.
            const double kept = std::min(1.0, theirs->outcome.models_destroyed / our_size) *
                                unit_share(state, *enemy, mine->outcome.model_equivalents);
            const double prevented = std::min(1.0, mine->outcome.models_destroyed / their_size) *
                                     unit_share(state, ours, theirs->outcome.model_equivalents);
            const double per_cp = (kept + prevented) / o.cost;
            if (!best || per_cp > best->value + kBetter) {
                best = ReactionPick{core::action::UseStratagem{core::Stratagem::Counteroffensive, id, std::nullopt},
                                    per_cp,
                                    client::unit_display_name(state, id) + ": Counteroffensive, it fights before " +
                                        client::unit_display_name(state, enemy->id) + " (" + shares(kept + prevented) +
                                        " for " + std::to_string(o.cost) + " CP)"};
            }
        }
    }
    return best;
}

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
        case core::RollKind::Explosives:      // M97 (D-099): no window follows either (15.02 lists neither)
        case core::RollKind::CrushingImpact:
            break;
    }
    return std::nullopt;
}

PolicyResult decide_stratagems(analysis::OddsCache& cache, const GameState& state, core::Side,
                               const MoveWeights& weights, std::uint64_t seed) {
    const core::GameAction decline = core::action::DeclineStratagems{};
    const std::optional<core::WindowInfo> w = core::window_info(state);
    if (!w) return PolicyResult{decline, "No stratagem", {}};
    // D-097 (M96): the windows that follow no roll.
    if (w->timing == core::WindowTiming::EndOfPhase) {
        if (weights.fire_overwatch > 0.0) {
            if (const auto pick = best_fire_overwatch(cache, state); pick && pick->value >= kOverwatchMinShare) {
                return PolicyResult{pick->action, pick->why, {}};
            }
        }
        if (weights.heroic_intervention > 0.0) {
            if (const auto pick = best_heroic_intervention(cache, state, seed); pick && pick->value >= kHeroicMinGain) {
                return PolicyResult{pick->action, pick->why, {}};
            }
        }
        return PolicyResult{decline, "No stratagem at the end of the phase: none is worth its CP", {}};
    }
    if (w->timing == core::WindowTiming::AfterChargeMove) {
        // D-099 (M97): just after a charge move, 15.06 Crushing Impact.
        if (weights.crushing_impact > 0.0) {
            if (const auto pick = best_crushing_impact(state); pick && pick->value >= kCrushMinGain) {
                return PolicyResult{pick->action, pick->why, {}};
            }
        }
        return PolicyResult{decline, "No Crushing Impact: it gains too little for its CP", {}};
    }
    if (w->timing == core::WindowTiming::AfterAttacks) {
        if (weights.counteroffensive > 0.0) {
            if (const auto pick = best_counteroffensive(cache, state); pick && pick->value >= kCounterMinGain) {
                return PolicyResult{pick->action, pick->why, {}};
            }
        }
        return PolicyResult{decline, "No Counteroffensive: it changes nothing worth its CP", {}};
    }
    if (!w->unit) return PolicyResult{decline, "No stratagem", {}};
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
