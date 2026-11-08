#include "normal/move_score.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "analysis/charge_odds.hpp"
#include "bounds.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/units.hpp"
#include "core/rules/charge.hpp"
#include "core/state/queries.hpp"
#include "directions.hpp"
#include "normal/charge.hpp"
#include "normal/value.hpp"

namespace wh40k::ai::normal {

namespace {

// A unit closing on another ends its move just outside engagement range (09.05: it must end unengaged).
constexpr float kCloseGapMm = core::kEngagementRangeMm + 0.1f * core::MM_PER_INCH;
// 24.16 [HEAVY]: remained stationary or moved no more than 3" (strategic knowledge, as analysis::allowance_after).
constexpr float kHeavyMoveMm = 3.0f * core::MM_PER_INCH;
// Coherency (03.03): within 2" of another model.
constexpr float kCoherencyMm = 2.0f * core::MM_PER_INCH;
constexpr float kFar = 1.0e6f;
constexpr double kGapTieBreakPerInch = 1.0e-5;

core::Position centroid_of(const std::vector<core::Position>& ps) {
    core::Position c;
    for (const core::Position& p : ps) {
        c.x += p.x;
        c.y += p.y;
    }
    const float n = static_cast<float>(std::max<std::size_t>(1, ps.size()));
    return core::Position{c.x / n, c.y / n, 0.0f};
}

std::vector<core::Position> positions_of(const core::Unit& u) {
    std::vector<core::Position> out;
    for (const core::Model& m : u.models) out.push_back(m.position);
    return out;
}

std::vector<float> ranged_ranges(const core::Unit& u) {
    std::vector<float> out;
    for (const core::Model& m : u.models) {
        for (const core::Weapon& w : m.weapons) {
            for (const core::WeaponProfile& p : w.profiles) {
                if (p.kind != core::WeaponKind::Ranged || !p.range_mm || !analysis::profile_supported(p)) continue;
                out.push_back(*p.range_mm);
            }
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool keeps_heavy(float moved_mm) { return !core::more_than(moved_mm, kHeavyMoveMm); }

// The strategic allowance of a hypothetical move (analysis::allowance_after; the engine confirms in the refine).
analysis::TurnAllowance allowance_of(const PlannedEnd& plan) {
    using analysis::MovePlan;
    switch (plan.type) {
        case core::MoveType::RemainStationary:
            return analysis::allowance_after(MovePlan::RemainStationary);
        case core::MoveType::Advance:
            return analysis::allowance_after(MovePlan::Advance);
        case core::MoveType::FallBack:
            return analysis::allowance_after(MovePlan::FallBack);
        default:
            return analysis::allowance_after(keeps_heavy(plan.longest_path_mm) ? MovePlan::NormalUpTo3
                                                                               : MovePlan::Normal);
    }
}

struct ShootOverride {
    double share = 0.0;  // the engine's best shooting, as a share of the enemy unit
    double models_destroyed = 0.0;
    std::optional<OtherShot> other;  // D-066: the engine's best shooting is at another enemy unit (then share is 0)
};

MoveScore score_impl(MoveScoreContext& ctx, const PlannedEnd& plan, const std::optional<ShootOverride>& shoot,
                     const ChargeProspect* prospect) {
    MoveScore s;
    const core::Unit* enemy = ctx.enemy();
    const core::Unit& us = ctx.unit();
    if (enemy == nullptr || us.models.empty() || plan.positions.size() != us.models.size()) return s;
    const analysis::TurnAllowance allow = allowance_of(plan);
    const bool engaged_stay = plan.type == core::MoveType::RemainStationary && ctx.start_engaged();
    const std::vector<float> gaps = ctx.enemy_gaps(plan.positions);
    s.gap_mm = gaps.empty() ? kFar : gaps.front();
    const float enemy_half = ctx.enemy_threat().max_range_mm * 0.5f;
    s.in_enemy_half_range = enemy_half > 0.0f && core::within(s.gap_mm, enemy_half);

    ScoreLine line = ctx.new_line();
    // D-066 (M56): the other enemy units (none with one enemy unit: then nothing below changes).
    const bool others = ctx.other_count() > 0;
    if (engaged_stay) {
        // 10.06: close-quarters shooting at the unit we are engaged with, then our fight (we are the active player
        // and select first, 12.04), then the rest of the horizon.
        line.we_hit(ctx.close_quarters_rate(true));
        s.shoot_now = line.value();
        OtherThreat threat;
        if (others) threat = ctx.other_threat(plan.positions, std::nullopt);
        s.hold = ctx.engaged_line(line, 1.0, &s.p_survive, others ? &threat : nullptr) - s.shoot_now;
        s.enemy_reply = "fights on or falls back";
    } else {
        double share = 0.0;
        double models_destroyed = 0.0;
        if (allow.shoot) {
            analysis::AttackOutcome out;
            share = ctx.our_shooting(plan.positions, allow.type, allow.heavy, &out, &s.half_range_models);
            models_destroyed = out.models_destroyed;
        }
        if (shoot) {
            share = shoot->share;
            models_destroyed = shoot->models_destroyed;
        }
        // D-066: our best shot over every enemy unit; shooting another unit leaves the closest one untouched.
        std::optional<OtherShot> other;
        if (others) {
            if (shoot) {
                other = shoot->other;
            } else if (allow.shoot) {
                other = ctx.better_other_shot(plan.positions, allow.type, allow.heavy, share);
            }
            if (other) {
                share = 0.0;
                models_destroyed = 0.0;
            }
        }
        line.we_hit(share);
        s.shoot_now = line.value();
        // The defender removes its front models (the worst case for a charge, M40's distance_after_casualties).
        const auto k = static_cast<std::size_t>(std::floor(models_destroyed + 1e-9));
        const float gap_after = k < gaps.size() ? gaps[k] : kFar;
        const core::Position our_centroid = centroid_of(plan.positions);
        OtherThreat threat;
        if (others) threat = ctx.other_threat(plan.positions, other);
        const OtherThreat* tp = others ? &threat : nullptr;
        const double hold_total =
            ctx.hold_line(line, gap_after, our_centroid, &s.enemy_reply, &s.p_enemy_charge, &s.p_survive, tp);
        s.hold = hold_total - s.shoot_now;
        if (allow.charge) {
            const auto n = static_cast<double>(us.models.size());
            if (prospect != nullptr) {
                // M42's charge prospect: the exact odds of each roll and the models its charge move engages (only
                // they fight in this Fight phase); our line values the fight.
                const auto& pmf = analysis::two_d6_pmf();
                double gain = 0.0;
                for (std::size_t r = 0; r <= 12; ++r) {
                    if (!prospect->success_by_roll[r]) continue;
                    const double fighters = n > 0.0 ? static_cast<double>(prospect->engaged_by_roll[r]) / n : 0.0;
                    gain += pmf[r] * std::max(0.0, ctx.engaged_line(line, fighters, nullptr, tp) - hold_total);
                }
                s.p_charge = prospect->p_success;
                s.charge = gain;
            } else if (gap_after < kFar && core::within(gap_after, core::inches_to_mm(core::kChargeRangeInches))) {
                const double p = analysis::p_charge_roll_covers(gap_after);
                if (p > 0.0) {
                    s.p_charge = p;
                    s.charge = p * std::max(0.0, ctx.engaged_line(line, 1.0, nullptr, tp) - hold_total);
                }
            }
        }
        if (other) s.shoot_now += other->share;  // D-066: the other unit's losses, after the closest one's line
    }

    // Board-edge room: backed against an edge, the unit can't keep away next turn (scaled by the enemy's shooting).
    if (!engaged_stay && ctx.turns() >= 2 && ctx.our_threat().move_mm > 0.0f) {  // we move again in the horizon
        const float room = ctx.room_away(plan.positions, true, ctx.enemy_centroid());
        const float m = ctx.our_threat().move_mm;
        const double pinned = 1.0 - static_cast<double>(std::min(room, m) / m);
        const double threat = ctx.ranged_rate(false, core::ShootingType::Normal, true, enemy_half);
        s.edge = -ctx.weights().edge * pinned * threat;
    }
    // Coherency slack (03.03): models in a group of two or fewer (linked by "within 2\"") lose a model to coherency
    // whenever one of them is a casualty (03.03 doesn't need one group, so a unit can stand as separate pairs).
    if (us.models.size() >= 3) {
        const std::size_t n = us.models.size();
        std::vector<std::size_t> group(n);
        for (std::size_t i = 0; i < n; ++i) group[i] = i;
        const auto root = [&](std::size_t i) {
            while (group[i] != i) i = group[i] = group[group[i]];
            return i;
        };
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = i + 1; j < n; ++j) {
                // D-074 part 3: each model at its (fixed) facing (round bases: the old call).
                const float d = ctx.unit_round()
                                    ? core::base_distance(plan.positions[i], us.models[i].base, plan.positions[j],
                                                          us.models[j].base)
                                    : core::base_distance(
                                          core::BasePose{plan.positions[i], us.models[i].base, us.models[i].facing_deg},
                                          core::BasePose{plan.positions[j], us.models[j].base, us.models[j].facing_deg});
                if (core::within(d, kCoherencyMm)) group[root(i)] = root(j);
            }
        }
        std::vector<std::size_t> size(n, 0);
        for (std::size_t i = 0; i < n; ++i) ++size[root(i)];
        std::size_t fragile = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (size[root(i)] <= 2) ++fragile;
        }
        s.coherency = -ctx.weights().coherency * static_cast<double>(fragile) / static_cast<double>(n);
    }
    // Ties (positions the score cannot tell apart): the winning side closes, the losing side keeps away.
    if (s.gap_mm < kFar) {
        s.tie_break = -kGapTieBreakPerInch * ctx.lean() * static_cast<double>(s.gap_mm / core::MM_PER_INCH);
    }
    s.total = s.shoot_now + s.charge + s.hold + s.edge + s.coherency + s.tie_break;
    return s;
}

}  // namespace

// ---- the context ----

MoveScoreContext::MoveScoreContext(analysis::OddsCache& cache, const core::GameState& origin, core::UnitId unit,
                                   MoveWeights weights, Generation generation)
    : cache_(cache), origin_(origin), weights_(weights), generation_(generation) {
    const core::Unit* u = origin.find_unit(unit);
    if (u == nullptr) throw std::logic_error("MoveScoreContext: unknown unit");
    us_ = *u;
    if (origin.flow.move && origin.flow.move->unit == unit) {
        for (core::Model& m : us_.models) {
            for (const core::ModelStart& st : origin.flow.move->starts) {
                if (st.model == m.id) {
                    m.position = st.position;
                    m.facing_deg = st.facing_deg;  // D-074 part 3 (0 for a round base)
                }
            }
        }
    }
    us_round_ = all_round(us_.models);
    for (const core::Model& m : us_.models) us_extents_.push_back(edge_extents(m.base, m.facing_deg));
    us_threat_ = analysis::threat_ranges(us_);
    our_ranges_ = ranged_ranges(us_);
    turns_ = turns_left(origin);
    start_engaged_ = core::is_engaged(origin, us_);
    if (const core::Unit* e = closest_enemy(origin, us_)) {
        enemy_ = *e;
        enemy_models_ = core::posed_models(*e);
        enemy_round_ = all_round(enemy_models_);
        for (const core::PosedModel& m : enemy_models_) enemy_extents_.push_back(edge_extents(m.base, m.facing_deg));
        enemy_centroid_ = centroid_of(positions_of(*e));
        enemy_snap_ = analysis::snapshot_of(*e);
        them_threat_ = analysis::threat_ranges(*e);
        enemy_ranges_ = ranged_ranges(*e);
        const std::vector<core::UnitId> with_us = core::engaged_enemy_units(origin, us_);
        const bool engaged_with_us = std::find(with_us.begin(), with_us.end(), e->id) != with_us.end();
        enemy_engaged_elsewhere_ = !engaged_with_us && core::is_engaged(origin, *e);
        // Time preference (game length, D-007): the side losing the exchange prefers its losses later, the side
        // winning it prefers the enemy's sooner. The exchange: each side's best shooting at half range and melee.
        const double ours =
            ranged_rate(true, core::ShootingType::Normal, true, us_threat_.max_range_mm * 0.5f) + melee_rate(true);
        const double theirs =
            ranged_rate(false, core::ShootingType::Normal, true, them_threat_.max_range_mm * 0.5f) + melee_rate(false);
        const double total = ours + theirs;
        const double lean = total > 0.0 ? std::clamp((ours - theirs) / total, -1.0, 1.0) : 0.0;
        lean_ = lean;
        our_discount_ = 1.0 - weights_.delay * std::max(0.0, -lean);
        their_discount_ = 1.0 - weights_.delay * std::max(0.0, lean);
        const double progress =
            origin.battle_rounds > 0
                ? std::clamp(static_cast<double>(origin.flow.battle_round) / static_cast<double>(origin.battle_rounds),
                             0.0, 1.0)
                : 0.0;
        survival_weight_ = weights_.survival * std::max(0.0, -lean) * progress;
        // D-066 (M56): every other enemy unit, in unit order (M55: none).
        if (generation_ != Generation::M55) {
            for (const core::Unit* o : enemy_units(origin, us_.side)) {
                if (o->id == e->id) continue;
                Other x;
                x.unit = *o;
                x.models = core::posed_models(*o);
                x.round = all_round(x.models);
                x.snap = analysis::snapshot_of(*o);
                x.threat = analysis::threat_ranges(*o);
                x.ranges = ranged_ranges(*o);
                x.engaged = core::is_engaged(origin, *o);
                x.locked = x.engaged && std::find(with_us.begin(), with_us.end(), o->id) == with_us.end();
                x.other_target_mm = kFar;
                for (const core::Unit& f : origin.units) {
                    if (f.side != us_.side || f.id == us_.id || f.models.empty()) continue;
                    x.other_target_mm = std::min(x.other_target_mm, core::unit_distance(f, *o).value_or(kFar));
                }
                x.melee = melee_share(cache_, *o, us_);
                x.cq = ranged_share(cache_, *o, us_, 0.0f, true);
                if (!us_.models.empty()) {
                    const analysis::Volley mv = analysis::unit_melee_volley(cache_, *o, us_);
                    x.melee_wipe = analysis::expected_outcome(cache_, mv, analysis::snapshot_of(us_)).p_wipe();
                }
                others_.push_back(std::move(x));
            }
        }
    }
}

ScoreLine MoveScoreContext::new_line() const {
    return ScoreLine(alive_share(us_), enemy_ ? alive_share(*enemy_) : 0.0, our_discount_, their_discount_);
}

std::uint64_t MoveScoreContext::band_of(bool ours, float gap_mm) const {
    // The volley depends on the gap only through each profile's range and half range (odds.cpp within_range,
    // within_half_range), so the band is those tests' answers.
    const std::vector<float>& ranges = ours ? our_ranges_ : enemy_ranges_;
    std::uint64_t band = 0;
    for (std::size_t i = 0; i < ranges.size() && i < 32; ++i) {
        if (core::within(gap_mm, ranges[i])) band |= 1ull << (2 * i);
        if (core::within(gap_mm, ranges[i] * 0.5f)) band |= 1ull << (2 * i + 1);
    }
    return band;
}

double MoveScoreContext::ranged_rate(bool ours, core::ShootingType type, bool heavy, float gap_mm) {
    if (!enemy_) return 0.0;
    const RateKey key{ours, static_cast<int>(type), heavy, band_of(ours, gap_mm)};
    if (const auto it = rates_.find(key); it != rates_.end()) return it->second;
    const core::Unit& a = ours ? us_ : *enemy_;
    const core::Unit& t = ours ? *enemy_ : us_;
    const analysis::Volley v = analysis::unit_ranged_volley(cache_, a, t, analysis::ShotContext{type, heavy, gap_mm});
    const double share =
        unit_share(t, analysis::expected_outcome(cache_, v, analysis::snapshot_of(t)).model_equivalents);
    rates_.emplace(key, share);
    return share;
}

double MoveScoreContext::melee_rate(bool ours) {
    if (!enemy_) return 0.0;
    std::optional<double>& slot = melee_[ours ? 0 : 1];
    if (!slot) slot = ours ? melee_share(cache_, us_, *enemy_) : melee_share(cache_, *enemy_, us_);
    return *slot;
}

double MoveScoreContext::close_quarters_rate(bool ours) {
    if (!enemy_) return 0.0;
    std::optional<double>& slot = cq_[ours ? 0 : 1];
    if (!slot) {
        slot = ours ? ranged_share(cache_, us_, *enemy_, 0.0f, true) : ranged_share(cache_, *enemy_, us_, 0.0f, true);
    }
    return *slot;
}

double MoveScoreContext::enemy_ranged_wipe(core::ShootingType type, bool heavy, float gap_mm) {
    if (!enemy_ || us_.models.empty()) return 0.0;
    const RateKey key{false, static_cast<int>(type), heavy, band_of(false, gap_mm)};
    if (const auto it = wipes_.find(key); it != wipes_.end()) return it->second;
    const analysis::Volley v =
        analysis::unit_ranged_volley(cache_, *enemy_, us_, analysis::ShotContext{type, heavy, gap_mm});
    const double p = analysis::expected_outcome(cache_, v, analysis::snapshot_of(us_)).p_wipe();
    wipes_.emplace(key, p);
    return p;
}

double MoveScoreContext::enemy_melee_wipe() {
    if (!enemy_ || us_.models.empty()) return 0.0;
    if (!melee_wipe_) {
        const analysis::Volley v = analysis::unit_melee_volley(cache_, *enemy_, us_);
        melee_wipe_ = analysis::expected_outcome(cache_, v, analysis::snapshot_of(us_)).p_wipe();
    }
    return *melee_wipe_;
}

double MoveScoreContext::enemy_engaged_wipe() {
    if (!enemy_ || us_.models.empty()) return 0.0;
    if (!engaged_wipe_) {
        // Its turn in the combat: close-quarters shooting, then the fight.
        analysis::Volley v = analysis::unit_ranged_volley(
            cache_, *enemy_, us_, analysis::ShotContext{core::ShootingType::CloseQuarters, false, 0.0f});
        const analysis::Volley melee = analysis::unit_melee_volley(cache_, *enemy_, us_);
        v.insert(v.end(), melee.begin(), melee.end());
        engaged_wipe_ = analysis::expected_outcome(cache_, v, analysis::snapshot_of(us_)).p_wipe();
    }
    return *engaged_wipe_;
}

double MoveScoreContext::our_shooting(const std::vector<core::Position>& positions, core::ShootingType type, bool heavy,
                                      analysis::AttackOutcome* outcome, int* half_range_models) {
    if (!enemy_ || positions.size() != us_.models.size()) return 0.0;
    analysis::Volley volley;
    int half = 0;
    for (std::size_t i = 0; i < us_.models.size(); ++i) {
        const core::Model& m = us_.models[i];
        float gap = kFar;
        if (us_round_ && enemy_round_) {  // the round-only code, as before
            for (const core::PosedModel& e : enemy_models_) {
                gap = std::min(gap, core::base_distance(positions[i], m.base, e.position, e.base));
            }
        } else {
            const core::BasePose at{positions[i], m.base, m.facing_deg};  // D-074 part 3: range, exactly
            for (const core::PosedModel& e : enemy_models_) gap = std::min(gap, core::base_distance(at, e.pose()));
        }
        float longest = 0.0f;
        for (const core::Weapon& w : m.weapons) {
            for (const core::WeaponProfile& p : w.profiles) {
                if (p.kind == core::WeaponKind::Ranged && p.range_mm && analysis::profile_supported(p)) {
                    longest = std::max(longest, *p.range_mm);
                }
            }
        }
        if (longest > 0.0f && core::within(gap, longest * 0.5f)) ++half;
        if (enemy_engaged_elsewhere_) continue;  // 04.02: not while it is engaged with a friendly unit
        const std::uint64_t band = band_of(true, gap);
        const auto key = std::make_tuple(i, static_cast<int>(type), heavy, band);
        auto it = model_volleys_.find(key);
        if (it == model_volleys_.end()) {
            core::Unit one = us_;
            one.models = {m};
            one.models.front().position = positions[i];
            const analysis::ShotContext context{type, heavy, gap};
            it = model_volleys_.emplace(key, analysis::unit_ranged_volley(cache_, one, *enemy_, context)).first;
        }
        volley.insert(volley.end(), it->second.begin(), it->second.end());
    }
    if (half_range_models != nullptr) *half_range_models = half;
    const analysis::AttackOutcome o = analysis::expected_outcome(cache_, volley, enemy_snap_);
    if (outcome != nullptr) *outcome = o;
    return unit_share(*enemy_, o.model_equivalents);
}

std::vector<float> MoveScoreContext::enemy_gaps(const std::vector<core::Position>& positions) const {
    std::vector<float> out;
    out.reserve(enemy_models_.size());
    if (us_round_ && enemy_round_) {  // the round-only code, as before
        for (const core::PosedModel& e : enemy_models_) {
            float gap = kFar;
            for (std::size_t i = 0; i < us_.models.size() && i < positions.size(); ++i) {
                gap = std::min(gap, core::base_distance(positions[i], us_.models[i].base, e.position, e.base));
            }
            out.push_back(gap);
        }
    } else {  // D-074 part 3: each model at its facing
        for (const core::PosedModel& e : enemy_models_) {
            float gap = kFar;
            for (std::size_t i = 0; i < us_.models.size() && i < positions.size(); ++i) {
                gap = std::min(gap, core::base_distance(
                                        core::BasePose{positions[i], us_.models[i].base, us_.models[i].facing_deg},
                                        e.pose()));
            }
            out.push_back(gap);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

float MoveScoreContext::room_away(const std::vector<core::Position>& positions, bool ours,
                                  const core::Position& from) const {
    const core::Position c = centroid_of(positions);
    float ax = c.x - from.x;
    float ay = c.y - from.y;
    const float len = std::sqrt(ax * ax + ay * ay);
    if (!(len > 0.0f) || positions.empty()) return 0.0f;
    ax /= len;
    ay /= len;
    const float w = origin_.battlefield.width_mm;
    const float h = origin_.battlefield.depth_mm;
    float best = 0.0f;
    for (const client::UnitVector& d : client::kDirections16) {
        const float dot = d.x * ax + d.y * ay;
        if (dot < 0.5f) continue;  // within 60 degrees of straight away
        float t = kFar;
        for (std::size_t i = 0; i < positions.size(); ++i) {
            // D-074 part 3: the edge by the base's half extents at its facing (round: its radius, as before).
            const core::BaseExtents& ext = ours ? us_extents_[std::min(i, us_extents_.size() - 1)]
                                                : enemy_extents_[std::min(i, enemy_extents_.size() - 1)];
            const float rx = ext.half_x_mm;
            const float ry = ext.half_y_mm;
            const core::Position& p = positions[i];
            if (d.x > 1e-6f) t = std::min(t, (w - rx - p.x) / d.x);
            if (d.x < -1e-6f) t = std::min(t, (rx - p.x) / d.x);
            if (d.y > 1e-6f) t = std::min(t, (h - ry - p.y) / d.y);
            if (d.y < -1e-6f) t = std::min(t, (ry - p.y) / d.y);
        }
        best = std::max(best, std::max(0.0f, t) * dot);
    }
    return best;
}

double MoveScoreContext::our_next_turn(const ScoreLine& line, float gap_mm) {
    if (turns_ < 2) return line.value();
    ScoreLine y = line;
    y.next_half_turn();
    // Stay ([HEAVY]), close to our half range or close by our M; shoot; then charge if the roll can cover the gap.
    const float m = us_threat_.move_mm;
    const float half = us_threat_.max_range_mm * 0.5f;
    std::vector<float> ends{gap_mm};
    if (m > 0.0f && gap_mm < kFar) {
        const float close = std::max(gap_mm - m, kCloseGapMm);
        if (close < gap_mm) ends.push_back(close);
        if (half > 0.0f && half < gap_mm && gap_mm - half <= m) {
            ends.push_back(std::max(half - 0.01f * core::MM_PER_INCH, kCloseGapMm));
        }
    }
    double best = 0.0;
    for (std::size_t i = 0; i < ends.size(); ++i) {
        const float end = ends[i];
        ScoreLine z = y;
        if (gap_mm < kFar) z.we_hit(ranged_rate(true, core::ShootingType::Normal, keeps_heavy(gap_mm - end), end));
        const double nocharge = z.value();
        const double p = gap_mm < kFar ? analysis::p_charge_roll_covers(end) : 0.0;
        double v = nocharge;
        if (p > 0.0) {
            ScoreLine c = z;
            c.we_hit(melee_rate(true));  // Fights First (11.04)
            c.they_hit(melee_rate(false));
            v = (1.0 - p) * nocharge + p * std::max(c.value(), nocharge);
        }
        if (i == 0 || v > best) best = v;
    }
    return best;
}

double MoveScoreContext::enemy_turn_from(const ScoreLine& line, float from_mm, float to_mm, double* p_charge,
                                         double* p_survive, const OtherThreat* others) {
    const bool heavy = keeps_heavy(from_mm - to_mm);
    ScoreLine y = line;
    y.next_half_turn();
    y.they_hit(ranged_rate(false, core::ShootingType::Normal, heavy, to_mm));
    if (others != nullptr) y.others_hit(others->unengaged);  // D-066: the other enemy units' replies
    const double w = survival_weight_;
    double shot_survive = 1.0 - enemy_ranged_wipe(core::ShootingType::Normal, heavy, to_mm);
    if (others != nullptr) shot_survive *= others->survive;
    const double nocharge = our_next_turn(y, to_mm) + w * shot_survive;
    const double p = analysis::p_charge_roll_covers(to_mm);
    if (p_charge != nullptr) *p_charge = p;
    if (!(p > 0.0)) {
        if (p_survive != nullptr) *p_survive = shot_survive;
        return nocharge;
    }
    // Their charge: they strike first (Fights First), we reply; then our turn engaged (close-quarters shooting,
    // we fight first, they reply). Made only if it is worse for us than not charging.
    ScoreLine c = y;
    c.they_hit(melee_rate(false));
    c.we_hit(melee_rate(true));
    if (turns_ >= 2) {
        c.next_half_turn();
        c.we_hit(close_quarters_rate(true));
        c.we_hit(melee_rate(true));
        c.they_hit(melee_rate(false));
    }
    const double charge_survive = shot_survive * (1.0 - enemy_melee_wipe());
    const double charged = c.value() + w * charge_survive;
    const bool charges = charged < nocharge;
    if (p_survive != nullptr) *p_survive = (1.0 - p) * shot_survive + p * (charges ? charge_survive : shot_survive);
    return (1.0 - p) * nocharge + p * std::min(charged, nocharge);
}

double MoveScoreContext::hold_line(const ScoreLine& line, float gap_mm, const core::Position& our_centroid,
                                   std::string* reply, double* p_enemy_charge, double* p_survive,
                                   const OtherThreat* others) {
    if (p_enemy_charge != nullptr) *p_enemy_charge = 0.0;
    if (p_survive != nullptr) *p_survive = 1.0;
    const double w = survival_weight_;
    if (turns_ == 0 || !enemy_) {
        if (reply != nullptr) *reply = "the game ends";
        return line.value() + w;
    }
    if (enemy_engaged_elsewhere_ || gap_mm >= kFar) {
        // Engaged with another unit of ours it can neither shoot us nor charge (04.02, 11.02); wiped out, nothing.
        if (reply != nullptr) *reply = gap_mm >= kFar ? "is wiped out" : "is locked in combat";
        ScoreLine y = line;
        y.next_half_turn();
        if (others != nullptr) {  // D-066: the other enemy units still reply
            y.others_hit(others->unengaged);
            if (p_survive != nullptr) *p_survive = others->survive;
            return our_next_turn(y, gap_mm >= kFar ? kFar : gap_mm) + w * others->survive;
        }
        return our_next_turn(y, gap_mm >= kFar ? kFar : gap_mm) + w;
    }
    const float m = them_threat_.move_mm;
    double best = 0.0;
    std::string best_reply;
    double best_p = 0.0;
    double best_survive = 1.0;
    bool first = true;
    const auto consider = [&](double v, const char* what, double p, double survive) {
        if (first || v < best - 1e-12) {
            best = v;
            best_reply = what;
            best_p = p;
            best_survive = survive;
            first = false;
        }
    };
    {  // stay and shoot ([HEAVY]), then charge if the roll can cover the gap
        double p = 0.0;
        double survive = 1.0;
        const double v = enemy_turn_from(line, gap_mm, gap_mm, &p, &survive, others);
        consider(v, p > 0.0 ? "stays, shoots and charges" : "stays and shoots", p, survive);
    }
    if (m > 0.0f) {
        const float half = them_threat_.max_range_mm * 0.5f;
        if (half > 0.0f && half < gap_mm && gap_mm - half <= m) {
            double p = 0.0;
            double survive = 1.0;
            const double v = enemy_turn_from(line, gap_mm, std::max(half - 0.01f * core::MM_PER_INCH, kCloseGapMm), &p,
                                             &survive, others);
            consider(v, "closes to half range", p, survive);
        }
        const float close = std::max(gap_mm - m, kCloseGapMm);
        if (close < gap_mm) {
            double p = 0.0;
            double survive = 1.0;
            const double v = enemy_turn_from(line, gap_mm, close, &p, &survive, others);
            consider(v, p > 0.0 ? "closes and charges" : "closes and shoots", p, survive);
        }
        // Advance (09.06): [ASSAULT] weapons only, no charge.
        if (them_threat_.max_assault_range_mm > 0.0f) {
            float mean = 0.0f;
            for (std::size_t f = 1; f <= 6; ++f) {
                mean += static_cast<float>(analysis::d6_pmf()[f]) * them_threat_.advance_mm[f];
            }
            const float adv = std::max(gap_mm - mean, kCloseGapMm);
            ScoreLine y = line;
            y.next_half_turn();
            y.they_hit(ranged_rate(false, core::ShootingType::Assault, false, adv));
            if (others != nullptr) y.others_hit(others->unengaged);
            double survive = 1.0 - enemy_ranged_wipe(core::ShootingType::Assault, false, adv);
            if (others != nullptr) survive *= others->survive;
            consider(our_next_turn(y, adv) + w * survive, "advances and shoots", 0.0, survive);
        }
        // Keep away from us, as far as the battlefield lets it.
        std::vector<core::Position> theirs;
        for (const core::PosedModel& e : enemy_models_) theirs.push_back(e.position);
        const float room = std::min(m, room_away(theirs, false, our_centroid));
        if (room > 0.5f * core::MM_PER_INCH) {
            const float away = gap_mm + room;
            ScoreLine y = line;
            y.next_half_turn();
            y.they_hit(ranged_rate(false, core::ShootingType::Normal, keeps_heavy(room), away));
            if (others != nullptr) y.others_hit(others->unengaged);
            double survive = 1.0 - enemy_ranged_wipe(core::ShootingType::Normal, keeps_heavy(room), away);
            if (others != nullptr) survive *= others->survive;
            consider(our_next_turn(y, away) + w * survive, "keeps away and shoots", 0.0, survive);
        }
    }
    if (reply != nullptr) *reply = best_reply;
    if (p_enemy_charge != nullptr) *p_enemy_charge = best_p;
    if (p_survive != nullptr) *p_survive = best_survive;
    return best;
}

double MoveScoreContext::engaged_line(const ScoreLine& line, double our_fighters, double* p_survive,
                                      const OtherThreat* others) {
    ScoreLine c = line;
    // Our fight now: Fights First after a charge (11.04), or we select first (12.04).
    c.we_hit(melee_rate(true) * std::clamp(our_fighters, 0.0, 1.0));
    c.they_hit(melee_rate(false));
    const double w = survival_weight_;
    const double now_survive = 1.0 - enemy_melee_wipe();
    if (turns_ < 1 || !enemy_) {
        if (p_survive != nullptr) *p_survive = now_survive;
        return c.value() + w * now_survive;
    }
    // Their turn: stay in the combat (close-quarters shooting, they fight first, we reply) or fall back (09.07: no
    // shooting or charge for them; then our turn from about their M away).
    ScoreLine stay = c;
    stay.next_half_turn();
    stay.they_hit(close_quarters_rate(false));
    // D-066: the other enemy units can't shoot us while we are engaged (04.02); they may charge (or fight) us.
    if (others != nullptr) stay.others_hit(others->engaged);
    stay.they_hit(melee_rate(false));
    stay.we_hit(melee_rate(true));
    if (turns_ >= 2) {
        stay.next_half_turn();
        stay.we_hit(close_quarters_rate(true));
        stay.we_hit(melee_rate(true));
        stay.they_hit(melee_rate(false));
    }
    double on_survive = now_survive * (1.0 - enemy_engaged_wipe());
    double away_survive = now_survive;
    if (others != nullptr) {
        on_survive *= others->survive_engaged;
        away_survive *= others->survive;
    }
    const double fight_on = stay.value() + w * on_survive;
    double fall_back = fight_on;
    if (them_threat_.move_mm > 0.0f) {
        ScoreLine away = c;
        away.next_half_turn();
        if (others != nullptr) away.others_hit(others->unengaged);  // it fell back: we stand unengaged
        fall_back = our_next_turn(away, them_threat_.move_mm) + w * away_survive;
    }
    if (p_survive != nullptr) *p_survive = fall_back < fight_on ? away_survive : on_survive;
    return std::min(fight_on, fall_back);
}


// ---- D-066 (M56): the other enemy units ----

std::uint64_t MoveScoreContext::other_band(const Other& o, float gap_mm) const {
    std::uint64_t band = 0;
    for (std::size_t i = 0; i < o.ranges.size() && i < 32; ++i) {
        if (core::within(gap_mm, o.ranges[i])) band |= 1ull << (2 * i);
        if (core::within(gap_mm, o.ranges[i] * 0.5f)) band |= 1ull << (2 * i + 1);
    }
    return band;
}

// Its whole-unit shooting at us across `gap_mm` (Normal shooting): (share of our unit, P(it wipes us out)).
std::pair<double, double> MoveScoreContext::other_ranged(Other& o, bool heavy, float gap_mm) {
    const std::pair<bool, std::uint64_t> key{heavy, other_band(o, gap_mm)};
    if (const auto it = o.ranged.find(key); it != o.ranged.end()) return it->second;
    std::pair<double, double> v{0.0, 0.0};
    if (!us_.models.empty()) {
        const analysis::Volley volley = analysis::unit_ranged_volley(
            cache_, o.unit, us_, analysis::ShotContext{core::ShootingType::Normal, heavy, gap_mm});
        if (!volley.empty()) {
            const analysis::AttackOutcome out = analysis::expected_outcome(cache_, volley, analysis::snapshot_of(us_));
            v = {unit_share(us_, out.model_equivalents), out.p_wipe()};
        }
    }
    o.ranged.emplace(key, v);
    return v;
}

float MoveScoreContext::gap_to_other(const Other& o, const std::vector<core::Position>& positions) const {
    float gap = kFar;
    for (std::size_t i = 0; i < us_.models.size() && i < positions.size(); ++i) {
        if (us_round_ && o.round) {  // the round-only code, as before
            for (const core::PosedModel& e : o.models) {
                gap = std::min(gap, core::base_distance(positions[i], us_.models[i].base, e.position, e.base));
            }
            continue;
        }
        const core::BasePose at{positions[i], us_.models[i].base, us_.models[i].facing_deg};  // D-074 part 3
        for (const core::PosedModel& e : o.models) gap = std::min(gap, core::base_distance(at, e.pose()));
    }
    return gap;
}

double MoveScoreContext::our_shooting_at_other(std::size_t index, const std::vector<core::Position>& positions,
                                               core::ShootingType type, bool heavy, analysis::AttackOutcome* outcome) {
    if (outcome != nullptr) *outcome = analysis::AttackOutcome{};
    Other& o = others_[index];
    if (o.engaged || o.models.empty() || positions.size() != us_.models.size()) return 0.0;  // 04.02: unengaged only
    std::vector<std::uint64_t> bands(us_.models.size());
    std::vector<float> gaps(us_.models.size());
    bool any = false;
    for (std::size_t i = 0; i < us_.models.size(); ++i) {
        float gap = kFar;
        if (us_round_ && o.round) {  // the round-only code, as before
            for (const core::PosedModel& e : o.models) {
                gap = std::min(gap, core::base_distance(positions[i], us_.models[i].base, e.position, e.base));
            }
        } else {
            const core::BasePose at{positions[i], us_.models[i].base, us_.models[i].facing_deg};  // D-074 part 3
            for (const core::PosedModel& e : o.models) gap = std::min(gap, core::base_distance(at, e.pose()));
        }
        gaps[i] = gap;
        bands[i] = band_of(true, gap);
        any = any || bands[i] != 0;
    }
    if (!any) return 0.0;  // out of range of every weapon
    const auto key = std::make_tuple(static_cast<int>(type), heavy, bands);
    auto hit = o.our_shots.find(key);
    if (hit == o.our_shots.end()) {
        analysis::Volley volley;
        for (std::size_t i = 0; i < us_.models.size(); ++i) {
            const auto vkey = std::make_tuple(i, static_cast<int>(type), heavy, bands[i]);
            auto it = o.our_volleys.find(vkey);
            if (it == o.our_volleys.end()) {
                core::Unit one = us_;
                one.models = {us_.models[i]};
                one.models.front().position = positions[i];
                it = o.our_volleys
                         .emplace(vkey, analysis::unit_ranged_volley(cache_, one, o.unit,
                                                                     analysis::ShotContext{type, heavy, gaps[i]}))
                         .first;
            }
            volley.insert(volley.end(), it->second.begin(), it->second.end());
        }
        const analysis::AttackOutcome out = analysis::expected_outcome(cache_, volley, o.snap);
        hit = o.our_shots.emplace(key, std::make_pair(unit_share(o.unit, out.model_equivalents), out.models_destroyed))
                  .first;
    }
    if (outcome != nullptr) outcome->models_destroyed = hit->second.second;
    return hit->second.first;
}

std::optional<OtherShot> MoveScoreContext::better_other_shot(const std::vector<core::Position>& positions,
                                                             core::ShootingType type, bool heavy,
                                                             double closest_share) {
    std::optional<OtherShot> best;
    double best_share = closest_share;
    for (std::size_t i = 0; i < others_.size(); ++i) {
        analysis::AttackOutcome out;
        const double v = our_shooting_at_other(i, positions, type, heavy, &out);
        if (v > best_share + 1e-9) {
            best = OtherShot{i, v, out.models_destroyed};
            best_share = v;
        }
    }
    return best;
}

OtherThreat MoveScoreContext::other_threat(const std::vector<core::Position>& positions,
                                           const std::optional<OtherShot>& shot) {
    OtherThreat t;
    const float m_close = kCloseGapMm;
    for (std::size_t i = 0; i < others_.size(); ++i) {
        Other& o = others_[i];
        if (o.locked || o.models.empty()) continue;  // 04.02, 11.02: busy with another of our units
        double left = 1.0;  // its strength left after our shooting this turn
        if (shot && shot->index == i) {
            const double alive = alive_share(o.unit);
            left = alive > 0.0 ? std::clamp(1.0 - shot->share / alive, 0.0, 1.0) : 0.0;
        }
        // Which replies count (an enemy unit engaged with us always does: it can only fight us):
        // - winning the exchange, the side plays for tempo and only its total losses matter: marginal exposure, an
        //   enemy unit that can reach another of our units anyway (its Move and its longest range) shoots or charges
        //   one of them whatever this move does, so only one with no other target counts;
        // - losing it, the side plays for the draw (D-007) and this unit's own survival matters: an enemy unit counts
        //   whenever this unit would be its closest target (it shoots and charges the closest, D-010: a tie counts).
        const bool losing = lean_ < 0.0;
        const bool busy = !losing && core::within(o.other_target_mm, o.threat.shooting_reach_mm());
        if (busy && !o.engaged) continue;
        const float g = gap_to_other(o, positions);
        if (!(g < kFar)) continue;
        if (losing && !o.engaged && core::more_than(g, o.other_target_mm)) continue;
        if (core::within(g, core::kEngagementRangeMm)) {
            // Engaged with us: in its turn it shoots at close quarters and fights.
            const double loss = left * (o.cq + o.melee);
            t.unengaged += loss;
            t.engaged += loss;
            t.survive *= 1.0 - left * o.melee_wipe;
            t.survive_engaged *= 1.0 - left * o.melee_wipe;
            continue;
        }
        if (busy) continue;
        // Its reply worst for us: stay and shoot ([HEAVY]), close to its half range, or close by its M; then a
        // charge if the roll covers the gap (it strikes first).
        const float mv = o.threat.move_mm;
        std::vector<float> ends{g};
        if (mv > 0.0f) {
            const float half = o.threat.max_range_mm * 0.5f;
            if (half > 0.0f && half < g && g - half <= mv) {
                ends.push_back(std::max(half - 0.01f * core::MM_PER_INCH, m_close));
            }
            const float close = std::max(g - mv, m_close);
            if (close < g) ends.push_back(close);
        }
        double worst = -1.0;
        double worst_wipe = 0.0;
        for (const float end : ends) {
            const std::pair<double, double> r = other_ranged(o, keeps_heavy(g - end), end);
            const double p = analysis::p_charge_roll_covers(end);
            const double loss = r.first + p * o.melee;
            if (loss > worst + 1e-12) {
                worst = loss;
                worst_wipe = 1.0 - (1.0 - r.second) * (1.0 - p * o.melee_wipe);
            }
        }
        t.unengaged += left * std::max(0.0, worst);
        t.survive *= 1.0 - left * worst_wipe;
        // While we are engaged it can't shoot us (04.02); it may charge after a Normal move.
        const double pc = o.threat.p_charge_after_move(g);
        t.engaged += left * pc * o.melee;
        t.survive_engaged *= 1.0 - left * pc * o.melee_wipe;
    }
    return t;
}

// ---- the scores ----

MoveScore score_move_plan(MoveScoreContext& ctx, const PlannedEnd& plan) {
    return score_impl(ctx, plan, std::nullopt, nullptr);
}

MoveScore refine_move_score(MoveScoreContext& ctx, const PlannedEnd& plan, const core::GameState& after,
                            std::uint64_t seed, bool with_charge_prospect) {
    const core::Unit* enemy = ctx.enemy();
    if (enemy == nullptr) return score_move_plan(ctx, plan);
    ShootOverride shoot;
    const bool engaged_stay = plan.type == core::MoveType::RemainStationary && ctx.start_engaged();
    if (plan.type != core::MoveType::FallBack && !engaged_stay) {
        if (const auto best = analysis::best_shooting(ctx.cache(), after, ctx.unit().id, enemy->id)) {
            if (const core::Unit* e = after.find_unit(enemy->id)) {
                shoot.share = unit_share(*e, best->outcome.model_equivalents);
                shoot.models_destroyed = best->outcome.models_destroyed;
            }
        }
        // D-066: the engine's best shooting at every other enemy unit; the best unit is the one shot.
        double best_share = shoot.share;
        for (std::size_t i = 0; i < ctx.other_count(); ++i) {
            const core::UnitId id = ctx.other(i).id;
            const auto best = analysis::best_shooting(ctx.cache(), after, ctx.unit().id, id);
            const core::Unit* e = best ? after.find_unit(id) : nullptr;
            if (e == nullptr) continue;
            const double v = unit_share(*e, best->outcome.model_equivalents);
            if (v > best_share + 1e-9) {
                best_share = v;
                shoot.other = OtherShot{i, v, best->outcome.models_destroyed};
            }
        }
    }
    std::optional<ChargeProspect> prospect;
    const analysis::TurnAllowance allow = allowance_of(plan);
    const bool may_charge = allow.charge && !engaged_stay;
    if (with_charge_prospect && may_charge) {
        if (shoot.other) {
            prospect = charge_prospect(ctx.cache(), after, ctx.unit().id, seed, shoot.other->models_destroyed,
                                       ctx.other(shoot.other->index).id, ctx.generation());
        } else {
            prospect = charge_prospect(ctx.cache(), after, ctx.unit().id, seed, shoot.models_destroyed, std::nullopt,
                                       ctx.generation());
        }
    }
    MoveScore s = score_impl(ctx, plan, shoot, prospect ? &*prospect : nullptr);
    // Asked for and not possible (no charge can be declared from there): no charge term.
    if (with_charge_prospect && may_charge && !prospect) {
        s.total -= s.charge;
        s.charge = 0.0;
        s.p_charge = 0.0;
    }
    s.refined = true;
    return s;
}

}  // namespace wh40k::ai::normal
