#include "normal/move_score.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "analysis/charge_odds.hpp"
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
    if (engaged_stay) {
        // 10.06: close-quarters shooting at the unit we are engaged with, then our fight (we are the active player
        // and select first, 12.04), then the rest of the horizon.
        line.we_hit(ctx.close_quarters_rate(true));
        s.shoot_now = line.value();
        s.hold = ctx.engaged_line(line, 1.0, &s.p_survive) - s.shoot_now;
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
        line.we_hit(share);
        s.shoot_now = line.value();
        // The defender removes its front models (the worst case for a charge, M40's distance_after_casualties).
        const auto k = static_cast<std::size_t>(std::floor(models_destroyed + 1e-9));
        const float gap_after = k < gaps.size() ? gaps[k] : kFar;
        const core::Position our_centroid = centroid_of(plan.positions);
        const double hold_total =
            ctx.hold_line(line, gap_after, our_centroid, &s.enemy_reply, &s.p_enemy_charge, &s.p_survive);
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
                    gain += pmf[r] * std::max(0.0, ctx.engaged_line(line, fighters) - hold_total);
                }
                s.p_charge = prospect->p_success;
                s.charge = gain;
            } else if (gap_after < kFar && core::within(gap_after, core::inches_to_mm(core::kChargeRangeInches))) {
                const double p = analysis::p_charge_roll_covers(gap_after);
                if (p > 0.0) {
                    s.p_charge = p;
                    s.charge = p * std::max(0.0, ctx.engaged_line(line) - hold_total);
                }
            }
        }
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
                const float d =
                    core::base_distance(plan.positions[i], us.models[i].base, plan.positions[j], us.models[j].base);
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
                                   MoveWeights weights)
    : cache_(cache), origin_(origin), weights_(weights) {
    const core::Unit* u = origin.find_unit(unit);
    if (u == nullptr) throw std::logic_error("MoveScoreContext: unknown unit");
    us_ = *u;
    if (origin.flow.move && origin.flow.move->unit == unit) {
        for (core::Model& m : us_.models) {
            for (const core::ModelStart& st : origin.flow.move->starts) {
                if (st.model == m.id) m.position = st.position;
            }
        }
    }
    us_threat_ = analysis::threat_ranges(us_);
    our_ranges_ = ranged_ranges(us_);
    turns_ = turns_left(origin);
    start_engaged_ = core::is_engaged(origin, us_);
    if (const core::Unit* e = closest_enemy(origin, us_)) {
        enemy_ = *e;
        enemy_models_ = core::placed_models(*e);
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
        for (const core::PlacedModel& e : enemy_models_) {
            gap = std::min(gap, core::base_distance(positions[i], m.base, e.position, e.base));
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
    for (const core::PlacedModel& e : enemy_models_) {
        float gap = kFar;
        for (std::size_t i = 0; i < us_.models.size() && i < positions.size(); ++i) {
            gap = std::min(gap, core::base_distance(positions[i], us_.models[i].base, e.position, e.base));
        }
        out.push_back(gap);
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
            const core::Base& b = ours ? us_.models[std::min(i, us_.models.size() - 1)].base
                                       : enemy_models_[std::min(i, enemy_models_.size() - 1)].base;
            const float r = core::base_radius(b);
            const core::Position& p = positions[i];
            if (d.x > 1e-6f) t = std::min(t, (w - r - p.x) / d.x);
            if (d.x < -1e-6f) t = std::min(t, (r - p.x) / d.x);
            if (d.y > 1e-6f) t = std::min(t, (h - r - p.y) / d.y);
            if (d.y < -1e-6f) t = std::min(t, (r - p.y) / d.y);
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
                                         double* p_survive) {
    const bool heavy = keeps_heavy(from_mm - to_mm);
    ScoreLine y = line;
    y.next_half_turn();
    y.they_hit(ranged_rate(false, core::ShootingType::Normal, heavy, to_mm));
    const double w = survival_weight_;
    const double shot_survive = 1.0 - enemy_ranged_wipe(core::ShootingType::Normal, heavy, to_mm);
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
                                   std::string* reply, double* p_enemy_charge, double* p_survive) {
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
        const double v = enemy_turn_from(line, gap_mm, gap_mm, &p, &survive);
        consider(v, p > 0.0 ? "stays, shoots and charges" : "stays and shoots", p, survive);
    }
    if (m > 0.0f) {
        const float half = them_threat_.max_range_mm * 0.5f;
        if (half > 0.0f && half < gap_mm && gap_mm - half <= m) {
            double p = 0.0;
            double survive = 1.0;
            const double v =
                enemy_turn_from(line, gap_mm, std::max(half - 0.01f * core::MM_PER_INCH, kCloseGapMm), &p, &survive);
            consider(v, "closes to half range", p, survive);
        }
        const float close = std::max(gap_mm - m, kCloseGapMm);
        if (close < gap_mm) {
            double p = 0.0;
            double survive = 1.0;
            const double v = enemy_turn_from(line, gap_mm, close, &p, &survive);
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
            const double survive = 1.0 - enemy_ranged_wipe(core::ShootingType::Assault, false, adv);
            consider(our_next_turn(y, adv) + w * survive, "advances and shoots", 0.0, survive);
        }
        // Keep away from us, as far as the battlefield lets it.
        std::vector<core::Position> theirs;
        for (const core::PlacedModel& e : enemy_models_) theirs.push_back(e.position);
        const float room = std::min(m, room_away(theirs, false, our_centroid));
        if (room > 0.5f * core::MM_PER_INCH) {
            const float away = gap_mm + room;
            ScoreLine y = line;
            y.next_half_turn();
            y.they_hit(ranged_rate(false, core::ShootingType::Normal, keeps_heavy(room), away));
            const double survive = 1.0 - enemy_ranged_wipe(core::ShootingType::Normal, keeps_heavy(room), away);
            consider(our_next_turn(y, away) + w * survive, "keeps away and shoots", 0.0, survive);
        }
    }
    if (reply != nullptr) *reply = best_reply;
    if (p_enemy_charge != nullptr) *p_enemy_charge = best_p;
    if (p_survive != nullptr) *p_survive = best_survive;
    return best;
}

double MoveScoreContext::engaged_line(const ScoreLine& line, double our_fighters, double* p_survive) {
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
    stay.they_hit(melee_rate(false));
    stay.we_hit(melee_rate(true));
    if (turns_ >= 2) {
        stay.next_half_turn();
        stay.we_hit(close_quarters_rate(true));
        stay.we_hit(melee_rate(true));
        stay.they_hit(melee_rate(false));
    }
    const double on_survive = now_survive * (1.0 - enemy_engaged_wipe());
    const double fight_on = stay.value() + w * on_survive;
    double fall_back = fight_on;
    if (them_threat_.move_mm > 0.0f) {
        ScoreLine away = c;
        away.next_half_turn();
        fall_back = our_next_turn(away, them_threat_.move_mm) + w * now_survive;
    }
    if (p_survive != nullptr) *p_survive = fall_back < fight_on ? now_survive : on_survive;
    return std::min(fight_on, fall_back);
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
    }
    std::optional<ChargeProspect> prospect;
    const analysis::TurnAllowance allow = allowance_of(plan);
    const bool may_charge = allow.charge && !engaged_stay;
    if (with_charge_prospect && may_charge) {
        prospect = charge_prospect(ctx.cache(), after, ctx.unit().id, seed, shoot.models_destroyed);
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
