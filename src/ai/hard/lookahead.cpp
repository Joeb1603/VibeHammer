#include "hard/lookahead.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>
#include <utility>
#include <variant>

#include "analysis/charge_odds.hpp"
#include "baseline_bot.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/state/queries.hpp"
#include "directions.hpp"
#include "fallback.hpp"
#include "normal/charge.hpp"
#include "reach_area.hpp"
#include "simulator.hpp"
#include "state_hash.hpp"

namespace wh40k::ai::hard {

namespace {

using core::GameAction;
using core::GameState;
using core::Position;

constexpr float kEngagedMarginMm = 0.05f;  // ends this much further than engagement range (03.04), beyond EPS
constexpr float kEdgeMarginMm = 0.02f;     // bases this far inside the battlefield edge (03.01)
constexpr int kBisections = 6;             // "as far as it can go" along a direction
constexpr float kMinMoveMm = 0.25f * core::MM_PER_INCH;  // shorter translations are not worth proposing

int half_turn_of(const GameState& s) { return std::max(0, (s.flow.battle_round - 1) * 2 + s.flow.turn_in_round); }

Position centroid(const std::vector<Position>& ps) {
    Position c;
    for (const Position& p : ps) {
        c.x += p.x;
        c.y += p.y;
    }
    const float n = static_cast<float>(std::max<std::size_t>(1, ps.size()));
    return Position{c.x / n, c.y / n, 0.0f};
}

const core::Unit* enemy_of(const GameState& s, const core::Unit& u) {
    for (const core::Unit& o : s.units) {
        if (o.side != u.side && !o.models.empty()) return &o;
    }
    return nullptr;
}

// The move in progress with no model moved yet: every model's start, the enemy's models, the battlefield.
struct MoveSetup {
    core::UnitId unit;
    std::vector<core::ModelId> ids;
    std::vector<core::Base> bases;
    std::vector<Position> starts;
    float max_mm = 0.0f;  // the slowest model's maximum
    std::vector<core::PlacedModel> enemies;
    std::vector<core::PlacedModel> others;
    Position enemy_centroid;
    bool through_enemies = false;  // 09.07 Desperate Escape
    float width = 0.0f;
    float depth = 0.0f;
};

std::optional<MoveSetup> setup_of(const GameState& s) {
    if (!s.flow.move) return std::nullopt;
    const core::Unit* u = s.find_unit(s.flow.move->unit);
    if (u == nullptr || u->models.empty()) return std::nullopt;
    const core::Unit* e = enemy_of(s, *u);
    if (e == nullptr) return std::nullopt;
    MoveSetup m;
    m.unit = u->id;
    m.max_mm = -1.0f;
    for (const core::Model& model : u->models) {
        const std::optional<Position> start = core::move_start_position(s, model.id);
        const std::optional<float> max = core::max_move_distance_mm(s, model.id);
        if (!start || !max) return std::nullopt;
        m.ids.push_back(model.id);
        m.bases.push_back(model.base);
        m.starts.push_back(*start);
        m.max_mm = m.max_mm < 0.0f ? *max : std::min(m.max_mm, *max);
    }
    m.enemies = core::enemy_models(s, u->side);
    for (const core::Unit& o : s.units) {
        if (o.side != u->side || o.id == u->id) continue;
        for (const core::PlacedModel& p : core::placed_models(o)) m.others.push_back(p);
    }
    std::vector<Position> ps;
    for (const core::Model& model : e->models) ps.push_back(model.position);
    m.enemy_centroid = centroid(ps);
    m.through_enemies = s.flow.move->mode == core::FallBackMode::DesperateEscape;
    m.width = s.battlefield.width_mm;
    m.depth = s.battlefield.depth_mm;
    return m;
}

// The unit translated by (dx, dy) * t passes the cheap checks (the engine still decides): bases on the
// battlefield, straight lines clear of enemy bases (unless the move may pass through them), ends outside
// engagement range (09.05-09.07) and not on another model. A translation keeps the unit's coherency.
bool translation_ok(const MoveSetup& m, float dx, float dy, float t) {
    if (core::more_than(t, m.max_mm)) return false;
    for (std::size_t i = 0; i < m.starts.size(); ++i) {
        const Position a = m.starts[i];
        const Position b{a.x + dx * t, a.y + dy * t, a.z};
        const float r = core::base_radius(m.bases[i]);
        if (b.x < r + kEdgeMarginMm || b.x > m.width - r - kEdgeMarginMm || b.y < r + kEdgeMarginMm ||
            b.y > m.depth - r - kEdgeMarginMm) {
            return false;
        }
        for (const core::PlacedModel& e : m.enemies) {
            if (!m.through_enemies && client::point_segment_distance(e.position, a, b) < r + core::base_radius(e.base)) {
                return false;
            }
            const float d = core::base_distance(b, m.bases[i], e.position, e.base);
            if (!(d > core::kEngagementRangeMm + core::EPS_MM + kEngagedMarginMm)) return false;
        }
        for (const core::PlacedModel& o : m.others) {
            if (core::bases_overlap(b, m.bases[i], o.position, o.base)) return false;
        }
    }
    return true;
}

// The proposals (furthest along the move first, D-027) and the commit for the translation.
std::vector<GameAction> translation_steps(const MoveSetup& m, float dx, float dy, float t) {
    std::vector<std::size_t> order(m.starts.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    const auto ahead = [&](std::size_t i) { return m.starts[i].x * dx + m.starts[i].y * dy; };
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return ahead(a) > ahead(b); });
    std::vector<GameAction> out;
    if (t >= kMinMoveMm) {
        for (const std::size_t i : order) {
            const Position& a = m.starts[i];
            out.push_back(core::action::ProposeModelMove{m.ids[i], {Position{a.x + dx * t, a.y + dy * t, a.z}}});
        }
    }
    out.push_back(core::action::CommitUnitMove{});
    return out;
}

// A translation of up to `want_mm` along (dx, dy), or along the fixed direction closest to it that stands, as
// far as it can go (bisection). nullopt if none goes at least kMinMoveMm (`need_some`) or none at all.
std::optional<std::vector<GameAction>> plan_translation(const MoveSetup& m, float dx, float dy, float want_mm,
                                                        bool need_some) {
    const float len = std::sqrt(dx * dx + dy * dy);
    if (!(len > 0.0f) || !(want_mm > 0.0f)) {
        if (need_some) return std::nullopt;
        return translation_steps(m, 1.0f, 0.0f, 0.0f);
    }
    dx /= len;
    dy /= len;
    std::vector<client::UnitVector> dirs{client::UnitVector{dx, dy}};
    std::vector<client::UnitVector> table(client::kDirections16.begin(), client::kDirections16.end());
    std::stable_sort(table.begin(), table.end(), [&](const client::UnitVector& a, const client::UnitVector& b) {
        return a.x * dx + a.y * dy > b.x * dx + b.y * dy;
    });
    for (const client::UnitVector& d : table) {
        if (d.x * dx + d.y * dy >= 0.5f) dirs.push_back(d);  // within 60 degrees of the wanted direction
    }
    const float want = std::min(want_mm, m.max_mm);
    for (const client::UnitVector& d : dirs) {
        if (translation_ok(m, d.x, d.y, want)) return translation_steps(m, d.x, d.y, want);
        float lo = 0.0f;
        float hi = want;
        for (int k = 0; k < kBisections; ++k) {
            const float mid = 0.5f * (lo + hi);
            if (translation_ok(m, d.x, d.y, mid)) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        if (lo >= kMinMoveMm) return translation_steps(m, d.x, d.y, lo);
    }
    if (need_some) return std::nullopt;
    return translation_steps(m, 1.0f, 0.0f, 0.0f);
}

std::optional<GameAction> offered_start(const std::vector<GameAction>& legal, core::MoveType type,
                                        std::optional<core::FallBackMode> mode = std::nullopt) {
    for (const GameAction& a : legal) {
        const auto* m = std::get_if<core::action::StartMove>(&a);
        if (m != nullptr && m->move_type == type && (!mode || m->fall_back_mode == mode)) return a;
    }
    return std::nullopt;
}

// The rollout policy (see the header). `queue` gets the rest of a multi-step answer.
class RolloutPolicy {
public:
    RolloutPolicy(const Evaluator& eval, std::uint64_t seed) : eval_(eval), seed_(seed) {}

    GameAction next(const GameState& s, core::Side side, std::deque<GameAction>& queue) {
        const std::vector<GameAction> legal = core::legal_actions(s, side);
        const core::DecisionKind& kind = s.pending->kind;
        if (std::holds_alternative<core::decision::MoveUnits>(kind)) {
            if (auto a = move_units(s, legal)) return *a;
        } else if (std::holds_alternative<core::decision::MoveModels>(kind) && s.flow.move &&
                   s.flow.move->paths.empty()) {
            if (auto a = unit_move(s, queue)) return *a;
        } else if (std::holds_alternative<core::decision::Charge>(kind)) {
            if (auto a = charge(s, side, legal)) return *a;
        } else if (const auto* d = std::get_if<core::decision::SelectChargeTargets>(&kind)) {
            for (const GameAction& a : legal) {
                const auto* m = std::get_if<core::action::StartChargeMove>(&a);
                if (m == nullptr || m->unit != d->unit) continue;
                if (auto plan = normal::plan_charge_move(s, side, d->unit, m->targets, seed_, {})) {
                    for (std::size_t i = 1; i < plan->steps.size(); ++i) queue.push_back(plan->steps[i].action);
                    return plan->steps.front().action;
                }
                break;
            }
            return core::action::SkipChargeMove{d->unit};
        }
        const PolicyResult r = easy_policy(s, side, legal, seed_);
        for (const PlannedStep& step : r.rest) queue.push_back(step.action);
        return r.action;
    }

    // A unit whose move could not be made this phase stays put (no loop).
    void stuck(core::UnitId unit) { stuck_.push_back(unit); }

private:
    std::optional<GameAction> move_units(const GameState& s, const std::vector<GameAction>& legal) {
        std::optional<core::UnitId> unit;
        for (const GameAction& a : legal) {
            if (const auto* m = std::get_if<core::action::StartMove>(&a)) {
                unit = m->unit;
                break;
            }
        }
        if (!unit) return std::nullopt;
        const std::optional<GameAction> stay = offered_start(legal, core::MoveType::RemainStationary);
        if (std::find(stuck_.begin(), stuck_.end(), *unit) != stuck_.end()) return stay;
        const Evaluator::TurnChoice c = eval_.turn_choice(s);
        if (c.fall_back) {
            std::optional<GameAction> fb =
                offered_start(legal, core::MoveType::FallBack, core::FallBackMode::OrderedRetreat);
            if (!fb) fb = offered_start(legal, core::MoveType::FallBack, core::FallBackMode::DesperateEscape);
            if (fb) return fb;
        }
        if (c.advance) {
            if (auto adv = offered_start(legal, core::MoveType::Advance)) return adv;
        }
        if (std::fabs(c.towards_mm) > kMinMoveMm) {
            if (auto normal = offered_start(legal, core::MoveType::Normal)) return normal;
        }
        return stay ? stay : std::optional<GameAction>{};
    }

    std::optional<GameAction> unit_move(const GameState& s, std::deque<GameAction>& queue) {
        const core::MoveInProgress& mv = *s.flow.move;
        if (mv.type == core::MoveType::Charge || mv.type == core::MoveType::PileIn ||
            mv.type == core::MoveType::Consolidation) {
            return std::nullopt;  // the baseline's (a charge move is planned at SelectChargeTargets)
        }
        const std::optional<MoveSetup> m = setup_of(s);
        if (!m) return std::nullopt;
        const Position c = centroid(m->starts);
        const float ax = m->enemy_centroid.x - c.x;
        const float ay = m->enemy_centroid.y - c.y;
        float towards = 0.0f;
        bool need_some = false;
        if (mv.type == core::MoveType::FallBack) {
            towards = -m->max_mm;  // 09.07: as far away as it can, it must end unengaged
            need_some = true;
        } else {
            // No model has moved yet (paths empty): the unit stands where the move started.
            const Evaluator::TurnChoice choice = eval_.turn_choice(s);
            if (mv.type == core::MoveType::Advance) {
                const core::Unit* u = s.find_unit(mv.unit);
                const int roll = u != nullptr ? u->phase_flags.advance_roll.value_or(0) : 0;
                towards = roll >= 1 && roll <= 6 ? choice.advance_towards_mm[static_cast<std::size_t>(roll)] : 0.0f;
            } else {
                towards = choice.towards_mm;
            }
        }
        const float sign = towards >= 0.0f ? 1.0f : -1.0f;
        std::optional<std::vector<GameAction>> steps =
            plan_translation(*m, sign * ax, sign * ay, std::fabs(towards), need_some);
        if (!steps) {
            stuck(mv.unit);
            return GameAction{core::action::ResetUnitMove{}};
        }
        for (std::size_t i = 1; i < steps->size(); ++i) queue.push_back((*steps)[i]);
        return steps->front();
    }

    std::optional<GameAction> charge(const GameState& s, core::Side side, const std::vector<GameAction>& legal) {
        for (const GameAction& a : legal) {
            const auto* d = std::get_if<core::action::DeclareCharge>(&a);
            if (d == nullptr) continue;
            const core::Unit* u = s.find_unit(d->unit);
            if (u == nullptr || u->side != side) continue;
            const core::Unit* e = enemy_of(s, *u);
            if (e == nullptr) continue;
            const double p = analysis::p_charge_roll_covers(core::unit_distance(*u, *e).value_or(0.0f));
            if (eval_.wants_charge(s, p)) return a;
        }
        for (const GameAction& a : legal) {
            if (std::holds_alternative<core::action::EndChargeStep>(a)) return a;
        }
        return std::nullopt;
    }

    const Evaluator& eval_;
    std::uint64_t seed_;
    std::vector<core::UnitId> stuck_;
};

}  // namespace

std::uint64_t rollout_seed(const GameState& state) {
    Fnv1a h;
    h.u64(0x6c6f6f6b61686561ull);  // "lookahea"
    h.i32(state.flow.battle_round);
    h.i32(state.flow.turn_in_round);
    for (const core::Unit& u : state.units) {
        h.u64(u.id.v);
        for (const core::Model& m : u.models) {
            h.u64(m.id.v);
            Position p = m.position;
            if (state.flow.move && state.flow.move->unit == u.id) {
                if (const std::optional<Position> start = core::move_start_position(state, m.id)) p = *start;
            }
            h.f32(p.x);
            h.f32(p.y);
            h.i32(m.wounds_remaining);
        }
    }
    return h.value();
}

MonteCarloResult monte_carlo(const Evaluator& eval, analysis::OddsCache& odds, const GameState& start,
                             core::Side side, std::uint64_t seed, const LookaheadWeights& weights,
                             const Opening& opening) {
    (void)odds;
    MonteCarloResult out;
    if (!eval.ok()) return out;
    const int h_start = half_turn_of(start);
    const int h_stop = start.pending && core::turn_side(start) == side ? h_start + 2 : h_start + 1;
    double sum = 0.0;
    double sum_v = 0.0;
    for (std::size_t i = 0; i < weights.rollouts && out.actions < weights.budget_actions; ++i) {
        Fnv1a rs;
        rs.u64(seed);
        rs.u64(static_cast<std::uint64_t>(i));
        Simulator sim(start, rs.value());
        RolloutPolicy policy(eval, rs.value());
        std::deque<GameAction> queue(opening.actions.begin(), opening.actions.end());
        bool advance_moves_taken = false;
        std::size_t steps = 0;
        bool cut = false;
        while (true) {
            const GameState& s = sim.state();
            if (s.result || !s.pending) break;
            if (half_turn_of(s) >= h_stop) break;
            if (steps >= weights.rollout_actions || out.actions >= weights.budget_actions) {
                cut = true;
                break;
            }
            const core::Side who = s.pending->side;
            if (queue.empty() && !advance_moves_taken && s.flow.move && s.flow.move->type == core::MoveType::Advance &&
                s.flow.move->paths.empty()) {
                advance_moves_taken = true;
                const core::Unit* u = s.find_unit(s.flow.move->unit);
                const int roll = u != nullptr ? u->phase_flags.advance_roll.value_or(0) : 0;
                if (roll >= 1 && roll <= 6) {
                    const auto& moves = opening.after_advance_roll[static_cast<std::size_t>(roll)];
                    queue.insert(queue.end(), moves.begin(), moves.end());
                }
            }
            GameAction a;
            if (!queue.empty()) {
                a = queue.front();
                queue.pop_front();
            } else {
                a = policy.next(s, who, queue);
            }
            ++steps;
            ++out.actions;
            if (sim.step(who, a).ok()) continue;
            // Refused (the rollout's plan didn't fit the state): the always-legal fallback, else stop here.
            queue.clear();
            if (s.flow.move && s.flow.move->unit.v != 0) policy.stuck(s.flow.move->unit);
            const FallbackResult fb = fallback_action(sim.state(), who);
            if (!fb.action || !sim.step(who, *fb.action).ok()) {
                cut = true;
                break;
            }
        }
        const Evaluation e = eval.evaluate(sim.state());
        sum += e.score;
        sum_v += e.v;
        ++out.rollouts;
        if (cut) ++out.cut;
        if (sim.state().result) ++out.game_over;
    }
    if (out.rollouts > 0) {
        out.mean = sum / static_cast<double>(out.rollouts);
        out.mean_v = sum_v / static_cast<double>(out.rollouts);
    }
    return out;
}

}  // namespace wh40k::ai::hard
