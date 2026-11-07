#include "unit_style.hpp"
#include "normal/movement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <utility>
#include <variant>

#include "analysis/charge_odds.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/path.hpp"
#include "core/geometry/units.hpp"
#include "core/rules/mortal_wounds.hpp"
#include "core/state/queries.hpp"
#include "directions.hpp"
#include "normal/fight.hpp"
#include "normal/value.hpp"
#include "reach_area.hpp"
#include "simulator.hpp"
#include "state_hash.hpp"

namespace wh40k::ai::normal {

namespace {

using core::GameAction;
using core::GameState;
using core::Position;

constexpr float kIn = core::MM_PER_INCH;
constexpr float kSlotGapMm = 0.5f;         // between neighbouring bases in a formation (in coherency, not touching)
constexpr float kEngagedMarginMm = 0.05f;  // ends this much further than engagement range (03.04), beyond EPS
constexpr float kEdgeMarginMm = 0.02f;     // bases this far inside the battlefield edge (03.01)
constexpr float kParkMm = 1.0f;            // a parked model sits this far beyond touching its start position
constexpr int kBisections = 8;             // "as far as it can go" along a direction
constexpr double kBetter = 1e-9;
// Distance steps for translations (inches); the move's maximum is always added.
constexpr std::array<float, 25> kStepsIn{0.5f,  1.0f,  1.5f,  2.0f,  2.5f,  3.0f,  4.0f,  5.0f,  6.0f,
                                         7.0f,  8.0f,  9.0f,  10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f,
                                         16.0f, 17.0f, 18.0f, 19.0f, 20.0f, 22.0f, 24.0f};
// Formation centres along the line to the enemy (inches; positive towards it).
constexpr std::array<float, 9> kFormationOffsetsIn{-9.0f, -6.0f, -3.0f, 0.0f, 3.0f, 6.0f, 9.0f, -12.0f, 12.0f};

std::string inches(float mm) {
    char buf[32];
    const float v = mm / kIn;
    if (std::fabs(v - std::round(v)) < 0.05f) {
        std::snprintf(buf, sizeof buf, "%.0f\"", static_cast<double>(v));
    } else {
        std::snprintf(buf, sizeof buf, "%.1f\"", static_cast<double>(v));
    }
    return buf;
}

std::string signed_value(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%+.2f", v);
    return buf;
}

std::string percent(double p) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f%%", 100.0 * p);
    return buf;
}

PolicyResult single(GameAction a, std::string reason) { return PolicyResult{std::move(a), std::move(reason), {}}; }

PolicyResult from_steps(std::vector<PlannedStep> steps, std::size_t first, const std::string& reason) {
    PolicyResult r{steps[first].action, reason, {}};
    for (std::size_t i = first + 1; i < steps.size(); ++i) {
        steps[i].reason = reason;
        r.rest.push_back(std::move(steps[i]));
    }
    return r;
}

bool offered(const std::vector<GameAction>& legal, const GameAction& a) {
    return std::find(legal.begin(), legal.end(), a) != legal.end();
}

Position centroid_of(const std::vector<Position>& ps) {
    Position c;
    for (const Position& p : ps) {
        c.x += p.x;
        c.y += p.y;
    }
    const float n = static_cast<float>(std::max<std::size_t>(1, ps.size()));
    return Position{c.x / n, c.y / n, 0.0f};
}

// ---- the geometry of the move in progress, for the cheap checks (the engine still decides) ----

struct MoveGeometry {
    core::UnitId unit;
    core::MoveType type = core::MoveType::Normal;
    bool through_enemies = false;  // 09.07 Desperate Escape
    std::vector<core::ModelId> ids;
    std::vector<core::Base> bases;
    std::vector<Position> starts;
    std::vector<float> max_mm;
    float unit_max_mm = 0.0f;  // the slowest model's maximum
    std::vector<core::PlacedModel> enemies;
    std::vector<core::PlacedModel> others;  // friendly models of other units
    float width = 0.0f;
    float depth = 0.0f;
    bool has_enemy = false;
    Position enemy_centroid;
};

std::optional<MoveGeometry> geometry_of(const GameState& origin) {
    if (!origin.flow.move) return std::nullopt;
    const core::MoveInProgress& mv = *origin.flow.move;
    const core::Unit* u = origin.find_unit(mv.unit);
    if (u == nullptr || u->models.empty()) return std::nullopt;
    MoveGeometry g;
    g.unit = u->id;
    g.type = mv.type;
    g.through_enemies = mv.mode == core::FallBackMode::DesperateEscape;
    g.width = origin.battlefield.width_mm;
    g.depth = origin.battlefield.depth_mm;
    g.unit_max_mm = -1.0f;
    for (const core::Model& m : u->models) {
        const std::optional<Position> start = core::move_start_position(origin, m.id);
        const std::optional<float> max = core::max_move_distance_mm(origin, m.id);
        if (!start || !max) return std::nullopt;
        g.ids.push_back(m.id);
        g.bases.push_back(m.base);
        g.starts.push_back(*start);
        g.max_mm.push_back(*max);
        g.unit_max_mm = g.unit_max_mm < 0.0f ? *max : std::min(g.unit_max_mm, *max);
    }
    g.enemies = core::enemy_models(origin, u->side);
    for (const core::Unit& other : origin.units) {
        if (other.side != u->side || other.id == u->id) continue;
        for (const core::PlacedModel& p : core::placed_models(other)) g.others.push_back(p);
    }
    if (const core::Unit* e = closest_enemy(origin, *u)) {
        std::vector<Position> ps;
        for (const core::Model& m : e->models) ps.push_back(m.position);
        g.enemy_centroid = centroid_of(ps);
        g.has_enemy = true;
    }
    return g;
}

bool on_battlefield(const MoveGeometry& g, std::size_t i, const Position& p) {
    const float r = core::base_radius(g.bases[i]) + kEdgeMarginMm;
    return p.x >= r && p.x <= g.width - r && p.y >= r && p.y <= g.depth - r;
}

// 03.01: the straight line from `a` to `b` doesn't pass through an enemy model (unless the move may).
bool segment_clear(const MoveGeometry& g, std::size_t i, const Position& a, const Position& b) {
    if (g.through_enemies) return true;
    const float r = core::base_radius(g.bases[i]);
    for (const core::PlacedModel& e : g.enemies) {
        if (client::point_segment_distance(e.position, a, b) < r + core::base_radius(e.base)) return false;
    }
    return true;
}

// The end positions: on the battlefield, not on another model, out of engagement range (09.05-09.07 After Moving)
// and in coherency (03.03).
bool ends_ok(const MoveGeometry& g, const std::vector<Position>& ends) {
    for (std::size_t i = 0; i < ends.size(); ++i) {
        if (!on_battlefield(g, i, ends[i])) return false;
        for (const core::PlacedModel& e : g.enemies) {
            const float d = core::base_distance(ends[i], g.bases[i], e.position, e.base);
            if (!(d > core::kEngagementRangeMm + core::EPS_MM + kEngagedMarginMm)) return false;
        }
        for (const core::PlacedModel& o : g.others) {
            if (core::bases_overlap(ends[i], g.bases[i], o.position, o.base)) return false;
        }
        for (std::size_t j = i + 1; j < ends.size(); ++j) {
            if (core::bases_overlap(ends[i], g.bases[i], ends[j], g.bases[j])) return false;
        }
    }
    std::vector<core::PlacedModel> placed;
    for (std::size_t i = 0; i < ends.size(); ++i) placed.push_back(core::PlacedModel{g.ids[i], ends[i], g.bases[i]});
    return core::check_coherency(placed).ok();
}

// "3\" back", "2\" obliquely towards": the centroid's move relative to the line to the enemy.
std::string move_label(const MoveGeometry& g, float dx, float dy) {
    const float len = std::sqrt(dx * dx + dy * dy);
    if (!(len > 0.01f)) return "stays where it is";
    std::string dir = "";
    if (g.has_enemy) {
        const Position c = centroid_of(g.starts);
        float ax = g.enemy_centroid.x - c.x;
        float ay = g.enemy_centroid.y - c.y;
        const float al = std::sqrt(ax * ax + ay * ay);
        if (al > 0.0f) {
            ax /= al;
            ay /= al;
            const float cosv = (dx * ax + dy * ay) / len;
            dir = cosv >= 0.98f   ? "towards"
                  : cosv >= 0.35f ? "obliquely towards"
                  : cosv > -0.35f ? "sideways"
                  : cosv > -0.98f ? "obliquely back"
                                  : "back";
        }
    }
    return inches(len) + (dir.empty() ? "" : " " + dir);
}

std::optional<MoveCandidate> translation(const MoveGeometry& g, const client::UnitVector& d, float t) {
    MoveCandidate c;
    c.ends.resize(g.starts.size());
    c.lengths.assign(g.starts.size(), t);
    for (std::size_t i = 0; i < g.starts.size(); ++i) {
        if (core::more_than(t, g.max_mm[i])) return std::nullopt;
        c.ends[i] = Position{g.starts[i].x + d.x * t, g.starts[i].y + d.y * t, g.starts[i].z};
        if (!segment_clear(g, i, g.starts[i], c.ends[i])) return std::nullopt;
    }
    if (!ends_ok(g, c.ends)) return std::nullopt;
    // D-027: furthest along the move first, so no model ends on one still to move (as translate_unit).
    std::vector<std::size_t> order(g.starts.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    const auto ahead = [&](std::size_t i) { return g.starts[i].x * d.x + g.starts[i].y * d.y; };
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return ahead(a) > ahead(b); });
    for (const std::size_t i : order) c.proposals.push_back(core::action::ProposeModelMove{g.ids[i], {c.ends[i]}});
    c.longest_mm = t;
    c.label = move_label(g, d.x * t, d.y * t);
    return c;
}

// The proposals for moving each model straight to `ends` (unit order): an order in which no model ends on a model
// still at its current position (D-027); a cycle is broken by parking one model beside its start first and
// proposing it again later. nullopt if no order is found.
std::optional<std::vector<core::action::ProposeModelMove>> slot_proposals(const MoveGeometry& g,
                                                                          const std::vector<Position>& ends) {
    const std::size_t n = ends.size();
    std::vector<Position> now = g.starts;
    std::vector<bool> done(n, false);
    std::vector<bool> parked(n, false);
    std::vector<core::action::ProposeModelMove> out;
    // Furthest along the unit's displacement first (ties in unit order).
    float mx = 0.0f;
    float my = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        mx += ends[i].x - g.starts[i].x;
        my += ends[i].y - g.starts[i].y;
    }
    std::vector<std::size_t> order(n);
    for (std::size_t i = 0; i < n; ++i) order[i] = i;
    const auto ahead = [&](std::size_t i) { return g.starts[i].x * mx + g.starts[i].y * my; };
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return ahead(a) > ahead(b); });
    const auto free_at = [&](std::size_t i, const Position& p, bool avoid_slots) {
        for (std::size_t j = 0; j < n; ++j) {
            if (j == i) continue;
            if (core::bases_overlap(p, g.bases[i], now[j], g.bases[j])) return false;
            if (avoid_slots && !done[j] && core::bases_overlap(p, g.bases[i], ends[j], g.bases[j])) return false;
        }
        return true;
    };
    for (std::size_t guard = 0; guard < 3 * n + 3; ++guard) {
        bool all = true;
        bool progressed = false;
        for (const std::size_t i : order) {
            if (done[i]) continue;
            all = false;
            if (ends[i] == g.starts[i] && !parked[i]) {
                done[i] = true;  // not moved at all
                progressed = true;
                continue;
            }
            if (!free_at(i, ends[i], false)) continue;
            out.push_back(core::action::ProposeModelMove{g.ids[i], {ends[i]}});
            now[i] = ends[i];
            done[i] = true;
            progressed = true;
        }
        if (all) return out;
        if (progressed) continue;
        // A cycle: park the first waiting model beside its start (not on any model or any slot still to fill).
        bool parked_one = false;
        for (const std::size_t i : order) {
            if (done[i] || parked[i]) continue;
            const float step = core::base_radius(g.bases[i]) * 2.0f + kParkMm;
            for (const client::UnitVector& d : client::kDirections16) {
                const Position p{g.starts[i].x + d.x * step, g.starts[i].y + d.y * step, g.starts[i].z};
                if (!on_battlefield(g, i, p) || core::more_than(step, g.max_mm[i]) ||
                    !segment_clear(g, i, g.starts[i], p) || !free_at(i, p, true)) {
                    continue;
                }
                bool clear_of_others = true;
                for (const core::PlacedModel& o : g.others)
                    clear_of_others = clear_of_others && !core::bases_overlap(p, g.bases[i], o.position, o.base);
                for (const core::PlacedModel& e : g.enemies)
                    clear_of_others = clear_of_others && !core::bases_overlap(p, g.bases[i], e.position, e.base);
                if (!clear_of_others) continue;
                out.push_back(core::action::ProposeModelMove{g.ids[i], {p}});
                now[i] = p;
                parked[i] = true;
                parked_one = true;
                break;
            }
            if (parked_one) break;
        }
        if (!parked_one) return std::nullopt;
    }
    return std::nullopt;
}

// Line abreast (ranks = 1) or two ranks facing the enemy, centred `offset` from the unit's centroid along the line
// to it (positive towards it), shifted inside the battlefield.
std::optional<MoveCandidate> formation(const MoveGeometry& g, int ranks, float offset_mm) {
    const std::size_t n = g.starts.size();
    if (!g.has_enemy || n < 2 || (ranks == 2 && n < 4)) return std::nullopt;
    const Position c0 = centroid_of(g.starts);
    float ax = g.enemy_centroid.x - c0.x;
    float ay = g.enemy_centroid.y - c0.y;
    const float al = std::sqrt(ax * ax + ay * ay);
    if (!(al > 0.0f)) return std::nullopt;
    ax /= al;
    ay /= al;
    const float px = -ay;  // along the line, left to right facing the enemy
    const float py = ax;
    float spacing = 0.0f;
    float radius = 0.0f;
    for (const core::Base& b : g.bases) {
        spacing = std::max(spacing, b.diameter_mm + kSlotGapMm);
        radius = std::max(radius, core::base_radius(b));
    }
    const std::size_t per_rank = (n + static_cast<std::size_t>(ranks) - 1) / static_cast<std::size_t>(ranks);
    // Slots relative to the centre: rank 0 is the front (towards the enemy).
    struct Slot {
        float lateral;
        float depth;
        std::size_t rank;
    };
    std::vector<Slot> slots;
    for (std::size_t k = 0; k < static_cast<std::size_t>(ranks); ++k) {
        const std::size_t count = std::min(per_rank, n - std::min(n, k * per_rank));
        for (std::size_t j = 0; j < count; ++j) {
            const float lateral = (static_cast<float>(j) - 0.5f * static_cast<float>(count - 1)) * spacing;
            const float depth = (0.5f * static_cast<float>(ranks - 1) - static_cast<float>(k)) * spacing;
            slots.push_back(Slot{lateral, depth, k});
        }
    }
    float cx = c0.x + ax * offset_mm;
    float cy = c0.y + ay * offset_mm;
    // Shift the whole formation inside the battlefield.
    float min_x = 1e9f;
    float max_x = -1e9f;
    float min_y = 1e9f;
    float max_y = -1e9f;
    for (const Slot& s : slots) {
        const float x = px * s.lateral + ax * s.depth;
        const float y = py * s.lateral + ay * s.depth;
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
    }
    const float lo = radius + kEdgeMarginMm + 0.01f;
    if (max_x - min_x > g.width - 2.0f * lo || max_y - min_y > g.depth - 2.0f * lo) return std::nullopt;
    cx = std::clamp(cx, lo - min_x, g.width - lo - max_x);
    cy = std::clamp(cy, lo - min_y, g.depth - lo - max_y);
    // Models to slots: the models closest to the enemy to the front rank, each rank in order along the line.
    std::vector<std::size_t> by_depth(n);
    for (std::size_t i = 0; i < n; ++i) by_depth[i] = i;
    const auto depth_of = [&](std::size_t i) { return g.starts[i].x * ax + g.starts[i].y * ay; };
    const auto lateral_of = [&](std::size_t i) { return g.starts[i].x * px + g.starts[i].y * py; };
    std::stable_sort(by_depth.begin(), by_depth.end(),
                     [&](std::size_t a, std::size_t b) { return depth_of(a) > depth_of(b); });
    MoveCandidate cand;
    cand.ends.resize(n);
    cand.lengths.resize(n);
    std::size_t next = 0;
    for (std::size_t k = 0; k < static_cast<std::size_t>(ranks); ++k) {
        std::vector<std::size_t> rank_slots;
        for (std::size_t s = 0; s < slots.size(); ++s) {
            if (slots[s].rank == k) rank_slots.push_back(s);
        }
        std::vector<std::size_t> models(by_depth.begin() + static_cast<std::ptrdiff_t>(next),
                                        by_depth.begin() + static_cast<std::ptrdiff_t>(next + rank_slots.size()));
        next += rank_slots.size();
        std::stable_sort(models.begin(), models.end(),
                         [&](std::size_t a, std::size_t b) { return lateral_of(a) < lateral_of(b); });
        for (std::size_t j = 0; j < models.size(); ++j) {
            const Slot& s = slots[rank_slots[j]];
            const std::size_t i = models[j];
            cand.ends[i] =
                Position{cx + px * s.lateral + ax * s.depth, cy + py * s.lateral + ay * s.depth, g.starts[i].z};
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        const float len = core::centre_distance(g.starts[i], cand.ends[i]);
        if (core::more_than(len, g.max_mm[i]) || !segment_clear(g, i, g.starts[i], cand.ends[i])) return std::nullopt;
        cand.lengths[i] = len;
        cand.longest_mm = std::max(cand.longest_mm, len);
    }
    if (!ends_ok(g, cand.ends)) return std::nullopt;
    std::optional<std::vector<core::action::ProposeModelMove>> proposals = slot_proposals(g, cand.ends);
    if (!proposals) return std::nullopt;
    cand.proposals = std::move(*proposals);
    const Position c1 = centroid_of(cand.ends);
    cand.label =
        std::string(ranks == 1 ? "into line abreast, " : "into two ranks, ") + move_label(g, c1.x - c0.x, c1.y - c0.y);
    return cand;
}

// ---- simulation and choice ----

struct Simulated {
    std::vector<PlannedStep> steps;
    GameState after;
};

// D-054: the whole move made on a scratch copy first: every proposal valid, move_end_problems empty, the commit not
// reverted.
// (`sim` holds the origin, fresh.)
std::optional<Simulated> simulate_on(Simulator& sim, core::Side side, const MoveCandidate& c) {
    Simulated out;
    for (const core::action::ProposeModelMove& p : c.proposals) {
        out.steps.push_back(PlannedStep{state_hash(sim.state()), p, {}});
        if (!sim.step(side, p).ok()) return std::nullopt;
    }
    if (!core::move_end_problems(sim.state()).empty()) return std::nullopt;
    out.steps.push_back(PlannedStep{state_hash(sim.state()), core::action::CommitUnitMove{}, {}});
    std::vector<core::GameEvent> events;
    if (!sim.step(side, core::action::CommitUnitMove{}, &events).ok()) return std::nullopt;
    for (const core::GameEvent& e : events) {
        if (std::holds_alternative<core::event::UnitMoveReverted>(e)) return std::nullopt;
    }
    out.after = sim.state();
    return out;
}

std::optional<Simulated> simulate(const GameState& origin, core::Side side, const MoveCandidate& c,
                                  std::uint64_t seed) {
    Simulator sim(origin, seed);
    return simulate_on(sim, side, c);
}

// Every model's path fits the move's maximum distance in `origin` (an Advance's candidates are made once for the
// largest roll and filtered per roll).
bool fits(const GameState& origin, const MoveCandidate& c) {
    const core::Unit* u = origin.find_unit(origin.flow.move->unit);
    for (std::size_t i = 0; i < u->models.size() && i < c.lengths.size(); ++i) {
        const std::optional<float> max = core::max_move_distance_mm(origin, u->models[i].id);
        if (!max || core::more_than(c.lengths[i], *max)) return false;
    }
    return true;
}

struct Ranked {
    std::size_t index = 0;
    MoveScore score;
};

std::vector<Ranked> rank(MoveScoreContext& ctx, core::MoveType type, const std::vector<MoveCandidate>& cands) {
    std::vector<Ranked> out;
    out.reserve(cands.size());
    for (std::size_t i = 0; i < cands.size(); ++i) {
        out.push_back(Ranked{i, score_move_plan(ctx, PlannedEnd{type, cands[i].ends, cands[i].longest_mm})});
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Ranked& a, const Ranked& b) { return a.score.total > b.score.total; });
    return out;
}

// The best of the ranked candidates, simulated in full and refined (`precise` of them at most).
std::optional<PlannedMove> choose(MoveScoreContext& ctx, const GameState& origin, core::Side side,
                                  const std::vector<MoveCandidate>& cands, const std::vector<Ranked>& ranked,
                                  std::uint64_t seed, bool prospects, std::size_t precise = kPreciseMoves) {
    const core::MoveType type = origin.flow.move->type;
    std::optional<PlannedMove> best;
    std::size_t attempts = 0;
    std::size_t refined = 0;
    std::size_t prospected = 0;
    std::optional<Simulator> scratch;  // M54: one simulator, reset for each candidate (simulate's result)
    for (const Ranked& r : ranked) {
        if (refined >= precise || attempts >= kMaxSimulatedMoves) break;
        const MoveCandidate& c = cands[r.index];
        if (!fits(origin, c)) continue;
        ++attempts;
        if (scratch) {
            scratch->reset(origin, seed);
        } else {
            scratch.emplace(origin, seed);
        }
        std::optional<Simulated> sim = simulate_on(*scratch, side, c);
        if (!sim) continue;
        ++refined;
        const bool cp = prospects && prospected < kChargeProspects && r.score.p_charge > 0.0;
        if (cp) ++prospected;
        const MoveScore s = refine_move_score(ctx, PlannedEnd{type, c.ends, c.longest_mm}, sim->after, seed, cp);
        if (!best || s.total > best->score.total + kBetter) {
            best = PlannedMove{c, s, std::move(sim->steps), std::move(sim->after)};
        }
    }
    return best;
}

std::string move_type_text(core::MoveType t, std::optional<core::FallBackMode> mode) {
    switch (t) {
        case core::MoveType::RemainStationary:
            return "Remain Stationary";
        case core::MoveType::Normal:
            return "Normal move";
        case core::MoveType::Advance:
            return "Advance";
        case core::MoveType::FallBack:
            return mode == core::FallBackMode::DesperateEscape ? "Fall Back (Desperate Escape)"
                                                               : "Fall Back (Ordered Retreat)";
        default:
            return "move";
    }
}

// "13.2\" from Termagants, outside its half range, its charge odds next turn 8%".
std::string position_text(const GameState& gs, core::UnitId unit, const MoveScore& s) {
    const core::Unit* u = gs.find_unit(unit);
    const core::Unit* e = u != nullptr ? closest_enemy(gs, *u) : nullptr;
    if (e == nullptr) return "no enemy left";
    std::string out = inches(s.gap_mm) + " from " + client::unit_display_name(gs, e->id);
    if (s.half_range_models > 0) {
        out += ", " + std::to_string(s.half_range_models) + " of " + std::to_string(u->models.size()) +
               " models within half range";
    }
    if (s.gap_mm > core::kEngagementRangeMm) {
        out += s.in_enemy_half_range ? ", within its half range" : ", outside its half range";
        out += s.p_enemy_charge > 0.005 ? ", its charge odds next turn " + percent(s.p_enemy_charge)
                                        : ", outside its likely charge range";
    }
    if (s.p_charge > 0.0) out += ", charge odds " + percent(s.p_charge);
    return out;
}

std::string name_of(const GameState& gs, core::UnitId id) {
    return client::unit_display_name(gs, id);  // D-065
}

// ---- move types at the MoveUnits decision ----

struct Option {
    GameAction start;
    double value = 0.0;
    std::vector<PlannedStep> steps;  // the whole plan when no dice are rolled (StartMove first)
    std::string text;                // "Normal move 5\" back"
    MoveScore score;
};

// An Advance before its roll (D-054): each of the six results on a scratch state with that roll scripted, the best
// move for it; the candidates are made once for the largest roll and filtered per roll.
std::optional<Option> advance_option(analysis::OddsCache& cache, const GameState& gs, core::Side side,
                                     core::UnitId unit) {
    const GameAction start = core::action::StartMove{unit, core::MoveType::Advance, std::nullopt};
    std::array<std::optional<GameState>, 7> states;
    std::size_t widest = 1;
    float widest_mm = -1.0f;
    const core::ModelId first = gs.find_unit(unit)->models.front().id;
    for (int face = 1; face <= 6; ++face) {
        Simulator sim(gs, 0);
        core::ScriptedDice dice{face};  // not consumed when the roll is reused (D-005)
        if (!sim.step(side, start, dice).ok()) return std::nullopt;
        const auto f = static_cast<std::size_t>(face);
        states[f] = sim.state();
        const float max = core::max_move_distance_mm(*states[f], first).value_or(0.0f);
        if (max > widest_mm) {
            widest_mm = max;
            widest = f;
        }
    }
    const std::vector<MoveCandidate> cands = move_candidates(*states[widest]);
    MoveScoreContext ctx(cache, *states[widest], unit);
    const std::vector<Ranked> ranked = rank(ctx, core::MoveType::Advance, cands);
    Option opt;
    opt.start = start;
    double mean = 0.0;
    double best_value = 0.0;
    bool have = false;
    for (std::size_t f = 1; f <= 6; ++f) {
        const std::optional<PlannedMove> pm =
            choose(ctx, *states[f], side, cands, ranked, decision_seed(*states[f]), false, kAdvanceEstimateMoves);
        if (!pm) return std::nullopt;
        mean += analysis::d6_pmf()[f] * pm->score.total;
        if (!have || pm->score.total > best_value) {
            best_value = pm->score.total;
            opt.score = pm->score;
            have = true;
        }
    }
    opt.value = mean;
    opt.text = "Advance (" + signed_value(mean) + " on average over the advance roll)";
    return opt;
}

// The expected mortal wounds of one hazard roll (06.03), from make_hazard_rolls with each face scripted.
double hazard_wounds_per_model(const core::Unit& unit) {
    double total = 0.0;
    for (int face = 1; face <= 6; ++face) {
        core::ScriptedDice dice{face};
        total +=
            analysis::d6_pmf()[static_cast<std::size_t>(face)] * core::make_hazard_rolls(dice, unit, 1).mortal_wounds;
    }
    return total;
}

}  // namespace

// ---- candidates ----

std::vector<MoveCandidate> move_candidates(const GameState& origin) {
    std::vector<MoveCandidate> out;
    const std::optional<MoveGeometry> geo = geometry_of(origin);
    if (!geo) return out;
    const MoveGeometry& g = *geo;
    // Staying where it is (a Normal or Advance move with no model moved: possible while the unit is unengaged).
    if (ends_ok(g, g.starts)) {
        MoveCandidate stay;
        stay.label = "stays where it is";
        stay.ends = g.starts;
        stay.lengths.assign(g.starts.size(), 0.0f);
        out.push_back(std::move(stay));
    }
    // Translations: fixed steps up to the maximum, and as far as it can go where a step first fails.
    std::vector<float> steps;
    for (const float s : kStepsIn) {
        if (!core::more_than(s * kIn, g.unit_max_mm)) steps.push_back(s * kIn);
    }
    if (g.unit_max_mm > 0.0f && (steps.empty() || g.unit_max_mm > steps.back() + 0.01f)) steps.push_back(g.unit_max_mm);
    // The directions most in line with the enemy (towards or away) first, so a tie goes to the straighter move.
    std::vector<client::UnitVector> dirs(client::kDirections16.begin(), client::kDirections16.end());
    if (g.has_enemy) {
        const Position c = centroid_of(g.starts);
        const float ax = g.enemy_centroid.x - c.x;
        const float ay = g.enemy_centroid.y - c.y;
        const float al = std::max(1e-3f, std::sqrt(ax * ax + ay * ay));
        // |cos| to the line, in thousandths (an integer key keeps the order strict and deterministic).
        const auto inline_with = [&](const client::UnitVector& d) {
            return static_cast<int>(std::lround(1000.0f * std::fabs(d.x * ax + d.y * ay) / al));
        };
        std::stable_sort(dirs.begin(), dirs.end(), [&](const client::UnitVector& a, const client::UnitVector& b) {
            return inline_with(a) > inline_with(b);
        });
    }
    for (const client::UnitVector& d : dirs) {
        float last_ok = 0.0f;
        bool failed = false;
        for (const float t : steps) {
            std::optional<MoveCandidate> c = translation(g, d, t);
            if (c) {
                out.push_back(std::move(*c));
                last_ok = t;
                continue;
            }
            if (!failed && last_ok > 0.0f) {
                // As far as it can go: bisect between the last step that stood and this one.
                float lo = last_ok;
                float hi = t;
                std::optional<MoveCandidate> edge;
                for (int k = 0; k < kBisections; ++k) {
                    const float mid = 0.5f * (lo + hi);
                    if (std::optional<MoveCandidate> m = translation(g, d, mid)) {
                        lo = mid;
                        edge = std::move(m);
                    } else {
                        hi = mid;
                    }
                }
                if (edge && lo > last_ok + 0.1f * kIn) out.push_back(std::move(*edge));
            }
            failed = true;
        }
    }
    // Formations facing the enemy.
    for (const int ranks : {1, 2}) {
        for (const float off : kFormationOffsetsIn) {
            if (std::optional<MoveCandidate> c = formation(g, ranks, off * kIn)) out.push_back(std::move(*c));
        }
    }
    return out;
}

std::optional<PlannedMove> plan_unit_move(analysis::OddsCache& cache, const GameState& origin) {
    if (!origin.flow.move || !origin.flow.move->paths.empty()) return std::nullopt;
    const core::Unit* u = origin.find_unit(origin.flow.move->unit);
    if (u == nullptr || u->models.empty()) return std::nullopt;
    const std::vector<MoveCandidate> cands = move_candidates(origin);
    MoveScoreContext ctx(cache, origin, u->id);
    const std::vector<Ranked> ranked = rank(ctx, origin.flow.move->type, cands);
    return choose(ctx, origin, u->side, cands, ranked, decision_seed(origin), true);
}

std::vector<PlannedMove> standing_moves(analysis::OddsCache& cache, const GameState& origin, const MoveWeights& weights,
                                        std::size_t count, std::size_t attempts) {
    std::vector<PlannedMove> out;
    if (!origin.flow.move || !origin.flow.move->paths.empty()) return out;
    const core::Unit* u = origin.find_unit(origin.flow.move->unit);
    if (u == nullptr || u->models.empty()) return out;
    const std::vector<MoveCandidate> cands = move_candidates(origin);
    MoveScoreContext ctx(cache, origin, u->id, weights);
    const std::vector<Ranked> ranked = rank(ctx, origin.flow.move->type, cands);
    const std::uint64_t seed = decision_seed(origin);
    std::size_t tried = 0;
    for (const Ranked& r : ranked) {
        if (out.size() >= count || tried >= attempts) break;
        const MoveCandidate& c = cands[r.index];
        if (!fits(origin, c)) continue;
        ++tried;
        std::optional<Simulated> sim = simulate(origin, u->side, c, seed);
        if (!sim) continue;
        out.push_back(PlannedMove{c, r.score, std::move(sim->steps), std::move(sim->after)});
    }
    return out;
}

std::optional<PolicyResult> decide_move_units(analysis::OddsCache& cache, const GameState& gs, core::Side side,
                                              const std::vector<GameAction>& legal, std::uint64_t seed,
                                              MoveTypeValues* values) {
    std::optional<core::UnitId> unit;
    for (const GameAction& a : legal) {
        if (const auto* m = std::get_if<core::action::StartMove>(&a)) {
            unit = m->unit;
            break;
        }
    }
    if (!unit) return std::nullopt;
    const core::Unit& u = *gs.find_unit(*unit);
    std::vector<Option> options;
    MoveScoreContext ctx(cache, gs, *unit);

    // Remain Stationary (09.04).
    const GameAction stay = core::action::StartMove{*unit, core::MoveType::RemainStationary, std::nullopt};
    if (offered(legal, stay)) {
        Simulator sim(gs, seed);
        if (sim.step(side, stay).ok()) {
            std::vector<Position> starts;
            for (const core::Model& m : u.models) starts.push_back(m.position);
            Option o;
            o.start = stay;
            o.score = refine_move_score(ctx, PlannedEnd{core::MoveType::RemainStationary, starts, 0.0f}, sim.state(),
                                        seed, true);
            o.value = o.score.total;
            o.text = "Remain Stationary";
            if (values != nullptr) values->remain_stationary = o.value;
            options.push_back(std::move(o));
        }
    }
    // A move with no dice (Normal, Ordered Retreat): the whole plan.
    const auto planned = [&](const GameAction& start, double cost) -> std::optional<Option> {
        Simulator sim(gs, seed);
        if (!sim.step(side, start).ok()) return std::nullopt;
        std::optional<PlannedMove> pm = plan_unit_move(cache, sim.state());
        if (!pm) return std::nullopt;
        const auto& m = std::get<core::action::StartMove>(start);
        if (pm->candidate.proposals.empty() && m.move_type == core::MoveType::Normal) return std::nullopt;  // = stay
        Option o;
        o.start = start;
        o.value = pm->score.total - cost;
        o.score = pm->score;
        o.text = move_type_text(m.move_type, m.fall_back_mode) + " " + pm->candidate.label;
        o.steps.push_back(PlannedStep{state_hash(gs), start, {}});
        for (PlannedStep& s : pm->steps) o.steps.push_back(std::move(s));
        return o;
    };
    const GameAction normal = core::action::StartMove{*unit, core::MoveType::Normal, std::nullopt};
    if (offered(legal, normal)) {
        if (std::optional<Option> o = planned(normal, 0.0)) {
            if (values != nullptr) values->normal = o->value;
            options.push_back(std::move(*o));
        }
    }
    const GameAction advance = core::action::StartMove{*unit, core::MoveType::Advance, std::nullopt};
    if (offered(legal, advance)) {
        if (std::optional<Option> o = advance_option(cache, gs, side, *unit)) {
            if (values != nullptr) values->advance = o->value;
            options.push_back(std::move(*o));
        }
    }
    const GameAction retreat =
        core::action::StartMove{*unit, core::MoveType::FallBack, core::FallBackMode::OrderedRetreat};
    const GameAction escape =
        core::action::StartMove{*unit, core::MoveType::FallBack, core::FallBackMode::DesperateEscape};
    if (offered(legal, retreat)) {
        if (std::optional<Option> o = planned(retreat, 0.0)) {
            if (values != nullptr) values->fall_back = o->value;
            options.push_back(std::move(*o));
        }
    } else if (offered(legal, escape)) {
        // Decided before the hazard rolls (D-054): planned with no failed roll, less the expected losses.
        Simulator sim(gs, seed);
        core::ScriptedDice sixes(std::vector<int>(u.models.size(), 6));
        if (sim.step(side, escape, sixes).ok()) {
            if (std::optional<PlannedMove> pm = plan_unit_move(cache, sim.state())) {
                const int w = std::max(1, u.models.front().characteristics.wounds);
                const double lost = static_cast<double>(u.models.size()) * hazard_wounds_per_model(u) / w;
                Option o;
                o.start = escape;
                o.score = pm->score;
                o.value = pm->score.total - unit_share(u, lost);  // lost now, before the move
                o.text = "Fall Back (Desperate Escape) " + pm->candidate.label + ", expecting " +
                         signed_value(-unit_share(u, lost)) + " from hazard rolls";
                if (values != nullptr) values->fall_back = o.value;
                options.push_back(std::move(o));
            }
        }
    }
    if (options.empty()) return std::nullopt;
    std::size_t best = 0;
    for (std::size_t i = 1; i < options.size(); ++i) {
        if (options[i].value > options[best].value + kBetter) best = i;
    }
    Option& chosen = options[best];
    std::string others;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i == best) continue;
        const auto& m = std::get<core::action::StartMove>(options[i].start);
        others += (others.empty() ? "" : ", ") + move_type_text(m.move_type, m.fall_back_mode) + " " +
                  signed_value(options[i].value);
    }
    const std::string reason = client::unit_display_name(gs, u.id) + ": " + chosen.text + " - " + position_text(gs, *unit, chosen.score) + " (" +
                               signed_value(chosen.value) + (others.empty() ? "" : "; " + others) + ")";
    if (!chosen.steps.empty()) return from_steps(std::move(chosen.steps), 0, reason);
    return single(chosen.start, reason);
}

std::optional<PolicyResult> continue_unit_move(analysis::OddsCache& cache, const GameState& gs, core::Side side) {
    if (!gs.flow.move) return std::nullopt;
    const core::MoveInProgress& mv = *gs.flow.move;
    if (mv.type != core::MoveType::Normal && mv.type != core::MoveType::Advance &&
        mv.type != core::MoveType::FallBack) {
        return std::nullopt;
    }
    const std::string who = name_of(gs, mv.unit);
    const GameState origin = move_origin(gs);
    if (std::optional<PlannedMove> pm = plan_unit_move(cache, origin)) {
        std::string text = move_type_text(mv.type, mv.mode);
        if (mv.type == core::MoveType::Advance) {
            const core::Unit* u = gs.find_unit(mv.unit);
            if (u != nullptr && u->phase_flags.advance_roll)
                text += " (roll " + std::to_string(*u->phase_flags.advance_roll) + ")";
        }
        const std::string reason = who + ": " + text + " " + pm->candidate.label + " - " +
                                   position_text(origin, mv.unit, pm->score) + " (" + signed_value(pm->score.total) +
                                   ")";
        const std::uint64_t h = state_hash(gs);
        for (std::size_t i = 0; i < pm->steps.size(); ++i) {
            if (pm->steps[i].expect_hash == h) return from_steps(std::move(pm->steps), i, reason);
        }
    }
    // The move so far isn't this plan's (a state the bot didn't make): commit it if it stands, otherwise reset.
    if (core::move_end_problems(gs).empty()) {
        return single(core::action::CommitUnitMove{}, who + ": the move so far stands; commit it");
    }
    (void)side;
    return single(core::action::ResetUnitMove{}, who + ": the move so far would not stand; reset it (D-018)");
}

}  // namespace wh40k::ai::normal
