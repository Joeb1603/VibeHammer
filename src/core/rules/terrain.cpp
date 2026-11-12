#include "core/rules/terrain.hpp"

#include <algorithm>

#include "core/geometry/distance.hpp"
#include "core/geometry/oval.hpp"
#include "core/geometry/visibility.hpp"
#include "core/model/keywords.hpp"
#include "core/state/queries.hpp"

namespace wh40k::core {

namespace {

bool overlaps(const Rect& a, const Rect& b) {
    return a.min_x <= b.max_x && b.min_x <= a.max_x && a.min_y <= b.max_y && b.min_y <= a.max_y;
}

// The base's box, grown by EPS_MM (within is "at most EPS_MM apart").
Rect pose_box(const BasePose& pose) {
    const float r = circumscribed_radius(pose.base) + EPS_MM;
    return Rect{pose.position.x - r, pose.position.y - r, pose.position.x + r, pose.position.y + r};
}

std::string unit_label(UnitId id) { return "unit " + std::to_string(id.v); }
std::string model_label(ModelId id) { return "model " + std::to_string(id.v); }

// 06.01: every model on the battlefield but those of the observing and observed units.
std::vector<PosedModel> model_blockers(const GameState& state, const Unit* own, const Unit& target) {
    std::vector<PosedModel> out;
    for (const Unit& u : state.units) {
        if (u.id == target.id || (own != nullptr && u.id == own->id)) continue;
        for (const Model& m : u.models) out.push_back(posed_of(m));
    }
    return out;
}

}  // namespace

bool takes_cover_and_hides(const Unit& unit) {
    return has_keyword(unit, "INFANTRY") || has_keyword(unit, "BEASTS") || has_keyword(unit, "SWARM");
}

bool moves_through_dense(const Unit& unit) { return takes_cover_and_hides(unit) || has_keyword(unit, "MOBILE"); }

bool is_obscuring(const TerrainArea& area) {
    return std::any_of(area.features.begin(), area.features.end(),
                       [](const TerrainFeature& f) { return is_solid(f.category); });
}

bool has_dense(const TerrainArea& area) {
    return std::any_of(area.features.begin(), area.features.end(),
                       [](const TerrainFeature& f) { return f.category == TerrainCategory::Dense; });
}

bool blocks_move(const TerrainFeature& feature, const Unit& unit) {
    // 13.06: other models move through dense features only where all their sections are 2" or less in height.
    return feature.category == TerrainCategory::Dense && feature.height_mm.has_value() &&
           more_than(*feature.height_mm, kDenseMoveHeightMm) && !moves_through_dense(unit);
}

bool model_within_area(const BasePose& pose, const TerrainArea& area) { return within_shape(pose, area.shape); }

const TerrainFeature& feature_of(const GameState& state, FeatureRef ref) {
    return state.terrain.at(ref.area).features.at(ref.feature);
}

std::string feature_label(const TerrainFeature& feature) {
    std::string out = "'" + feature.name + "' (" + std::string(terrain_category_name(feature.category));
    if (feature.height_mm) out += ", " + format_inches(*feature.height_mm) + " tall";
    return out + ")";
}

std::optional<FeatureRef> solid_feature_under(const GameState& state, const BasePose& pose) {
    const Rect box = pose_box(pose);
    for (std::size_t a = 0; a < state.terrain.size(); ++a) {
        const std::vector<TerrainFeature>& features = state.terrain[a].features;
        for (std::size_t f = 0; f < features.size(); ++f) {
            if (!is_solid(features[f].category) || !overlaps(box, shape_bounds(features[f].shape))) continue;
            if (overlaps_shape(pose, features[f].shape)) return FeatureRef{a, f};
        }
    }
    return std::nullopt;
}

PathFeatures path_features(const GameState& state, const Unit& unit) {
    PathFeatures out;
    for (const TerrainArea& area : state.terrain) {
        for (const TerrainFeature& f : area.features) {
            if (blocks_move(f, unit)) out.uncrossable.push_back(PathFeature{&f.shape, feature_label(f)});
            if (is_solid(f.category)) out.no_end.push_back(PathFeature{&f.shape, feature_label(f)});
        }
    }
    return out;
}

bool is_hidden(const GameState& state, const Unit& unit, const Model& model) {
    if (!takes_cover_and_hides(unit) || unit.ranged_attacks.this_turn || unit.ranged_attacks.previous_turn) return false;
    const BasePose pose = pose_of(model);
    return std::any_of(state.terrain.begin(), state.terrain.end(),
                       [&](const TerrainArea& a) { return has_dense(a) && model_within_area(pose, a); });
}

// ---- TerrainView ----

TerrainView::TerrainView(const GameState& state) : state_(state) {
    for (std::size_t i = 0; i < state_.terrain.size(); ++i) {
        const TerrainArea& a = state_.terrain[i];
        const std::uint32_t bit = std::uint32_t{1} << i;
        if (is_obscuring(a)) obscuring_ |= bit;
        if (has_dense(a)) dense_ |= bit;
        bounds_.push_back(shape_bounds(a.shape));
    }
}

std::uint32_t TerrainView::within_mask(const Model& model) {
    const auto it = within_.find(model.id.v);
    if (it != within_.end()) return it->second;
    std::uint32_t mask = 0;
    const BasePose pose = pose_of(model);
    const Rect box = pose_box(pose);
    for (std::size_t i = 0; i < state_.terrain.size(); ++i) {
        if (overlaps(box, bounds_[i]) && model_within_area(pose, state_.terrain[i])) mask |= std::uint32_t{1} << i;
    }
    within_.emplace(model.id.v, mask);
    return mask;
}

bool TerrainView::hidden(const Unit& unit, const Model& model) {
    if (!takes_cover_and_hides(unit) || unit.ranged_attacks.this_turn || unit.ranged_attacks.previous_turn) return false;
    return (within_mask(model) & dense_) != 0;
}

std::span<const TerrainShape* const> TerrainView::sight_blockers(std::uint32_t mask) {
    auto it = sight_.find(mask);
    if (it == sight_.end()) {
        std::vector<const TerrainShape*> out;
        for (std::size_t i = 0; i < state_.terrain.size(); ++i) {
            const std::uint32_t bit = std::uint32_t{1} << i;
            const TerrainArea& a = state_.terrain[i];
            if ((mask & bit) == 0) {
                if (obscuring_ & bit) out.push_back(&a.shape);  // 13.10
                continue;
            }
            for (const TerrainFeature& f : a.features) {
                // 13.11 (D-088): the whole footprint, in 2D. TODO(z): only the parts 3" or less from ground level.
                if (f.category == TerrainCategory::Dense) out.push_back(&f.shape);
            }
        }
        it = sight_.emplace(mask, std::move(out)).first;
    }
    return it->second;
}

std::span<const TerrainShape* const> TerrainView::cover_blockers(std::uint32_t mask) {
    auto it = cover_.find(mask);
    if (it == cover_.end()) {
        std::vector<const TerrainShape*> out;
        for (std::size_t i = 0; i < state_.terrain.size(); ++i) {
            const std::uint32_t bit = std::uint32_t{1} << i;
            const TerrainArea& a = state_.terrain[i];
            if ((mask & bit) == 0) {
                if (obscuring_ & bit) out.push_back(&a.shape);  // an intervening obscuring area
                continue;
            }
            for (const TerrainFeature& f : a.features) {
                if (is_solid(f.category)) out.push_back(&f.shape);  // an intervening Light or Dense feature
            }
        }
        it = cover_.emplace(mask, std::move(out)).first;
    }
    return it->second;
}

bool model_visible(TerrainView& view, const Model& observer, const Unit& observed_unit, const Model& observed,
                   std::span<const PosedModel> blockers) {
    const PosedModel from = posed_of(observer);
    const PosedModel to = posed_of(observed);
    if (view.empty()) return has_line_of_sight(from, to, blockers);
    // 13.09: a hidden model can only be visible to enemy models within its detection range (15", D-009 within).
    if (view.hidden(observed_unit, observed) && more_than(base_distance(from.pose(), to.pose()), kDetectionRangeMm)) {
        return false;
    }
    const std::uint32_t mask = view.within_mask(observer) | view.within_mask(observed);
    return has_line_of_sight(from, to, blockers, view.sight_blockers(mask));
}

VisibilityProblem visibility_problem(const GameState& state, const Model& observer, const Unit& target) {
    const Unit* own = find_unit_of_model(state, observer.id);
    const std::vector<PosedModel> blockers = model_blockers(state, own, target);
    TerrainView view(state);
    bool all_hidden_far = !target.models.empty();
    bool line_without_terrain = false;
    const PosedModel from = posed_of(observer);
    for (const Model& m : target.models) {
        if (model_visible(view, observer, target, m, blockers)) return {};
        const bool hidden_far = !view.empty() && view.hidden(target, m) &&
                                more_than(base_distance(from.pose(), pose_of(m)), kDetectionRangeMm);
        if (!hidden_far) {
            all_hidden_far = false;
            if (!line_without_terrain && !view.empty()) line_without_terrain = has_line_of_sight(from, posed_of(m), blockers);
        }
    }
    VisibilityProblem p;
    if (all_hidden_far) {
        p.kind = VisibilityProblemKind::Hidden;
        p.text = "13.09: " + unit_label(target.id) + " is hidden (within a terrain area with a dense terrain feature, "
                 "and it made no ranged attacks this turn or last) and " + model_label(observer.id) +
                 " is not within its 15\" detection range";
    } else if (line_without_terrain) {
        p.kind = VisibilityProblemKind::Terrain;
        p.text = "13.10 / 13.11: every line of sight from " + model_label(observer.id) + " to " +
                 unit_label(target.id) + " crosses an obscuring terrain area or a dense terrain feature";
    } else {
        p.kind = VisibilityProblemKind::Models;
        p.text = "06.01: " + unit_label(target.id) + " is not visible to " + model_label(observer.id);
    }
    return p;
}

bool benefit_of_cover(TerrainView& view, const Model& attacker, const Unit& target) {
    if (view.empty() || target.models.empty()) return false;
    const bool takes_cover = takes_cover_and_hides(target);
    const PosedModel from = posed_of(attacker);
    const std::uint32_t attacker_mask = view.within_mask(attacker);
    for (const Model& m : target.models) {
        const std::uint32_t mask = view.within_mask(m);
        if (takes_cover && mask != 0) continue;  // INFANTRY/BEASTS/SWARM within a terrain area
        // Not fully visible due to intervening terrain (D-088).
        if (fully_visible_past(from, posed_of(m), view.cover_blockers(attacker_mask | mask))) return false;
    }
    return true;
}

bool benefit_of_cover(const GameState& state, const Model& attacker, const Unit& target) {
    TerrainView view(state);
    return benefit_of_cover(view, attacker, target);
}

}  // namespace wh40k::core
