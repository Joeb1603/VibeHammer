#include "normal/move_score.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "analysis/charge_odds.hpp"
#include "bounds.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/units.hpp"
#include "core/rules/charge.hpp"
#include "core/state/points_destroyed.hpp"
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
        // D-083 part 2 (M80): the synced partners' volleys on the focus this turn.
        if (ctx.supported()) line.allies_hit(ctx.team()->support_now);
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
        // D-083 part 2 (M80): the synced partners' volleys on the focus this turn (not this unit's value).
        const MoveTeam* team = ctx.team();
        if (ctx.supported()) line.allies_hit(team->support_now);
        // The defender removes its front models (the worst case for a charge, M40's distance_after_casualties).
        const auto k = static_cast<std::size_t>(std::floor(models_destroyed + 1e-9));
        const float gap_after = k < gaps.size() ? gaps[k] : kFar;
        const core::Position our_centroid = centroid_of(plan.positions);
        OtherThreat threat;
        if (others) threat = ctx.other_threat(plan.positions, other);
        const OtherThreat* tp = others ? &threat : nullptr;
        double hold_total =
            ctx.hold_line(line, gap_after, our_centroid, &s.enemy_reply, &s.p_enemy_charge, &s.p_survive, tp);
        // D-083 part 2 (M80): the focus answers one of the group's members in its reach; this unit's share of its
        // reply (the rest of the time it answers a partner, and only the other enemy units reply to us).
        s.reply_share = ctx.supported() ? ctx.reply_share(s.gap_mm) : 1.0;
        if (s.reply_share < 1.0) {
            const double elsewhere = ctx.hold_line(line, gap_after, our_centroid, nullptr, nullptr, nullptr, tp, true);
            hold_total = s.reply_share * hold_total + (1.0 - s.reply_share) * elsewhere;
        }
        s.hold = hold_total - s.shoot_now;
        if (allow.charge) {
            const auto n = static_cast<double>(us.models.size());
            const bool off_line = team != nullptr && team->line && prospect != nullptr &&
                                  std::find(prospect->targets.begin(), prospect->targets.end(), team->focus) ==
                                      prospect->targets.end();
            if (off_line) {
                // D-083 part 2 (M80): the best charge is at another unit than the focus the line follows: the charge
                // planner's own value of it (its hold and charged values, M42 / D-066).
                s.p_charge = prospect->p_success;
                s.charge = prospect->gain;
            } else if (prospect != nullptr) {
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
    // D-083 part 2 (M80): approach and cohesion, for a unit with partners only (added only then, so every other score
    // is the same sum as before).
    if (ctx.team() != nullptr) {
        if (ctx.team()->line) s.focus = ctx.team()->focus;
        s.approach = ctx.approach_term(plan.positions);
        s.cohesion = ctx.cohesion_term(plan.positions);
        s.total += s.approach + s.cohesion;
    }
    return s;
}

// D-083 part 2 (M80): the focus of the group `us` follows in `plan`: it has partners there, the focus (an enemy unit)
// has models, and the group is a clear surplus: its gain at least `follow` of its risk (what it puts at stake). nullptr:
// the unit doesn't follow a group.
const core::Unit* followed_focus(const ArmyPlan& plan, const core::GameState& origin, const core::Unit& us,
                                 double follow) {
    if (!plan.active) return nullptr;
    const UnitPlan* up = plan.find(us.id);
    const PlanGroup* group = plan.group_of(us.id);
    if (up == nullptr || group == nullptr || up->partners.empty() || !up->focus) return nullptr;
    if (group->gain < follow * group->risk) return nullptr;
    const core::Unit* focus = origin.find_unit(*up->focus);
    if (focus == nullptr || focus->models.empty() || focus->side == us.side) return nullptr;
    return focus;
}

}  // namespace

// ---- the context ----

MoveScoreContext::MoveScoreContext(analysis::OddsCache& cache, const core::GameState& origin, core::UnitId unit,
                                   MoveWeights weights, Generation generation, const ArmyPlan* plan)
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
    us_mv_ = monster_or_vehicle(us_);
    points_rule_ = origin.result_rule == core::ResultRule::PointsDestroyed;
    for (const core::Model& m : us_.models) us_extents_.push_back(edge_extents(m.base, m.facing_deg));
    us_threat_ = analysis::threat_ranges(us_);
    our_ranges_ = ranged_ranges(us_);
    turns_ = turns_left(origin);
    start_engaged_ = core::is_engaged(origin, us_);
    // D-083 part 2 (M80): the group the unit follows, from the army plan (pre-teamwork generations read none).
    std::optional<ArmyPlan> own_plan;
    const core::Unit* focus = nullptr;
    bool line_focus = false;
    if (!pre_teamwork(generation_)) {
        if (plan == nullptr) {
            own_plan = army_plan(cache_, origin, us_.side, generation_);
            plan = &*own_plan;
        }
        focus = followed_focus(*plan, origin, us_, weights_.follow);
        if (focus != nullptr && !points_rule_) {
            // Under D-007 a unit that loses its exchange with the focus (as lean_ measures it: its best attacks against
            // the focus's, at half range and in melee) plays for the draw (D-066) and keeps its own line.
            const double ours = ranged_share(cache_, origin, us_, *focus, us_threat_.max_range_mm * 0.5f) +
                                melee_share(cache_, origin, us_, *focus);
            const analysis::ThreatRanges fth = analysis::threat_ranges(*focus);
            const double theirs = ranged_share(cache_, origin, *focus, us_, fth.max_range_mm * 0.5f) +
                                  melee_share(cache_, origin, *focus, us_);
            if (ours < theirs) focus = nullptr;
        }
        // The line follows the focus while some partner reaches it by our next turn (its arrival in the plan); until
        // then the unit keeps its closest enemy unit's line and only approaches the focus.
        if (focus != nullptr) {
            for (const PlanMember& m : plan->group_of(unit)->members) {
                line_focus = line_focus || (m.unit != unit && m.arrival <= 1);
            }
        }
    }
    const core::Unit* e = line_focus ? focus : closest_enemy(origin, us_);
    if (e != nullptr) {
        enemy_ = *e;
        enemy_mv_ = monster_or_vehicle(*e);
        rate_us_ = hazard_rate(origin, us_, *e);
        rate_them_ = hazard_rate(origin, *e, us_);
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
        if (points_rule_) {
            // D-077 (M68): the points tiebreak (D-076) has no draw to wait for and needs no wipe-out to win, so no time
            // preference. A wipe-out still loses at once (D-007), so the survival term stays for the side's last unit,
            // worth what surviving buys on the engine's exact tally: a win 2, a draw 1, nothing when behind.
            our_discount_ = 1.0;
            their_discount_ = 1.0;
            bool last = true;
            for (const core::Unit& f : origin.units) {
                if (f.side == us_.side && f.id != us_.id && !f.models.empty()) last = false;
            }
            if (last) {
                const core::PointsTally tally = core::points_destroyed(origin);
                const std::int64_t ours_pts = tally.destroyed[core::side_index(us_.side)];
                const std::int64_t theirs_pts = tally.destroyed[1 - core::side_index(us_.side)];
                const double result = ours_pts > theirs_pts ? 1.0 : ours_pts < theirs_pts ? -1.0 : 0.0;
                survival_weight_ *= 1.0 + result;
            } else {
                survival_weight_ = 0.0;
            }
        }
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
                x.mv = monster_or_vehicle(*o);
                x.rate_us = hazard_rate(origin, us_, *o);
                x.rate_them = hazard_rate(origin, *o, us_);
                x.melee = melee_share(cache_, origin, *o, us_);
                x.cq = ranged_share(cache_, origin, *o, us_, 0.0f, true);
                if (!us_.models.empty()) {
                    const analysis::Volley mv = analysis::unit_melee_volley(cache_, *o, us_, x.rate_them);
                    x.melee_wipe = analysis::expected_outcome(cache_, mv, analysis::snapshot_of(us_)).p_wipe();
                }
                others_.push_back(std::move(x));
            }
        }
        if (focus != nullptr) make_team(*plan, *focus, line_focus);
    }
}

void MoveScoreContext::make_team(const ArmyPlan& plan, const core::Unit& focus, bool line) {
    const PlanGroup* group = plan.group_of(us_.id);
    const UnitPlan* up = plan.find(us_.id);
    if (group == nullptr || up == nullptr) return;
    MoveTeam t;
    t.focus = focus.id;
    t.line = line;
    t.focus_models = core::posed_models(focus);
    if (const PlanMember* me = group->member(us_.id)) t.mode = me->mode;
    t.start_gap_mm = core::unit_distance(us_, focus).value_or(kFar);
    const float m = us_threat_.move_mm;
    t.per_turn_mm = m > 0.0f ? m + kMeanAdvanceMm : 0.0f;
    // The line's horizon: our next turn's Move, then our shooting (Shoot) or a charge at even odds (7", Charge).
    const float attack = t.mode == AttackMode::Shoot ? us_threat_.max_range_mm : 7.0f * core::MM_PER_INCH;
    t.reach_mm = m + attack;
    const bool our_turn = core::turn_side(origin_) == us_.side;
    for (const core::UnitId pid : up->partners) {
        const core::Unit* p = origin_.find_unit(pid);
        const PlanMember* pm = group->member(pid);
        if (p == nullptr || p->models.empty() || pm == nullptr) continue;
        MoveTeam::Partner x;
        x.unit = pid;
        x.arrival = pm->arrival;
        x.mode = pm->mode;
        x.gap_mm = core::unit_distance(*p, focus).value_or(kFar);
        x.models = core::posed_models(*p);
        const analysis::ThreatRanges pth = analysis::threat_ranges(*p);
        const bool moved = our_turn && origin_.flow.stage == core::Stage::MoveUnits && p->phase_flags.selected_to_move;
        x.catch_up_mm = moved || !our_turn ? 0.0f : pth.move_mm;
        x.synced = x.arrival <= 1;
        if (x.synced) {
            // Its expected volleys on the focus from its arrival (the plan's mode): engaged, close-quarters shooting and
            // the fight; a shooter from its longest range (or the gap, if closer); a charger at the odds of covering the
            // gap left after its moves, then engaged.
            const double fight = melee_share(cache_, origin_, *p, focus);
            const double engaged = ranged_share(cache_, origin_, *p, focus, 0.0f, true) + fight;
            double first = 0.0;
            double later = 0.0;
            if (x.mode == AttackMode::Engaged) {
                first = later = engaged;
            } else if (x.mode == AttackMode::Shoot) {
                const float at = std::min(x.gap_mm, pth.max_range_mm);
                if (const std::optional<analysis::ShotContext> c = next_turn_shot(origin_, *p, focus, at)) {
                    first = later = ranged_share(cache_, origin_, *p, focus, *c);
                }
            } else {
                // Its moves before the charge: this turn's (unless it has moved: a Normal move if it charges now, an
                // Advance if it charges next turn), then next turn's Normal move.
                const bool still = x.catch_up_mm > 0.0f;
                const float travel = x.arrival == 0 ? (still ? pth.move_mm : 0.0f)
                                                    : (still ? pth.move_mm + kMeanAdvanceMm : 0.0f) + pth.move_mm;
                const double pc =
                    analysis::p_charge_roll_covers(std::max(x.gap_mm - travel, core::kEngagementRangeMm));
                first = pc * fight;
                later = pc * engaged + (1.0 - pc) * first;
            }
            if (x.arrival == 0) {
                x.now = first;
                x.next = later;
            } else {
                x.next = first;
            }
            t.support_now += x.now;
            t.support_next += x.next;
            t.synced = true;
        }
        t.partners.push_back(std::move(x));
    }
    team_ = std::move(t);
}

double MoveScoreContext::approach_term(const std::vector<core::Position>& positions) const {
    if (!team_ || !(team_->per_turn_mm > 0.0f) || team_->mode == AttackMode::Engaged) return 0.0;
    float gap_mm = kFar;
    for (std::size_t i = 0; i < us_.models.size() && i < positions.size(); ++i) {
        const core::BasePose at{positions[i], us_.models[i].base, us_.models[i].facing_deg};
        for (const core::PosedModel& m : team_->focus_models) {
            gap_mm = std::min(gap_mm, core::base_distance(at, m.pose()));
        }
    }
    if (!(gap_mm < kFar)) return 0.0;
    const auto turns = [&](float g) {
        return static_cast<double>(std::max(0.0f, g - team_->reach_mm) / team_->per_turn_mm);
    };
    return weights_.approach * (turns(team_->start_gap_mm) - turns(gap_mm));
}

double MoveScoreContext::cohesion_term(const std::vector<core::Position>& positions) const {
    if (!team_ || !(weights_.cohesion > 0.0) || team_->partners.empty()) return 0.0;
    float best = kFar;
    for (const MoveTeam::Partner& p : team_->partners) {
        float gap = kFar;
        for (std::size_t i = 0; i < us_.models.size() && i < positions.size(); ++i) {
            const core::BasePose at{positions[i], us_.models[i].base, us_.models[i].facing_deg};
            for (const core::PosedModel& m : p.models) gap = std::min(gap, core::base_distance(at, m.pose()));
        }
        best = std::min(best, std::max(0.0f, gap - p.catch_up_mm));
    }
    if (!(best < kFar)) return 0.0;
    const double beyond = std::max(0.0, static_cast<double>(best / core::MM_PER_INCH) - 6.0);
    return -weights_.cohesion * beyond;
}

double MoveScoreContext::reply_share(float gap_mm) const {
    if (!supported()) return 1.0;  // alone against the focus
    // The focus attacks one unit a phase (D-010): it answers one of the group's members in its reach (its Move and its
    // longest range, or a charge after its Normal move): this unit, after the move, and the synced partners where they
    // stand. Each counts an equal share (the enemy answers only one of them).
    const auto in_reach = [&](float g) {
        return core::within(g, them_threat_.shooting_reach_mm()) || them_threat_.p_charge_after_move(g) > 0.0;
    };
    if (!in_reach(gap_mm)) return 1.0;
    int k = 1;
    for (const MoveTeam::Partner& p : team_->partners) {
        if (p.synced && in_reach(p.gap_mm)) ++k;
    }
    return 1.0 / static_cast<double>(k);
}

ScoreLine MoveScoreContext::new_line() const {
    return ScoreLine(alive_share(origin_, us_), enemy_ ? alive_share(origin_, *enemy_) : 0.0, our_discount_,
                     their_discount_);
}

analysis::ShotContext MoveScoreContext::shot(bool ours, core::ShootingType type, bool heavy, float gap_mm) const {
    analysis::ShotContext c;
    c.type = type;
    c.heavy = heavy;
    c.gap_mm = gap_mm;
    c.hazard_rate = ours ? rate_us_ : rate_them_;  // D-077
    return c;
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
    const analysis::Volley v = analysis::unit_ranged_volley(cache_, a, t, shot(ours, type, heavy, gap_mm));
    const double share =
        unit_share(origin_, t, analysis::expected_outcome(cache_, v, analysis::snapshot_of(t)).model_equivalents);
    rates_.emplace(key, share);
    return share;
}

double MoveScoreContext::melee_rate(bool ours) {
    if (!enemy_) return 0.0;
    std::optional<double>& slot = melee_[ours ? 0 : 1];
    if (!slot) slot = ours ? melee_share(cache_, origin_, us_, *enemy_) : melee_share(cache_, origin_, *enemy_, us_);
    return *slot;
}

double MoveScoreContext::close_quarters_rate(bool ours) {
    if (!enemy_) return 0.0;
    std::optional<double>& slot = cq_[ours ? 0 : 1];
    if (!slot) {
        slot = ours ? ranged_share(cache_, origin_, us_, *enemy_, 0.0f, true)
                    : ranged_share(cache_, origin_, *enemy_, us_, 0.0f, true);
    }
    return *slot;
}

std::pair<double, double> MoveScoreContext::enemy_elsewhere_shooting(float gap_mm) {
    if (!enemy_ || !enemy_mv_ || us_.models.empty()) return {0.0, 0.0};
    const std::uint64_t band = band_of(false, gap_mm);
    if (const auto it = elsewhere_.find(band); it != elsewhere_.end()) return it->second;
    analysis::ShotContext c = shot(false, core::ShootingType::CloseQuarters, false, gap_mm);
    c.elsewhere = true;  // 10.06: its MONSTER/VEHICLE models, at a unit it isn't engaged with
    const analysis::Volley v = analysis::unit_ranged_volley(cache_, *enemy_, us_, c);
    std::pair<double, double> out{0.0, 0.0};
    if (!v.empty()) {
        const analysis::AttackOutcome o = analysis::expected_outcome(cache_, v, analysis::snapshot_of(us_));
        out = {unit_share(origin_, us_, o.model_equivalents), o.p_wipe()};
    }
    elsewhere_.emplace(band, out);
    return out;
}

double MoveScoreContext::enemy_ranged_wipe(core::ShootingType type, bool heavy, float gap_mm) {
    if (!enemy_ || us_.models.empty()) return 0.0;
    const RateKey key{false, static_cast<int>(type), heavy, band_of(false, gap_mm)};
    if (const auto it = wipes_.find(key); it != wipes_.end()) return it->second;
    const analysis::Volley v = analysis::unit_ranged_volley(cache_, *enemy_, us_, shot(false, type, heavy, gap_mm));
    const double p = analysis::expected_outcome(cache_, v, analysis::snapshot_of(us_)).p_wipe();
    wipes_.emplace(key, p);
    return p;
}

double MoveScoreContext::enemy_melee_wipe() {
    if (!enemy_ || us_.models.empty()) return 0.0;
    if (!melee_wipe_) {
        const analysis::Volley v = analysis::unit_melee_volley(cache_, *enemy_, us_, rate_them_);
        melee_wipe_ = analysis::expected_outcome(cache_, v, analysis::snapshot_of(us_)).p_wipe();
    }
    return *melee_wipe_;
}

double MoveScoreContext::enemy_engaged_wipe() {
    if (!enemy_ || us_.models.empty()) return 0.0;
    if (!engaged_wipe_) {
        // Its turn in the combat: close-quarters shooting, then the fight.
        analysis::Volley v =
            analysis::unit_ranged_volley(cache_, *enemy_, us_, shot(false, core::ShootingType::CloseQuarters, false, 0.0f));
        const analysis::Volley melee = analysis::unit_melee_volley(cache_, *enemy_, us_, rate_them_);
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
        // 04.02: not while it is engaged with a friendly unit (D-077, 17.03: unless it is a MONSTER/VEHICLE, at -1).
        if (enemy_engaged_elsewhere_ && !enemy_mv_) continue;
        const std::uint64_t band = band_of(true, gap);
        const auto key = std::make_tuple(i, static_cast<int>(type), heavy, band);
        auto it = model_volleys_.find(key);
        if (it == model_volleys_.end()) {
            core::Unit one = us_;
            one.models = {m};
            one.models.front().position = positions[i];
            analysis::ShotContext context = shot(true, type, heavy, gap);
            context.target_engaged = enemy_engaged_elsewhere_;  // D-077: 17.03
            it = model_volleys_.emplace(key, analysis::unit_ranged_volley(cache_, one, *enemy_, context)).first;
        }
        volley.insert(volley.end(), it->second.begin(), it->second.end());
    }
    if (half_range_models != nullptr) *half_range_models = half;
    const analysis::AttackOutcome o = analysis::expected_outcome(cache_, volley, enemy_snap_);
    if (outcome != nullptr) *outcome = o;
    return unit_share(origin_, *enemy_, o.model_equivalents);
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
    if (supported()) y.allies_hit(team_->support_next);  // D-083 part 2: our partners' volleys
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
        if (supported()) c.allies_hit(team_->support_next);  // D-083 part 2: our partners' volleys
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
                                   const OtherThreat* others, bool reply_elsewhere) {
    if (p_enemy_charge != nullptr) *p_enemy_charge = 0.0;
    if (p_survive != nullptr) *p_survive = 1.0;
    const double w = survival_weight_;
    if (turns_ == 0 || !enemy_) {
        if (reply != nullptr) *reply = "the game ends";
        return line.value() + w;
    }
    if (reply_elsewhere && !enemy_engaged_elsewhere_ && gap_mm < kFar) {
        // D-083 part 2 (M80): the focus replies to a partner (the shared reply); the other enemy units still reply.
        if (reply != nullptr) *reply = "replies to a partner";
        ScoreLine y = line;
        y.next_half_turn();
        double survive = 1.0;
        if (others != nullptr) {
            y.others_hit(others->unengaged);
            survive *= others->survive;
        }
        if (p_survive != nullptr) *p_survive = survive;
        return our_next_turn(y, gap_mm) + w * survive;
    }
    if (enemy_engaged_elsewhere_ || gap_mm >= kFar) {
        // Engaged with another unit of ours it can neither shoot us nor charge (04.02, 11.02); wiped out, nothing.
        // D-077 (M68): a MONSTER/VEHICLE still shoots us with its MONSTER/VEHICLE models (10.06, at -1).
        if (reply != nullptr) *reply = gap_mm >= kFar ? "is wiped out" : "is locked in combat";
        ScoreLine y = line;
        y.next_half_turn();
        double survive = 1.0;
        if (enemy_engaged_elsewhere_ && enemy_mv_ && gap_mm < kFar) {
            const std::pair<double, double> r = enemy_elsewhere_shooting(gap_mm);
            y.they_hit(r.first);
            survive = 1.0 - r.second;
            if (reply != nullptr) *reply = "is locked in combat and shoots";
        }
        if (others != nullptr) {  // D-066: the other enemy units still reply
            y.others_hit(others->unengaged);
            survive *= others->survive;
            if (p_survive != nullptr) *p_survive = survive;
            return our_next_turn(y, gap_mm >= kFar ? kFar : gap_mm) + w * survive;
        }
        if (p_survive != nullptr) *p_survive = survive;
        return our_next_turn(y, gap_mm >= kFar ? kFar : gap_mm) + w * survive;
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
        if (supported()) stay.allies_hit(team_->support_next);  // D-083 part 2: our partners' volleys
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
        analysis::ShotContext c;
        c.type = core::ShootingType::Normal;
        c.heavy = heavy;
        c.gap_mm = gap_mm;
        c.hazard_rate = o.rate_them;  // D-077
        const analysis::Volley volley = analysis::unit_ranged_volley(cache_, o.unit, us_, c);
        if (!volley.empty()) {
            const analysis::AttackOutcome out = analysis::expected_outcome(cache_, volley, analysis::snapshot_of(us_));
            v = {unit_share(origin_, us_, out.model_equivalents), out.p_wipe()};
        }
    }
    o.ranged.emplace(key, v);
    return v;
}

std::pair<double, double> MoveScoreContext::other_at_engaged_us(Other& o, float gap_mm) {
    const std::uint64_t key = other_band(o, gap_mm);
    if (const auto it = o.at_engaged_us.find(key); it != o.at_engaged_us.end()) return it->second;
    std::pair<double, double> v{0.0, 0.0};
    if (!us_.models.empty()) {
        analysis::ShotContext c;  // 17.03: Normal shooting at our engaged MONSTER/VEHICLE, at -1, no [BLAST]
        c.type = core::ShootingType::Normal;
        c.heavy = true;
        c.gap_mm = gap_mm;
        c.target_engaged = true;
        c.hazard_rate = o.rate_them;
        const analysis::Volley volley = analysis::unit_ranged_volley(cache_, o.unit, us_, c);
        if (!volley.empty()) {
            const analysis::AttackOutcome out = analysis::expected_outcome(cache_, volley, analysis::snapshot_of(us_));
            v = {unit_share(origin_, us_, out.model_equivalents), out.p_wipe()};
        }
    }
    o.at_engaged_us.emplace(key, v);
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
    // 04.02: unengaged only (D-077, 17.03: or an engaged MONSTER/VEHICLE, at -1)
    if ((o.engaged && !o.mv) || o.models.empty() || positions.size() != us_.models.size()) return 0.0;
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
                analysis::ShotContext c;
                c.type = type;
                c.heavy = heavy;
                c.gap_mm = gaps[i];
                c.target_engaged = o.engaged;  // D-077: 17.03 (an engaged MONSTER/VEHICLE)
                c.hazard_rate = o.rate_us;
                it = o.our_volleys.emplace(vkey, analysis::unit_ranged_volley(cache_, one, o.unit, c)).first;
            }
            volley.insert(volley.end(), it->second.begin(), it->second.end());
        }
        const analysis::AttackOutcome out = analysis::expected_outcome(cache_, volley, o.snap);
        hit = o.our_shots
                  .emplace(key, std::make_pair(unit_share(origin_, o.unit, out.model_equivalents), out.models_destroyed))
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
        // 04.02, 11.02: busy with another of our units. D-077: a MONSTER/VEHICLE engaged with one still shoots (10.06),
        // but that unit is always another target of its, so neither rule below counts it here.
        if (o.locked || o.models.empty()) continue;
        double left = 1.0;  // its strength left after our shooting this turn
        if (shot && shot->index == i) {
            const double alive = alive_share(origin_, o.unit);
            left = alive > 0.0 ? std::clamp(1.0 - shot->share / alive, 0.0, 1.0) : 0.0;
        }
        // Which replies count (an enemy unit engaged with us always does: it can only fight us):
        // - winning the exchange, the side plays for tempo and only its total losses matter: marginal exposure, an
        //   enemy unit that can reach another of our units anyway (its Move and its longest range) shoots or charges
        //   one of them whatever this move does, so only one with no other target counts;
        // - losing it, the side plays for the draw (D-007) and this unit's own survival matters: an enemy unit counts
        //   whenever this unit would be its closest target (it shoots and charges the closest, D-010: a tie counts).
        // D-077: under the points tiebreak (D-076) there is no draw to play for, so every side counts by marginal
        // exposure.
        const bool losing = plays_for_draw();
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
        // While we are engaged it can't shoot us (04.02; D-077: unless we are a MONSTER/VEHICLE, 17.03, at -1); it may
        // charge after a Normal move.
        const double pc = o.threat.p_charge_after_move(g);
        t.engaged += left * pc * o.melee;
        t.survive_engaged *= 1.0 - left * pc * o.melee_wipe;
        if (us_mv_) {
            const std::pair<double, double> r = other_at_engaged_us(o, g);
            t.engaged += left * r.first;
            t.survive_engaged *= 1.0 - left * r.second;
        }
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
        // D-077 (M68): [HAZARDOUS] priced at our exchange rate against each target (hazard_rate).
        const core::Unit* us = after.find_unit(ctx.unit().id);
        const auto rate = [&](const core::Unit& e) { return us != nullptr ? hazard_rate(after, *us, e) : 0.0; };
        if (const core::Unit* e = after.find_unit(enemy->id)) {
            if (const auto best = analysis::best_shooting(ctx.cache(), after, ctx.unit().id, enemy->id, rate(*e))) {
                shoot.share = unit_share(after, *e, best->outcome.model_equivalents);
                shoot.models_destroyed = best->outcome.models_destroyed;
            }
        }
        // D-066: the engine's best shooting at every other enemy unit; the best unit is the one shot.
        double best_share = shoot.share;
        for (std::size_t i = 0; i < ctx.other_count(); ++i) {
            const core::UnitId id = ctx.other(i).id;
            const core::Unit* e = after.find_unit(id);
            const auto best = e != nullptr ? analysis::best_shooting(ctx.cache(), after, ctx.unit().id, id, rate(*e))
                                           : std::nullopt;
            if (!best) continue;
            const double v = unit_share(after, *e, best->outcome.model_equivalents);
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
