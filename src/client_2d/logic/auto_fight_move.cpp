#include "auto_fight_move.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <variant>

#include "core/engine/movement.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
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
};

// Closeness band, then friendly links, then base contact, then the gap (to 0.1 mm), then the shorter move.
bool better(const Candidate& a, const Candidate& b) {
    if (a.band != b.band) return a.band < b.band;
    if (a.links != b.links) return a.links > b.links;
    const bool a_contact = core::in_base_contact(a.gap);
    const bool b_contact = core::in_base_contact(b.gap);
    if (a_contact != b_contact) return a_contact;
    const long a_tenths = std::lround(a.gap * 10.0f);
    const long b_tenths = std::lround(b.gap * 10.0f);
    if (a_tenths != b_tenths) return a_tenths < b_tenths;
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

// The spots one straight line can take `model` to, best first (D-050).
std::vector<Candidate> candidates(const core::GameState& work, const core::Unit& unit, const core::Model& model,
                                  const core::MoveReach& reach, bool keep_coherency) {
    const ReachRegion region = model_region(reach);
    if (region.empty) return {};
    const std::vector<const core::Model*> targets = target_models(work);
    const float r = core::base_radius(model.base);
    std::vector<core::Position> points;

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
    }
    // D-049: this close, only the direct line (no sliding round to another model).
    const std::optional<float> start_gap = gap_to_closest_target(work, model, reach.from);
    const bool direct_only = start_gap && *start_gap <= kWorthwhileGainMm + core::EPS_MM;
    if (!direct_only) {
        // Base contact all round each target model in reach.
        for (const core::Model* t : targets) {
            const float touching = r + core::base_radius(t->base) + kContactNudgeMm;
            if (core::centre_distance(reach.from, t->position) - touching > reach.remaining_mm + core::EPS_MM) continue;
            for (int i = 0; i < kAngleSteps; ++i) {
                const float a = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / kAngleSteps;
                points.push_back(core::Position{t->position.x + touching * std::cos(a),
                                                t->position.y + touching * std::sin(a), 0.0f});
            }
        }
        // A coarse sample of the whole area.
        for (float d = kSampleStepMm; d <= reach.remaining_mm; d += kSampleStepMm) {
            for (int i = 0; i < kAngleSteps; ++i) {
                const float a = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / kAngleSteps;
                points.push_back(core::Position{reach.from.x + d * std::cos(a), reach.from.y + d * std::sin(a), 0.0f});
            }
        }
    }

    std::vector<Candidate> out;
    for (const core::Position& p : points) {
        if (core::centre_distance(reach.from, p) > reach.remaining_mm + core::EPS_MM) continue;
        if (!in_region(region, p)) continue;
        if (keep_coherency && !core::check_coherency(placed(unit, model.id, p)).ok()) continue;
        const std::optional<float> gap = gap_to_closest_target(work, model, p);
        if (!gap) continue;
        out.push_back(Candidate{p, static_cast<int>(std::floor(*gap / kAutoMoveGapBandMm)),
                                friendly_links(unit, model, p), core::centre_distance(reach.from, p), *gap});
    }
    std::stable_sort(out.begin(), out.end(), better);
    return out;
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
        const std::vector<Candidate> options = candidates(now, unit, *it, *reach, keep_coherency);
        for (const Candidate& c : options) {
            const core::GameAction move = core::action::ProposeModelMove{id, {c.p}};
            if (!work.submit(move).ok()) continue;  // the state is unchanged on a rejection
            result.moves.push_back(move);
            break;
        }
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

}  // namespace wh40k::client
