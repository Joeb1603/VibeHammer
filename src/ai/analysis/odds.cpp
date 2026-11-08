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
#include "core/rules/mortal_wounds.hpp"
#include "core/rules/random_characteristics.hpp"
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
    if (core::twin_linked(profile)) {
        // 24.38 (M61, D-072): the bot re-rolls every failed wound roll and no other. Each first face, then each face of
        // a failed one's re-roll, through core's own functions: P(wound) = p + (1 - p)p and P(critical wound) =
        // c + (1 - p)c exactly. Profiles without the ability keep the arithmetic above unchanged.
        double wound = 0.0;
        double critical = 0.0;
        for (int first = 1; first <= kFaces; ++first) {
            core::ScriptedDice first_die{first};
            core::WoundRolls w = core::make_wound_rolls(first_die, profile, 1, toughness);
            const std::vector<std::size_t> failed = core::failed_wound_rolls(w);
            if (failed.empty()) {
                wound += static_cast<double>(w.wounds()) / kFaces;
                critical += static_cast<double>(w.critical_wounds()) / kFaces;
                continue;
            }
            for (int second = 1; second <= kFaces; ++second) {
                core::WoundRolls again = w;
                core::ScriptedDice second_die{second};
                (void)core::reroll_wound_rolls(second_die, profile, again, failed);
                wound += static_cast<double>(again.wounds()) / (kFaces * kFaces);
                critical += static_cast<double>(again.critical_wounds()) / (kFaces * kFaces);
            }
        }
        o.wound = wound;
        o.critical_wound = critical;
        o.twin_linked = true;
    }
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

// 17.03 / 10.06: a MONSTER/VEHICLE unit (any of its models), as the engine's shooting rules test it.
bool unit_is_monster_or_vehicle(const core::Unit& unit) {
    return std::any_of(unit.models.begin(), unit.models.end(),
                       [&unit](const core::Model& m) { return core::is_monster_or_vehicle(unit, m); });
}

// D-077 (M68): the engagement facts of a hypothetical shot (ShotContext), as the engine's target_problem and
// make_weapon_selections read them from a state (04.02, 10.06, 17.03, D-073).
struct EngagementFacts {
    bool close_quarters = false;  // close-quarters shooting (10.06)
    bool with_target = false;     // the shooter is engaged with the target
    bool target_engaged = false;  // the target is engaged (with the shooter or another unit)
    bool target_mv = false;       // the target is a MONSTER/VEHICLE unit (only read, and only worked out, when engaged)
};

EngagementFacts facts_of(const ShotContext& context, const core::Unit& target) {
    EngagementFacts f;
    f.close_quarters = context.type == core::ShootingType::CloseQuarters;
    f.with_target = f.close_quarters && !context.elsewhere;
    f.target_engaged = context.target_engaged || f.with_target;
    f.target_mv = f.target_engaged && unit_is_monster_or_vehicle(target);
    return f;
}

// 04.02 / 10.06 / 17.03: may this model's profile target the unit (the target rules of the engine's target_ok, without
// range and visibility, which the caller and the state judge)?
bool target_allowed(const EngagementFacts& f, bool mv_model, const core::WeaponProfile& p) {
    const bool blast = p.has<core::ability::Blast>();
    if (f.close_quarters && !mv_model) {
        if (!f.with_target) return false;  // 10.06: only enemy units engaged with its unit
    } else if (f.close_quarters && f.with_target) {
        if (blast) return false;  // 10.06: [BLAST] can't target a unit its unit is engaged with
    } else if (f.target_engaged && !f.target_mv) {
        return false;  // 04.02 (17.03: an engaged MONSTER/VEHICLE unit can be targeted)
    }
    return !(blast && f.target_engaged && f.target_mv);  // 17.03, Rules Appendix FAQ (D-073)
}

// The -1s to hit of make_weapon_selections (D-073, added: D-011's cap is applied by make_hit_rolls).
int hit_modifier_of(const EngagementFacts& f, bool mv_model, const core::WeaponProfile& p) {
    const bool cq_at_engaged = is_close_quarters(p) && f.with_target;
    int modifier = 0;
    if (f.close_quarters && mv_model && !cq_at_engaged) modifier -= 1;  // 10.06 MONSTER/VEHICLE models
    if (f.target_mv && f.target_engaged && !cq_at_engaged) modifier -= 1;  // 17.03
    return modifier;
}

void add_to_volley(Volley& volley, const core::AttackProfile& profile, int dice,
                   const std::vector<core::DiceExpr>& random = {}) {
    if (dice <= 0 && random.empty()) return;
    for (VolleyBatch& b : volley) {
        if (b.profile == profile) {  // D-006: identical attacks are one batch
            b.attack_dice += dice;
            b.random_attacks.insert(b.random_attacks.end(), random.begin(), random.end());  // D-069
            return;
        }
    }
    volley.push_back(VolleyBatch{profile, dice, random});
}

Volley to_volley(std::span<const core::AttackBatch> batches) {
    Volley v;
    for (const core::AttackBatch& b : batches) v.push_back(volley_batch(b));
    return v;
}

// D-069: [a + b] = sum of P(a) P(b).
std::vector<double> convolve(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.empty() || b.empty()) return {};
    std::vector<double> out(a.size() + b.size() - 1, 0.0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] == 0.0) continue;
        for (std::size_t j = 0; j < b.size(); ++j) out[i + j] += a[i] * b[j];
    }
    return out;
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
                          profile.wound_modifier,  toughness, core::sustained_hits(profile),
                          core::twin_linked(profile)};
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

const std::vector<double>& OddsCache::value_pmf(const core::DiceExpr& expr) {
    ++lookups_;
    const ExprKey key{expr.num_dice, expr.sides, expr.modifier};
    auto it = values_.find(key);
    if (it == values_.end()) {
        ++computed_;
        std::vector<double> pmf{1.0};  // the sum of no dice is 0
        if (expr.num_dice > 0) {
            // One die of the expression, through core's own roll (01.05: a D3 halves the D6, rounding up).
            const core::DiceExpr one{1, expr.sides, 0};
            std::vector<double> die(static_cast<std::size_t>(one.max_value()) + 1, 0.0);
            for (int face = 1; face <= kFaces; ++face) {
                core::ScriptedDice d{face};
                die[static_cast<std::size_t>(core::roll_characteristic(d, one).value)] += 1.0 / kFaces;
            }
            for (int i = 0; i < expr.num_dice; ++i) pmf = convolve(pmf, die);
        }
        std::vector<double> shifted(static_cast<std::size_t>(std::max(0, static_cast<int>(expr.modifier))), 0.0);
        shifted.insert(shifted.end(), pmf.begin(), pmf.end());
        it = values_.emplace(key, std::move(shifted)).first;
    }
    return it->second;
}

const std::vector<double>& OddsCache::sum_pmf(std::span<const core::DiceExpr> exprs) {
    ++lookups_;
    std::vector<ExprKey> key;
    for (const core::DiceExpr& e : exprs) key.emplace_back(e.num_dice, e.sides, e.modifier);
    std::sort(key.begin(), key.end());
    auto it = sums_.find(key);
    if (it == sums_.end()) {
        ++computed_;
        std::vector<double> pmf{1.0};
        for (const core::DiceExpr& e : exprs) pmf = convolve(pmf, value_pmf(e));
        it = sums_.emplace(std::move(key), std::move(pmf)).first;
    }
    return it->second;
}

double pmf_mean(const std::vector<double>& pmf) {
    double m = 0.0;
    for (std::size_t v = 0; v < pmf.size(); ++v) m += static_cast<double>(v) * pmf[v];
    return m;
}

VolleyBatch volley_batch(const core::AttackBatch& batch) {
    VolleyBatch v{batch.profile, batch.attack_dice, {}};
    for (const core::RandomAttackDice& r : batch.random_attacks) {
        v.attack_dice += r.bonus;  // D-069: added after the roll; the same count either way
        v.random_attacks.push_back(r.attacks);
    }
    return v;
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
        const bool random_a = !batch.random_attacks.empty();  // D-069
        if (batch.attack_dice <= 0 && !random_a) continue;
        const HitWoundOdds& hw = cache.hit_wound(batch.profile, target.toughness);
        // D-069: a random D branches over its values; a fixed one takes the single transition it always did.
        const bool random_d = !batch.profile.damage.is_fixed();
        const int damage = random_d ? 0 : batch.profile.damage.fixed_value();
        const std::vector<double>* d_pmf = random_d ? &cache.value_pmf(batch.profile.damage) : nullptr;
        // D-069: the random A's dice, P(n) for n = 0..; and their mean, for the expected counts.
        const std::vector<double>* a_pmf = random_a ? &cache.sum_pmf(batch.random_attacks) : nullptr;
        const double extra_dice = random_a ? pmf_mean(*a_pmf) : 0.0;
        // `mass` lands on slot s, which had w wounds left, after `d` damage: the excess is lost if it is destroyed
        // (05.04, Appendix: Destroyed).
        const auto land = [&](std::vector<double>& to, std::size_t s, std::size_t w, int d, double mass) {
            const int left = static_cast<int>(w) - d;
            if (left > 0) {
                to[s * width + static_cast<std::size_t>(left)] += mass;
            } else {
                const std::size_t ns = s + 1;
                const std::size_t nw = ns < slots ? static_cast<std::size_t>(target.slots[ns].wounds_left) : 0;
                to[ns * width + nw] += mass;
            }
        };
        // One step that inflicts damage on slot s with chance chance[its group]: from -> to.
        const auto apply_one = [&](const std::vector<double>& from, std::vector<double>& to,
                                   const std::vector<double>& chance) {
            std::fill(to.begin(), to.end(), 0.0);
            to[slots * width] += from[slots * width];  // the unit is destroyed: excess attacks are lost
            for (std::size_t s = 0; s < slots; ++s) {
                const double qs = chance[target.slots[s].group];
                for (std::size_t w = 1; w < width; ++w) {
                    const double p = from[s * width + w];
                    if (p == 0.0) continue;
                    to[s * width + w] += p * (1.0 - qs);
                    if (d_pmf == nullptr) {
                        land(to, s, w, damage, p * qs);
                    } else {
                        for (std::size_t d = 1; d < d_pmf->size(); ++d) {
                            if ((*d_pmf)[d] != 0.0) land(to, s, w, static_cast<int>(d), p * qs * (*d_pmf)[d]);
                        }
                    }
                }
            }
        };
        // The batch's dice, one die at a time: the fixed ones, then (D-069) the random A's, each count weighted by its
        // chance (04.03).
        const auto run_dice = [&](const auto& one_die) {
            for (int die = 0; die < batch.attack_dice; ++die) one_die();
            if (!random_a) return;
            std::vector<double> mix(dist.size(), 0.0);
            for (std::size_t i = 0; i < dist.size(); ++i) mix[i] = (*a_pmf)[0] * dist[i];
            for (std::size_t n = 1; n < a_pmf->size(); ++n) {
                one_die();
                const double pn = (*a_pmf)[n];
                if (pn == 0.0) continue;
                for (std::size_t i = 0; i < dist.size(); ++i) mix[i] += pn * dist[i];
            }
            dist.swap(mix);
        };
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
            if (random_a) {
                out.attack_dice += extra_dice;
                out.hits += extra_dice * hits_per_die;
                out.wounds += extra_dice * hits_per_die * hw.wound;
                out.unsaved += extra_dice * hits_per_die * hw.wound * u[target.slots[0].group];
            }
            std::vector<double> step(dist.size(), 0.0);
            std::vector<double> acc(dist.size(), 0.0);
            run_dice([&]() {
                for (std::size_t i = 0; i < dist.size(); ++i) acc[i] = pk[0] * dist[i];
                for (int k = 1; k <= x + 1; ++k) {
                    // One more wound: dist -> step (the same transition as below, with u for the slot's group).
                    apply_one(dist, step, u);
                    dist.swap(step);
                    for (std::size_t i = 0; i < dist.size(); ++i) acc[i] += pk[static_cast<std::size_t>(k)] * dist[i];
                }
                dist.swap(acc);
            });
            continue;
        }
        {
            std::vector<double> q(target.groups.size(), 0.0);  // P(one die inflicts damage | group)
            for (std::size_t g = 0; g < q.size(); ++g) {
                q[g] = hw.hit * hw.wound * cache.unsaved(target.groups[g], batch.profile.armour_penetration);
            }
            out.attack_dice += batch.attack_dice;
            out.hits += batch.attack_dice * hw.hit;
            out.wounds += batch.attack_dice * hw.hit * hw.wound;
            out.unsaved += batch.attack_dice * q[target.slots[0].group];
            if (random_a) {
                out.attack_dice += extra_dice;
                out.hits += extra_dice * hw.hit;
                out.wounds += extra_dice * hw.hit * hw.wound;
                out.unsaved += extra_dice * q[target.slots[0].group];
            }
            run_dice([&]() {
                apply_one(dist, next, q);
                dist.swap(next);
            });
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

    // D-069: each batch's random A, rolled by resolve_attack_batch with the same dice.
    std::vector<std::vector<core::RandomAttackDice>> random(volley.size());
    for (std::size_t b = 0; b < volley.size(); ++b) {
        for (const core::DiceExpr& e : volley[b].random_attacks) random[b].push_back(core::RandomAttackDice{0, {}, e, 0});
    }
    core::DiceState dice_state{seed, 0};  // the bot's own dice (D-054)
    core::SeededDice dice(dice_state);
    const core::AllocationOrderChooser first_order =
        [](const core::Unit&, std::span<const core::AllocationGroup>,
           std::span<const std::vector<std::size_t>> legal) { return legal.front(); };
    const core::ModelChooser first_model = [](const core::Unit&, std::span<const core::ModelId> options) {
        return options.front();
    };
    // 24.38 (D-072): the attacker re-rolls every failed wound roll, as the bots do.
    const core::WoundRerollChooser all_failed = [](const core::WoundRolls& w) { return core::failed_wound_rolls(w); };
    double sum_d = 0.0;
    double sum_d2 = 0.0;
    double sum_m = 0.0;
    double sum_m2 = 0.0;
    for (int i = 0; i < samples; ++i) {
        core::Unit unit = stripped;
        int damage = 0;
        for (std::size_t b = 0; b < volley.size(); ++b) {
            const VolleyBatch& vb = volley[b];
            if (unit.models.empty()) break;  // the rest of the volley has nothing to attack
            const core::AttackBatch batch{unit.id, vb.profile, {}, vb.attack_dice, random[b]};
            const core::BatchResult r =
                core::resolve_attack_batch(dice, unit, batch, first_order, first_model, all_failed);
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
    // D-026 / D-071 ([TWIN-LINKED] is resolved, D-072). D-069: a random A or D is rolled; a random-A melee profile is
    // refused by the same check.
    return !core::unimplemented_ability_problem(profile);
}

double hazard_model_equivalents(const core::Unit& firer, int rolls) {
    if (rolls <= 0 || firer.models.empty()) return 0.0;
    // 06.03: whether one roll fails and its mortal wounds (1, or 3 if every model is a MONSTER/VEHICLE), face by face.
    double p_fail = 0.0;
    int per_failure = 0;
    for (int face = 1; face <= kFaces; ++face) {
        core::ScriptedDice die{face};
        const core::HazardRolls h = core::make_hazard_rolls(die, firer, 1);
        if (h.failures > 0) {
            p_fail += 1.0 / kFaces;
            per_failure = h.mortal_wounds;
        }
    }
    if (!(p_fail > 0.0) || per_failure <= 0) return 0.0;
    // 06.02 reads CHARACTER and the models' wounds; the rest is dropped so the copies are cheap.
    core::Unit stripped;
    stripped.id = firer.id;
    stripped.side = firer.side;
    if (core::has_keyword(firer, "Character")) stripped.keywords = {"Character"};
    stripped.models = firer.models;
    for (core::Model& m : stripped.models) {
        m.weapons.clear();
        m.type_name.clear();
    }
    const auto left = [](const core::Unit& u) {  // model equivalents left: a W-wound model at w wounds counts w / W
        double total = 0.0;
        for (const core::Model& m : u.models) {
            total += static_cast<double>(m.wounds_remaining) / static_cast<double>(std::max(1, m.characteristics.wounds));
        }
        return total;
    };
    const core::ModelChooser first = [](const core::Unit&, std::span<const core::ModelId> options) {
        return options.front();
    };
    const double before = left(stripped);
    double expected = 0.0;
    for (int k = 1; k <= rolls; ++k) {  // k failed rolls: k x per_failure mortal wounds, inflicted together (06.03)
        const double pk = binomial(rolls, k, p_fail);
        if (pk == 0.0) continue;
        core::Unit u = stripped;
        (void)core::inflict_mortal_wounds(u, k * per_failure, first);
        expected += pk * (before - left(u));
    }
    return expected;
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
            if (b.target == t) v.push_back(volley_batch(b));
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
                                          core::UnitId target, double hazard_rate) {
    const core::Unit* unit = state.find_unit(shooter);
    const core::Unit* enemy = state.find_unit(target);
    if (unit == nullptr || enemy == nullptr || core::is_destroyed(*enemy)) return std::nullopt;
    const TargetSnapshot snap = snapshot_of(*enemy);
    // D-077 (M68): [HAZARDOUS] priced at `hazard_rate` (0: not, as before M68), one hazard roll per weapon (24.15).
    std::optional<double> one_roll;
    const auto hazard_cost = [&](int rolls) {
        if (!(hazard_rate > 0.0) || rolls <= 0) return 0.0;
        if (rolls == 1) {
            if (!one_roll) one_roll = hazard_model_equivalents(*unit, 1);
            return hazard_rate * *one_roll;
        }
        return hazard_rate * hazard_model_equivalents(*unit, rolls);
    };
    std::optional<ShootingPlan> best;
    double best_value = 0.0;
    for (const core::ShootingType type : core::eligible_shooting_types(state, *unit)) {
        const auto options = core::shot_options(state, *unit, type);
        std::vector<core::ShotDeclaration> chosen;
        bool any_hazardous = false;
        for (const core::Model& model : unit->models) {
            std::vector<Candidate<core::ShotDeclaration>> cands;
            for (const core::ShotDeclaration& opt : options) {
                if (opt.model != model.id || opt.target != target) continue;
                const core::ShotDeclaration one[] = {opt};
                const auto batches = shooting_batches(state, shooter, type, one);
                const Volley v = to_volley(batches);
                const core::WeaponProfile& p = model.weapons[opt.weapon_index].profiles[opt.profile_index];
                double value = expected_outcome(cache, v, snap).model_equivalents;
                if (p.has<core::ability::Hazardous>()) value -= hazard_cost(1);  // D-077: its own hazard roll
                cands.push_back({opt, value, is_close_quarters(p), opt.weapon_index});
            }
            // 24.07 applies outside close-quarters shooting, to non-MONSTER/VEHICLE models.
            const bool one_set = type != core::ShootingType::CloseQuarters && !core::is_monster_or_vehicle(*unit, model);
            for (const auto& d : pick_model_set(cands, one_set)) {
                any_hazardous = any_hazardous ||
                                model.weapons[d.weapon_index].profiles[d.profile_index].has<core::ability::Hazardous>();
                chosen.push_back(d);
            }
        }
        if (chosen.empty()) continue;
        const auto batches = shooting_batches(state, shooter, type, chosen);
        if (batches.empty()) continue;  // the combination broke a rule after all
        const Volley v = to_volley(batches);
        ShootingPlan plan{type, chosen, expected_outcome(cache, v, snap)};
        double value = plan.outcome.model_equivalents;
        if (any_hazardous && hazard_rate > 0.0) {  // D-077: the engine's count of hazard rolls (24.15)
            value -= hazard_cost(
                core::hazardous_weapons_selected(state, core::make_weapon_selections(state, *unit, type, chosen)));
        }
        if (!best || value > best_value) {
            best = std::move(plan);
            best_value = value;
        }
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
    // D-077 (M68): who is engaged (04.02, 10.06, 17.03). With the default context (an unengaged target, close-quarters
    // shooting at the unit engaged with) and no MONSTER/VEHICLE on either side nothing below changes the M67 volley.
    const EngagementFacts facts = facts_of(context, target);
    if (facts.target_engaged && !facts.with_target && !facts.target_mv) return volley;  // 04.02: engaged, not ours
    const TargetSnapshot snap = snapshot_of(target);
    core::AttackContext attack;
    attack.type = core::AttackType::Shooting;
    attack.heavy_conditions_met = context.heavy;
    std::optional<double> hazard;  // D-077: the attacker's expected loss to one hazard roll (24.15), when priced
    struct Pick {
        core::AttackProfile profile;
        int dice = 0;
        std::vector<core::DiceExpr> random;  // D-069: a random A, with its bonus dice in `dice`
    };
    for (const core::Model& model : attacker.models) {
        // Only read in close-quarters shooting (10.06), so only worked out there.
        const bool mv_model = facts.close_quarters && core::is_monster_or_vehicle(attacker, model);
        std::vector<Candidate<Pick>> cands;
        for (std::size_t w = 0; w < model.weapons.size(); ++w) {
            for (const core::WeaponProfile& p : model.weapons[w].profiles) {
                if (p.kind != core::WeaponKind::Ranged || !profile_supported(p)) continue;
                if (!allowed_for_type(attacker, model, p, context.type) || !within_range(p, context.gap_mm)) continue;
                if (!target_allowed(facts, mv_model, p)) continue;  // D-077: 04.02, 10.06, 17.03
                core::WeaponSelection sel;
                sel.target = target.id;
                sel.target_within_half_range = within_half_range(p, context.gap_mm);       // 24.30
                sel.target_models_at_selection = static_cast<int>(target.models.size());  // 24.05
                core::AttackProfile ap = core::make_attack_profile(p, attack);
                ap.hit_modifier += hit_modifier_of(facts, mv_model, p);  // D-077: 10.06 and 17.03, as the engine adds them
                const core::SelectionDice sd = core::selection_dice(p, sel, attack);
                std::vector<core::DiceExpr> random;
                if (sd.random) random.push_back(*sd.random);  // D-069
                const int dice = sd.dice + sd.bonus;
                const VolleyBatch one[] = {VolleyBatch{ap, dice, random}};
                double value = expected_outcome(cache, one, snap).model_equivalents;
                if (context.hazard_rate > 0.0 && p.has<core::ability::Hazardous>()) {
                    if (!hazard) hazard = hazard_model_equivalents(attacker, 1);
                    value -= context.hazard_rate * *hazard;  // D-077: its hazard roll, in the target's model equivalents
                }
                cands.push_back({Pick{ap, dice, random}, value, is_close_quarters(p), w});
            }
        }
        const bool one_set =
            context.type != core::ShootingType::CloseQuarters && !core::is_monster_or_vehicle(attacker, model);
        for (const Pick& pick : pick_model_set(cands, one_set)) add_to_volley(volley, pick.profile, pick.dice, pick.random);
    }
    return volley;
}

Volley unit_melee_volley(OddsCache& cache, const core::Unit& attacker, const core::Unit& target, double hazard_rate) {
    Volley volley;
    if (target.models.empty()) return volley;
    const TargetSnapshot snap = snapshot_of(target);
    core::AttackContext attack;
    attack.type = core::AttackType::Fighting;
    const auto usable = [](const core::WeaponProfile& p) {
        return p.kind == core::WeaponKind::Melee && profile_supported(p);
    };
    std::optional<double> hazard;  // D-077: the attacker's expected loss to one hazard roll (24.15), when priced
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
                double value = expected_outcome(cache, std::span<const VolleyBatch>(&one, 1), snap).model_equivalents;
                if (hazard_rate > 0.0 && p.has<core::ability::Hazardous>()) {
                    if (!hazard) hazard = hazard_model_equivalents(attacker, 1);
                    value -= hazard_rate * *hazard;  // D-077: its hazard roll, in the target's model equivalents
                }
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
