#include "fight_move_skip.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <variant>

#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/oval.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/fight.hpp"
#include "core/state/queries.hpp"
#include "reach_area.hpp"

namespace wh40k::client {

namespace {

constexpr int kAngleSteps = 72;           // every 5 degrees
constexpr float kRadiusStepMm = 0.5f;

std::optional<core::UnitId> moving_unit(const core::GameAction& a) {
    if (const auto* p = std::get_if<core::action::StartPileIn>(&a)) return p->unit;
    if (const auto* c = std::get_if<core::action::StartConsolidation>(&a)) return c->unit;
    return std::nullopt;
}

// D-047: some point of the model's area is more than kWorthwhileMoveMm from its start.
bool can_move_far_enough(const core::MoveReach& reach) {
    if (reach.immobile || reach.remaining_mm <= kWorthwhileMoveMm + core::EPS_MM) return false;
    const ReachRegion region = trimmed_region(model_region(reach));  // M54: the same test, fewer circles
    if (region.empty) return false;
    // M54: the same spots in the same order, each angle's cosine and sine worked out once (from the same
    // expressions), and the spots in_region certainly refuses skipped (RayRefusals).
    std::array<float, kAngleSteps> cosines{};
    std::array<float, kAngleSteps> sines{};
    for (int i = 0; i < kAngleSteps; ++i) {
        const float a = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / kAngleSteps;
        cosines[static_cast<std::size_t>(i)] = std::cos(a);
        sines[static_cast<std::size_t>(i)] = std::sin(a);
    }
    std::vector<float> radii;
    for (float r = kWorthwhileMoveMm + core::EPS_MM; r <= reach.remaining_mm; r += kRadiusStepMm) radii.push_back(r);
    const RayRefusals refusals(region, reach.start, cosines, sines, radii);
    for (std::size_t j = 0; j < radii.size(); ++j) {
        const float r = radii[j];
        for (std::size_t i = 0; i < cosines.size(); ++i) {
            if (refusals.refused(i, j)) continue;  // in_region would refuse it
            const core::Position p{reach.start.x + r * cosines[i], reach.start.y + r * sines[i], 0.0f};
            if (in_region(region, p)) return true;
        }
    }
    return false;
}

// D-047: the model could reach the engagement range (03.04) of a model of an enemy unit it isn't engaged
// with at its start. Generous on purpose: only the distance it may move is considered.
bool could_make_new_contact(const core::GameState& state, const core::Unit& unit, const core::Model& model,
                            const core::MoveReach& reach) {
    if (reach.immobile) return false;
    const std::vector<core::UnitId> engaged =
        core::enemy_units_engaged_with_model(state, unit, model, reach.start);
    // D-074 part 3: generous, so by the bases' circumscribed radii (for a round base its radius, as before).
    const float r = core::circumscribed_radius(model.base);
    for (const core::Unit& other : state.units) {
        if (other.side == unit.side || core::is_destroyed(other)) continue;
        if (std::find(engaged.begin(), engaged.end(), other.id) != engaged.end()) continue;
        for (const core::Model& o : other.models) {
            const float centres = std::hypot(o.position.x - reach.start.x, o.position.y - reach.start.y);
            const float gap =
                centres - r - core::circumscribed_radius(o.base) - core::kEngagementRangeMm - core::EPS_MM;
            if (gap <= reach.remaining_mm) return true;
        }
    }
    return false;
}

// D-048: in a pile-in or an Ongoing Consolidation, the model is within kNearBaseContactMm of an enemy
// model, so for skipping it counts as in base contact (it "cannot be moved").
bool nearly_in_base_contact(const core::GameState& state, const core::Unit& unit, const core::Model& model,
                            const core::Position& start) {
    if (!state.flow.move) return false;
    const auto& mv = *state.flow.move;
    const bool pile_in = mv.type == core::MoveType::PileIn;
    const bool ongoing = mv.type == core::MoveType::Consolidation &&
                         mv.consolidation_mode == core::ConsolidationMode::Ongoing;
    if (!pile_in && !ongoing) return false;
    // D-074 part 3: the model at its start pose (it hasn't moved or turned yet in the preview).
    const core::BasePose at{start, model.base, model.facing_deg};
    for (const core::Unit& other : state.units) {
        if (other.side == unit.side || core::is_destroyed(other)) continue;
        for (const core::Model& o : other.models) {
            if (core::base_distance(at, core::pose_of(o)) <= kNearBaseContactMm) return true;
        }
    }
    return false;
}

// D-047 / D-049: the model could end both well away from its start and noticeably closer to the target.
bool can_get_worthwhile_closer(const core::GameState& state, const core::Model& model,
                               const core::MoveReach& reach) {
    const std::optional<float> gap = gap_to_closest_target(state, model, reach.start);
    if (gap && *gap <= kWorthwhileGainMm + core::EPS_MM) return false;
    return can_move_far_enough(reach);
}

bool skips_fight_moves(const core::GameAction& a) {
    using namespace core::action;
    return std::holds_alternative<EndPileIns>(a) || std::holds_alternative<EndConsolidations>(a) ||
           std::holds_alternative<SkipPileIn>(a);
}

}  // namespace

std::optional<float> gap_to_closest_target(const core::GameState& state, const core::Model& model,
                                           const core::Position& pos) {
    if (!state.flow.move) return std::nullopt;
    std::optional<float> gap;
    for (const core::UnitId id : state.flow.move->targets) {
        const core::Unit* t = state.find_unit(id);
        if (t == nullptr || core::is_destroyed(*t)) continue;
        const std::optional<float> d = core::model_distance_to_unit(model, pos, *t);
        if (d && (!gap || *d < *gap)) gap = d;
    }
    return gap;
}

bool fight_move_worthwhile(const core::GameState& state, core::Side side, const core::GameAction& start) {
    const std::optional<core::UnitId> id = moving_unit(start);
    if (!id) return true;
    const std::optional<core::GameState> preview = core::preview_fight_move(state, side, start);
    if (!preview) return true;
    const core::Unit* unit = preview->find_unit(*id);
    if (unit == nullptr) return true;
    for (const core::Model& m : unit->models) {
        const std::optional<core::MoveReach> reach = core::move_reach(*preview, m.id);
        if (!reach) continue;  // not moving (e.g. destroyed)
        if (nearly_in_base_contact(*preview, *unit, m, reach->start)) continue;  // D-048
        if (can_get_worthwhile_closer(*preview, m, *reach) || could_make_new_contact(*preview, *unit, m, *reach)) {
            return true;
        }
    }
    return false;
}

std::optional<core::GameAction> pointless_fight_step_skip(const core::GameState& state,
                                                          const std::vector<core::GameAction>& legal) {
    if (!state.pending) return std::nullopt;
    const auto& kind = state.pending->kind;
    if (!std::holds_alternative<core::decision::PileIn>(kind) &&
        !std::holds_alternative<core::decision::Consolidate>(kind) &&
        !std::holds_alternative<core::decision::OverrunPileIn>(kind)) {
        return std::nullopt;
    }
    std::optional<core::GameAction> skip;
    bool any_move = false;
    for (const core::GameAction& a : legal) {
        if (skips_fight_moves(a)) {
            skip = a;
        } else if (moving_unit(a)) {
            any_move = true;
            if (fight_move_worthwhile(state, state.pending->side, a)) return std::nullopt;
        } else {
            return std::nullopt;  // something else to do
        }
    }
    if (!any_move) return std::nullopt;  // a lone skip is D-036's
    return skip;
}

}  // namespace wh40k::client
