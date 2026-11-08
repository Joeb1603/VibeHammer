#include "auto_fight_move.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <optional>
#include <span>
#include <variant>

#include "core/engine/movement.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/model/keywords.hpp"
#include "core/state/queries.hpp"
#include "fight_move_skip.hpp"
#include "reach_area.hpp"
#include "session.hpp"

namespace wh40k::client {

namespace {

constexpr int kAngleSteps = 72;          // every 5 degrees
constexpr float kSampleStepMm = 1.0f;    // radial step of the coarse sample of the area
constexpr float kContactNudgeMm = 0.005f;  // touching points sit this far out: below EPS_MM, still contact

struct Candidate {
    core::Position p;
    int band = 0;      // gap to the closest target, in kAutoMoveGapBandMm bands
    int links = 0;     // friendly models within 2" (03.03), capped at 2
    float path = 0.0f;  // length of the straight line
    float gap = 0.0f;
    long tenths = 0;   // the gap to 0.1 mm, std::lround(gap * 10) (M54: once, not in every comparison)
};

Candidate candidate_at(const core::Position& p, int band, int links, float path, float gap) {
    return Candidate{p, band, links, path, gap, std::lround(gap * 10.0f)};
}

// Closeness band, then friendly links, then base contact, then the gap (to 0.1 mm), then the shorter move.
bool better(const Candidate& a, const Candidate& b) {
    if (a.band != b.band) return a.band < b.band;
    if (a.links != b.links) return a.links > b.links;
    const bool a_contact = core::in_base_contact(a.gap);
    const bool b_contact = core::in_base_contact(b.gap);
    if (a_contact != b_contact) return a_contact;
    if (a.tenths != b.tenths) return a.tenths < b.tenths;
    return a.path < b.path;
}

core::Position towards(const core::Position& from, const core::Position& to, float distance_mm) {
    const float d = core::centre_distance(from, to);
    if (d <= 0.0f) return from;
    const float k = distance_mm / d;
    return core::Position{from.x + (to.x - from.x) * k, from.y + (to.y - from.y) * k, from.z};
}

std::vector<core::PlacedModel> placed(const core::Unit& unit, std::optional<core::ModelId> moved = std::nullopt,
                                      core::Position at = {}) {
    std::vector<core::PlacedModel> out;
    for (const core::Model& m : unit.models) {
        out.push_back(core::PlacedModel{m.id, (moved && m.id == *moved) ? at : m.position, m.base});
    }
    return out;
}

int friendly_links(const core::Unit& unit, const core::Model& model, const core::Position& p) {
    int links = 0;
    for (const core::Model& o : unit.models) {
        if (o.id == model.id) continue;
        if (core::within(core::base_distance(p, model.base, o.position, o.base), core::kCoherencyNearMm)) ++links;
    }
    return std::min(links, 2);
}

// The target models (of the move's target units), alive.
std::vector<const core::Model*> target_models(const core::GameState& state) {
    std::vector<const core::Model*> out;
    for (const core::UnitId id : state.flow.move->targets) {
        const core::Unit* t = state.find_unit(id);
        if (t == nullptr || core::is_destroyed(*t)) continue;
        for (const core::Model& m : t->models) out.push_back(&m);
    }
    return out;
}

// M54: gap_to_closest_target(work, model, p) for many spots p: the alive target models, gathered once and grouped
// by base. The gap is the least base_distance to any of them (a minimum, so the grouping doesn't change it), and
// for one pair of bases base_distance only grows with the squared centre distance it takes the root of (rounding
// keeps sqrt and the subtractions in order), so each group's least squared distance gives its least gap exactly.
class TargetGap {
public:
    TargetGap(const core::GameState& work, const core::Model& model) : base_(model.base) {
        for (const core::Model* t : target_models(work)) {
            auto g = std::find_if(groups_.begin(), groups_.end(), [&](const Group& x) { return x.base == t->base; });
            if (g == groups_.end()) g = groups_.insert(groups_.end(), Group{t->base, {}});
            g->centres.push_back(t->position);
        }
    }
    [[nodiscard]] std::optional<float> at(const core::Position& p) const {
        std::optional<float> gap;
        for (const Group& g : groups_) {
            float least = 0.0f;
            for (std::size_t i = 0; i < g.centres.size(); ++i) {
                // centre_distance(p, t) before its sqrt
                const float dx = g.centres[i].x - p.x;
                const float dy = g.centres[i].y - p.y;
                const float q = dx * dx + dy * dy;
                if (i == 0 || q < least) least = q;
            }
            // base_distance(p, base_, t, t.base) for the closest t of the group
            const float d = std::max(0.0f, std::sqrt(least) - core::base_radius(base_) - core::base_radius(g.base));
            if (!gap || d < *gap) gap = d;
        }
        return gap;
    }

private:
    struct Group {
        core::Base base;
        std::vector<core::Position> centres;
    };
    core::Base base_;
    std::vector<Group> groups_;
};

// The spots one straight line can take `model` to, best first (D-050), handed to `accept` in that order until it
// takes one (returns true).
template <typename Accept>
void candidates(const core::GameState& work, const core::Unit& unit, const core::Model& model,
                const core::MoveReach& reach, bool keep_coherency, Accept accept) {
    const ReachRegion region = trimmed_region(model_region(reach));  // M54: the same test, fewer circles
    if (region.empty) return;
    const std::vector<const core::Model*> targets = target_models(work);
    const float r = core::base_radius(model.base);
    std::vector<core::Position> points;
    std::vector<std::pair<std::size_t, int>> on_ray;  // M54: the coarse spots' (ray, k), parallel to `points`
    constexpr std::pair<std::size_t, int> kOffRay{0, 0};
    std::array<float, kAngleSteps> cosines{};
    std::array<float, kAngleSteps> sines{};
    int k_max = 0;

    // The direct line into base contact with the nearest target model (or as far as it can go).
    const core::Model* nearest = nullptr;
    float nearest_gap = 0.0f;
    for (const core::Model* t : targets) {
        const float g = core::base_distance(reach.from, model.base, t->position, t->base);
        if (nearest == nullptr || g < nearest_gap) {
            nearest = t;
            nearest_gap = g;
        }
    }
    if (nearest != nullptr && nearest_gap > 0.0f) {
        points.push_back(towards(reach.from, nearest->position, std::min(nearest_gap, reach.remaining_mm)));
        on_ray.push_back(kOffRay);
    }
    // D-049: this close, only the direct line (no sliding round to another model).
    const std::optional<float> start_gap = gap_to_closest_target(work, model, reach.from);
    const bool direct_only = start_gap && *start_gap <= kWorthwhileGainMm + core::EPS_MM;
    if (!direct_only) {
        // M54: room for every spot at once (the area sample's rings, one per whole millimetre, and the contact rings).
        const std::size_t rings = reach.remaining_mm > 0.0f ? static_cast<std::size_t>(reach.remaining_mm) + 1 : 0;
        points.reserve(points.size() + (rings + targets.size()) * static_cast<std::size_t>(kAngleSteps));
        on_ray.reserve(points.capacity());
        // M54: each angle's cosine and sine once (the same values as before, from the same expressions).
        for (int i = 0; i < kAngleSteps; ++i) {
            const float a = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / kAngleSteps;
            cosines[static_cast<std::size_t>(i)] = std::cos(a);
            sines[static_cast<std::size_t>(i)] = std::sin(a);
        }
        // Base contact all round each target model in reach.
        for (const core::Model* t : targets) {
            const float touching = r + core::base_radius(t->base) + kContactNudgeMm;
            if (core::centre_distance(reach.from, t->position) - touching > reach.remaining_mm + core::EPS_MM) continue;
            for (std::size_t i = 0; i < cosines.size(); ++i) {
                points.push_back(core::Position{t->position.x + touching * cosines[i],
                                                t->position.y + touching * sines[i], 0.0f});
                on_ray.push_back(kOffRay);
            }
        }
        // A coarse sample of the whole area (d = 1, 2, ... mm: sums of 1.0f are exact).
        int k = 0;
        for (float d = kSampleStepMm; d <= reach.remaining_mm; d += kSampleStepMm) {
            ++k;
            for (std::size_t i = 0; i < cosines.size(); ++i) {
                points.push_back(core::Position{reach.from.x + d * cosines[i], reach.from.y + d * sines[i], 0.0f});
                on_ray.emplace_back(i, k);
            }
        }
        k_max = k;
    }
    // M54: the coarse spots in_region certainly refuses (ray i, distance index k - 1: the k-th ring is at k mm).
    std::vector<float> rings(static_cast<std::size_t>(k_max));
    for (int k = 1; k <= k_max; ++k) rings[static_cast<std::size_t>(k - 1)] = static_cast<float>(k) * kSampleStepMm;
    static_assert(kSampleStepMm == 1.0f, "the rings are at whole millimetres (sums of 1.0f are exact)");
    const RayRefusals refusals(region, reach.from, cosines, sines, rings);

    // M54: 03.03 with only this model moved, from a table of the others (check_coherency's answer).
    const std::vector<core::PlacedModel> now = placed(unit);
    std::size_t moved = now.size();
    for (std::size_t i = 0; i < now.size(); ++i) {
        if (now[i].id == model.id) moved = i;
    }
    const core::CoherencyWithOneMoved coherency(now, moved);
    const TargetGap target_gap(work, model);
    const core::ModelId model_id = model.id;
    const core::Base base = model.base;
    const core::Position from = reach.from;
    // friendly_links(unit, model, p), on the copy.
    const auto links_at = [&](const core::Position& p) {
        int links = 0;
        for (const core::PlacedModel& o : now) {
            if (o.id == model_id) continue;
            if (core::within(core::base_distance(p, base, o.position, o.base), core::kCoherencyNearMm)) ++links;
        }
        return std::min(links, 2);
    };
    std::vector<Candidate> out;
    for (std::size_t j = 0; j < points.size(); ++j) {
        const core::Position& p = points[j];
        if (core::centre_distance(from, p) > reach.remaining_mm + core::EPS_MM) continue;
        if (on_ray[j].second > 0 && refusals.refused(on_ray[j].first, static_cast<std::size_t>(on_ray[j].second - 1))) {
            continue;  // in_region would refuse it
        }
        if (!in_region(region, p)) continue;
        if (keep_coherency && !coherency.ok_at(p)) continue;
        const std::optional<float> gap = target_gap.at(p);
        if (!gap) continue;
        out.push_back(candidate_at(p, static_cast<int>(std::floor(*gap / kAutoMoveGapBandMm)), links_at(p),
                                   core::centre_distance(from, p), *gap));
    }
    std::stable_sort(out.begin(), out.end(), better);
    for (const Candidate& c : out) {
        if (accept(c)) return;
    }
}

// How good an arrangement is (D-050): more models in base contact with a target, then less gap in total.
struct Score {
    int in_contact = 0;
    float total_gap = 0.0f;
};

bool better_score(const Score& a, const Score& b) {
    if (a.in_contact != b.in_contact) return a.in_contact > b.in_contact;
    return a.total_gap < b.total_gap - core::EPS_MM;
}

Score score_of(const core::GameState& work) {
    Score s;
    for (const core::Model& m : work.find_unit(work.flow.move->unit)->models) {
        const std::optional<float> gap = gap_to_closest_target(work, m, m.position);
        if (!gap) continue;
        if (core::in_base_contact(*gap)) ++s.in_contact;
        s.total_gap += *gap;
    }
    return s;
}

struct Pass {
    AutoFightMove result;
    Score score;
};

// One greedy pass over the models in `order`, in a scratch session on a copy of `state`: every placement goes
// through GameSession::submit (validate, then apply), as the player's own moves would.
Pass place_models(const core::GameState& state, const std::vector<core::ModelId>& order) {
    GameSession work(state);
    const core::UnitId unit_id = state.flow.move->unit;
    const bool keep_coherency = core::check_coherency(placed(*state.find_unit(unit_id))).ok();
    AutoFightMove result;
    for (const core::ModelId id : order) {
        const core::GameState& now = work.state();
        const core::Unit& unit = *now.find_unit(unit_id);
        const auto it = std::find_if(unit.models.begin(), unit.models.end(),
                                     [&](const core::Model& m) { return m.id == id; });
        if (it == unit.models.end()) continue;
        const std::optional<core::MoveReach> reach = core::move_reach(now, id);
        if (!reach || reach->immobile) continue;
        candidates(now, unit, *it, *reach, keep_coherency, [&](const Candidate& c) {
            const core::GameAction move = core::action::ProposeModelMove{id, {c.p}};
            if (!work.submit(move).ok()) return false;  // the state is unchanged on a rejection
            result.moves.push_back(move);
            return true;
        });
    }
    if (result.moves.empty()) {
        result.problems.push_back("no model can get closer");
    } else {
        result.problems = core::move_end_problems(work.state());
    }
    return Pass{std::move(result), score_of(work.state())};
}

}  // namespace

bool auto_fight_move_available(const core::GameState& state) {
    if (!state.flow.move || !state.pending) return false;
    const auto* d = std::get_if<core::decision::MoveModels>(&state.pending->kind);
    if (d == nullptr || d->unit != state.flow.move->unit) return false;
    const core::MoveType type = state.flow.move->type;
    if (type != core::MoveType::PileIn && type != core::MoveType::Consolidation) return false;
    return state.flow.move->paths.empty();
}

AutoFightMove auto_fight_move(const core::GameState& state) {
    if (!auto_fight_move_available(state)) return AutoFightMove{{}, {"no pile-in or consolidation to plan"}};
    // TODO(M66): remove when auto moves handle ovals. D-074 part 2: the arrangement is planned on round-only geometry
    // (base_radius throws for an oval), so with any oval base on the battlefield it refuses cleanly instead.
    for (const core::Unit& u : state.units) {
        for (const core::Model& m : u.models) {
            if (m.base.shape != core::BaseShape::Round) {
                return AutoFightMove{{}, {"D-074: Auto move does not handle oval bases yet"}};
            }
        }
    }
    const core::Unit& unit = *state.find_unit(state.flow.move->unit);
    // Closest to the target first; ties by model id (deterministic).
    std::vector<std::pair<float, core::ModelId>> by_gap;
    for (const core::Model& m : unit.models) {
        by_gap.emplace_back(gap_to_closest_target(state, m, m.position).value_or(0.0f), m.id);
    }
    std::stable_sort(by_gap.begin(), by_gap.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second.v < b.second.v;
    });
    std::vector<core::ModelId> order;
    for (const auto& [gap, id] : by_gap) order.push_back(id);

    // Both orders: closest first usually wins, but a rear model moving first can make room for the
    // models in front (it keeps them in coherency further forward). The better legal result is kept.
    Pass first = place_models(state, order);
    std::reverse(order.begin(), order.end());
    Pass second = place_models(state, order);
    if (first.result.ok() && second.result.ok()) {
        return better_score(second.score, first.score) ? second.result : first.result;
    }
    if (first.result.ok()) return first.result;
    if (second.result.ok()) return second.result;
    return AutoFightMove{{}, first.result.problems};
}

FightMoveShape fight_move_shape(const core::GameState& state, core::UnitId unit_id,
                                const std::vector<core::UnitId>& targets) {
    FightMoveShape s;
    const core::Unit* unit = state.find_unit(unit_id);
    if (unit == nullptr) return s;
    std::vector<const core::Unit*> alive;
    for (const core::UnitId id : targets) {
        const core::Unit* t = state.find_unit(id);
        if (t != nullptr && !core::is_destroyed(*t)) alive.push_back(t);
    }
    if (alive.empty()) return s;
    for (const core::Model& m : unit->models) {
        std::optional<float> gap;
        for (const core::Unit* t : alive) {
            const std::optional<float> d = core::model_distance_to_unit(m, m.position, *t);
            if (d && (!gap || *d < *gap)) gap = d;
        }
        if (gap) {
            if (core::within(*gap, core::kEngagementRangeMm)) ++s.engaged;
            if (core::in_base_contact(*gap)) ++s.in_contact;
            s.total_gap += *gap;
        }
        s.links += friendly_links(*unit, m, m.position);
    }
    return s;
}

FightMoveShape shape_change(const FightMoveShape& before, const FightMoveShape& after) {
    return FightMoveShape{after.engaged - before.engaged, after.in_contact - before.in_contact,
                          after.total_gap - before.total_gap, after.links - before.links};
}

bool tidier(const FightMoveShape& a, const FightMoveShape& b) {
    if (a.engaged != b.engaged) return a.engaged > b.engaged;
    if (a.in_contact != b.in_contact) return a.in_contact > b.in_contact;
    if (std::fabs(a.total_gap - b.total_gap) > kAutoMoveGapBandMm) return a.total_gap < b.total_gap;
    return a.links > b.links;
}

std::string describe_shape_change(const FightMoveShape& c) {
    std::vector<std::string> parts;
    const auto count = [&](int n, const char* what) {
        if (n > 0) parts.push_back(std::to_string(n) + " more " + what);
    };
    count(c.engaged, "engaged");
    count(c.in_contact, "in base contact");
    if (c.total_gap < -kAutoMoveGapBandMm) {
        char buf[48];
        std::snprintf(buf, sizeof buf, "%.1f\" closer in total", static_cast<double>(-c.total_gap / core::MM_PER_INCH));
        parts.emplace_back(buf);
    }
    if (c.links > 0) parts.push_back("tighter coherency");
    if (parts.empty()) return "no closer";
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) out += (i > 0 ? ", " : "") + parts[i];
    return out;
}

}  // namespace wh40k::client
