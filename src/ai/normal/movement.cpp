#include "unit_style.hpp"
#include "normal/movement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <limits>
#include <utility>
#include <variant>

#include "analysis/charge_odds.hpp"
#include "bounds.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/path.hpp"
#include "core/geometry/terrain_shape.hpp"
#include "core/geometry/units.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/mortal_wounds.hpp"
#include "core/rules/terrain.hpp"
#include "core/state/objectives.hpp"
#include "core/state/queries.hpp"
#include "directions.hpp"
#include "normal/defense.hpp"
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

// D-083 part 2 (M80): " with Legionaries 5", " with Havocs 4 and Legionaries 5", " with A, B and C"; " alone" for a
// unit that closes in on its closest enemy unit without a group (part 5, M83).
std::string with_text(const GameState& gs, const MoveTeam& team) {
    if (team.area) return {};          // M87 (D-086): the area's text names it
    if (team.alone) return " alone";  // D-083 part 5 (M83): no group, closing in on the closest enemy unit
    std::string out;
    for (std::size_t i = 0; i < team.partners.size(); ++i) {
        out += i == 0 ? " with " : i + 1 == team.partners.size() ? " and " : ", ";
        out += client::unit_display_name(gs, team.partners[i].unit);
    }
    return out;
}

// "Screamer-killer 3 with Legionaries 5" (nothing without a team).
std::string team_text(const GameState& gs, const MoveTeam* team) {
    if (team == nullptr) return {};
    if (team->area) return team->area_text;  // M87 (D-086): "Centre (take)"
    return client::unit_display_name(gs, team->focus) + with_text(gs, *team);
}

// ---- the geometry of the move in progress, for the cheap checks (the engine still decides) ----

// D-074 part 3 (M66): an oval keeps its facing (`facings`, from the move's start) through the whole move; the checks
// use bounding circles and extents (bounds.hpp).
struct MoveGeometry {
    core::UnitId unit;
    core::MoveType type = core::MoveType::Normal;
    bool through_enemies = false;  // 09.07 Desperate Escape
    std::vector<core::ModelId> ids;
    std::vector<core::Base> bases;
    std::vector<float> facings;
    std::vector<Position> starts;
    std::vector<float> max_mm;
    float unit_max_mm = 0.0f;  // the slowest model's maximum
    std::vector<core::PosedModel> enemies;
    // D-077 (M68), 17.01: the moving unit's models that may pass through enemy models that aren't MONSTER/VEHICLE
    // models (a Normal or Advance move of a MONSTER/VEHICLE, as the engine's moves_through_non_monster_enemies), and
    // which enemy models (in `enemies`' order) are MONSTER/VEHICLE models, which still block them.
    std::vector<bool> passes_infantry;
    std::vector<bool> enemy_mv;
    bool any_passes = false;
    std::vector<core::PosedModel> others;  // friendly models of other units
    // The circumscribed radii (avoid_radius_mm) of `bases`, `enemies` and `others`, worked out once (the same floats).
    std::vector<float> radius;
    std::vector<float> enemy_radius;
    std::vector<float> other_radius;
    std::vector<core::BaseExtents> extents;  // of `bases` at `facings` (round: the radius both ways)
    bool all_round = true;  // every model here is round: coherency by the old round-only check
    float width = 0.0f;
    float depth = 0.0f;
    bool has_enemy = false;
    Position enemy_centroid;
    // D-083 part 2 (M80): a unit with partners lines up on its focus (enemy_centroid is the focus's), and its labels
    // name the focus and the partners.
    bool teamed = false;
    // M87 (D-086): the closest enemy unit's centroid (enemy_centroid is the area's centre for a unit heading for one).
    Position closest_centroid;
    bool has_closest = false;
    std::string focus_name;
    std::string with_text;  // " with Legionaries 5 and Havocs 4"
    // M88 (D-087): the Light and Dense features no model ends a move on, and the Dense features over 2" this unit's
    // models can't move through (13.06). Empty without terrain, so every check below is as before.
    std::vector<core::TerrainShape> no_end;
    std::vector<core::TerrainShape> uncrossable;
};

std::optional<MoveGeometry> geometry_of(const GameState& origin, const MoveTeam* team = nullptr) {
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
        g.passes_infantry.push_back(core::moves_through_non_monster_enemies(mv.type, *u, m));
        g.any_passes = g.any_passes || g.passes_infantry.back();
        g.bases.push_back(m.base);
        g.facings.push_back(core::move_start_facing_deg(origin, m.id).value_or(m.facing_deg));
        g.starts.push_back(*start);
        g.max_mm.push_back(*max);
        g.unit_max_mm = g.unit_max_mm < 0.0f ? *max : std::min(g.unit_max_mm, *max);
    }
    g.enemies = core::posed_enemy_models(origin, u->side);
    if (g.any_passes) {  // in posed_enemy_models' order: units in order, then their models
        for (const core::Unit& e : origin.units) {
            if (e.side == u->side) continue;
            for (const core::Model& m : e.models) g.enemy_mv.push_back(core::is_monster_or_vehicle(e, m));
        }
    }
    for (const core::Unit& other : origin.units) {
        if (other.side != u->side || other.id == u->id) continue;
        for (const core::Model& m : other.models) g.others.push_back(core::posed_of(m));
    }
    const core::Unit* focus = team != nullptr ? origin.find_unit(team->focus) : nullptr;
    if (focus != nullptr && focus->models.empty()) focus = nullptr;
    if (const core::Unit* e = focus != nullptr ? focus : closest_enemy(origin, *u)) {
        std::vector<Position> ps;
        for (const core::Model& m : e->models) ps.push_back(m.position);
        g.enemy_centroid = centroid_of(ps);
        g.has_enemy = true;
    }
    g.closest_centroid = g.enemy_centroid;
    g.has_closest = g.has_enemy;
    if (team != nullptr && team->area) {
        // M87 (D-086): a unit heading for an area lines its directions and formations up on the area's centre.
        g.enemy_centroid = team->area_centre;
        g.has_enemy = true;
        g.teamed = true;
        g.focus_name = team->area_text;
        g.with_text.clear();
    } else if (focus != nullptr) {
        g.teamed = true;
        g.focus_name = client::unit_display_name(origin, focus->id);
        g.with_text = with_text(origin, *team);
    }
    for (const core::Base& b : g.bases) g.radius.push_back(avoid_radius_mm(b));
    for (std::size_t i = 0; i < g.bases.size(); ++i) g.extents.push_back(edge_extents(g.bases[i], g.facings[i]));
    for (const core::PosedModel& e : g.enemies) g.enemy_radius.push_back(avoid_radius_mm(e.base));
    for (const core::PosedModel& o : g.others) g.other_radius.push_back(avoid_radius_mm(o.base));
    g.all_round = std::all_of(g.bases.begin(), g.bases.end(),
                              [](const core::Base& b) { return b.shape == core::BaseShape::Round; });
    for (const core::TerrainArea& a : origin.terrain) {  // M88
        for (const core::TerrainFeature& f : a.features) {
            if (core::is_solid(f.category)) g.no_end.push_back(f.shape);
            if (core::blocks_move(f, *u)) g.uncrossable.push_back(f.shape);
        }
    }
    return g;
}

// M88 (D-087): model i's base at `p` (an oval keeps its facing through the move): the engine's own test, exact.
core::BasePose avoid_pose(const MoveGeometry& g, std::size_t i, const Position& p) {
    return core::BasePose{p, g.bases[i], g.facings[i]};
}

// M88 (D-087), 03.01 Ending a Move: model i's base at `p` is on no Light or Dense feature.
bool off_terrain(const MoveGeometry& g, std::size_t i, const Position& p) {
    for (const core::TerrainShape& f : g.no_end) {
        if (core::overlaps_shape(avoid_pose(g, i, p), f)) return false;
    }
    return true;
}

// D-074 part 3: by the base's half extents at its facing (round: its radius both ways, as before).
bool on_battlefield(const MoveGeometry& g, std::size_t i, const Position& p) {
    const float rx = g.extents[i].half_x_mm + kEdgeMarginMm;
    const float ry = g.extents[i].half_y_mm + kEdgeMarginMm;
    return p.x >= rx && p.x <= g.width - rx && p.y >= ry && p.y <= g.depth - ry;
}

// 03.01: the straight line from `a` to `b` doesn't pass through an enemy model (unless the move may). D-074 part 3:
// by the circumscribed circles (avoid). D-077 (M68), 17.01: a MONSTER/VEHICLE model's Normal or Advance move may pass
// through enemy models that aren't MONSTER/VEHICLE models (ends_ok still keeps it off them and out of Engagement
// Range).
bool segment_clear(const MoveGeometry& g, std::size_t i, const Position& a, const Position& b) {
    // M88 (D-087), 13.06: no straight line through a Dense feature the model can't move through (the engine's test;
    // any move type, a Desperate Escape included).
    for (const core::TerrainShape& f : g.uncrossable) {
        if (core::translation_crosses_shape(avoid_pose(g, i, a), b, f)) return false;
    }
    if (g.through_enemies) return true;
    const float r = g.radius[i];
    const bool passes = g.any_passes && g.passes_infantry[i];
    for (std::size_t k = 0; k < g.enemies.size(); ++k) {
        if (passes && !g.enemy_mv[k]) continue;  // 17.01
        if (client::point_segment_distance(g.enemies[k].position, a, b) < r + g.enemy_radius[k]) return false;
    }
    return true;
}

// The end positions: on the battlefield, not on another model, out of engagement range (09.05-09.07 After Moving)
// and in coherency (03.03). D-074 part 3: Engagement Range and other models by the circumscribed circles (avoid),
// coherency by core's pose-aware check (each model at its facing).
bool ends_ok(const MoveGeometry& g, const std::vector<Position>& ends) {
    for (std::size_t i = 0; i < ends.size(); ++i) {
        if (!on_battlefield(g, i, ends[i])) return false;
        if (!off_terrain(g, i, ends[i])) return false;  // M88
        const float r = g.radius[i];
        for (std::size_t k = 0; k < g.enemies.size(); ++k) {
            // avoid_gap_mm (base_distance's formula with the circumscribed radii)
            const float d = std::max(0.0f, core::centre_distance(ends[i], g.enemies[k].position) - r - g.enemy_radius[k]);
            if (!(d > core::kEngagementRangeMm + core::EPS_MM + kEngagedMarginMm)) return false;
        }
        for (std::size_t k = 0; k < g.others.size(); ++k) {
            // avoid_overlap (bases_overlap's formula with the circumscribed radii)
            if (core::centre_distance(ends[i], g.others[k].position) < r + g.other_radius[k] - core::EPS_MM) return false;
        }
        for (std::size_t j = i + 1; j < ends.size(); ++j) {
            if (core::centre_distance(ends[i], ends[j]) < r + g.radius[j] - core::EPS_MM) return false;
        }
    }
    if (g.all_round) {  // the round-only check, as before
        std::vector<core::PlacedModel> placed;
        for (std::size_t i = 0; i < ends.size(); ++i) placed.push_back(core::PlacedModel{g.ids[i], ends[i], g.bases[i]});
        return core::check_coherency(placed).ok();
    }
    std::vector<core::PosedModel> placed;
    for (std::size_t i = 0; i < ends.size(); ++i) {
        placed.push_back(core::PosedModel{g.ids[i], ends[i], g.bases[i], g.facings[i]});
    }
    return core::check_coherency(std::span<const core::PosedModel>(placed)).ok();
}

// D-083 part 2 (M80): a unit with partners names its focus and partners: "6\" towards Screamer-killer 3 with
// Legionaries 5", "3\" back from ...", "stays where it is, on ... with ...".
std::string team_label(const MoveGeometry& g, const std::string& label) {
    if (!g.teamed) return label;
    const auto ends_with = [&](const char* w) {
        const std::string t = w;
        return label.size() >= t.size() && label.compare(label.size() - t.size(), t.size(), t) == 0;
    };
    const char* link = ends_with("towards")    ? " "
                       : ends_with("back")     ? " from "
                       : ends_with("sideways") ? " to "
                                               : ", on ";
    return label + link + g.focus_name + g.with_text;
}

// "3\" back", "2\" obliquely towards": the centroid's move relative to the line to the enemy.
std::string move_label(const MoveGeometry& g, float dx, float dy) {
    const float len = std::sqrt(dx * dx + dy * dy);
    if (!(len > 0.01f)) return team_label(g, "stays where it is");
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
    return team_label(g, inches(len) + (dir.empty() ? "" : " " + dir));
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
            const float touching = g.radius[i] + g.radius[j] - core::EPS_MM;  // avoid_overlap
            if (core::centre_distance(p, now[j]) < touching) return false;
            if (avoid_slots && !done[j] && core::centre_distance(p, ends[j]) < touching) return false;
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
            // D-075: clear of its own start at every facing (the circumscribed circle: room for a 120 mm oval).
            const float step = g.radius[i] * 2.0f + kParkMm;
            for (const client::UnitVector& d : client::kDirections16) {
                const Position p{g.starts[i].x + d.x * step, g.starts[i].y + d.y * step, g.starts[i].z};
                if (!on_battlefield(g, i, p) || core::more_than(step, g.max_mm[i]) || !off_terrain(g, i, p) ||
                    !segment_clear(g, i, g.starts[i], p) || !free_at(i, p, true)) {
                    continue;
                }
                bool clear_of_others = true;
                for (std::size_t k = 0; k < g.others.size(); ++k) {
                    clear_of_others = clear_of_others && !avoid_overlap(p, g.bases[i], g.others[k].position, g.others[k].base);
                }
                for (std::size_t k = 0; k < g.enemies.size(); ++k) {
                    clear_of_others = clear_of_others && !avoid_overlap(p, g.bases[i], g.enemies[k].position, g.enemies[k].base);
                }
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
// to it (positive towards it), shifted inside the battlefield. D-083 part 2 (M80): `at`, centred there instead (the
// spots beside a partner), facing the enemy from there.
std::optional<MoveCandidate> formation(const MoveGeometry& g, int ranks, float offset_mm,
                                       const std::optional<Position>& at = std::nullopt) {
    const std::size_t n = g.starts.size();
    if (!g.has_enemy || n < 2 || (ranks == 2 && n < 4)) return std::nullopt;
    const Position c0 = centroid_of(g.starts);
    const Position from = at ? *at : c0;
    float ax = g.enemy_centroid.x - from.x;
    float ay = g.enemy_centroid.y - from.y;
    const float al = std::sqrt(ax * ax + ay * ay);
    if (!(al > 0.0f)) return std::nullopt;
    ax /= al;
    ay /= al;
    const float px = -ay;  // along the line, left to right facing the enemy
    const float py = ax;
    float spacing = 0.0f;
    float radius = 0.0f;
    for (const core::Base& b : g.bases) {
        // D-074 part 3: slots by the circumscribed circles (round: the diameter and the radius, as before).
        spacing = std::max(spacing, 2.0f * avoid_radius_mm(b) + kSlotGapMm);
        radius = std::max(radius, avoid_radius_mm(b));
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
    float cx = at ? at->x : c0.x + ax * offset_mm;
    float cy = at ? at->y : c0.y + ay * offset_mm;
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

// "13.2\" from Termagants, outside its half range, its charge odds next turn 8%". D-083 part 2 (M80): from the focus
// of a unit with partners (the line's enemy), else the closest enemy unit.
std::string position_text(const GameState& gs, core::UnitId unit, const MoveScore& s) {
    const core::Unit* u = gs.find_unit(unit);
    const core::Unit* e = s.focus ? gs.find_unit(*s.focus) : u != nullptr ? closest_enemy(gs, *u) : nullptr;
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
    if (s.objective != 0.0) out += ", objectives " + signed_value(s.objective);  // M87 (D-086)
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
                                     core::UnitId unit, const ArmyPlan* plan,
                                     const MoveWeights& weights) {
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
    MoveScoreContext ctx(cache, *states[widest], unit, weights, plan);
    const std::vector<MoveCandidate> cands =
        move_candidates(*states[widest], ctx.team(), ctx.area_spots(), ctx.cover_spots());
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
    const std::string team = team_text(gs, ctx.team());
    opt.text = "Advance" + (team.empty() ? "" : " on " + team) + " (" + signed_value(mean) +
               " on average over the advance roll)";
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

// D-077 (M68): a Desperate Escape is planned with no failed hazard roll (D-054), so before one is made, every likely
// outcome (06.03: one to three failed rolls, the casualties chosen as Normal chooses them, 06.02) must still leave a
// fall-back move that stands. Otherwise the losses can leave no coherent arrangement out of Engagement Range (09.07,
// 03.03) and the move would have to be reset (a UnitMoveReverted): Hard's escape_stands (D-067), for Normal. Failing and
// passing faces come from make_hazard_rolls itself.
bool escape_stands(analysis::OddsCache& cache, const GameState& gs, core::Side side, const GameAction& escape,
                   const core::Unit& unit, const MoveWeights& weights) {
    std::optional<int> fail;
    std::optional<int> pass;
    for (int face = 1; face <= 6; ++face) {
        core::ScriptedDice dice{face};
        if (core::make_hazard_rolls(dice, unit, 1).mortal_wounds > 0) {
            if (!fail) fail = face;
        } else if (!pass) {
            pass = face;
        }
    }
    if (!pass) return false;
    if (!fail) return true;  // a hazard roll never fails
    const int n = static_cast<int>(unit.models.size());
    for (int k = 1; k <= std::min(3, n - 1); ++k) {
        std::vector<int> faces(static_cast<std::size_t>(n), *pass);
        for (int i = 0; i < k; ++i) faces[static_cast<std::size_t>(i)] = *fail;
        core::ScriptedDice dice(faces);
        Simulator sim(gs, 0);
        if (!sim.step(side, escape, dice).ok()) return false;
        for (int guard = 0; guard < 8 && sim.state().pending &&
                            std::holds_alternative<core::decision::OrderCasualties>(sim.state().pending->kind);
             ++guard) {
            const auto& d = std::get<core::decision::OrderCasualties>(sim.state().pending->kind);
            const std::optional<CasualtyOrder> o = casualty_order(cache, sim.state());
            if (!o || !sim.step(side, core::action::OrderCasualties{d.target, o->priority}).ok()) return false;
        }
        const GameState& after = sim.state();
        if (!after.pending || !std::holds_alternative<core::decision::MoveModels>(after.pending->kind)) continue;
        // M87 (D-086): the default weights, but the objective ones as the caller plays them (with both 0, as before M87).
        MoveWeights guard{};
        guard.objective = weights.objective;
        guard.objective_approach = weights.objective_approach;
        if (standing_moves(cache, after, guard, 1, kMaxSimulatedMoves).empty()) return false;
    }
    return true;
}

// 09.07 Desperate Escape: the model equivalents expected to be lost to its hazard rolls, one per model. A unit whose
// models share one W: the M43 expression (every mortal wound costs 1 / W of a model). D-077 (M68): otherwise each
// model's own W, through core's 06.02 allocation (analysis::hazard_model_equivalents).
double escape_losses(const core::Unit& u) {
    const int w = std::max(1, u.models.front().characteristics.wounds);
    const bool one_w = std::all_of(u.models.begin(), u.models.end(), [&](const core::Model& m) {
        return std::max(1, m.characteristics.wounds) == w;
    });
    if (!one_w) return analysis::hazard_model_equivalents(u, static_cast<int>(u.models.size()));
    return static_cast<double>(u.models.size()) * hazard_wounds_per_model(u) / w;
}

// D-083 part 2 (M80): a few spots beside the partner nearest to the unit (edge to edge), level with it on either side
// of its line to the focus and 3" further back: the unit translated straight there (as far as its move goes), and a
// line abreast centred there facing the focus.
void beside_partner(const MoveGeometry& g, const MoveTeam& team, std::vector<MoveCandidate>& out) {
    const MoveTeam::Partner* nearest = nullptr;
    float best = std::numeric_limits<float>::max();
    for (const MoveTeam::Partner& p : team.partners) {
        float gap = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i < g.starts.size(); ++i) {
            const core::BasePose at{g.starts[i], g.bases[i], g.facings[i]};
            for (const core::PosedModel& m : p.models) gap = std::min(gap, core::base_distance(at, m.pose()));
        }
        if (gap < best) {
            best = gap;
            nearest = &p;
        }
    }
    if (nearest == nullptr || nearest->models.empty()) return;
    std::vector<Position> ps;
    for (const core::PosedModel& m : nearest->models) ps.push_back(m.position);
    const Position pc = centroid_of(ps);
    float ax = g.enemy_centroid.x - pc.x;
    float ay = g.enemy_centroid.y - pc.y;
    const float al = std::sqrt(ax * ax + ay * ay);
    if (!(al > 0.0f)) return;
    ax /= al;
    ay /= al;
    const float px = -ay;
    const float py = ax;
    // Half widths across the line: the partner's spread, and ours in a line abreast.
    float partner_half = 0.0f;
    for (const core::PosedModel& m : nearest->models) {
        partner_half = std::max(partner_half, std::fabs((m.position.x - pc.x) * px + (m.position.y - pc.y) * py) +
                                                  avoid_radius_mm(m.base));
    }
    float spacing = 0.0f;
    for (const core::Base& b : g.bases) spacing = std::max(spacing, 2.0f * avoid_radius_mm(b) + kSlotGapMm);
    const float our_half = 0.5f * spacing * static_cast<float>(g.starts.size());
    const float side = partner_half + our_half + 2.0f * kIn;
    const Position c0 = centroid_of(g.starts);
    for (const float back : {0.0f, 3.0f * kIn}) {
        for (const float sgn : {-1.0f, 1.0f}) {
            const Position spot{pc.x + px * side * sgn - ax * back, pc.y + py * side * sgn - ay * back, 0.0f};
            float dx = spot.x - c0.x;
            float dy = spot.y - c0.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > 0.1f * kIn) {
                const float t = std::min(len, g.unit_max_mm);
                const client::UnitVector d{dx / len, dy / len};
                if (std::optional<MoveCandidate> c = translation(g, d, t)) out.push_back(std::move(*c));
            }
            if (std::optional<MoveCandidate> c = formation(g, 1, 0.0f, spot)) out.push_back(std::move(*c));
        }
    }
}

// M87 (D-086): spots on an objective area: the unit translated straight at its centre (as far as the move goes), and a
// line abreast at that point facing the closest enemy unit (two ranks too when the centre is within the move).
void area_spots_for(const MoveGeometry& g, const core::ObjectiveArea& area, std::vector<MoveCandidate>& out) {
    const Position c0 = centroid_of(g.starts);
    const float dx = area.centre.x - c0.x;
    const float dy = area.centre.y - c0.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (!(len > 0.1f * kIn) || !(g.unit_max_mm > 0.0f)) return;
    const float t = std::min(len, g.unit_max_mm);
    const client::UnitVector d{dx / len, dy / len};
    if (std::optional<MoveCandidate> c = translation(g, d, t)) out.push_back(std::move(*c));
    if (!g.has_closest) return;
    MoveGeometry facing = g;  // formations face the closest enemy unit, standing on the area
    facing.enemy_centroid = g.closest_centroid;
    const Position spot{c0.x + d.x * t, c0.y + d.y * t, 0.0f};
    if (std::optional<MoveCandidate> c = formation(facing, 1, 0.0f, spot)) out.push_back(std::move(*c));
    if (!core::more_than(len, g.unit_max_mm)) {
        if (std::optional<MoveCandidate> c = formation(facing, 2, 0.0f, spot)) out.push_back(std::move(*c));
    }
}

// ---- M89 (D-090): spots in cover, out of sight, and a detour ----

// How far from `p` along `d` (a unit vector) the shape's boundary is (0 when p is outside), in 0.25" steps up to 40".
float exit_distance(const core::TerrainShape& shape, const Position& p, const client::UnitVector& d) {
    constexpr float kStep = 0.25f * kIn;
    float t = 0.0f;
    while (t < 40.0f * kIn) {
        const core::oval_detail::Vec q{p.x + d.x * (t + kStep), p.y + d.y * (t + kStep)};
        if (core::point_shape_signed_distance(q, shape) > 0.0) break;
        t += kStep;
    }
    return t;
}

// Every end of `c` is within the area (13.08: cover from standing in it needs every model there).
bool all_within(const MoveGeometry& g, const std::vector<Position>& ends, const core::TerrainArea& area) {
    for (std::size_t i = 0; i < ends.size(); ++i) {
        if (!core::model_within_area(avoid_pose(g, i, ends[i]), area)) return false;
    }
    return true;
}

// The unit translated straight at `spot` (as far as the move goes), and a line abreast there facing the closest enemy
// unit, each labelled `what`.
void spots_at(const MoveGeometry& g, const Position& spot, const std::string& what, std::vector<MoveCandidate>& out) {
    const Position c0 = centroid_of(g.starts);
    const float dx = spot.x - c0.x;
    const float dy = spot.y - c0.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (!(len > 0.1f * kIn) || !(g.unit_max_mm > 0.0f)) return;
    const float t = std::min(len, g.unit_max_mm);
    const client::UnitVector d{dx / len, dy / len};
    if (std::optional<MoveCandidate> c = translation(g, d, t)) {
        c->label = what + ", " + c->label;
        out.push_back(std::move(*c));
    }
    if (!g.has_closest) return;
    MoveGeometry facing = g;
    facing.enemy_centroid = g.closest_centroid;
    const Position at{c0.x + d.x * t, c0.y + d.y * t, 0.0f};
    if (std::optional<MoveCandidate> c = formation(facing, 1, 0.0f, at)) {
        c->label = what + ", " + c->label;
        out.push_back(std::move(*c));
    }
}

// M89 (D-090): for every terrain area the move can reach (some base within its Move of it):
// - a unit that takes cover (13.08: INFANTRY, BEASTS, SWARM) gets spots at the area's inner point ("into cover in
//   ..."), unless D-086 already gave the area spots (`skip_objectives` and an objective's area), and, in an area with a
//   Dense feature (13.09), a line abreast as far from the closest enemy unit as still leaves every model within the
//   area ("hides in ...");
// - any unit gets spots behind an obscuring area (13.10), on its far side from the closest enemy unit ("behind ...").
void cover_spots_for(const GameState& origin, const MoveGeometry& g, const core::Unit& u, bool skip_objectives,
                     std::vector<MoveCandidate>& out) {
    const bool takes_cover = core::takes_cover_and_hides(u);
    float radius = 0.0f;
    for (const float r : g.radius) radius = std::max(radius, r);
    for (const core::TerrainArea& area : origin.terrain) {
        bool reach = false;
        for (std::size_t k = 0; k < g.starts.size() && !reach; ++k) {
            reach = core::within(core::shape_signed_distance(avoid_pose(g, k, g.starts[k]), area.shape), g.unit_max_mm);
        }
        if (!reach) continue;
        const Position inner = core::shape_inner_point(area.shape);
        client::UnitVector away{0.0f, 0.0f};
        if (g.has_closest) {
            const float ax = inner.x - g.closest_centroid.x;
            const float ay = inner.y - g.closest_centroid.y;
            const float al = std::sqrt(ax * ax + ay * ay);
            if (al > 1e-3f) away = client::UnitVector{ax / al, ay / al};
        }
        const bool moved_away = away.x != 0.0f || away.y != 0.0f;
        if (takes_cover && !(skip_objectives && area.objective)) spots_at(g, inner, "into cover in " + area.name, out);
        if (takes_cover && core::has_dense(area) && moved_away && g.has_closest && g.starts.size() >= 2) {
            MoveGeometry facing = g;
            facing.enemy_centroid = g.closest_centroid;
            for (const float off : {4.0f, 3.0f, 2.0f, 1.0f}) {
                const Position at{inner.x + away.x * off * kIn, inner.y + away.y * off * kIn, 0.0f};
                std::optional<MoveCandidate> c = formation(facing, 1, 0.0f, at);
                if (c && all_within(g, c->ends, area)) {
                    c->label = "hides in " + area.name + ", " + c->label;
                    out.push_back(std::move(*c));
                    break;
                }
            }
        }
        if (core::is_obscuring(area) && moved_away) {
            // The first spot past the area's far side, in 1" steps, where the unit stands (translated, or in line
            // abreast facing the closest enemy unit).
            const float exit = exit_distance(area.shape, inner, away);
            const std::size_t before = out.size();
            for (float extra = 0.5f; extra <= 4.5f && out.size() == before; extra += 1.0f) {
                const float behind = exit + radius + extra * kIn;
                const Position at{inner.x + away.x * behind, inner.y + away.y * behind, 0.0f};
                spots_at(g, at, "behind " + area.name, out);
            }
        }
    }
}

// A trig-free stand-in for the angle of (x, y) (D-054: no trigonometry in the bot), in (-2, 2] instead of (-pi, pi]: the
// "diamond angle", monotonic in the angle, 0 along +x and positive to its left.
float diamond_angle(float y, float x) {
    const float ax = std::fabs(x);
    const float ay = std::fabs(y);
    if (!(ax + ay > 0.0f)) return 0.0f;
    const float a = x >= 0.0f ? y / (ax + ay) : (y >= 0.0f ? 2.0f - y / (ax + ay) : -2.0f - y / (ax + ay));
    return a;
}

// M89 (D-090): models that can't cross a Dense feature (13.06) go round it: when the straight move towards the line's
// enemy (or area) is blocked by such a polygon, a two-leg path via each of its two silhouette corners (pushed out by
// the largest base's avoid radius + 0.3"), then on towards the enemy for the rest of the move (as far as it goes).
// Every model takes the same offset.
void detours_for(const MoveGeometry& g, std::vector<MoveCandidate>& out) {
    if (g.uncrossable.empty() || !g.has_enemy || !(g.unit_max_mm > 0.0f)) return;
    const Position c0 = centroid_of(g.starts);
    const float tx = g.enemy_centroid.x - c0.x;
    const float ty = g.enemy_centroid.y - c0.y;
    const float tl = std::sqrt(tx * tx + ty * ty);
    if (!(tl > 1.0f * kIn)) return;
    const client::UnitVector d{tx / tl, ty / tl};
    const float full = std::min(tl, g.unit_max_mm);
    // The blocking polygons: those the straight move would cross for some model.
    std::vector<const core::TerrainShape*> blocking;
    for (const core::TerrainShape& f : g.uncrossable) {
        if (f.is_circle()) continue;  // D-090: circles are left out
        for (std::size_t i = 0; i < g.starts.size(); ++i) {
            const Position e{g.starts[i].x + d.x * full, g.starts[i].y + d.y * full, g.starts[i].z};
            if (core::translation_crosses_shape(avoid_pose(g, i, g.starts[i]), e, f)) {
                blocking.push_back(&f);
                break;
            }
        }
    }
    float radius = 0.0f;
    for (const float r : g.radius) radius = std::max(radius, r);
    for (const core::TerrainShape* f : blocking) {
        // The silhouette corners seen from the centroid: the vertices at the extreme angles left and right of d.
        std::optional<std::size_t> left;
        std::optional<std::size_t> right;
        float best_l = 0.0f;
        float best_r = 0.0f;
        const std::size_t nv = f->vertices.size();
        for (std::size_t v = 0; v < nv; ++v) {
            const float vx = f->vertices[v].x - c0.x;
            const float vy = f->vertices[v].y - c0.y;
            const float angle = diamond_angle(-d.y * vx + d.x * vy, vx * d.x + vy * d.y);  // left positive
            if (!left || angle > best_l) {
                best_l = angle;
                left = v;
            }
            if (!right || angle < best_r) {
                best_r = angle;
                right = v;
            }
        }
        for (const auto& [corner, name] : {std::pair{left, "left"}, std::pair{right, "right"}}) {
            if (!corner) continue;
            // Off the corner by `push` from both edges at it: the sum of their outward normals (counter-clockwise
            // polygon: the outward normal of edge a -> b is (dy, -dx)).
            const Position& vtx = f->vertices[*corner];
            const Position& prev = f->vertices[(*corner + nv - 1) % nv];
            const Position& next = f->vertices[(*corner + 1) % nv];
            const auto normal = [](const Position& a, const Position& b) {
                const float ex = b.x - a.x;
                const float ey = b.y - a.y;
                const float el = std::max(1e-6f, std::sqrt(ex * ex + ey * ey));
                return client::UnitVector{ey / el, -ex / el};
            };
            const client::UnitVector n1 = normal(prev, vtx);
            const client::UnitVector n2 = normal(vtx, next);
            const float push = radius + 0.3f * kIn;
            const Position via{vtx.x + (n1.x + n2.x) * push, vtx.y + (n1.y + n2.y) * push, 0.0f};
            const float l1x = via.x - c0.x;
            const float l1y = via.y - c0.y;
            const float leg1 = std::sqrt(l1x * l1x + l1y * l1y);
            if (!(leg1 > 0.1f * kIn) || core::more_than(leg1, g.unit_max_mm)) continue;
            // Leg 2: from the corner towards the enemy, and on in the straight move's direction (past the wall), each
            // as far as the move and the ground allow.
            const float rx = g.enemy_centroid.x - via.x;
            const float ry = g.enemy_centroid.y - via.y;
            const float rl = std::sqrt(rx * rx + ry * ry);
            const float rest = std::min(g.unit_max_mm - leg1, rl);
            const client::UnitVector towards = rl > 1e-3f ? client::UnitVector{rx / rl, ry / rl} : d;
            const bool same = std::fabs(towards.x - d.x) < 1e-3f && std::fabs(towards.y - d.y) < 1e-3f;
            for (int leg = 0; leg < (same ? 1 : 2); ++leg) {
                const client::UnitVector e2 = leg == 0 ? towards : d;
                const auto build = [&](float t2) -> std::optional<MoveCandidate> {
                    MoveCandidate c;
                    const std::size_t n = g.starts.size();
                    c.ends.resize(n);
                    c.lengths.assign(n, leg1 + t2);
                    std::vector<Position> mids(n);
                    for (std::size_t i = 0; i < n; ++i) {
                        if (core::more_than(leg1 + t2, g.max_mm[i])) return std::nullopt;
                        mids[i] = Position{g.starts[i].x + l1x, g.starts[i].y + l1y, g.starts[i].z};
                        c.ends[i] = Position{mids[i].x + e2.x * t2, mids[i].y + e2.y * t2, g.starts[i].z};
                        if (!on_battlefield(g, i, mids[i])) return std::nullopt;
                        if (!segment_clear(g, i, g.starts[i], mids[i]) || !segment_clear(g, i, mids[i], c.ends[i])) {
                            return std::nullopt;
                        }
                    }
                    if (!ends_ok(g, c.ends)) return std::nullopt;
                    // D-027: furthest along the move first.
                    std::vector<std::size_t> order(n);
                    for (std::size_t i = 0; i < n; ++i) order[i] = i;
                    const auto ahead = [&](std::size_t i) { return g.starts[i].x * d.x + g.starts[i].y * d.y; };
                    std::stable_sort(order.begin(), order.end(),
                                     [&](std::size_t a, std::size_t b) { return ahead(a) > ahead(b); });
                    for (const std::size_t i : order) {
                        c.proposals.push_back(core::action::ProposeModelMove{g.ids[i], {mids[i], c.ends[i]}});
                    }
                    c.longest_mm = leg1 + t2;
                    const Position c1 = centroid_of(c.ends);
                    c.label = std::string("round the wall (") + name + "), " + move_label(g, c1.x - c0.x, c1.y - c0.y);
                    return c;
                };
                std::optional<MoveCandidate> c = build(std::max(0.0f, rest));
                if (!c && rest > 0.0f) {
                    float lo = 0.0f;
                    float hi = rest;
                    std::optional<MoveCandidate> edge = build(0.0f);
                    for (int k = 0; k < kBisections; ++k) {
                        const float mid = 0.5f * (lo + hi);
                        if (std::optional<MoveCandidate> m = build(mid)) {
                            lo = mid;
                            edge = std::move(m);
                        } else {
                            hi = mid;
                        }
                    }
                    c = std::move(edge);
                }
                if (c) out.push_back(std::move(*c));
            }
        }
    }
}

}  // namespace

// ---- candidates ----

std::vector<MoveCandidate> move_candidates(const GameState& origin, const MoveTeam* team, bool area_spots,
                                           bool cover_spots) {
    std::vector<MoveCandidate> out;
    const std::optional<MoveGeometry> geo = geometry_of(origin, team);
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
    if (g.teamed) beside_partner(g, *team, out);
    if (area_spots && origin.mission) {
        // M87 (D-086): the team's area, and every area the move can reach (a model's base within range of it).
        const core::Unit* u = origin.find_unit(g.unit);
        for (std::size_t i = 0; i < origin.mission->objectives.size(); ++i) {
            const core::ObjectiveArea& area = origin.mission->objectives[i];
            bool reach = team != nullptr && team->area == i;
            for (std::size_t k = 0; !reach && k < g.starts.size() && u != nullptr; ++k) {
                const float gap = core::objective_gap_mm(core::BasePose{g.starts[k], g.bases[k], g.facings[k]}, area);
                reach = core::within(gap, g.unit_max_mm);
            }
            if (reach) area_spots_for(g, area, out);
        }
    }
    if (cover_spots && !origin.terrain.empty()) {
        // M89 (D-090): after every other candidate, so their order is unchanged.
        if (const core::Unit* u = origin.find_unit(g.unit)) {
            cover_spots_for(origin, g, *u, area_spots && origin.mission, out);
        }
        detours_for(g, out);
    }
    return out;
}

std::optional<std::vector<core::action::ProposeModelMove>> slot_moves(const GameState& origin,
                                                                      const std::vector<Position>& ends) {
    const std::optional<MoveGeometry> geo = geometry_of(origin);
    if (!geo || ends.size() != geo->starts.size()) return std::nullopt;
    return slot_proposals(*geo, ends);
}

std::optional<PlannedMove> plan_unit_move(analysis::OddsCache& cache, const GameState& origin, const ArmyPlan* plan,
                                          const MoveWeights& weights) {
    if (!origin.flow.move || !origin.flow.move->paths.empty()) return std::nullopt;
    const core::Unit* u = origin.find_unit(origin.flow.move->unit);
    if (u == nullptr || u->models.empty()) return std::nullopt;
    MoveScoreContext ctx(cache, origin, u->id, weights, plan);
    const std::vector<MoveCandidate> cands = move_candidates(origin, ctx.team(), ctx.area_spots(), ctx.cover_spots());
    const std::vector<Ranked> ranked = rank(ctx, origin.flow.move->type, cands);
    return choose(ctx, origin, u->side, cands, ranked, decision_seed(origin), true);
}

std::vector<PlannedMove> standing_moves(analysis::OddsCache& cache, const GameState& origin, const MoveWeights& weights,
                                        std::size_t count, std::size_t attempts) {
    std::vector<PlannedMove> out;
    if (!origin.flow.move || !origin.flow.move->paths.empty()) return out;
    const core::Unit* u = origin.find_unit(origin.flow.move->unit);
    if (u == nullptr || u->models.empty()) return out;
    MoveScoreContext ctx(cache, origin, u->id, weights);
    const std::vector<MoveCandidate> cands = move_candidates(origin, ctx.team(), ctx.area_spots(), ctx.cover_spots());
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

std::optional<core::UnitId> unit_to_move(const GameState& gs, const std::vector<GameAction>& legal) {
    std::vector<core::UnitId> offered_units;
    for (const GameAction& a : legal) {
        if (const auto* m = std::get_if<core::action::StartMove>(&a)) {
            if (std::find(offered_units.begin(), offered_units.end(), m->unit) == offered_units.end()) {
                offered_units.push_back(m->unit);
            }
        }
    }
    if (offered_units.empty()) return std::nullopt;
    if (offered_units.size() == 1) return offered_units.front();
    // D-066 (M56): the unit closest to an enemy unit first (an engaged unit is always closer than an unengaged one);
    // a tie keeps the engine's order.
    std::optional<core::UnitId> best;
    float best_gap = 0.0f;
    for (const core::UnitId id : offered_units) {
        const core::Unit* u = gs.find_unit(id);
        if (u == nullptr) continue;
        const core::Unit* e = closest_enemy(gs, *u);
        const float gap = e != nullptr ? core::unit_distance(*u, *e).value_or(0.0f) : std::numeric_limits<float>::max();
        if (!best || gap < best_gap) {
            best = id;
            best_gap = gap;
        }
    }
    return best ? best : offered_units.front();
}

std::optional<PolicyResult> decide_move_units(analysis::OddsCache& cache, const GameState& gs, core::Side side,
                                              const std::vector<GameAction>& legal, std::uint64_t seed,
                                              MoveTypeValues* values, const ArmyPlan* plan,
                                              const MoveWeights& weights) {
    const std::optional<core::UnitId> unit = unit_to_move(gs, legal);
    if (!unit) return std::nullopt;
    const core::Unit& u = *gs.find_unit(*unit);
    std::vector<Option> options;
    // D-083 part 2 (M80): the army plan, once per decision (the same for the move's start: the moving unit counts as
    // not moved, D-083 part 1).
    std::optional<ArmyPlan> own_plan;
    if (plan == nullptr) {
        own_plan = army_plan(cache, gs, side);
        plan = &*own_plan;
    }
    MoveScoreContext ctx(cache, gs, *unit, weights, plan);

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
            const std::string team = team_text(gs, ctx.team());
            o.text = "Remain Stationary" + (team.empty() ? "" : ", on " + team);
            if (values != nullptr) values->remain_stationary = o.value;
            options.push_back(std::move(o));
        }
    }
    // A move with no dice (Normal, Ordered Retreat): the whole plan.
    const auto planned = [&](const GameAction& start, double cost) -> std::optional<Option> {
        Simulator sim(gs, seed);
        if (!sim.step(side, start).ok()) return std::nullopt;
        std::optional<PlannedMove> pm = plan_unit_move(cache, sim.state(), plan, weights);
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
        if (std::optional<Option> o = advance_option(cache, gs, side, *unit, plan, weights)) {
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
            if (std::optional<PlannedMove> pm = plan_unit_move(cache, sim.state(), plan, weights)) {
                const double lost = escape_losses(u);
                Option o;
                o.start = escape;
                o.score = pm->score;
                o.value = pm->score.total - unit_share(gs, u, lost);  // lost now, before the move
                o.text = "Fall Back (Desperate Escape) " + pm->candidate.label + ", expecting " +
                         signed_value(-unit_share(gs, u, lost)) + " from hazard rolls";
                if (values != nullptr) values->fall_back = o.value;
                options.push_back(std::move(o));
            }
        }
    }
    if (options.empty()) return std::nullopt;
    const auto best_of = [&](std::optional<std::size_t> skip) {
        std::optional<std::size_t> b;
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (i == skip) continue;
            if (!b || options[i].value > options[*b].value + kBetter) b = i;
        }
        return b;
    };
    std::size_t best = *best_of(std::nullopt);
    // D-077 (M68): a Desperate Escape is made only when the likely hazard losses leave a fall-back move that stands
    // (escape_stands); otherwise the next best option. Checked only for the escape that would be made, so every other
    // decision (and its reason) is as before.
    std::optional<std::size_t> vetoed;
    if (options[best].start == escape && options.size() > 1 && !escape_stands(cache, gs, side, escape, u, weights)) {
        vetoed = best;
        best = *best_of(best);
    }
    Option& chosen = options[best];
    std::string others;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i == best) continue;
        const auto& m = std::get<core::action::StartMove>(options[i].start);
        others += (others.empty() ? "" : ", ") + move_type_text(m.move_type, m.fall_back_mode) + " " +
                  signed_value(options[i].value);
        if (vetoed == i) others += ", but its likely hazard losses leave no move that stands";
    }
    const std::string reason = client::unit_display_name(gs, u.id) + ": " + chosen.text + " - " + position_text(gs, *unit, chosen.score) + " (" +
                               signed_value(chosen.value) + (others.empty() ? "" : "; " + others) + ")";
    if (!chosen.steps.empty()) return from_steps(std::move(chosen.steps), 0, reason);
    return single(chosen.start, reason);
}

std::optional<PolicyResult> continue_unit_move(analysis::OddsCache& cache, const GameState& gs, core::Side side,
                                               const MoveWeights& weights) {
    if (!gs.flow.move) return std::nullopt;
    const core::MoveInProgress& mv = *gs.flow.move;
    if (mv.type != core::MoveType::Normal && mv.type != core::MoveType::Advance &&
        mv.type != core::MoveType::FallBack) {
        return std::nullopt;
    }
    const std::string who = name_of(gs, mv.unit);
    const GameState origin = move_origin(gs);
    if (std::optional<PlannedMove> pm = plan_unit_move(cache, origin, nullptr, weights)) {
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
