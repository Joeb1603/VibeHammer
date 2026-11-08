#include "auto_charge_move.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <variant>

#include "core/engine/movement.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/oval.hpp"
#include "core/model/keywords.hpp"
#include "core/state/queries.hpp"
#include "base_shape.hpp"
#include "directions.hpp"
#include "fight_move_skip.hpp"
#include "reach_area.hpp"
#include "session.hpp"

namespace wh40k::client {

namespace {

constexpr float kOneInchMm = core::MM_PER_INCH;      // 11.04 While Moving: "within 1""
constexpr float kContactNudgeMm = 0.005f;            // a touching spot: below EPS_MM, so still base contact
constexpr float kRingInsetMm = 0.2f;                 // a "within 1"" or "engaged" ring spot sits this far inside
constexpr float kReachInsetMm = 0.01f;               // a spot at full reach sits this far inside it
constexpr float kScanStepMm = 2.0f;                  // the block translation is scanned down in these steps
constexpr int kBisectSteps = 8;                      // then refined to under 0.01 mm
constexpr int kRefinePasses = 3;
constexpr int kMaxCoherencyChecks = 400;             // per model per pass (D-054: a fixed work budget)
constexpr float kBetterGapMm = 0.05f;                // a spot must improve a model's gap by more than this
constexpr float kSameDirection = 0.9994f;            // block directions closer than about 2 degrees are one
constexpr float kBaselineShortMm = core::MM_PER_INCH;  // the M39 baseline stops 1" short of the targets

struct TargetModel {
    core::Position p;
    core::Base base;
    std::size_t unit = 0;  // index into the (alive) charge targets
    float facing = 0.0f;   // D-074 part 3
    [[nodiscard]] core::BasePose pose() const { return core::BasePose{p, base, facing}; }
};

struct Spot {
    core::Position p;
    int tier = 3;  // 0 base contact, 1 within 1", 2 engaged, 3 none
    float gap = 0.0f;
    float path = 0.0f;
};

struct Mover {
    core::ModelId id;
    core::Base base;
    float r = 0.0f;         // a round base's radius
    bool round = true;      // D-074 part 3: the base is round
    float facing = 0.0f;    // D-074 part 3: its facing, kept (an auto move never turns a model)
    core::Position start;
    float reach = 0.0f;
    bool movable = false;
    ReachRegion region;                // where it may end, less the unit's own models (checked as an arrangement)
    std::vector<Spot> spots;           // by (tier, gap, path)
    std::vector<std::size_t> by_move;  // spots by (engaged first, then the shortest move; else the gap)
};

// D-074 part 3: the ring spots round an oval pair's target (contact_ring) depend only on the target, the moving base and
// its facing, and the ring, so the models of a unit with the same base share them.
struct RingSpots {
    std::size_t target = 0;
    core::Base base;
    float facing = 0.0f;
    float ring = 0.0f;
    std::vector<std::optional<core::Position>> spots;  // one per direction
};

struct Problem {
    std::vector<Mover> movers;  // the moving unit's models, in unit order
    std::vector<TargetModel> targets;
    std::size_t target_units = 0;
    mutable std::vector<core::PosedModel> placed;  // scratch for the coherency check (no allocation per check)
    mutable std::vector<core::PlacedModel> placed_round;  // the same for an all-round unit (the round-only check)
    mutable std::vector<RingSpots> ring_spots;     // D-074 part 3: computed once per target, base, facing and ring
    bool all_round = true;  // D-074 part 3: every mover and target is round (the round-only code, as before)
};

core::BasePose pose_at(const Mover& m, const core::Position& p) { return core::BasePose{p, m.base, m.facing}; }

// The two movers' bases at p and q meet (centre distance below both radii, the arrangement's quick test; D-074 part 3:
// by the pose-aware signed distance for a pair with an oval).
bool oval_bases_meet(const Mover& a, const core::Position& p, const Mover& b, const core::Position& q) {
    return core::signed_base_distance(pose_at(a, p), pose_at(b, q)) < 0.0f;
}

inline bool bases_meet(const Mover& a, const core::Position& p, const Mover& b, const core::Position& q) {
    if (a.round && b.round) [[likely]] return core::centre_distance(p, q) < a.r + b.r;
    return oval_bases_meet(a, p, b, q);
}

using Arrangement = std::vector<core::Position>;  // each mover's end (its start when it isn't moved)

enum class Aim : std::uint8_t {
    Closest,    // each model as close to a target as it can get: base contact, else within 1", else engaged
    LeastMove,  // each model engaged with the shortest move it can (keeps the block together), else closest
};

int tier_of(float gap) {
    if (core::in_base_contact(gap)) return 0;
    if (core::within(gap, kOneInchMm)) return 1;
    if (core::within(gap, core::kEngagementRangeMm)) return 2;
    return 3;
}

// D-074 part 3: the pose-aware versions, for a problem with an oval base (out of line, so the round code stays small).
float oval_gap_at(const Problem& pb, const Mover& m, const core::Position& p) {
    float best = std::numeric_limits<float>::max();
    const core::BasePose at = pose_at(m, p);
    for (const TargetModel& t : pb.targets) best = std::min(best, core::base_distance(at, t.pose()));
    return best;
}

bool oval_engages_unit(const Problem& pb, const Mover& m, const core::Position& p, std::size_t unit) {
    const core::BasePose at = pose_at(m, p);
    for (const TargetModel& t : pb.targets) {
        if (t.unit == unit && core::within(core::base_distance(at, t.pose()), core::kEngagementRangeMm)) return true;
    }
    return false;
}

float gap_at(const Problem& pb, const Mover& m, const core::Position& p) {
    if (!pb.all_round) [[unlikely]] return oval_gap_at(pb, m, p);
    float best = std::numeric_limits<float>::max();
    for (const TargetModel& t : pb.targets) best = std::min(best, core::base_distance(p, m.base, t.p, t.base));
    return best;
}

bool engages_unit(const Problem& pb, const Mover& m, const core::Position& p, std::size_t unit) {
    if (!pb.all_round) [[unlikely]] return oval_engages_unit(pb, m, p, unit);
    for (const TargetModel& t : pb.targets) {
        if (t.unit == unit && core::within(core::base_distance(p, m.base, t.p, t.base), core::kEngagementRangeMm)) {
            return true;
        }
    }
    return false;
}

std::optional<UnitVector> direction(float dx, float dy) {
    const float len = std::sqrt(dx * dx + dy * dy);
    if (!(len > 0.0f)) return std::nullopt;
    return UnitVector{dx / len, dy / len};
}

core::Position offset(const core::Position& p, UnitVector d, float t) {
    return core::Position{p.x + d.x * t, p.y + d.y * t, p.z};
}

// ---- set-up: each model's area and its candidate spots ----

void add_spot(std::vector<Spot>& out, const Problem& pb, const Mover& m, const core::Position& p) {
    const float path = core::centre_distance(m.start, p);
    if (!(path > 0.0f) || path > m.reach) return;
    if (!in_region(m.region, p)) return;
    const float gap = gap_at(pb, m, p);
    out.push_back(Spot{p, tier_of(gap), gap, path});
}

std::vector<Spot> spots_for(const Problem& pb, const Mover& m) {
    std::vector<Spot> out;
    for (std::size_t ti = 0; ti < pb.targets.size(); ++ti) {
        const TargetModel& t = pb.targets[ti];
        const bool round_pair = m.round && t.base.shape == core::BaseShape::Round;
        // D-074 part 3: an oval pair's reach check by the circumscribed radii (generous: the engine decides).
        const float touching = round_pair ? m.r + core::base_radius(t.base)
                                          : core::circumscribed_radius(m.base) + core::circumscribed_radius(t.base);
        const float centres = core::centre_distance(m.start, t.p);
        // The straight line towards it: into base contact, or as far as the model can go.
        if (const auto d = direction(t.p.x - m.start.x, t.p.y - m.start.y)) {
            const float to_contact = round_pair ? centres - touching - kContactNudgeMm
                                                : distance_until_gap(pose_at(m, m.start), t.pose(), kContactNudgeMm);
            const float along = std::min(m.reach - kReachInsetMm, to_contact);
            if (along > 0.0f) add_spot(out, pb, m, offset(m.start, *d, along));
        }
        // Rings round it (base contact, within 1", engaged), if the model could get engaged with it.
        if (centres - touching - core::kEngagementRangeMm > m.reach) continue;
        const float rings[] = {kContactNudgeMm, kOneInchMm - kRingInsetMm, core::kEngagementRangeMm - kRingInsetMm};
        // D-075: round a base bigger than 32 mm, more directions (72 for every smaller base, the old table).
        const int count = contact_direction_count(t.base);
        const std::vector<UnitVector> dirs = direction_table(count);
        for (const float ring : rings) {
            const std::size_t step = ring == kContactNudgeMm ? 1 : 2;  // every 5 degrees in contact, else 10 (for 72)
            if (round_pair) {
                for (std::size_t k = 0; k < dirs.size(); k += step) add_spot(out, pb, m, offset(t.p, dirs[k], touching + ring));
                continue;
            }
            // D-074 part 3: the pose-aware contact spots (contact_ring), once per target, base, facing and ring.
            auto cached = std::find_if(pb.ring_spots.begin(), pb.ring_spots.end(), [&](const RingSpots& r) {
                return r.target == ti && r.base == m.base && r.facing == m.facing && r.ring == ring;
            });
            if (cached == pb.ring_spots.end()) {
                const int every = static_cast<int>(dirs.size() / step);
                std::vector<std::optional<core::Position>> spots = contact_ring(t.pose(), m.base, m.facing, every, ring);
                RingSpots r{ti, m.base, m.facing, ring, std::vector<std::optional<core::Position>>(dirs.size())};
                for (std::size_t k = 0; k < spots.size(); ++k) r.spots[k * step] = spots[k];
                cached = pb.ring_spots.insert(pb.ring_spots.end(), std::move(r));
            }
            for (std::size_t k = 0; k < dirs.size(); k += step) {
                if (cached->spots[k]) add_spot(out, pb, m, *cached->spots[k]);
            }
        }
    }
    // Spots that don't engage: rings round the start, for models that can't reach the targets (or to keep the
    // unit in coherency).
    const float fractions[] = {1.0f, 0.5f};
    for (const float f : fractions) {
        for (std::size_t k = 0; k < kDirections72.size(); k += 2) {
            add_spot(out, pb, m, offset(m.start, kDirections72[k], m.reach * f - kReachInsetMm));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Spot& a, const Spot& b) {
        if (a.tier != b.tier) return a.tier < b.tier;
        if (a.gap != b.gap) return a.gap < b.gap;
        return a.path < b.path;
    });
    return out;
}

Problem set_up(const core::GameState& state) {
    Problem pb;
    const core::MoveInProgress& mv = *state.flow.move;
    const core::Unit& unit = *state.find_unit(mv.unit);
    for (const core::UnitId id : mv.targets) {
        const core::Unit* t = state.find_unit(id);
        if (t == nullptr || core::is_destroyed(*t)) continue;
        for (const core::Model& m : t->models) {
            pb.targets.push_back(TargetModel{m.position, m.base, pb.target_units, m.facing_deg});
        }
        ++pb.target_units;
    }
    for (const core::Model& m : unit.models) {
        Mover mover;
        mover.id = m.id;
        mover.base = m.base;
        mover.round = m.base.shape == core::BaseShape::Round;
        mover.r = mover.round ? core::base_radius(m.base) : core::circumscribed_radius(m.base);
        mover.facing = m.facing_deg;
        mover.start = m.position;
        const std::optional<core::MoveReach> reach = core::move_reach(state, m.id);
        if (reach && !reach->immobile && reach->remaining_mm > 0.0f && m.characteristics.move_mm) {
            mover.movable = true;
            mover.reach = reach->remaining_mm;
            mover.region = model_region(*reach);
            // The unit's own models: whether two of them end on each other is checked for the arrangement as a
            // whole, and the order of the moves keeps each one off the models still to move (D-027).
            std::erase_if(mover.region.outside_all, [&](const core::ReachCircle& c) {
                return std::any_of(unit.models.begin(), unit.models.end(),
                                   [&](const core::Model& o) { return o.id == c.model; });
            });
            std::erase_if(mover.region.outside_shapes, [&](const core::ReachShape& c) {  // D-074 part 3
                return std::any_of(unit.models.begin(), unit.models.end(),
                                   [&](const core::Model& o) { return o.id == c.model; });
            });
            mover.region = trimmed_region(mover.region);  // M54: the same test, fewer circles
        }
        pb.movers.push_back(std::move(mover));
    }
    for (const Mover& m : pb.movers) {
        pb.placed.push_back(core::PosedModel{m.id, m.start, m.base, m.facing});
        pb.placed_round.push_back(core::PlacedModel{m.id, m.start, m.base});
        pb.all_round = pb.all_round && m.round;
    }
    for (const TargetModel& t : pb.targets) pb.all_round = pb.all_round && t.base.shape == core::BaseShape::Round;
    for (Mover& m : pb.movers) {
        if (!m.movable) continue;
        m.spots = spots_for(pb, m);
        for (std::size_t i = 0; i < m.spots.size(); ++i) m.by_move.push_back(i);
        std::stable_sort(m.by_move.begin(), m.by_move.end(), [&](std::size_t a, std::size_t b) {
            const Spot& x = m.spots[a];
            const Spot& y = m.spots[b];
            const bool xe = x.tier <= 2;
            const bool ye = y.tier <= 2;
            if (xe != ye) return xe;
            if (xe) return x.path < y.path;
            return x.gap < y.gap;
        });
    }
    return pb;
}

// ---- an arrangement's checks (the engine has the last word: every proposal is submitted) ----

bool is_moved(const Problem& pb, const Arrangement& a, std::size_t i) { return !(a[i] == pb.movers[i].start); }

bool oval_on_another(const Problem& pb, const Arrangement& a, std::size_t i, const core::Position& p) {
    for (std::size_t j = 0; j < a.size(); ++j) {
        if (j != i && bases_meet(pb.movers[i], p, pb.movers[j], a[j])) return true;
    }
    return false;
}

bool on_another(const Problem& pb, const Arrangement& a, std::size_t i, const core::Position& p) {
    if (!pb.all_round) [[unlikely]] return oval_on_another(pb, a, i, p);
    for (std::size_t j = 0; j < a.size(); ++j) {
        if (j != i && core::centre_distance(p, a[j]) < pb.movers[i].r + pb.movers[j].r) return true;
    }
    return false;
}

bool oval_coherent(const Problem& pb, const Arrangement& a) {
    for (std::size_t i = 0; i < a.size(); ++i) pb.placed[i].position = a[i];
    return core::check_coherency(std::span<const core::PosedModel>(pb.placed)).ok();
}

bool coherent(const Problem& pb, const Arrangement& a) {
    if (!pb.all_round) [[unlikely]] return oval_coherent(pb, a);
    for (std::size_t i = 0; i < a.size(); ++i) pb.placed_round[i].position = a[i];
    return core::check_coherency(pb.placed_round).ok();
}

bool oval_has_a_neighbour(const Problem& pb, const Arrangement& a, std::size_t i, const core::Position& p) {
    const core::BasePose at = pose_at(pb.movers[i], p);
    for (std::size_t j = 0; j < a.size(); ++j) {
        if (j == i) continue;
        if (core::within(core::base_distance(at, pose_at(pb.movers[j], a[j])), core::kCoherencyNearMm)) return true;
    }
    return false;
}

// A quick necessary condition for coherency (the full check is core's): the spot has another model within 2".
bool has_a_neighbour(const Problem& pb, const Arrangement& a, std::size_t i, const core::Position& p) {
    if (a.size() < 2) return true;
    if (!pb.all_round) [[unlikely]] return oval_has_a_neighbour(pb, a, i, p);
    for (std::size_t j = 0; j < a.size(); ++j) {
        if (j == i) continue;
        if (core::within(core::base_distance(p, pb.movers[i].base, a[j], pb.movers[j].base), core::kCoherencyNearMm)) {
            return true;
        }
    }
    return false;
}

// D-027: an order to propose the moved models in so that none ends on a model still to move (the lowest index
// first among those free to go). nullopt if they block each other in a cycle.
std::optional<std::vector<std::size_t>> move_order(const Problem& pb, const Arrangement& a) {
    const std::size_t n = a.size();
    std::vector<std::vector<std::size_t>> waits_for(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (!is_moved(pb, a, i)) continue;
        for (std::size_t j = 0; j < n; ++j) {
            if (j == i || !is_moved(pb, a, j)) continue;
            const bool meet = pb.all_round
                                  ? core::centre_distance(a[i], pb.movers[j].start) < pb.movers[i].r + pb.movers[j].r
                                  : bases_meet(pb.movers[i], a[i], pb.movers[j], pb.movers[j].start);
            if (meet) waits_for[i].push_back(j);
        }
    }
    std::vector<bool> done(n, false);
    std::vector<std::size_t> order;
    std::size_t to_place = 0;
    for (std::size_t i = 0; i < n; ++i) to_place += is_moved(pb, a, i) ? 1 : 0;
    while (order.size() < to_place) {
        bool progressed = false;
        for (std::size_t i = 0; i < n; ++i) {
            if (done[i] || !is_moved(pb, a, i)) continue;
            const auto free = [&](std::size_t j) { return done[j]; };
            if (!std::all_of(waits_for[i].begin(), waits_for[i].end(), free)) continue;
            done[i] = true;
            order.push_back(i);
            progressed = true;
            break;
        }
        if (!progressed) return std::nullopt;
    }
    return order;
}

// Model i's end is on the start of another moved model (so it must move after that one, D-027). If not, an
// order that worked before i moved here still works.
bool ends_on_a_start(const Problem& pb, const Arrangement& a, std::size_t i) {
    for (std::size_t j = 0; j < a.size(); ++j) {
        if (j == i || !is_moved(pb, a, j)) continue;
        if (pb.all_round ? core::centre_distance(a[i], pb.movers[j].start) < pb.movers[i].r + pb.movers[j].r
                         : bases_meet(pb.movers[i], a[i], pb.movers[j], pb.movers[j].start)) {
            return true;
        }
    }
    return false;
}

std::size_t units_covered(const Problem& pb, const Arrangement& a) {
    std::size_t covered = 0;
    for (std::size_t u = 0; u < pb.target_units; ++u) {
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (engages_unit(pb, pb.movers[i], a[i], u)) {
                ++covered;
                break;
            }
        }
    }
    return covered;
}

ChargeArrangementScore score_of(const Problem& pb, const Arrangement& a) {
    ChargeArrangementScore s;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const float gap = gap_at(pb, pb.movers[i], a[i]);
        const int tier = tier_of(gap);
        s.engaged += tier <= 2 ? 1 : 0;
        s.within_1 += tier <= 1 ? 1 : 0;
        s.contacts += tier == 0 ? 1 : 0;
        s.total_gap_mm += gap;
    }
    return s;
}

// Could be proposed as it stands: every target engaged, coherent, no model on another, in some order, and at
// least one model moved.
bool complete(const Problem& pb, const Arrangement& a) {
    bool any = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!is_moved(pb, a, i)) continue;
        any = true;
        if (on_another(pb, a, i, a[i])) return false;
    }
    return any && units_covered(pb, a) == pb.target_units && coherent(pb, a) && move_order(pb, a).has_value();
}

// ---- start points: the unit moved as a block ----

std::optional<Arrangement> translated(const Problem& pb, UnitVector d, float t) {
    Arrangement a;
    for (const Mover& m : pb.movers) {
        if (!m.movable) return std::nullopt;
        const core::Position p = offset(m.start, d, t);
        if (!in_region(m.region, p)) return std::nullopt;
        a.push_back(p);
    }
    return a;
}

// The furthest the whole unit can move along `d` with every model's spot in its area.
std::optional<Arrangement> furthest_block(const Problem& pb, UnitVector d) {
    float reach = std::numeric_limits<float>::max();
    for (const Mover& m : pb.movers) reach = std::min(reach, m.movable ? m.reach : 0.0f);
    const float top = reach - kReachInsetMm;
    if (!(top > 0.0f)) return std::nullopt;
    if (auto a = translated(pb, d, top)) return a;
    for (float t = top - kScanStepMm; t > 0.0f; t -= kScanStepMm) {
        if (!translated(pb, d, t)) continue;
        float lo = t;
        float hi = std::min(top, t + kScanStepMm);
        for (int i = 0; i < kBisectSteps; ++i) {
            const float mid = 0.5f * (lo + hi);
            if (translated(pb, d, mid)) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        return translated(pb, d, lo);
    }
    return std::nullopt;
}

core::Position centroid(const std::vector<core::Position>& ps) {
    core::Position c;
    for (const core::Position& p : ps) {
        c.x += p.x;
        c.y += p.y;
    }
    const float n = static_cast<float>(std::max<std::size_t>(1, ps.size()));
    return core::Position{c.x / n, c.y / n, 0.0f};
}

std::vector<Arrangement> block_starts(const Problem& pb) {
    std::vector<core::Position> ours;
    for (const Mover& m : pb.movers) ours.push_back(m.start);
    std::vector<core::Position> theirs;
    for (const TargetModel& t : pb.targets) theirs.push_back(t.p);
    const core::Position from = centroid(ours);
    const core::Position to = centroid(theirs);

    std::vector<UnitVector> dirs;
    const auto add_dir = [&](std::optional<UnitVector> d) {
        if (!d) return;
        for (const UnitVector& o : dirs) {
            if (o.x * d->x + o.y * d->y > kSameDirection) return;
        }
        dirs.push_back(*d);
    };
    add_dir(direction(to.x - from.x, to.y - from.y));
    // The closest pair of models, and the closest target model to the unit's centre.
    float best = std::numeric_limits<float>::max();
    std::optional<UnitVector> pair;
    float best_centre = std::numeric_limits<float>::max();
    std::optional<UnitVector> to_closest;
    for (const Mover& m : pb.movers) {
        for (const TargetModel& t : pb.targets) {
            const float g = core::base_distance(pose_at(m, m.start), t.pose());
            if (g < best) {
                best = g;
                pair = direction(t.p.x - m.start.x, t.p.y - m.start.y);
            }
        }
    }
    for (const TargetModel& t : pb.targets) {
        const float g = core::centre_distance(from, t.p);
        if (g < best_centre) {
            best_centre = g;
            to_closest = direction(t.p.x - from.x, t.p.y - from.y);
        }
    }
    add_dir(pair);
    add_dir(to_closest);

    std::vector<Arrangement> out;
    for (const UnitVector& d : dirs) {
        if (auto a = furthest_block(pb, d)) out.push_back(std::move(*a));
    }
    // The M39 baseline: towards the targets' centre, 1" short of them (if that is a legal block move).
    if (!dirs.empty() && best < std::numeric_limits<float>::max() && best - kBaselineShortMm > 0.0f) {
        if (auto a = translated(pb, dirs.front(), best - kBaselineShortMm)) out.push_back(std::move(*a));
    }
    return out;
}

// ---- refinement: one model at a time to a better spot, the arrangement kept whole ----

struct Key {
    int primary = 3;
    float secondary = 0.0f;
};

Key key_of(Aim aim, int tier, float gap, float path) {
    if (aim == Aim::Closest) return Key{tier, gap};
    if (tier <= 2) return Key{0, path};
    return Key{1, gap};
}

bool better_key(const Key& a, const Key& b) {
    if (a.primary != b.primary) return a.primary < b.primary;
    return a.secondary < b.secondary - kBetterGapMm;
}

// Moves model i to the first spot in `order` that the predicate accepts and that keeps the arrangement whole.
template <typename Accept>
bool try_spots(const Problem& pb, Arrangement& a, std::size_t i, const std::vector<std::size_t>* order,
               std::size_t covered, Accept accept) {
    const Mover& m = pb.movers[i];
    int checks = 0;
    const std::size_t n = order != nullptr ? order->size() : m.spots.size();
    for (std::size_t k = 0; k < n; ++k) {
        const Spot& s = m.spots[order != nullptr ? (*order)[k] : k];
        const int verdict = accept(s);  // 1 take it if possible, 0 skip it, -1 stop (nothing further is better)
        if (verdict < 0) return false;
        if (verdict == 0) continue;
        if (on_another(pb, a, i, s.p) || !has_a_neighbour(pb, a, i, s.p)) continue;
        if (++checks > kMaxCoherencyChecks) return false;
        const core::Position was = a[i];
        a[i] = s.p;
        if (coherent(pb, a) && units_covered(pb, a) >= covered && (!ends_on_a_start(pb, a, i) || move_order(pb, a))) {
            return true;
        }
        a[i] = was;
    }
    return false;
}

std::vector<std::size_t> model_order(const Problem& pb, const Arrangement& a, bool closest_first) {
    std::vector<std::pair<float, std::size_t>> by_gap;
    for (std::size_t i = 0; i < a.size(); ++i) by_gap.emplace_back(gap_at(pb, pb.movers[i], a[i]), i);
    std::stable_sort(by_gap.begin(), by_gap.end(), [&](const auto& x, const auto& y) {
        if (x.first != y.first) return closest_first ? x.first < y.first : x.first > y.first;
        return x.second < y.second;
    });
    std::vector<std::size_t> out;
    for (const auto& [gap, i] : by_gap) out.push_back(i);
    return out;
}

Arrangement refine(const Problem& pb, Arrangement a, Aim aim, bool closest_first) {
    for (int pass = 0; pass < kRefinePasses; ++pass) {
        bool improved = false;
        // A charge target no model is engaged with yet (11.04 After Moving): the best spot that engages it.
        for (std::size_t u = 0; u < pb.target_units; ++u) {
            const std::size_t covered = units_covered(pb, a);
            bool has = false;
            for (std::size_t i = 0; i < a.size() && !has; ++i) has = engages_unit(pb, pb.movers[i], a[i], u);
            if (has) continue;
            for (const std::size_t i : model_order(pb, a, true)) {
                if (!pb.movers[i].movable) continue;
                if (try_spots(pb, a, i, nullptr, covered,
                              [&](const Spot& s) { return engages_unit(pb, pb.movers[i], s.p, u) ? 1 : 0; })) {
                    improved = true;
                    break;
                }
            }
        }
        for (const std::size_t i : model_order(pb, a, closest_first)) {
            const Mover& m = pb.movers[i];
            if (!m.movable) continue;
            const float gap = gap_at(pb, m, a[i]);
            const Key now = key_of(aim, tier_of(gap), gap, core::centre_distance(m.start, a[i]));
            const std::size_t covered = units_covered(pb, a);
            const std::vector<std::size_t>* order = aim == Aim::LeastMove ? &m.by_move : nullptr;
            if (try_spots(pb, a, i, order, covered, [&](const Spot& s) {
                    return better_key(key_of(aim, s.tier, s.gap, s.path), now) ? 1 : -1;
                })) {
                improved = true;
            }
        }
        if (!improved) break;
    }
    return a;
}

// ---- the engine's verdict ----

struct Verdict {
    std::vector<core::GameAction> moves;
    std::vector<std::string> problems;
    ChargeArrangementScore score;
};

// Every proposal through GameSession::submit on a copy of `state` (as the player's own moves would), then the
// engine's end-of-move checks.
Verdict submit_arrangement(const core::GameState& state, const Problem& pb, const Arrangement& a) {
    Verdict v;
    const std::optional<std::vector<std::size_t>> order = move_order(pb, a);
    if (!order) {
        v.problems.push_back("D-027: the models would end on each other");
        return v;
    }
    GameSession work(state);
    for (const std::size_t i : *order) {
        const core::GameAction move = core::action::ProposeModelMove{pb.movers[i].id, {a[i]}};
        const core::ValidationResult r = work.submit(move);
        if (!r.ok()) {
            v.problems = r.reasons;
            return v;
        }
        v.moves.push_back(move);
    }
    v.problems = core::move_end_problems(work.state());
    v.score = charge_arrangement_score(work.state());
    return v;
}

}  // namespace

// TODO(if-possible): 11.04 While Moving "each model that can end its move within 1" of (engaged with) a charge
// target must do so" is not enforced by the engine; ranking by these counts satisfies as many models as the
// arrangements tried allow, not necessarily every one that could.
bool better_charge_score(const ChargeArrangementScore& a, const ChargeArrangementScore& b) {
    if (a.engaged != b.engaged) return a.engaged > b.engaged;
    if (a.within_1 != b.within_1) return a.within_1 > b.within_1;
    if (a.contacts != b.contacts) return a.contacts > b.contacts;
    return a.total_gap_mm < b.total_gap_mm - core::EPS_MM;
}

ChargeArrangementScore charge_arrangement_score(const core::GameState& state) {
    ChargeArrangementScore s;
    if (!state.flow.move) return s;
    const core::Unit* u = state.find_unit(state.flow.move->unit);
    if (u == nullptr) return s;
    for (const core::Model& m : u->models) {
        const std::optional<float> gap = gap_to_closest_target(state, m, m.position);
        if (!gap) continue;
        s.engaged += core::within(*gap, core::kEngagementRangeMm) ? 1 : 0;
        s.within_1 += core::within(*gap, kOneInchMm) ? 1 : 0;
        s.contacts += core::in_base_contact(*gap) ? 1 : 0;
        s.total_gap_mm += *gap;
    }
    return s;
}

bool auto_charge_move_available(const core::GameState& state) {
    if (!state.flow.move || !state.pending) return false;
    const auto* d = std::get_if<core::decision::MoveModels>(&state.pending->kind);
    if (d == nullptr || d->unit != state.flow.move->unit) return false;
    return state.flow.move->type == core::MoveType::Charge && state.flow.move->paths.empty();
}

AutoChargeMove auto_charge_move(const core::GameState& state) {
    AutoChargeMove out;
    if (!auto_charge_move_available(state)) {
        out.problems.push_back("no charge move to plan");
        return out;
    }
    const Problem pb = set_up(state);
    if (pb.targets.empty()) {
        out.problems.push_back("no charge target has models left");
        return out;
    }
    Arrangement unmoved;
    for (const Mover& m : pb.movers) unmoved.push_back(m.start);

    std::vector<Arrangement> starts = block_starts(pb);
    if (starts.empty()) starts.push_back(unmoved);  // no block move: models one at a time from where they stand
    std::vector<Arrangement> found;
    const auto keep = [&](Arrangement a) {
        if (!complete(pb, a)) return;
        if (std::find(found.begin(), found.end(), a) == found.end()) found.push_back(std::move(a));
    };
    for (const Arrangement& s : starts) {
        keep(s);
        keep(refine(pb, s, Aim::Closest, true));
        keep(refine(pb, s, Aim::Closest, false));
        keep(refine(pb, refine(pb, s, Aim::LeastMove, true), Aim::Closest, true));
    }
    if (found.empty()) {
        out.problems.push_back(starts.front() == unmoved
                                   ? "11.04: no arrangement of the models ends engaged with every charge target"
                                   : "11.04: no arrangement found that ends engaged with every charge target in "
                                     "coherency (03.03)");
        return out;
    }
    std::vector<std::pair<ChargeArrangementScore, std::size_t>> ranked;
    for (std::size_t i = 0; i < found.size(); ++i) ranked.emplace_back(score_of(pb, found[i]), i);
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& x, const auto& y) { return better_charge_score(x.first, y.first); });
    std::vector<std::string> first_problems;
    for (const auto& [score, i] : ranked) {
        Verdict v = submit_arrangement(state, pb, found[i]);
        if (v.problems.empty() && !v.moves.empty()) {
            out.moves = std::move(v.moves);
            out.score = v.score;
            return out;
        }
        if (first_problems.empty()) first_problems = std::move(v.problems);
    }
    out.problems =
        first_problems.empty() ? std::vector<std::string>{"no arrangement the engine accepts"} : first_problems;
    return out;
}

}  // namespace wh40k::client
