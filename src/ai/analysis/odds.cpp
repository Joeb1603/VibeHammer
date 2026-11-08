#include "analysis/odds.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "core/dice/dice.hpp"
#include "core/geometry/distance.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "core/state/queries.hpp"

namespace wh40k::ai::analysis {

namespace {

constexpr int kFaces = 6;  // a D6 (01.05): the faces each per-die chance is counted over

// ---- per-die chances from core's roll functions ----

HitWoundOdds derive_hit_wound(const core::AttackProfile& profile, int toughness) {
    HitWoundOdds o;
    for (int face = 1; face <= kFaces; ++face) {
        core::ScriptedDice hit_die{face};  // [TORRENT] rolls nothing; the face is simply unused
        const core::HitRolls h = core::make_hit_rolls(hit_die, profile, 1);
        o.hit += h.rolled_hits();  // 24.36's extra hits are counted separately (`sustained`)
        o.critical_hit += h.critical_hits();
        core::ScriptedDice wound_die{face};
        const core::WoundRolls w = core::make_wound_rolls(wound_die, profile, 1, toughness);
        o.wound += w.wounds();
        o.critical_wound += w.critical_wounds();
    }
    o.hit /= kFaces;
    o.critical_hit /= kFaces;
    o.wound /= kFaces;
    o.critical_wound /= kFaces;
    o.sustained = core::sustained_hits(profile);
    return o;
}

// P(exactly k successes in n tries, each with chance p).
double binomial(int n, int k, double p) {
    if (k < 0 || k > n) return 0.0;
    double c = 1.0;
    for (int i = 1; i <= k; ++i) c = c * static_cast<double>(n - k + i) / static_cast<double>(i);
    return c * std::pow(p, k) * std::pow(1.0 - p, n - k);
}

int opt_or(const std::optional<int>& v) { return v ? *v : -1; }

bool is_close_quarters(const core::WeaponProfile& p) { return p.has<core::ability::CloseQuarters>(); }

// 10.05 / 10.06 (strategic knowledge for hypothetical situations; the live state uses shot_options).
bool allowed_for_type(const core::Unit& unit, const core::Model& model, const core::WeaponProfile& p,
                      core::ShootingType type) {
    switch (type) {
        case core::ShootingType::Normal: return true;
        case core::ShootingType::Assault: return p.has<core::ability::Assault>();
        case core::ShootingType::CloseQuarters: return is_close_quarters(p) || core::is_monster_or_vehicle(unit, model);
    }
    return false;
}

void add_to_volley(Volley& volley, const core::AttackProfile& profile, int dice) {
    if (dice <= 0) return;
    for (VolleyBatch& b : volley) {
        if (b.profile == profile) {  // D-006: identical attacks are one batch
            b.attack_dice += dice;
            return;
        }
    }
    volley.push_back(VolleyBatch{profile, dice});
}

Volley to_volley(std::span<const core::AttackBatch> batches) {
    Volley v;
    for (const core::AttackBatch& b : batches) v.push_back(VolleyBatch{b.profile, b.attack_dice});
    return v;
}

// One candidate weapon of a model: its value alone against the fresh target.
template <typename Decl>
struct Candidate {
    Decl decl;
    double value = 0.0;
    bool close_quarters = false;
    std::size_t weapon = 0;
};

// 24.07 for one model: the best profile per weapon, then the better of its [CLOSE-QUARTERS] set and its other
// set (or every weapon when 24.07 does not apply). Ties keep the earlier option.
template <typename Decl>
std::vector<Decl> pick_model_set(const std::vector<Candidate<Decl>>& cands, bool one_set_only) {
    std::vector<Candidate<Decl>> best;  // per weapon
    for (const auto& c : cands) {
        auto it = std::find_if(best.begin(), best.end(), [&](const auto& b) { return b.weapon == c.weapon; });
        if (it == best.end()) {
            best.push_back(c);
        } else if (c.value > it->value) {
            *it = c;
        }
    }
    double cq = 0.0;
    double other = 0.0;
    bool any_cq = false;
    bool any_other = false;
    for (const auto& b : best) {
        (b.close_quarters ? cq : other) += b.value;
        (b.close_quarters ? any_cq : any_other) = true;
    }
    const bool use_cq = any_cq && (!any_other || cq > other);
    std::vector<Decl> out;
    for (const auto& b : best) {
        if (!one_set_only || b.close_quarters == use_cq) out.push_back(b.decl);
    }
    return out;
}

}  // namespace

// ---- OddsCache ----

const HitWoundOdds& OddsCache::hit_wound(const core::AttackProfile& profile, int toughness) {
    ++lookups_;
    const HitWoundKey key{opt_or(profile.skill), profile.torrent,         profile.strength, profile.hit_modifier,
                          profile.wound_modifier,  toughness, core::sustained_hits(profile)};
    auto it = hit_wound_.find(key);
    if (it == hit_wound_.end()) {
        ++computed_;
        it = hit_wound_.emplace(key, derive_hit_wound(profile, toughness)).first;
    }
    return it->second;
}

double OddsCache::unsaved(const core::AllocationGroup& group, int armour_penetration) {
    ++lookups_;
    const SaveKey key{armour_penetration, opt_or(group.save), opt_or(group.invuln_save)};
    auto it = saves_.find(key);
    if (it == saves_.end()) {
        ++computed_;
        int damaging = 0;
        for (int face = 1; face <= kFaces; ++face) {
            if (core::check_save_roll(face, group, armour_penetration).outcome == core::SaveOutcome::InflictsDamage) {
                ++damaging;
            }
        }
        it = saves_.emplace(key, static_cast<double>(damaging) / kFaces).first;
    }
    return it->second;
}

// ---- TargetSnapshot ----

int TargetSnapshot::wounds_left() const noexcept {
    int total = 0;
    for (const Slot& s : slots) total += s.wounds_left;
    return total;
}

TargetSnapshot snapshot_of(const core::Unit& target) {
    TargetSnapshot snap;
    if (target.models.empty()) return snap;
    snap.toughness = core::target_toughness(target);
    const std::vector<core::AllocationGroup> groups = core::create_allocation_groups(target);
    const auto orders = core::legal_allocation_orders(target, groups);
    snap.groups = orders.empty() ? groups : core::order_groups(groups, orders.front());
    for (std::size_t g = 0; g < snap.groups.size(); ++g) {
        // 05.04 Select Model: a model that has lost wounds first, then the others (any order: same W and Sv).
        for (const bool wounded_pass : {true, false}) {
            for (const core::ModelId id : snap.groups[g].models) {
                const core::Model* m = core::find_model(target, id);
                if (m == nullptr) continue;
                const bool wounded = m->wounds_remaining < m->characteristics.wounds;
                if (wounded != wounded_pass) continue;
                snap.slots.push_back(TargetSnapshot::Slot{m->wounds_remaining, m->characteristics.wounds, g});
            }
        }
    }
    return snap;
}

TargetSnapshot snapshot_with_models(const core::Unit& target, int models) {
    core::Unit copy = target;
    const auto n = static_cast<std::size_t>(std::clamp(models, 0, static_cast<int>(copy.models.size())));
    copy.models.resize(n);
    for (core::Model& m : copy.models) {
        m.wounds_remaining = m.characteristics.wounds;
        m.weapons.clear();
    }
    return snapshot_of(copy);
}

// ---- the exact outcome ----

AttackOutcome expected_outcome(OddsCache& cache, std::span<const VolleyBatch> volley, const TargetSnapshot& target) {
    AttackOutcome out;
    const std::size_t slots = target.slots.size();
    out.models_destroyed_pmf.assign(slots + 1, 0.0);
    out.wounds_left_pmf.assign(static_cast<std::size_t>(std::max(0, target.wounds_left())) + 1, 0.0);
    if (slots == 0) {
        out.models_destroyed_pmf[0] = 1.0;
        out.wounds_left_pmf[0] = 1.0;
        return out;
    }
    int max_w = 0;
    for (const auto& s : target.slots) max_w = std::max(max_w, s.wounds_left);
    const std::size_t width = static_cast<std::size_t>(max_w) + 1;
    // dist[s * width + w]: s models destroyed, the front model (slot s) has w wounds left.
    std::vector<double> dist((slots + 1) * width, 0.0);
    std::vector<double> next(dist.size(), 0.0);
    dist[static_cast<std::size_t>(target.slots[0].wounds_left)] = 1.0;

    for (const VolleyBatch& batch : volley) {
        if (batch.attack_dice <= 0) continue;
        const HitWoundOdds& hw = cache.hit_wound(batch.profile, target.toughness);
        const int damage = batch.profile.damage.fixed_value();  // D-002
        if (hw.sustained > 0) {
            // 24.36 (M60): a die scores 0, 1 or 1 + X hits; each hit wounds and gets past the save independently.
            // P(k wounds from one die), then k one-wound steps (the front model can change between them).
            const int x = hw.sustained;
            const double normal = hw.hit - hw.critical_hit;
            std::vector<double> pk(static_cast<std::size_t>(x) + 2, 0.0);
            pk[0] = 1.0 - hw.hit;
            for (int k = 0; k <= x + 1; ++k) {
                pk[static_cast<std::size_t>(k)] +=
                    normal * binomial(1, k, hw.wound) + hw.critical_hit * binomial(x + 1, k, hw.wound);
            }
            std::vector<double> u(target.groups.size(), 0.0);  // P(one wound gets past the save | group)
            for (std::size_t g = 0; g < u.size(); ++g) {
                u[g] = cache.unsaved(target.groups[g], batch.profile.armour_penetration);
            }
            const double hits_per_die = hw.hit + x * hw.critical_hit;
            out.attack_dice += batch.attack_dice;
            out.hits += batch.attack_dice * hits_per_die;
            out.wounds += batch.attack_dice * hits_per_die * hw.wound;
            out.unsaved += batch.attack_dice * hits_per_die * hw.wound * u[target.slots[0].group];
            std::vector<double> step(dist.size(), 0.0);
            std::vector<double> acc(dist.size(), 0.0);
            for (int die = 0; die < batch.attack_dice; ++die) {
                for (std::size_t i = 0; i < dist.size(); ++i) acc[i] = pk[0] * dist[i];
                for (int k = 1; k <= x + 1; ++k) {
                    // One more wound: dist -> step (the same transition as below, with u for the slot's group).
                    std::fill(step.begin(), step.end(), 0.0);
                    step[slots * width] += dist[slots * width];
                    for (std::size_t s = 0; s < slots; ++s) {
                        const double us = u[target.slots[s].group];
                        for (std::size_t w = 1; w < width; ++w) {
                            const double p = dist[s * width + w];
                            if (p == 0.0) continue;
                            step[s * width + w] += p * (1.0 - us);
                            const int left = static_cast<int>(w) - damage;
                            if (left > 0) {
                                step[s * width + static_cast<std::size_t>(left)] += p * us;
                            } else {
                                const std::size_t ns = s + 1;
                                const std::size_t nw =
                                    ns < slots ? static_cast<std::size_t>(target.slots[ns].wounds_left) : 0;
                                step[ns * width + nw] += p * us;
                            }
                        }
                    }
                    dist.swap(step);
                    for (std::size_t i = 0; i < dist.size(); ++i) acc[i] += pk[static_cast<std::size_t>(k)] * dist[i];
                }
                dist.swap(acc);
            }
            continue;
        }
        std::vector<double> q(target.groups.size(), 0.0);       // P(one die inflicts damage | group)
        for (std::size_t g = 0; g < q.size(); ++g) {
            q[g] = hw.hit * hw.wound * cache.unsaved(target.groups[g], batch.profile.armour_penetration);
        }
        out.attack_dice += batch.attack_dice;
        out.hits += batch.attack_dice * hw.hit;
        out.wounds += batch.attack_dice * hw.hit * hw.wound;
        out.unsaved += batch.attack_dice * q[target.slots[0].group];
        for (int die = 0; die < batch.attack_dice; ++die) {
            std::fill(next.begin(), next.end(), 0.0);
            next[slots * width] += dist[slots * width];  // the unit is destroyed: excess attacks are lost
            for (std::size_t s = 0; s < slots; ++s) {
                const double qs = q[target.slots[s].group];
                for (std::size_t w = 1; w < width; ++w) {
                    const double p = dist[s * width + w];
                    if (p == 0.0) continue;
                    next[s * width + w] += p * (1.0 - qs);
                    const int left = static_cast<int>(w) - damage;
                    if (left > 0) {
                        next[s * width + static_cast<std::size_t>(left)] += p * qs;
                    } else {
                        // Destroyed; the excess damage is lost (05.04, Appendix: Destroyed).
                        const std::size_t ns = s + 1;
                        const std::size_t nw =
                            ns < slots ? static_cast<std::size_t>(target.slots[ns].wounds_left) : 0;
                        next[ns * width + nw] += p * qs;
                    }
                }
            }
            dist.swap(next);
        }
    }

    // Read the outcome off the final distribution.
    std::vector<int> wounds_behind(slots + 1, 0);  // wounds of the slots after s
    for (std::size_t s = slots; s-- > 0;) {
        wounds_behind[s] = wounds_behind[s + 1] + (s + 1 < slots ? target.slots[s + 1].wounds_left : 0);
    }
    const auto front_fraction = [&](std::size_t s, std::size_t w) {
        if (s >= slots) return 0.0;
        const double wc = target.slots[s].wounds_char;
        return wc > 0 ? (wc - static_cast<double>(w)) / wc : 0.0;
    };
    const double initial_left = target.wounds_left();
    double remaining = 0.0;
    double equivalents = 0.0;
    for (std::size_t s = 0; s <= slots; ++s) {
        for (std::size_t w = 0; w < width; ++w) {
            const double p = dist[s * width + w];
            if (p == 0.0) continue;
            out.models_destroyed_pmf[s] += p;
            out.models_destroyed += p * static_cast<double>(s);
            const int left = static_cast<int>(w) + (s < slots ? wounds_behind[s] : 0);
            if (left >= 0 && static_cast<std::size_t>(left) < out.wounds_left_pmf.size()) {
                out.wounds_left_pmf[static_cast<std::size_t>(left)] += p;
            }
            remaining += p * (static_cast<double>(w) + (s < slots ? wounds_behind[s] : 0));
            equivalents += p * (static_cast<double>(s) + front_fraction(s, w));
        }
    }
    out.damage = initial_left - remaining;
    out.model_equivalents =
        equivalents - front_fraction(0, static_cast<std::size_t>(target.slots[0].wounds_left));
    return out;
}

// ---- the sampled outcome ----

SampledOutcome sample_outcome(std::span<const VolleyBatch> volley, const core::Unit& target, std::uint64_t seed,
                              int samples) {
    SampledOutcome out;
    out.samples = samples;
    // Only what the attack sequence reads: CHARACTER (05.03 groups), the models' characteristics and wounds.
    // Names, other keywords, abilities and weapons are dropped so each sample's copy is cheap.
    core::Unit stripped;
    stripped.id = target.id;
    stripped.side = target.side;
    if (core::has_keyword(target, "Character")) stripped.keywords = {"Character"};
    stripped.models = target.models;
    for (core::Model& m : stripped.models) {
        m.weapons.clear();
        m.type_name.clear();
    }
    const int models_before = static_cast<int>(stripped.models.size());
    out.models_destroyed_pmf.assign(static_cast<std::size_t>(models_before) + 1, 0.0);

    core::DiceState dice_state{seed, 0};  // the bot's own dice (D-054)
    core::SeededDice dice(dice_state);
    const core::AllocationOrderChooser first_order =
        [](const core::Unit&, std::span<const core::AllocationGroup>,
           std::span<const std::vector<std::size_t>> legal) { return legal.front(); };
    const core::ModelChooser first_model = [](const core::Unit&, std::span<const core::ModelId> options) {
        return options.front();
    };
    double sum_d = 0.0;
    double sum_d2 = 0.0;
    double sum_m = 0.0;
    double sum_m2 = 0.0;
    for (int i = 0; i < samples; ++i) {
        core::Unit unit = stripped;
        int damage = 0;
        for (const VolleyBatch& vb : volley) {
            if (unit.models.empty()) break;  // the rest of the volley has nothing to attack
            const core::AttackBatch batch{unit.id, vb.profile, {}, vb.attack_dice};
            const core::BatchResult r = core::resolve_attack_batch(dice, unit, batch, first_order, first_model);
            for (const core::SaveStep& step : r.damage.steps) {
                if (step.damage) damage += step.damage->wounds_lost;
            }
        }
        const int destroyed = models_before - static_cast<int>(unit.models.size());
        out.models_destroyed_pmf[static_cast<std::size_t>(destroyed)] += 1.0;
        sum_d += damage;
        sum_d2 += static_cast<double>(damage) * damage;
        sum_m += destroyed;
        sum_m2 += static_cast<double>(destroyed) * destroyed;
    }
    if (samples > 0) {
        const double n = samples;
        out.damage = sum_d / n;
        out.models_destroyed = sum_m / n;
        out.damage_sd = std::sqrt(std::max(0.0, sum_d2 / n - out.damage * out.damage));
        out.models_sd = std::sqrt(std::max(0.0, sum_m2 / n - out.models_destroyed * out.models_destroyed));
        for (double& p : out.models_destroyed_pmf) p /= n;
    }
    return out;
}

// ---- profiles ----

bool profile_supported(const core::WeaponProfile& profile) {
    return profile.attacks.is_fixed() && profile.damage.is_fixed() &&  // D-002
           !core::unimplemented_ability_problem(profile);               // D-026
}

bool within_half_range(const core::WeaponProfile& profile, float gap_mm) {
    return profile.range_mm.has_value() && core::within(gap_mm, *profile.range_mm * 0.5f);  // 24.30, as core
}

bool within_range(const core::WeaponProfile& profile, float gap_mm) {
    return profile.range_mm.has_value() && core::within(gap_mm, *profile.range_mm);  // 04.02, D-009
}

// ---- from a game state ----

std::vector<core::AttackBatch> shooting_batches(const core::GameState& state, core::UnitId shooter,
                                                core::ShootingType type,
                                                std::span<const core::ShotDeclaration> shots) {
    const core::Unit* unit = state.find_unit(shooter);
    if (unit == nullptr || !core::shot_problems(state, *unit, type, shots).empty()) return {};
    const auto selections = core::make_weapon_selections(state, *unit, type, shots);
    return core::gather_attack_batches(state, selections, core::shooting_context(state, *unit));
}

std::vector<core::AttackBatch> melee_batches(const core::GameState& state, core::UnitId attacker,
                                             std::span<const core::MeleeDeclaration> declarations) {
    const core::Unit* unit = state.find_unit(attacker);
    if (unit == nullptr || !core::melee_problems(state, *unit, declarations).empty()) return {};
    const auto selections = core::make_melee_selections(state, declarations);
    core::AttackContext context;
    context.type = core::AttackType::Fighting;
    return core::gather_attack_batches(state, selections, context);
}

std::vector<TargetOutcome> evaluate_batches(OddsCache& cache, const core::GameState& state,
                                            std::span<const core::AttackBatch> batches) {
    std::vector<TargetOutcome> out;
    std::vector<core::UnitId> targets;
    for (const core::AttackBatch& b : batches) {
        if (std::find(targets.begin(), targets.end(), b.target) == targets.end()) targets.push_back(b.target);
    }
    for (const core::UnitId t : targets) {
        const core::Unit* unit = state.find_unit(t);
        if (unit == nullptr) continue;
        Volley v;
        for (const core::AttackBatch& b : batches) {
            if (b.target == t) v.push_back(VolleyBatch{b.profile, b.attack_dice});
        }
        out.push_back(TargetOutcome{t, expected_outcome(cache, v, snapshot_of(*unit))});
    }
    return out;
}

double total_model_equivalents(std::span<const TargetOutcome> outcomes) {
    double total = 0.0;
    for (const TargetOutcome& o : outcomes) total += o.outcome.model_equivalents;
    return total;
}

std::vector<TargetOutcome> evaluate_shots(OddsCache& cache, const core::GameState& state, core::UnitId shooter,
                                          core::ShootingType type, std::span<const core::ShotDeclaration> shots) {
    const auto batches = shooting_batches(state, shooter, type, shots);
    return evaluate_batches(cache, state, batches);
}

std::vector<TargetOutcome> evaluate_melee(OddsCache& cache, const core::GameState& state, core::UnitId attacker,
                                          std::span<const core::MeleeDeclaration> declarations) {
    const auto batches = melee_batches(state, attacker, declarations);
    return evaluate_batches(cache, state, batches);
}

std::optional<ShootingPlan> best_shooting(OddsCache& cache, const core::GameState& state, core::UnitId shooter,
                                          core::UnitId target) {
    const core::Unit* unit = state.find_unit(shooter);
    const core::Unit* enemy = state.find_unit(target);
    if (unit == nullptr || enemy == nullptr || core::is_destroyed(*enemy)) return std::nullopt;
    const TargetSnapshot snap = snapshot_of(*enemy);
    std::optional<ShootingPlan> best;
    for (const core::ShootingType type : core::eligible_shooting_types(state, *unit)) {
        const auto options = core::shot_options(state, *unit, type);
        std::vector<core::ShotDeclaration> chosen;
        for (const core::Model& model : unit->models) {
            std::vector<Candidate<core::ShotDeclaration>> cands;
            for (const core::ShotDeclaration& opt : options) {
                if (opt.model != model.id || opt.target != target) continue;
                const core::ShotDeclaration one[] = {opt};
                const auto batches = shooting_batches(state, shooter, type, one);
                const Volley v = to_volley(batches);
                const core::WeaponProfile& p = model.weapons[opt.weapon_index].profiles[opt.profile_index];
                cands.push_back({opt, expected_outcome(cache, v, snap).model_equivalents, is_close_quarters(p),
                                 opt.weapon_index});
            }
            // 24.07 applies outside close-quarters shooting, to non-MONSTER/VEHICLE models.
            const bool one_set = type != core::ShootingType::CloseQuarters && !core::is_monster_or_vehicle(*unit, model);
            for (const auto& d : pick_model_set(cands, one_set)) chosen.push_back(d);
        }
        if (chosen.empty()) continue;
        const auto batches = shooting_batches(state, shooter, type, chosen);
        if (batches.empty()) continue;  // the combination broke a rule after all
        const Volley v = to_volley(batches);
        ShootingPlan plan{type, chosen, expected_outcome(cache, v, snap)};
        if (!best || plan.outcome.model_equivalents > best->outcome.model_equivalents) best = std::move(plan);
    }
    return best;
}

std::optional<MeleePlanValue> best_melee(OddsCache& cache, const core::GameState& state, core::UnitId attacker,
                                         core::UnitId target) {
    const core::Unit* unit = state.find_unit(attacker);
    const core::Unit* enemy = state.find_unit(target);
    if (unit == nullptr || enemy == nullptr || core::is_destroyed(*enemy)) return std::nullopt;
    const TargetSnapshot snap = snapshot_of(*enemy);
    const auto options = core::melee_options(state, *unit);
    std::vector<core::MeleeDeclaration> chosen;
    for (const core::Model& model : unit->models) {
        const auto at_target = [&](const core::MeleeDeclaration& opt) {
            return opt.model == model.id && opt.targets.size() == 1 && opt.targets.front().unit == target;
        };
        const auto extra = [&](const core::MeleeDeclaration& opt) {
            return core::is_extra_attacks_weapon(model.weapons[opt.weapon_index]);
        };
        // 24.11 (D-071): [EXTRA ATTACKS] weapons are added below, unless the model has no other weapon to pick.
        const bool has_other =
            std::any_of(options.begin(), options.end(), [&](const auto& o) { return at_target(o) && !extra(o); });
        std::optional<Candidate<core::MeleeDeclaration>> best;
        for (const core::MeleeDeclaration& opt : options) {
            if (!at_target(opt) || (has_other && extra(opt))) continue;
            const core::MeleeDeclaration one[] = {opt};
            const Volley v = to_volley(melee_batches(state, attacker, one));
            const double value = expected_outcome(cache, v, snap).model_equivalents;
            if (!best || value > best->value) best = Candidate<core::MeleeDeclaration>{opt, value, false, 0};
        }
        if (!best) continue;
        chosen.push_back(best->decl);  // D-021: one melee weapon per model
        for (const core::MeleeDeclaration& opt : options) {  // 24.11: and each of its [EXTRA ATTACKS] weapons
            if (!at_target(opt) || !extra(opt) || opt.weapon_index == best->decl.weapon_index) continue;
            const bool taken = std::any_of(chosen.begin(), chosen.end(), [&](const core::MeleeDeclaration& d) {
                return d.model == opt.model && d.weapon_index == opt.weapon_index;
            });
            if (!taken) chosen.push_back(opt);
        }
    }
    if (chosen.empty()) return std::nullopt;
    const auto batches = melee_batches(state, attacker, chosen);
    if (batches.empty()) return std::nullopt;
    return MeleePlanValue{chosen, expected_outcome(cache, to_volley(batches), snap)};
}

// ---- PositionProbe ----

void PositionProbe::place(core::UnitId unit, std::span<const core::Position> positions) {
    core::Unit* u = scratch_.find_unit(unit);
    if (u == nullptr) throw std::logic_error("PositionProbe: unknown unit");
    for (std::size_t i = 0; i < u->models.size() && i < positions.size(); ++i) u->models[i].position = positions[i];
}

void PositionProbe::set_moved(core::UnitId unit, const core::UnitTurnFlags& flags) {
    core::Unit* u = scratch_.find_unit(unit);
    if (u == nullptr) throw std::logic_error("PositionProbe: unknown unit");
    u->turn_flags = flags;
    u->phase_flags.selected_to_shoot = false;
}

// ---- hypothetical ----

Volley unit_ranged_volley(OddsCache& cache, const core::Unit& attacker, const core::Unit& target,
                          const ShotContext& context) {
    Volley volley;
    if (target.models.empty()) return volley;
    const TargetSnapshot snap = snapshot_of(target);
    core::AttackContext attack;
    attack.type = core::AttackType::Shooting;
    attack.heavy_conditions_met = context.heavy;
    struct Pick {
        core::AttackProfile profile;
        int dice = 0;
    };
    for (const core::Model& model : attacker.models) {
        std::vector<Candidate<Pick>> cands;
        for (std::size_t w = 0; w < model.weapons.size(); ++w) {
            for (const core::WeaponProfile& p : model.weapons[w].profiles) {
                if (p.kind != core::WeaponKind::Ranged || !profile_supported(p)) continue;
                if (!allowed_for_type(attacker, model, p, context.type) || !within_range(p, context.gap_mm)) continue;
                core::WeaponSelection sel;
                sel.target = target.id;
                sel.target_within_half_range = within_half_range(p, context.gap_mm);       // 24.30
                sel.target_models_at_selection = static_cast<int>(target.models.size());  // 24.05
                const core::AttackProfile ap = core::make_attack_profile(p, attack);
                const int dice = core::gather_attack_dice(p, sel, attack);
                const VolleyBatch one[] = {VolleyBatch{ap, dice}};
                cands.push_back({Pick{ap, dice}, expected_outcome(cache, one, snap).model_equivalents,
                                 is_close_quarters(p), w});
            }
        }
        const bool one_set =
            context.type != core::ShootingType::CloseQuarters && !core::is_monster_or_vehicle(attacker, model);
        for (const Pick& pick : pick_model_set(cands, one_set)) add_to_volley(volley, pick.profile, pick.dice);
    }
    return volley;
}

Volley unit_melee_volley(OddsCache& cache, const core::Unit& attacker, const core::Unit& target) {
    Volley volley;
    if (target.models.empty()) return volley;
    const TargetSnapshot snap = snapshot_of(target);
    core::AttackContext attack;
    attack.type = core::AttackType::Fighting;
    const auto usable = [](const core::WeaponProfile& p) {
        return p.kind == core::WeaponKind::Melee && profile_supported(p);
    };
    for (const core::Model& model : attacker.models) {
        // 24.11 (D-071): the best of the model's other melee weapons, plus every [EXTRA ATTACKS] weapon; a model
        // with no other weapon it can use picks among its [EXTRA ATTACKS] weapons too.
        const bool has_other = std::any_of(model.weapons.begin(), model.weapons.end(), [&](const core::Weapon& w) {
            return !core::is_extra_attacks_weapon(w) && std::any_of(w.profiles.begin(), w.profiles.end(), usable);
        });
        std::optional<VolleyBatch> best;
        std::size_t best_weapon = 0;
        double best_value = 0.0;
        for (std::size_t wi = 0; wi < model.weapons.size(); ++wi) {
            const core::Weapon& weapon = model.weapons[wi];
            if (has_other && core::is_extra_attacks_weapon(weapon)) continue;
            for (const core::WeaponProfile& p : weapon.profiles) {
                if (!usable(p)) continue;
                core::WeaponSelection sel;
                sel.target = target.id;
                const VolleyBatch one{core::make_attack_profile(p, attack), core::gather_attack_dice(p, sel, attack)};
                const double value = expected_outcome(cache, std::span<const VolleyBatch>(&one, 1), snap).model_equivalents;
                if (!best || value > best_value) {
                    best = one;
                    best_weapon = wi;
                    best_value = value;
                }
            }
        }
        if (!best) continue;
        add_to_volley(volley, best->profile, best->attack_dice);  // D-021: one melee weapon per model
        for (std::size_t wi = 0; wi < model.weapons.size(); ++wi) {
            const core::Weapon& weapon = model.weapons[wi];
            if (wi == best_weapon || !core::is_extra_attacks_weapon(weapon)) continue;
            for (const core::WeaponProfile& p : weapon.profiles) {  // its first usable profile (D-001)
                if (!usable(p)) continue;
                core::WeaponSelection sel;
                sel.target = target.id;
                add_to_volley(volley, core::make_attack_profile(p, attack), core::gather_attack_dice(p, sel, attack));
                break;
            }
        }
    }
    return volley;
}

}  // namespace wh40k::ai::analysis
