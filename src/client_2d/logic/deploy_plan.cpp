#include "deploy_plan.hpp"

#include <algorithm>
#include <variant>

#include "base_shape.hpp"
#include "core/data/formation.hpp"
#include "core/data/load_error.hpp"
#include "core/data/zone_layout.hpp"
#include "core/geometry/oval.hpp"
#include "core/geometry/primitives.hpp"
#include "unit_style.hpp"

namespace wh40k::client {

namespace {

float facing_for(const core::Base& base, float facing_deg) {
    return base.shape == core::BaseShape::Round ? 0.0f : core::footprint_facing_deg(facing_deg);  // D-074
}

}  // namespace

std::optional<DeployPlan> DeployPlan::start(const core::GameState& state, core::UnitId unit) {
    const core::Unit* u = core::find_undeployed(state, unit);
    if (u == nullptr) return std::nullopt;
    if (const std::optional<core::action::DeployUnit> proposal = core::propose_set_up(state, unit)) {
        std::optional<DeployPlan> plan = from_action(state, *proposal);
        if (plan) plan->from_proposal_ = true;
        return plan;
    }
    // No legal spot found (D-081): the unit's formation at the middle of its zone's front line, measured as the
    // auto-layout measures it (core/data: geometry, not a rule), for the player to move by hand.
    DeployPlan plan;
    plan.unit_ = unit;
    plan.side_ = u->side;
    std::vector<core::Model> models = u->models;
    std::vector<core::Base> bases;
    for (const core::Model& m : models) bases.push_back(m.base);
    const core::DeploymentZone& zone = state.side(u->side).deployment_zone;
    core::Formation f;
    f.ranks = core::fewest_coherent_ranks(bases, state.battlefield, zone).value_or(1);
    f.centre_x_mm = state.battlefield.width_mm * 0.5f;
    try {
        core::place_formation(models, f, state.battlefield, zone, "M76 placement");
    } catch (const core::DataError&) {
        // No formation can be placed there (an east or west zone, which hand deployment refuses, D-081): every model
        // at the zone's centre, for the player to spread out.
        const core::Rect r = core::zone_rect(state.battlefield, zone);
        for (core::Model& m : models) {
            m.position = core::Position{(r.min_x + r.max_x) * 0.5f, (r.min_y + r.max_y) * 0.5f, 0.0f};
            m.facing_deg = 0.0f;
        }
    }
    for (const core::Model& m : models) {
        plan.models_.push_back(Placed{m.id, m.base, core::Position{m.position.x, m.position.y, 0.0f},
                                      facing_for(m.base, m.facing_deg)});
    }
    return plan;
}

std::optional<DeployPlan> DeployPlan::from_action(const core::GameState& state, const core::action::DeployUnit& action) {
    const core::Unit* u = core::find_undeployed(state, action.unit);
    if (u == nullptr || action.models.size() != u->models.size()) return std::nullopt;
    DeployPlan plan;
    plan.unit_ = u->id;
    plan.side_ = u->side;
    for (const core::Model& m : u->models) {
        const auto at = std::find_if(action.models.begin(), action.models.end(),
                                     [&](const core::ModelPlacement& p) { return p.model == m.id; });
        if (at == action.models.end()) return std::nullopt;
        plan.models_.push_back(Placed{m.id, m.base, at->position, facing_for(m.base, at->facing_deg)});
    }
    return plan;
}

const DeployPlan::Placed* DeployPlan::find(core::ModelId model) const {
    for (const Placed& p : models_) {
        if (p.model == model) return &p;
    }
    return nullptr;
}

core::Position DeployPlan::centre() const {
    if (models_.empty()) return core::Position{};
    double x = 0.0;
    double y = 0.0;
    for (const Placed& p : models_) {
        x += p.position.x;
        y += p.position.y;
    }
    const double n = static_cast<double>(models_.size());
    return core::Position{static_cast<float>(x / n), static_cast<float>(y / n), 0.0f};
}

void DeployPlan::translate(float dx_mm, float dy_mm) {
    for (Placed& p : models_) {
        p.position.x += dx_mm;
        p.position.y += dy_mm;
    }
}

bool DeployPlan::move_model(core::ModelId model, core::Position to) {
    for (Placed& p : models_) {
        if (p.model != model) continue;
        p.position = core::Position{to.x, to.y, 0.0f};
        return true;
    }
    return false;
}

bool DeployPlan::turn_model(core::ModelId model, float deg) {
    for (Placed& p : models_) {
        if (p.model != model) continue;
        if (p.base.shape == core::BaseShape::Round) return false;  // D-074: a round base has no facing
        p.facing_deg = core::footprint_facing_deg(p.facing_deg + deg);
        return true;
    }
    return false;
}

void DeployPlan::rotate_block(float deg) {
    if (deg == 0.0f) return;
    const core::Position c = centre();
    double s = 0.0;
    double co = 1.0;
    core::oval_detail::sin_cos_deg(static_cast<double>(deg), s, co);  // core's platform-independent sine and cosine
    for (Placed& p : models_) {
        // Counter-clockwise seen from above with +y north, as a positive facing turns (D-074).
        const double dx = static_cast<double>(p.position.x) - static_cast<double>(c.x);
        const double dy = static_cast<double>(p.position.y) - static_cast<double>(c.y);
        p.position.x = static_cast<float>(static_cast<double>(c.x) + dx * co - dy * s);
        p.position.y = static_cast<float>(static_cast<double>(c.y) + dx * s + dy * co);
        if (p.base.shape != core::BaseShape::Round) p.facing_deg = core::footprint_facing_deg(p.facing_deg + deg);
    }
}

std::optional<core::ModelId> DeployPlan::model_at(const core::Position& p) const {
    for (auto it = models_.rbegin(); it != models_.rend(); ++it) {
        if (base_contains(it->pose(), p)) return it->model;
    }
    return std::nullopt;
}

core::action::DeployUnit DeployPlan::action() const {
    core::action::DeployUnit a{unit_, {}};
    a.models.reserve(models_.size());
    for (const Placed& p : models_) a.models.push_back(core::ModelPlacement{p.model, p.position, p.facing_deg});
    return a;
}

std::vector<core::SetUpProblem> DeployPlan::problems(const core::GameState& state) const {
    return core::set_up_problem_details(state, side_, action());
}

std::vector<core::ModelId> models_at_fault(const std::vector<core::SetUpProblem>& problems) {
    std::vector<core::ModelId> out;
    for (const core::SetUpProblem& p : problems) {
        for (const core::ModelId id : p.models) {
            if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
        }
    }
    return out;
}

DeployChoices deploy_choices(const std::vector<core::GameAction>& legal, core::UnitId unit) {
    DeployChoices out;
    for (const core::GameAction& a : legal) {
        if (const auto* d = std::get_if<core::action::DeployUnit>(&a); d != nullptr && d->unit == unit && !d->models.empty()) {
            if (!out.auto_place) out.auto_place = *d;
        } else if (const auto* c = std::get_if<core::action::CannotSetUp>(&a); c != nullptr && c->unit == unit) {
            if (!out.cannot_set_up) out.cannot_set_up = *c;
        }
    }
    return out;
}

std::optional<core::UnitId> first_undeployed(const core::GameState& state, core::Side side) {
    std::optional<core::UnitId> out;
    for (const core::Unit& u : state.undeployed) {
        if (u.side == side && (!out || u.id < *out)) out = u.id;
    }
    return out;
}

std::string cannot_set_up_text(const core::GameState& state, core::UnitId unit) {
    const core::Unit* u = core::find_undeployed(state, unit);
    const std::string name = unit_display_name(state, unit);
    const core::Side other = u != nullptr ? core::opponent(u->side) : core::Side::A;
    const std::string scorer = other == core::Side::A ? "Side A" : "Side B";
    std::string out = "No legal spot is left for " + name + ": it counts as destroyed";
    if (u != nullptr && u->points > 0) {
        out += " and " + scorer + " scores its " + std::to_string(u->points) + " points (D-081, D-076).";
    } else {
        out += " (D-081).";
    }
    return out;
}

std::string models_and_points_text(const core::Unit& unit) {
    const std::size_t n = unit.models.size();
    std::string out = std::to_string(n) + (n == 1 ? " model" : " models");
    if (unit.points > 0) out += ", " + std::to_string(unit.points) + " pts";
    return out;
}

}  // namespace wh40k::client
