#include "normal/army_plan.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <tuple>
#include <utility>

#include "analysis/charge_odds.hpp"
#include "analysis/threat.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/state/queries.hpp"
#include "normal/charge.hpp"
#include "normal/objectives.hpp"
#include "normal/value.hpp"
#include "unit_style.hpp"

namespace wh40k::ai::normal {

namespace {

// A unit that closes on another ends just outside engagement range (09.05).
constexpr float kOutsideEngagementMm = core::kEngagementRangeMm + 1.0f;

// What a unit of ours still has of our turn 0.
struct Turn0 {
    bool move = true;
    bool shoot = true;
    bool charge = true;
};

// Its distinct usable ranged ranges (for the range bands of the rate cache, as MoveScoreContext's band_of).
std::vector<float> usable_ranges(const core::Unit& u) {
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

// A volley depends on the gap only through each profile's range and half range (odds.cpp within_range,
// within_half_range): the band is those tests' answers.
std::uint64_t band_of(const std::vector<float>& ranges, float gap_mm) {
    std::uint64_t band = 0;
    for (std::size_t i = 0; i < ranges.size() && i < 32; ++i) {
        if (core::within(gap_mm, ranges[i])) band |= 1ull << (2 * i);
        if (core::within(gap_mm, ranges[i] * 0.5f)) band |= 1ull << (2 * i + 1);
    }
    return band;
}

// D-083 part 1: what is left of a unit, its models' wounds left over their W (model equivalents), as a share
// (unit_share, points-weighted by D-077). A wounded monster is worth less, and easier to finish.
double left_share(const core::GameState& state, const core::Unit& u) {
    double me = 0.0;
    for (const core::Model& m : u.models) {
        const int w = std::max(1, m.characteristics.wounds);
        me += std::clamp(static_cast<double>(m.wounds_remaining) / static_cast<double>(w), 0.0, 1.0);
    }
    return unit_share(state, u, me);
}

// The state the plan reads: a move in progress with its unit at the move's start (as MoveScoreContext does), so a
// replan from the middle of a move gets the plan of its start.
std::optional<core::GameState> at_move_start(const core::GameState& state) {
    if (!state.flow.move) return std::nullopt;
    core::GameState s = state;
    core::Unit* u = s.find_unit(state.flow.move->unit);
    if (u == nullptr) return std::nullopt;
    for (core::Model& m : u->models) {
        for (const core::ModelStart& st : state.flow.move->starts) {
            if (st.model == m.id) {
                m.position = st.position;
                m.facing_deg = st.facing_deg;
            }
        }
    }
    return s;
}

class Planner {
public:
    // One of our units' attack on a target: its arrivals, mode and the gap now.
    struct Track {
        std::size_t unit = 0;  // index into the planner's units
        AttackMode mode = AttackMode::Shoot;
        int arrival = kNoArrival;
        int ts = kNoArrival;  // shooting arrival
        int tc = kNoArrival;  // charge arrival
        double p_tc = 0.0;    // P(the charge at tc succeeds)
        float d0 = 0.0f;
    };
    struct Lone {
        Track track;
        double value = 0.0;
    };
    struct Result {
        double value = 0.0;
        int first = kNoArrival;
        std::optional<core::UnitId> reply_to;
    };

    Planner(analysis::OddsCache& cache, const core::GameState& state, core::Side side)
        : cache_(cache), state_(state), side_(side) {
        for (const core::Unit& u : state.units) {
            if (u.models.empty()) continue;
            Info i;
            i.unit = &u;
            i.threat = analysis::threat_ranges(u);
            i.ranges = usable_ranges(u);
            i.engaged_with = core::engaged_enemy_units(state, u);
            i.mv = monster_or_vehicle(u);
            i.left = left_share(state, u);
            const std::size_t k = units_.size();
            index_.emplace(u.id.v, k);
            (u.side == side ? ours_ : theirs_).push_back(k);
            units_.push_back(std::move(i));
        }
        for (const std::size_t k : ours_) units_[k].turn0 = turn0_of(*units_[k].unit);
        const std::size_t n = units_.size();
        melee_.assign(n * n, std::nullopt);
        cq_.assign(n * n, std::nullopt);
        gaps_.assign(n * n, std::nullopt);
        lone_.assign(n * n, std::nullopt);
        closest_.assign(n, std::nullopt);
        compute_turns();
    }

    [[nodiscard]] const std::vector<std::size_t>& ours() const noexcept { return ours_; }
    [[nodiscard]] const std::vector<std::size_t>& theirs() const noexcept { return theirs_; }
    [[nodiscard]] int turns() const noexcept { return turns_; }
    // M87 (D-086): the battle round of our turn 0, and a unit's threat ranges.
    [[nodiscard]] int first_round() const noexcept { return first_round_; }
    [[nodiscard]] const analysis::ThreatRanges& threat(std::size_t k) const { return units_[k].threat; }
    [[nodiscard]] const core::Unit& unit(std::size_t k) const { return *units_[k].unit; }
    [[nodiscard]] std::size_t unit_count() const noexcept { return units_.size(); }
    [[nodiscard]] double left(std::size_t k) const { return units_[k].left; }
    [[nodiscard]] bool engaged(std::size_t k) const { return !units_[k].engaged_with.empty(); }
    [[nodiscard]] std::optional<std::size_t> index(core::UnitId id) const {
        const auto it = index_.find(id.v);
        if (it == index_.end()) return std::nullopt;
        return it->second;
    }
    [[nodiscard]] bool engaged_with(std::size_t u, std::size_t e) const {
        const std::vector<core::UnitId>& w = units_[u].engaged_with;
        return std::find(w.begin(), w.end(), units_[e].unit->id) != w.end();
    }

    // The enemy unit closest to ours (closest_enemy: edge to edge, a tie to unit order); nullopt if none.
    std::optional<std::size_t> closest(std::size_t u) {
        std::optional<std::optional<std::size_t>>& slot = closest_[u];
        if (!slot) {
            const core::Unit* c = closest_enemy(state_, *units_[u].unit);
            slot = c != nullptr ? index(c->id) : std::nullopt;
        }
        return *slot;
    }
    // The enemy unit an engaged unit of ours fights for the plan: its closest enemy unit if it is engaged with it, else
    // the first it is engaged with in unit order.
    std::optional<std::size_t> engaged_focus(std::size_t u) {
        if (const std::optional<std::size_t> c = closest(u); c && engaged_with(u, *c)) return c;
        for (const std::size_t e : theirs_) {
            if (engaged_with(u, e)) return e;
        }
        return std::nullopt;
    }

    float gap(std::size_t u, std::size_t e) {
        std::optional<float>& slot = gaps_[u * units_.size() + e];
        if (!slot) slot = core::unit_distance(*units_[u].unit, *units_[e].unit).value_or(0.0f);
        return *slot;
    }

    // Travel by the Normal move of our turn j, the unit advancing (M + the D6's mean, 09.06) in the turns before.
    [[nodiscard]] float travel(std::size_t u, int j) const {
        const Info& i = units_[u];
        const float m = i.threat.move_mm;
        const float a = m > 0.0f ? kMeanAdvanceMm : 0.0f;
        if (i.turn0.move) return static_cast<float>(j) * (m + a) + m;
        if (j == 0) return 0.0f;
        return static_cast<float>(j - 1) * (m + a) + m;
    }
    [[nodiscard]] float advance_bonus(std::size_t u, int j) const {
        const Info& i = units_[u];
        if (!(i.threat.move_mm > 0.0f) || (j == 0 && !i.turn0.move)) return 0.0f;
        return kMeanAdvanceMm;
    }

    Track track(std::size_t u, std::size_t e, AttackMode mode) {
        Track t;
        t.unit = u;
        t.mode = mode;
        t.d0 = gap(u, e);
        const Info& i = units_[u];
        if (engaged_with(u, e)) {
            t.mode = AttackMode::Engaged;
            t.arrival = t.ts = t.tc = 0;
            t.p_tc = 1.0;
            return t;
        }
        if (!i.engaged_with.empty()) return t;  // locked in another combat: no arrival here
        const float r = i.threat.max_range_mm;
        for (int j = 0; j < turns_ && r > 0.0f; ++j) {
            if ((j > 0 || i.turn0.shoot) && core::within(t.d0 - travel(u, j), r)) {
                t.ts = j;
                break;
            }
        }
        for (int j = 0; j < turns_; ++j) {
            if (j == 0 && !i.turn0.charge) continue;
            const double p = analysis::p_charge_roll_covers(std::max(t.d0 - travel(u, j), core::kEngagementRangeMm));
            if (p >= kPlanChargeOdds) {
                t.tc = j;
                t.p_tc = p;
                break;
            }
        }
        t.arrival = mode == AttackMode::Shoot ? t.ts : t.tc;
        return t;
    }

    // The earliest turn it can attack the target in either mode, and the gap (the candidates' order; no odds).
    std::pair<int, float> soonest(std::size_t u, std::size_t e) {
        const Track s = track(u, e, AttackMode::Shoot);
        return {s.mode == AttackMode::Engaged ? 0 : std::min(s.ts, s.tc), s.d0};
    }

    // The exchange of {u} with e in the better mode (a tie: Shoot); 0 when it can't arrive within the game.
    const Lone& lone(std::size_t u, std::size_t e) {
        std::optional<Lone>& slot = lone_[u * units_.size() + e];
        if (slot) return *slot;
        Lone l;
        const Track s = track(u, e, AttackMode::Shoot);
        if (s.mode == AttackMode::Engaged) {
            l.track = s;
            l.value = exchange({s}, e).value;
        } else {
            const Track c = track(u, e, AttackMode::Charge);
            const bool shoots = s.arrival < turns_;
            const bool charges = c.arrival < turns_;
            const double vs = shoots ? exchange({s}, e).value : 0.0;
            const double vc = charges ? exchange({c}, e).value : 0.0;
            if (charges && (!shoots || vc > vs + 1e-12)) {
                l.track = c;
                l.value = vc;
            } else {
                l.track = s;
                l.value = vs;
            }
        }
        slot = l;
        return *slot;
    }

    // What it gets apart: max(0, alone against e, alone against its closest enemy unit); engaged, alone against e.
    double alone(std::size_t u, std::size_t e) {
        if (engaged(u)) return lone(u, e).value;
        double v = std::max(0.0, lone(u, e).value);
        if (const std::optional<std::size_t> c = closest(u)) v = std::max(v, lone(u, *c).value);
        return v;
    }

    // The joint exchange of the group `g` (tracks in unit order) with target e (see army_plan.hpp, D-083 part 1).
    Result exchange(const std::vector<Track>& g, std::size_t e) {
        Result res;
        for (const Track& t : g) res.first = std::min(res.first, t.arrival);
        if (res.first >= turns_) return res;
        const double e0 = units_[e].left;
        std::vector<double> them;
        them.reserve(g.size());
        for (const Track& t : g) them.push_back(units_[t.unit].left);
        MultiExchange x(e0, std::move(them));  // D-066's class with the roles turned round: e defends, the group attacks
        const std::size_t n = g.size();
        std::vector<bool> in_combat(n, false);
        for (std::size_t i = 0; i < n; ++i) in_combat[i] = g[i].mode == AttackMode::Engaged;
        const auto any_engaged = [&] { return std::find(in_combat.begin(), in_combat.end(), true) != in_combat.end(); };
        const analysis::ThreatRanges& eth = units_[e].threat;
        const bool e_locked = !units_[e].engaged_with.empty();  // 11.02: engaged now, it can't charge
        float e_moved = 0.0f;  // how far the target has closed on the group in its turns (for its own reach)
        int volleys = 0;
        std::vector<double> fight(n, 0.0);
        for (int j = 0; j < turns_; ++j) {
            // ---- our turn j: shooting, charges, then the Fight phase: our engaged members strike first (11.04 Fights
            // First after a charge, 12.04 the active player selects first), then the target strikes back at one ----
            std::fill(fight.begin(), fight.end(), 0.0);
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t m = g[i].unit;
                if (in_combat[i]) {
                    if (const double v = cq(m, e); v > 0.0) x.they_hit(i, v);  // 10.06
                    fight[i] = 1.0;
                    continue;
                }
                if (const double v = shot(g[i], e, j); v > 0.0) x.they_hit(i, v);
                if (g[i].mode == AttackMode::Charge && j == g[i].tc) {
                    fight[i] = g[i].p_tc;
                    in_combat[i] = true;
                }
            }
            for (std::size_t i = 0; i < n; ++i) {
                if (fight[i] > 0.0) x.they_hit(i, fight[i] * melee(g[i].unit, e));
            }
            strike_back(x, g, e, fight);
            if (j >= res.first && ++volleys >= kPlanVolleys) break;
            if (!enemy_turn_after(j)) break;
            // ---- the target's turn ----
            if (any_engaged()) {
                // It fights one of the members engaged with it (close-quarters shooting and its fight), the one it hurts
                // most; then every engaged member strikes back.
                std::size_t best = n;
                double best_v = 0.0;
                for (std::size_t i = 0; i < n; ++i) {
                    if (!in_combat[i]) continue;
                    MultiExchange y = x;
                    y.we_hit(i, cq(e, g[i].unit) + melee(e, g[i].unit));
                    if (best == n || y.value() > best_v + 1e-12) {
                        best = i;
                        best_v = y.value();
                    }
                }
                if (!res.reply_to) res.reply_to = units_[g[best].unit].unit->id;
                x.we_hit(best, cq(e, g[best].unit) + melee(e, g[best].unit));
                for (std::size_t i = 0; i < n; ++i) {
                    if (in_combat[i]) x.they_hit(i, melee(g[i].unit, e));
                }
                continue;
            }
            // Its reply worst for us: shoot one member in reach (its M plus its longest range), or shoot it and charge
            // it (the member strikes back at the same odds); a tie to unit order, shooting before charging.
            std::optional<MultiExchange> best;
            std::size_t best_i = n;
            bool best_engages = false;
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t m = g[i].unit;
                const float at = std::max(position(g[i], j) - e_moved, kOutsideEngagementMm);
                double r = 0.0;
                if (eth.max_range_mm > 0.0f && core::within(at, eth.move_mm + eth.max_range_mm)) {
                    r = ranged(e, m, std::min(at, eth.max_range_mm), !core::more_than(at, eth.max_range_mm));
                }
                const double p = e_locked ? 0.0 : eth.p_charge_after_move(at);
                for (int kind = 0; kind < 2; ++kind) {
                    if (kind == 0 && !(r > 0.0)) continue;
                    if (kind == 1 && !(p > 0.0)) continue;
                    MultiExchange y = x;
                    if (r > 0.0) y.we_hit(i, r);
                    if (kind == 1) {
                        y.we_hit(i, p * melee(e, m));
                        y.they_hit(i, p * melee(m, e));
                    }
                    if (!best || y.value() > best->value() + 1e-12) {
                        best = std::move(y);
                        best_i = i;
                        best_engages = kind == 1 && p >= kPlanChargeOdds;
                    }
                }
            }
            if (best) {
                x = std::move(*best);
                if (!res.reply_to) res.reply_to = units_[g[best_i].unit].unit->id;
                if (best_engages) in_combat[best_i] = true;
            }
            if (!best_engages) e_moved += eth.move_mm;  // it closes on the group
        }
        res.value = -x.value();  // the target's losses minus the members'
        return res;
    }

private:
    struct Info {
        const core::Unit* unit = nullptr;
        analysis::ThreatRanges threat;
        std::vector<float> ranges;
        Turn0 turn0;
        std::vector<core::UnitId> engaged_with;
        bool mv = false;
        double left = 0.0;
    };

    Turn0 turn0_of(const core::Unit& u) const {
        Turn0 t;
        const core::Stage s = state_.flow.stage;
        if (s == core::Stage::NotStarted || s == core::Stage::Deploy) return t;
        if (core::turn_side(state_) != side_) return t;  // the opponent's turn: our next turn, all of it
        const bool moving = state_.flow.move && state_.flow.move->unit == u.id;
        const bool moved =
            s == core::Stage::MoveUnits ? u.phase_flags.selected_to_move && !moving : s > core::Stage::MoveUnits;
        if (!moved) return t;
        t.move = false;
        const bool fell_back = u.turn_flags.fell_back;  // 09.07: no shooting, no charge
        const bool advanced = u.turn_flags.advanced;    // 09.06: no charge
        // The phase flags are reset when their phase starts, so before it they are last turn's.
        const bool shot_already = s >= core::Stage::StartOfShootingPhase && u.phase_flags.selected_to_shoot;
        const bool declared = s >= core::Stage::StartOfChargePhase && u.phase_flags.declared_charge;
        t.shoot = s <= core::Stage::Shoot && !shot_already && !fell_back;
        t.charge = s <= core::Stage::Charge && !declared && !fell_back && !advanced;
        return t;
    }

    void compute_turns() {
        turns_ = 0;
        const core::GameState& s = state_;
        if (s.result || s.flow.stage == core::Stage::GameOver) return;
        our_index_ = side_ == s.first_turn ? 0 : 1;
        int round = s.flow.battle_round;
        int t = s.flow.turn_in_round;
        if (round <= 0 || s.flow.stage == core::Stage::NotStarted || s.flow.stage == core::Stage::Deploy) {
            round = 1;
            t = 0;
        }
        first_round_ = our_index_ >= t ? round : round + 1;
        turns_ = std::max(0, s.battle_rounds - first_round_ + 1);
    }
    // Whether the enemy has a turn after our turn j (07.02: the second player's turn ends a battle round).
    [[nodiscard]] bool enemy_turn_after(int j) const {
        return our_index_ == 0 || first_round_ + j < state_.battle_rounds;
    }

    // Our member's shooting at the target in our turn j, not engaged with it: Shoot from min(d0, R) ([HEAVY] when it
    // needn't move, or after its first volley), Charge after its Normal move at tc if in range.
    double shot(const Track& t, std::size_t e, int j) {
        const float r = units_[t.unit].threat.max_range_mm;
        if (!(r > 0.0f)) return 0.0;
        if (t.mode == AttackMode::Shoot) {
            if (j < t.ts) return 0.0;
            return ranged(t.unit, e, std::min(t.d0, r), !core::more_than(t.d0, r) || j > t.ts);
        }
        if (t.mode == AttackMode::Charge && j == t.tc) {
            const float at = std::max(t.d0 - travel(t.unit, j), 0.0f);
            if (core::within(at, r)) return ranged(t.unit, e, at, false);
        }
        return 0.0;
    }
    // Its gap to the target after our turn j's move (unengaged): a shooter stops at its longest range, the rest
    // advance.
    [[nodiscard]] float position(const Track& t, int j) const {
        if (t.mode == AttackMode::Shoot && j >= t.ts) return std::min(t.d0, units_[t.unit].threat.max_range_mm);
        return std::max(t.d0 - travel(t.unit, j) - advance_bonus(t.unit, j), kOutsideEngagementMm);
    }

    // In our Fight phase the target, engaged with the members that fought (`fight` > 0), strikes back at the one it
    // hurts most.
    void strike_back(MultiExchange& x, const std::vector<Track>& g, std::size_t e, const std::vector<double>& fight) {
        std::size_t best = g.size();
        double best_v = 0.0;
        for (std::size_t i = 0; i < g.size(); ++i) {
            if (!(fight[i] > 0.0)) continue;
            MultiExchange y = x;
            y.we_hit(i, fight[i] * melee(e, g[i].unit));
            if (best == g.size() || y.value() > best_v + 1e-12) {
                best = i;
                best_v = y.value();
            }
        }
        if (best < g.size()) x.we_hit(best, fight[best] * melee(e, g[best].unit));
    }

    // next_turn_shot's context, from the engagements found once (the same answer, 04.02, 10.06, 17.03).
    [[nodiscard]] std::optional<analysis::ShotContext> shot_context(std::size_t a, std::size_t b, float gap_mm) const {
        analysis::ShotContext ctx;
        if (engaged_with(a, b)) {
            ctx.type = core::ShootingType::CloseQuarters;
            return ctx;
        }
        ctx.target_engaged = !units_[b].engaged_with.empty();
        if (!units_[a].engaged_with.empty()) {
            if (!units_[a].mv) return std::nullopt;
            ctx.type = core::ShootingType::CloseQuarters;
            ctx.elsewhere = true;
            ctx.gap_mm = gap_mm;
            return ctx;
        }
        ctx.type = core::ShootingType::Normal;
        ctx.heavy = true;
        ctx.gap_mm = gap_mm;
        return ctx;
    }
    double melee(std::size_t a, std::size_t b) {
        std::optional<double>& slot = melee_[a * units_.size() + b];
        if (!slot) slot = melee_share(cache_, state_, *units_[a].unit, *units_[b].unit);
        return *slot;
    }
    double cq(std::size_t a, std::size_t b) {
        std::optional<double>& slot = cq_[a * units_.size() + b];
        if (!slot) slot = ranged_share(cache_, state_, *units_[a].unit, *units_[b].unit, 0.0f, true);
        return *slot;
    }
    double ranged(std::size_t a, std::size_t b, float gap_mm, bool heavy) {
        const auto key = std::make_tuple(a, b, heavy, band_of(units_[a].ranges, gap_mm));
        if (const auto it = ranged_.find(key); it != ranged_.end()) return it->second;
        double v = 0.0;
        if (std::optional<analysis::ShotContext> ctx = shot_context(a, b, gap_mm)) {
            if (ctx->type == core::ShootingType::Normal) ctx->heavy = heavy;
            v = ranged_share(cache_, state_, *units_[a].unit, *units_[b].unit, *ctx);
        }
        return ranged_.emplace(key, v).first->second;
    }

    analysis::OddsCache& cache_;
    const core::GameState& state_;
    core::Side side_;
    std::vector<Info> units_;  // the units with models, in unit order
    std::map<std::uint32_t, std::size_t> index_;
    std::vector<std::size_t> ours_;
    std::vector<std::size_t> theirs_;
    int turns_ = 0;
    int first_round_ = 1;
    int our_index_ = 0;
    std::vector<std::optional<double>> melee_;
    std::vector<std::optional<double>> cq_;
    std::map<std::tuple<std::size_t, std::size_t, bool, std::uint64_t>, double> ranged_;
    std::vector<std::optional<float>> gaps_;
    std::vector<std::optional<Lone>> lone_;
    std::vector<std::optional<std::optional<std::size_t>>> closest_;
};

ArmyPlan noop_plan(const core::GameState& state, core::Side side) {
    ArmyPlan plan;
    plan.side = side;
    for (const core::Unit& u : state.units) {
        if (u.side != side || u.models.empty()) continue;
        UnitPlan p;
        p.unit = u.id;
        if (const core::Unit* c = closest_enemy(state, u)) p.focus = c->id;
        plan.units.push_back(std::move(p));
    }
    return plan;
}

// A candidate group for one target.
struct Choice {
    std::size_t target = 0;
    std::vector<std::size_t> members;  // unit order
    double value = 0.0;
    double alone = 0.0;
    double risk = 0.0;
    double key = 0.0;  // gain per risk
    int first = kNoArrival;
    std::optional<core::UnitId> reply_to;
    [[nodiscard]] double gain() const noexcept { return value - alone; }
};

// The best paying group for target e from the seeds (always members) and the candidates (their order is the
// tie-break): per size, the best gain per risk; the smallest size with a paying group, a bigger one only if its best
// group gains kMinPlanGain more.
std::optional<Choice> best_group(Planner& p, std::size_t e, const std::vector<std::size_t>& seeds,
                                 const std::vector<std::size_t>& candidates) {
    const std::size_t lo = std::max<std::size_t>(2, seeds.size());
    const std::size_t hi = std::max(seeds.size(), kMaxGroupSize);
    std::optional<Choice> chosen;
    for (std::size_t size = lo; size <= hi; ++size) {
        const std::size_t add = size - seeds.size();
        if (add > candidates.size()) break;
        std::optional<Choice> here;
        std::vector<bool> pick(candidates.size(), false);
        std::fill(pick.begin(), pick.begin() + static_cast<std::ptrdiff_t>(add), true);
        do {
            std::vector<std::size_t> g = seeds;
            for (std::size_t k = 0; k < candidates.size(); ++k) {
                if (pick[k]) g.push_back(candidates[k]);
            }
            std::sort(g.begin(), g.end());  // unit order (indices follow GameState::units)
            std::vector<Planner::Track> tracks;
            Choice c;
            c.target = e;
            for (const std::size_t u : g) {
                tracks.push_back(p.lone(u, e).track);
                c.alone += p.alone(u, e);
                c.risk += p.left(u);
            }
            const Planner::Result r = p.exchange(tracks, e);
            c.members = std::move(g);
            c.value = r.value;
            c.first = r.first;
            c.reply_to = r.reply_to;
            if (!(c.value > kMinPlanValue) || !(c.gain() > kMinPlanGain)) continue;
            c.key = c.risk > 0.0 ? c.gain() / c.risk : c.gain();
            if (!here || c.key > here->key + 1e-12) here = std::move(c);
        } while (std::prev_permutation(pick.begin(), pick.end()));
        if (!here) continue;
        if (!chosen || here->gain() > chosen->gain() + kMinPlanGain) chosen = std::move(here);
    }
    return chosen;
}

// ---- M87 (D-086): area sets ----

// One area as the area pass sees it.
struct AreaView {
    std::size_t index = 0;               // into Mission::objectives
    int fixed = 0;                       // our units outside the pool, where they stand
    int them_now = 0;                    // the enemy's level of control now
    std::vector<LevelPmf> reach;         // [k]: the enemy's level at the end of its k-th turn from now (k >= 1)
    std::vector<int> arrival;            // per planner unit index (ours): our turns until within range
};

// A candidate set for one area.
struct AreaChoice {
    std::size_t area = 0;                // index into the views
    std::vector<std::size_t> members;    // planner indices, unit order
    double value = 0.0;
    double deny = 0.0;
    double risk = 0.0;
    double chance = 0.0;
    double key = 0.0;                    // value per risk
};

class AreaPass {
public:
    AreaPass(Planner& p, const core::GameState& gs, core::Side side) : p_(p), gs_(gs) {
        const core::Mission& m = *gs.mission;
        rate_ = vp_share(gs);
        ours_ = core::side_index(side);
        first_scoring_ = m.first_scoring_round;
        theirs_scores_ = next_scorings(gs, side, turns_left(gs)).theirs;
        const int turns = p.turns();
        const std::size_t n = m.objectives.size();
        for (std::size_t i = 0; i < n; ++i) {
            AreaView v;
            v.index = i;
            v.reach.assign(static_cast<std::size_t>(std::max(1, turns)), LevelPmf{});
            v.arrival.assign(p.unit_count(), kNoArrival);
            views_.push_back(std::move(v));
        }
        // The enemy: its level now, and by its k-th turn from now (the turns before advancing, M + the D6's mean, then
        // this one's move), each unit in one place (one_place).
        for (const std::size_t e : p.theirs()) {
            const core::Unit& eu = p.unit(e);
            const int oc = unit_oc(eu);
            std::vector<float> gaps(n, 0.0f);
            std::vector<bool> in_range(n, false);
            for (std::size_t i = 0; i < n; ++i) {
                views_[i].them_now += oc_within(eu, m.objectives[i]);
                gaps[i] = area_gap_mm(eu, m.objectives[i]);
                in_range[i] = core::within(gaps[i], 0.0f);
            }
            if (oc <= 0) continue;
            const analysis::ThreatRanges& th = p.threat(e);
            const float per_turn = th.move_mm > 0.0f ? th.move_mm + kMeanAdvanceMm : 0.0f;
            for (int k = 1; k < turns; ++k) {
                std::vector<double> pk(n, 0.0);
                for (std::size_t i = 0; i < n; ++i) {
                    pk[i] = p.engaged(e) ? (in_range[i] ? 1.0 : 0.0)
                                         : p_reaches_area(th, std::max(0.0f, gaps[i] - static_cast<float>(k - 1) * per_turn));
                }
                one_place(pk, in_range);
                for (std::size_t i = 0; i < n; ++i) views_[i].reach[static_cast<std::size_t>(k)].add(oc, pk[i]);
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            const core::ObjectiveArea& area = m.objectives[i];
            AreaView& v = views_[i];
            for (const std::size_t u : p.ours()) {
                const float gap = area_gap_mm(p.unit(u), area);
                for (int j = 0; j < turns; ++j) {
                    if (core::within(gap, p.travel(u, j))) {
                        v.arrival[u] = j;
                        break;
                    }
                }
            }
        }
    }

    [[nodiscard]] std::vector<AreaView>& views() { return views_; }

    // The value of `members` (planner indices) on view `v`: the scorings they make over its fixed level, from the
    // first one after the set's earliest arrival, three at most; and the enemy's next scoring the members arriving this
    // turn deny.
    [[nodiscard]] AreaChoice evaluate(std::size_t vi, const std::vector<std::size_t>& members) const {
        const AreaView& v = views_[vi];
        const core::ObjectiveArea& area = gs_.mission->objectives[v.index];
        const double vp_us = rate_ * static_cast<double>(area.vp[ours_]);
        const double vp_them = rate_ * static_cast<double>(area.vp[1 - ours_]);
        AreaChoice c;
        c.area = vi;
        c.members = members;
        int first = kNoArrival;
        int now = 0;
        for (const std::size_t u : members) {
            first = std::min(first, v.arrival[u]);
            if (v.arrival[u] == 0) now += unit_oc(p_.unit(u));
            c.risk += p_.left(u);
        }
        const int turns = p_.turns();
        int counted = 0;
        bool first_scoring = true;
        for (int k = first + 1; k < turns && counted < kPlanVolleys; ++k) {
            if (p_.first_round() + k < first_scoring_) continue;  // our turn k's round scores nothing yet
            int level = v.fixed;
            for (const std::size_t u : members) {
                if (v.arrival[u] <= k - 1) level += unit_oc(p_.unit(u));
            }
            const LevelPmf& r = v.reach[static_cast<std::size_t>(k)];
            const double gain = r.p_below(level) - r.p_below(v.fixed);
            if (first_scoring) c.chance = r.p_below(level);
            first_scoring = false;
            c.value += vp_us * gain;
            ++counted;
        }
        if (theirs_scores_ && v.them_now > v.fixed && now > 0) {
            const double without = 1.0;
            const double with = v.them_now > v.fixed + now ? 1.0 : 0.0;
            c.deny = vp_them * (without - with);
            c.value += c.deny;
        }
        c.key = c.risk > 0.0 ? c.value / c.risk : c.value;
        return c;
    }

private:
    Planner& p_;
    const core::GameState& gs_;
    double rate_ = 0.0;
    std::size_t ours_ = 0;
    int first_scoring_ = 1;
    bool theirs_scores_ = false;
    std::vector<AreaView> views_;
};

// The best set for view `vi` from the candidates (their order is the tie-break): per size (1 to kMaxGroupSize), the best
// value per risk; the smallest size with a set worth more than kMinPlanValue, a bigger one only if its best set is worth
// kMinPlanGain more.
std::optional<AreaChoice> best_area_set(const AreaPass& pass, std::size_t vi, const std::vector<std::size_t>& candidates) {
    std::optional<AreaChoice> chosen;
    for (std::size_t size = 1; size <= kMaxGroupSize && size <= candidates.size(); ++size) {
        std::optional<AreaChoice> here;
        std::vector<bool> pick(candidates.size(), false);
        std::fill(pick.begin(), pick.begin() + static_cast<std::ptrdiff_t>(size), true);
        do {
            std::vector<std::size_t> g;
            for (std::size_t k = 0; k < candidates.size(); ++k) {
                if (pick[k]) g.push_back(candidates[k]);
            }
            std::sort(g.begin(), g.end());  // unit order
            AreaChoice c = pass.evaluate(vi, g);
            if (!(c.value > kMinPlanValue)) continue;
            if (!here || c.key > here->key + 1e-12) here = std::move(c);
        } while (std::prev_permutation(pick.begin(), pick.end()));
        if (!here) continue;
        if (!chosen || here->value > chosen->value + kMinPlanGain) chosen = std::move(here);
    }
    return chosen;
}

// M87 (D-086): under a mission, in our turn after our Command phase's scoring, the units no group took are sent to
// objective areas, greedily by value per risk, at most one set per area and one area per unit.
void assign_areas(Planner& p, const core::GameState& gs, core::Side side, ArmyPlan& plan) {
    if (!gs.mission || gs.mission->objectives.empty() || core::turn_side(gs) != side) return;
    const core::Stage stage = gs.flow.stage;
    if (stage <= core::Stage::StartOfCommandPhase || stage >= core::Stage::GameOver) return;
    if (p.turns() <= 0 || !(vp_share(gs) > 0.0)) return;
    AreaPass pass(p, gs, side);
    std::vector<std::size_t> pool;
    for (const std::size_t u : p.ours()) {
        const UnitPlan* up = plan.find(p.unit(u).id);
        const bool grouped = up != nullptr && up->group.has_value();
        if (!grouped && !p.engaged(u) && unit_oc(p.unit(u)) > 0) pool.push_back(u);
    }
    for (AreaView& v : pass.views()) {
        const core::ObjectiveArea& area = gs.mission->objectives[v.index];
        for (const std::size_t u : p.ours()) {
            if (std::find(pool.begin(), pool.end(), u) == pool.end()) v.fixed += oc_within(p.unit(u), area);
        }
    }
    std::vector<std::size_t> open;
    for (std::size_t i = 0; i < pass.views().size(); ++i) open.push_back(i);
    for (;;) {
        std::optional<AreaChoice> best;
        for (const std::size_t vi : open) {
            const AreaView& v = pass.views()[vi];
            // Candidates: the pool units that arrive soonest, then the most OC per strength, then unit order.
            std::vector<std::tuple<int, double, std::size_t>> c;
            for (const std::size_t u : pool) {
                const int a = v.arrival[u];
                if (a >= p.turns()) continue;
                const double per = static_cast<double>(unit_oc(p.unit(u))) / std::max(1e-9, p.left(u));
                c.emplace_back(a, -per, u);
            }
            std::stable_sort(c.begin(), c.end());
            if (c.size() > kPlanCandidates) c.resize(kPlanCandidates);
            std::vector<std::size_t> candidates;
            for (const auto& entry : c) candidates.push_back(std::get<2>(entry));
            std::optional<AreaChoice> here = best_area_set(pass, vi, candidates);
            if (here && (!best || here->key > best->key + 1e-12)) best = std::move(here);
        }
        if (!best) break;
        const AreaView& v = pass.views()[best->area];
        AreaSet set;
        set.area = v.index;
        set.value = best->value;
        set.deny = best->deny;
        set.risk = best->risk;
        set.chance = best->chance;
        for (const std::size_t u : best->members) {
            AreaMember m;
            m.unit = p.unit(u).id;
            m.arrival = v.arrival[u];
            m.oc = unit_oc(p.unit(u));
            set.members.push_back(m);
            pool.erase(std::remove(pool.begin(), pool.end(), u), pool.end());
        }
        open.erase(std::remove(open.begin(), open.end(), best->area), open.end());
        const std::size_t index = plan.areas.size();
        for (UnitPlan& up : plan.units) {
            if (set.member(up.unit) != nullptr) up.area = index;
        }
        plan.areas.push_back(std::move(set));
    }
}

std::string signed2(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%+.2f", v);
    return buf;
}

// The state the plan reads (the move in progress at its start), and a planner on it.
struct PlanInput {
    std::optional<core::GameState> copy;
    const core::GameState* state = nullptr;
    explicit PlanInput(const core::GameState& s) : copy(at_move_start(s)), state(copy ? &*copy : &s) {}
    PlanInput(const PlanInput&) = delete;
    PlanInput& operator=(const PlanInput&) = delete;
};

}  // namespace

const PlanMember* PlanGroup::member(core::UnitId unit) const {
    for (const PlanMember& m : members) {
        if (m.unit == unit) return &m;
    }
    return nullptr;
}

const AreaMember* AreaSet::member(core::UnitId unit) const {
    for (const AreaMember& m : members) {
        if (m.unit == unit) return &m;
    }
    return nullptr;
}

const UnitPlan* ArmyPlan::find(core::UnitId unit) const {
    for (const UnitPlan& u : units) {
        if (u.unit == unit) return &u;
    }
    return nullptr;
}

const PlanGroup* ArmyPlan::group_of(core::UnitId unit) const {
    const UnitPlan* u = find(unit);
    return u != nullptr && u->group && *u->group < groups.size() ? &groups[*u->group] : nullptr;
}

const AreaSet* ArmyPlan::area_of(core::UnitId unit) const {
    const UnitPlan* u = find(unit);
    return u != nullptr && u->area && *u->area < areas.size() ? &areas[*u->area] : nullptr;
}

ArmyPlan no_plan(const core::GameState& state, core::Side side) { return noop_plan(state, side); }

ArmyPlan army_plan(analysis::OddsCache& cache, const core::GameState& state, core::Side side) {
    const PlanInput in(state);
    const core::GameState& gs = *in.state;
    ArmyPlan plan = noop_plan(gs, side);
    // D-083 part 1: no plan before the battle, with fewer than two of our units, or with no turn left.
    if (plan.units.size() < 2) {
        // M87 (D-086): a last unit still gets an area under a mission.
        if (gs.mission && !plan.units.empty() && gs.flow.stage != core::Stage::NotStarted &&
            gs.flow.stage != core::Stage::Deploy) {
            Planner p(cache, gs, side);
            assign_areas(p, gs, side, plan);
        }
        return plan;
    }
    if (gs.flow.stage == core::Stage::NotStarted || gs.flow.stage == core::Stage::Deploy) return plan;
    Planner p(cache, gs, side);
    if (p.turns() <= 0 || p.theirs().empty()) return plan;
    plan.active = true;

    // The pool (our unengaged units) and the seeds (our engaged units, by the enemy unit they fight).
    std::vector<std::size_t> pool;
    std::map<std::size_t, std::vector<std::size_t>> seeds;
    for (const std::size_t u : p.ours()) {
        if (!p.engaged(u)) {
            pool.push_back(u);
        } else if (const std::optional<std::size_t> f = p.engaged_focus(u)) {
            seeds[*f].push_back(u);
        }
    }
    std::vector<std::size_t> targets = p.theirs();
    for (;;) {
        std::optional<Choice> best;
        for (const std::size_t e : targets) {
            // Candidates: the pool units that reach it soonest (then the gap, then unit order), within the game.
            std::vector<std::pair<std::pair<int, float>, std::size_t>> c;
            for (const std::size_t u : pool) {
                const std::pair<int, float> s = p.soonest(u, e);
                if (s.first < p.turns()) c.emplace_back(s, u);
            }
            std::stable_sort(c.begin(), c.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            if (c.size() > kPlanCandidates) c.resize(kPlanCandidates);
            std::vector<std::size_t> candidates;
            for (const auto& entry : c) candidates.push_back(entry.second);
            const auto it = seeds.find(e);
            const std::vector<std::size_t> none;
            std::optional<Choice> here = best_group(p, e, it != seeds.end() ? it->second : none, candidates);
            if (here && (!best || here->key > best->key + 1e-12)) best = std::move(here);
        }
        if (!best) break;
        PlanGroup g;
        g.target = p.unit(best->target).id;
        g.value = best->value;
        g.alone = best->alone;
        g.gain = best->gain();
        g.risk = best->risk;
        g.first_contact = best->first;
        g.reply_to = best->reply_to;
        for (const std::size_t u : best->members) {
            const Planner::Lone& l = p.lone(u, best->target);
            PlanMember m;
            m.unit = p.unit(u).id;
            m.arrival = l.track.arrival;
            m.mode = l.track.mode;
            m.lone = l.value;
            m.alone = p.alone(u, best->target);
            g.members.push_back(m);
            pool.erase(std::remove(pool.begin(), pool.end(), u), pool.end());
        }
        targets.erase(std::remove(targets.begin(), targets.end(), best->target), targets.end());
        const std::size_t index = plan.groups.size();
        for (UnitPlan& up : plan.units) {
            if (g.member(up.unit) == nullptr) continue;
            up.focus = g.target;
            up.group = index;
            for (const PlanMember& o : g.members) {
                if (o.unit != up.unit) up.partners.push_back(o.unit);
            }
        }
        plan.groups.push_back(std::move(g));
    }
    assign_areas(p, gs, side, plan);  // M87 (D-086): after the groups, which it leaves as they are
    return plan;
}

LoneExchange lone_exchange(analysis::OddsCache& cache, const core::GameState& state, core::UnitId ours,
                           core::UnitId enemy) {
    LoneExchange out;
    const PlanInput in(state);
    const core::Unit* u = in.state->find_unit(ours);
    if (u == nullptr) return out;
    Planner p(cache, *in.state, u->side);
    const std::optional<std::size_t> ui = p.index(ours);
    const std::optional<std::size_t> ei = p.index(enemy);
    if (!ui || !ei) return out;
    const Planner::Track s = p.track(*ui, *ei, AttackMode::Shoot);
    const Planner::Lone& l = p.lone(*ui, *ei);
    out.gap_mm = s.d0;
    out.shoot_arrival = s.ts;
    out.charge_arrival = s.tc;
    out.mode = l.track.mode;
    out.arrival = l.track.arrival;
    out.value = l.value;
    return out;
}

JointExchange joint_exchange(analysis::OddsCache& cache, const core::GameState& state,
                             std::span<const core::UnitId> members, core::UnitId target) {
    JointExchange out;
    if (members.empty()) return out;
    const PlanInput in(state);
    const core::Unit* first = in.state->find_unit(members.front());
    if (first == nullptr) return out;
    Planner p(cache, *in.state, first->side);
    const std::optional<std::size_t> e = p.index(target);
    if (!e) return out;
    std::vector<std::size_t> g;
    for (const core::UnitId id : members) {
        if (const std::optional<std::size_t> u = p.index(id)) g.push_back(*u);
    }
    std::sort(g.begin(), g.end());
    g.erase(std::unique(g.begin(), g.end()), g.end());
    std::vector<Planner::Track> tracks;
    for (const std::size_t u : g) tracks.push_back(p.lone(u, *e).track);
    const Planner::Result r = p.exchange(tracks, *e);
    out.value = r.value;
    out.first_contact = r.first;
    out.reply_to = r.reply_to;
    return out;
}

const char* area_role(const core::GameState& state, const ArmyPlan& plan, const AreaSet& set) {
    if (state.mission && set.area < state.mission->objectives.size() &&
        state.mission->objectives[set.area].controller == plan.side) {
        return "hold";
    }
    return set.deny > 0.0 && set.deny * 2.0 >= set.value ? "contest" : "take";
}

std::string describe_area(const core::GameState& state, const ArmyPlan& plan, const AreaSet& set) {
    std::string s;
    for (std::size_t i = 0; i < set.members.size(); ++i) {
        if (i > 0) s += " + ";
        s += client::unit_display_name(state, set.members[i].unit);
    }
    const std::string name = state.mission && set.area < state.mission->objectives.size()
                                 ? state.mission->objectives[set.area].name
                                 : "an area";
    char chance[32];
    std::snprintf(chance, sizeof chance, "%.0f%%", 100.0 * set.chance);
    return s + " to " + area_role(state, plan, set) + " " + name + " (" + signed2(set.value) + ", " + chance + ")";
}

std::string describe(const core::GameState& state, const ArmyPlan& plan) {
    std::string s = describe_groups(state, plan);
    for (const AreaSet& a : plan.areas) s += "; " + describe_area(state, plan, a);
    return s;
}

std::string describe_groups(const core::GameState& state, const ArmyPlan& plan) {
    if (!plan.active) return "no plan";
    if (plan.groups.empty()) return "no group pays";
    std::string s;
    for (const PlanGroup& g : plan.groups) {
        if (!s.empty()) s += "; ";
        for (std::size_t i = 0; i < g.members.size(); ++i) {
            if (i > 0) s += " + ";
            s += client::unit_display_name(state, g.members[i].unit);
        }
        s += " on " + client::unit_display_name(state, g.target) + " (joint " + signed2(g.value) + ", apart " +
             signed2(g.alone);
        if (g.reply_to) s += ", reply on " + client::unit_display_name(state, *g.reply_to);
        s += ")";
    }
    return s;
}

std::string describe_unit(const core::GameState& state, const ArmyPlan& plan, core::UnitId unit) {
    const UnitPlan* u = plan.find(unit);
    const PlanGroup* g = plan.group_of(unit);
    if (u == nullptr || g == nullptr || u->partners.empty()) return {};
    std::string s = "with ";
    for (std::size_t i = 0; i < u->partners.size(); ++i) {
        if (i > 0) s += i + 1 == u->partners.size() ? " and " : ", ";
        s += client::unit_display_name(state, u->partners[i]);
    }
    return s + " on " + client::unit_display_name(state, g->target) + " (joint " + signed2(g->value) + ", apart " +
           signed2(g->alone) + ")";
}

}  // namespace wh40k::ai::normal
