#include "normal/shooting.hpp"

#include <algorithm>
#include <cstdio>
#include <numeric>
#include <utility>
#include <variant>

#include "core/model/keywords.hpp"
#include "core/rules/shooting.hpp"
#include "core/state/queries.hpp"
#include "normal/value.hpp"

namespace wh40k::ai::normal {

namespace {

using Loadout = std::vector<core::ShotDeclaration>;

constexpr std::size_t kMaxLoadouts = 64;  // per model; far more than any V1 model has (D-054: a fixed budget)
constexpr int kDescentPasses = 3;
constexpr double kBetter = 1e-9;  // a change must beat the current value by more than this (ties keep it)

const core::WeaponProfile& profile_of(const core::Model& m, const core::ShotDeclaration& d) {
    return m.weapons[d.weapon_index].profiles[d.profile_index];
}

// Every combination of one option per weapon (in first-appearance order), capped at kMaxLoadouts.
void add_products(const std::vector<std::vector<core::ShotDeclaration>>& per_weapon, std::vector<Loadout>& out) {
    if (per_weapon.empty()) return;
    std::vector<std::size_t> pick(per_weapon.size(), 0);
    for (;;) {
        if (out.size() >= kMaxLoadouts) return;
        Loadout l;
        for (std::size_t w = 0; w < per_weapon.size(); ++w) l.push_back(per_weapon[w][pick[w]]);
        out.push_back(std::move(l));
        std::size_t w = 0;
        while (w < per_weapon.size() && ++pick[w] == per_weapon[w].size()) pick[w++] = 0;
        if (w == per_weapon.size()) return;
    }
}

// A model's candidate loadouts from shot_options: one profile and target per weapon (D-016), within one weapon
// set when 24.07 applies (its [CLOSE-QUARTERS] weapons or its others; not in close-quarters shooting, and not
// for MONSTER / VEHICLE models).
std::vector<Loadout> loadouts_for(const core::Unit& unit, const core::Model& model, core::ShootingType type,
                                  const std::vector<core::ShotDeclaration>& options) {
    const bool one_set = type != core::ShootingType::CloseQuarters && !core::is_monster_or_vehicle(unit, model);
    std::vector<Loadout> out;
    for (const int set : {0, 1}) {
        std::vector<std::vector<core::ShotDeclaration>> per_weapon;
        std::vector<std::size_t> weapons;
        for (const core::ShotDeclaration& d : options) {
            if (d.model != model.id) continue;
            const bool cq = profile_of(model, d).has<core::ability::CloseQuarters>();
            if (one_set && cq != (set == 1)) continue;
            if (!one_set && set == 1) continue;  // every weapon goes in the one set
            const auto it = std::find(weapons.begin(), weapons.end(), d.weapon_index);
            if (it == weapons.end()) {
                weapons.push_back(d.weapon_index);
                per_weapon.push_back({d});
            } else {
                per_weapon[static_cast<std::size_t>(it - weapons.begin())].push_back(d);
            }
        }
        add_products(per_weapon, out);
    }
    return out;
}

// The declaration's value in unit shares; nullopt if the engine would refuse it.
std::optional<double> declaration_value(analysis::OddsCache& cache, const core::GameState& state, core::UnitId unit,
                                        core::ShootingType type, const std::vector<core::ShotDeclaration>& shots,
                                        std::vector<analysis::TargetOutcome>* outcomes = nullptr) {
    if (shots.empty()) return 0.0;
    const std::vector<core::AttackBatch> batches = analysis::shooting_batches(state, unit, type, shots);
    if (batches.empty()) return std::nullopt;
    std::vector<analysis::TargetOutcome> outs = evaluate_in_best_order(cache, state, batches);
    const double v = share_value(state, outs);
    if (outcomes != nullptr) *outcomes = std::move(outs);
    return v;
}

std::vector<core::ShotDeclaration> flatten(const std::vector<Loadout>& per_model) {
    std::vector<core::ShotDeclaration> out;
    for (const Loadout& l : per_model) out.insert(out.end(), l.begin(), l.end());
    return out;
}

// The best order of `volley` whose first batch is `first`: the rest permuted (or by damage per attack).
double best_value_starting_with(analysis::OddsCache& cache, std::span<const analysis::VolleyBatch> volley,
                                std::size_t first, const analysis::TargetSnapshot& target) {
    analysis::Volley rest;
    for (std::size_t i = 0; i < volley.size(); ++i) {
        if (i != first) rest.push_back(volley[i]);
    }
    const std::vector<std::size_t> rest_order = best_batch_order(cache, rest, target);
    analysis::Volley ordered{volley[first]};
    for (const std::size_t i : rest_order) ordered.push_back(rest[i]);
    return analysis::expected_outcome(cache, ordered, target).model_equivalents;
}

}  // namespace

std::vector<analysis::TargetOutcome> evaluate_in_best_order(analysis::OddsCache& cache, const core::GameState& state,
                                                           std::span<const core::AttackBatch> batches) {
    std::vector<analysis::TargetOutcome> out;
    std::vector<core::UnitId> targets;
    for (const core::AttackBatch& b : batches) {
        if (std::find(targets.begin(), targets.end(), b.target) == targets.end()) targets.push_back(b.target);
    }
    for (const core::UnitId t : targets) {
        const core::Unit* unit = state.find_unit(t);
        if (unit == nullptr || unit->models.empty()) continue;
        analysis::Volley v;
        for (const core::AttackBatch& b : batches) {
            if (b.target == t) v.push_back(analysis::VolleyBatch{b.profile, b.attack_dice});
        }
        const analysis::TargetSnapshot snap = analysis::snapshot_of(*unit);
        const std::vector<std::size_t> order = best_batch_order(cache, v, snap);
        analysis::Volley ordered;
        for (const std::size_t i : order) ordered.push_back(v[i]);
        out.push_back(analysis::TargetOutcome{t, analysis::expected_outcome(cache, ordered, snap)});
    }
    return out;
}

std::optional<ShootingChoice> plan_shooting(analysis::OddsCache& cache, const core::GameState& state, core::UnitId unit,
                                            core::ShootingType type) {
    const core::Unit* u = state.find_unit(unit);
    if (u == nullptr) return std::nullopt;
    const std::vector<core::ShotDeclaration> options = core::shot_options(state, *u, type);
    if (options.empty()) return std::nullopt;

    // Each model's loadouts, and its best one valued alone against the fresh target.
    std::vector<std::vector<Loadout>> cands;
    std::vector<Loadout> current;
    for (const core::Model& m : u->models) {
        std::vector<Loadout> mine = loadouts_for(*u, m, type, options);
        Loadout best;
        double best_value = -1.0;
        for (const Loadout& l : mine) {
            const std::optional<double> v = declaration_value(cache, state, unit, type, l);
            if (v && *v > best_value + kBetter) {
                best_value = *v;
                best = l;
            }
        }
        cands.push_back(std::move(mine));
        current.push_back(std::move(best));
    }
    std::optional<double> value = declaration_value(cache, state, unit, type, flatten(current));
    if (!value) return std::nullopt;  // the combination broke a rule (not expected: each model's own choice)

    // Coordinate descent on the whole volley: overkill and mixed profiles are judged together.
    for (int pass = 0; pass < kDescentPasses; ++pass) {
        bool improved = false;
        for (std::size_t i = 0; i < current.size(); ++i) {
            if (cands[i].size() < 2) continue;
            for (const Loadout& l : cands[i]) {
                if (l == current[i]) continue;
                std::vector<Loadout> trial = current;
                trial[i] = l;
                const std::optional<double> v = declaration_value(cache, state, unit, type, flatten(trial));
                if (v && *v > *value + kBetter) {
                    value = v;
                    current = std::move(trial);
                    improved = true;
                }
            }
        }
        if (!improved) break;
    }
    ShootingChoice out;
    out.unit = unit;
    out.type = type;
    out.shots = flatten(current);
    if (out.shots.empty()) return std::nullopt;
    out.value = declaration_value(cache, state, unit, type, out.shots, &out.outcomes).value_or(0.0);
    return out;
}

std::optional<ShootingChoice> choose_shooting(analysis::OddsCache& cache, const core::GameState& state,
                                              const std::vector<core::GameAction>& legal) {
    std::optional<ShootingChoice> best;
    for (const core::GameAction& a : legal) {
        const auto* shoot = std::get_if<core::action::Shoot>(&a);
        if (shoot == nullptr) continue;
        std::optional<ShootingChoice> c = plan_shooting(cache, state, shoot->unit, shoot->type);
        if (!c || !(c->value > kBetter)) continue;
        if (!best || c->value > best->value + kBetter) best = std::move(c);
    }
    return best;
}

double order_value(analysis::OddsCache& cache, std::span<const analysis::VolleyBatch> volley,
                   std::span<const std::size_t> order, const analysis::TargetSnapshot& target) {
    analysis::Volley v;
    for (const std::size_t i : order) v.push_back(volley[i]);
    return analysis::expected_outcome(cache, v, target).model_equivalents;
}

std::vector<std::size_t> best_batch_order(analysis::OddsCache& cache, std::span<const analysis::VolleyBatch> volley,
                                          const analysis::TargetSnapshot& target) {
    std::vector<std::size_t> order(volley.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    if (volley.size() < 2) return order;
    if (volley.size() > kPermutedBatches) {
        const auto damage = [&](std::size_t i) {
            const core::DiceExpr& d = volley[i].profile.damage;
            return d.is_fixed() ? d.fixed_value() : 0;  // D-002: V1 resolves fixed damage only
        };
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return damage(a) > damage(b); });
        return order;
    }
    std::vector<std::size_t> best = order;
    double best_value = order_value(cache, volley, order, target);
    while (std::next_permutation(order.begin(), order.end())) {
        const double v = order_value(cache, volley, order, target);
        if (v > best_value + kBetter) {
            best_value = v;
            best = order;
        }
    }
    return best;
}

std::size_t choose_attack_batch(analysis::OddsCache& cache, const core::GameState& state,
                                const std::vector<core::GameAction>& legal) {
    std::optional<std::size_t> best;
    double best_value = 0.0;
    const core::AttackFlow* f = state.flow.attack ? &*state.flow.attack : nullptr;
    for (const core::GameAction& a : legal) {
        const auto* pick = std::get_if<core::action::SelectAttackBatch>(&a);
        if (pick == nullptr) continue;
        double value = 0.0;
        if (f != nullptr && pick->batch < f->batches.size()) {
            const core::UnitId t = f->batches[pick->batch].target;
            const core::Unit* target = state.find_unit(t);
            if (target != nullptr && !target->models.empty()) {
                analysis::Volley v{analysis::VolleyBatch{f->batches[pick->batch].profile, f->batches[pick->batch].attack_dice}};
                for (std::size_t j = 0; j < f->batches.size(); ++j) {
                    if (j == pick->batch || f->batch_done[j] || f->batches[j].target != t) continue;
                    v.push_back(analysis::VolleyBatch{f->batches[j].profile, f->batches[j].attack_dice});
                }
                value = unit_share(*target, best_value_starting_with(cache, v, 0, analysis::snapshot_of(*target)));
            }
        }
        if (!best || value > best_value + kBetter) {
            best = pick->batch;
            best_value = value;
        }
    }
    return best.value_or(0);
}

std::string describe_shooting(const core::GameState& state, const ShootingChoice& choice) {
    const core::Unit* u = state.find_unit(choice.unit);
    std::vector<std::pair<std::string, int>> counts;  // "profile at target" -> models, first-appearance order
    for (const core::ShotDeclaration& d : choice.shots) {
        const core::Model* m = u != nullptr ? core::find_model(*u, d.model) : nullptr;
        const core::Unit* t = state.find_unit(d.target);
        const std::string key = (m != nullptr ? profile_of(*m, d).name : std::string("?")) + " at " +
                                (t != nullptr ? t->name : std::string("?"));
        auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& c) { return c.first == key; });
        if (it == counts.end()) {
            counts.emplace_back(key, 1);
        } else {
            ++it->second;
        }
    }
    std::string text = (u != nullptr ? u->name : std::string("?")) + ": ";
    for (std::size_t i = 0; i < counts.size(); ++i) {
        if (i > 0) text += ", ";
        text += std::to_string(counts[i].second) + "x " + counts[i].first;
    }
    double models = 0.0;
    for (const analysis::TargetOutcome& o : choice.outcomes) models += o.outcome.model_equivalents;
    char buf[64];
    std::snprintf(buf, sizeof buf, " (expected %.2f models)", models);
    return text + buf;
}

}  // namespace wh40k::ai::normal
