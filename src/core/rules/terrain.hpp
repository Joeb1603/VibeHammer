#pragma once

// 13 Terrain (M88): read-only rules queries over GameState::terrain, for the engine (movement, set-up, shooting), the
// client and the bots. Terrain is 2D (D-087): areas and features are footprints on the battlefield plane.
// - 13.06 Terrain and Movement (D-087): who moves through which features; no model ends a move or is set up on a
//   Light or Dense feature.
// - 13.09 Hidden, 13.10 Obscuring, 13.11 Solid in 2D and 06.01 Fully Visible (D-088).
// - 13.08 Benefit of Cover (D-089).
// Every query is a function of the state only. Without terrain they answer as if terrain didn't exist.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/geometry/path.hpp"
#include "core/geometry/pose.hpp"
#include "core/state/game_state.hpp"
#include "core/state/terrain.hpp"

namespace wh40k::core {

// ---- Keywords ----

// 13.08 / 13.09: the unit has the INFANTRY, BEASTS or SWARM keyword (it takes cover within a terrain area and can be
// hidden).
[[nodiscard]] bool takes_cover_and_hides(const Unit& unit);
// 13.06: INFANTRY, BEASTS, SWARM or MOBILE models move horizontally through Dense features.
[[nodiscard]] bool moves_through_dense(const Unit& unit);

// ---- Areas and features ----

// 13.10: an area holding one or more Light or Dense features is an obscuring terrain area.
[[nodiscard]] bool is_obscuring(const TerrainArea& area);
// 13.09: an area holding one or more Dense features.
[[nodiscard]] bool has_dense(const TerrainArea& area);
// 13.06 (D-087): `unit`'s models can't move through `feature`: it is Dense and more than 2" tall, and the unit isn't
// INFANTRY/BEASTS/SWARM/MOBILE. (Moving vertically over it is not offered in 2D. TODO(z): 13.06 Moving Vertically.)
[[nodiscard]] bool blocks_move(const TerrainFeature& feature, const Unit& unit);
// D-009 / D-088: some part of the base is within the terrain area.
[[nodiscard]] bool model_within_area(const BasePose& pose, const TerrainArea& area);

// A feature by its area and its place in that area (file order).
struct FeatureRef {
    std::size_t area = 0;
    std::size_t feature = 0;
    friend bool operator==(const FeatureRef&, const FeatureRef&) = default;
};
// The feature `ref` names.
[[nodiscard]] const TerrainFeature& feature_of(const GameState& state, FeatureRef ref);
// For reasons and the client: "'Centre ruin, west wall' (dense, 5.00\" tall)" / "'Barricade' (light)".
[[nodiscard]] std::string feature_label(const TerrainFeature& feature);
// D-087 / 03.01 Ending a Move: the first Light or Dense feature (file order) the base overlaps by more than EPS_MM,
// or nullopt.
[[nodiscard]] std::optional<FeatureRef> solid_feature_under(const GameState& state, const BasePose& pose);

// 13.06 / 03.01 for a move of `unit`'s models: the features they can't move through (blocks_move) and those they
// can't end on (Light and Dense), as PosedPathContext takes them. The shapes point into `state`. Empty without
// terrain.
struct PathFeatures {
    std::vector<PathFeature> uncrossable;
    std::vector<PathFeature> no_end;
};
[[nodiscard]] PathFeatures path_features(const GameState& state, const Unit& unit);

// ---- Visibility (D-088) ----

// 13.09: the model is hidden: its unit is INFANTRY/BEASTS/SWARM, it is within an area holding a Dense feature, and its
// unit made no ranged attacks this turn or the previous turn (Unit::ranged_attacks).
[[nodiscard]] bool is_hidden(const GameState& state, const Unit& unit, const Model& model);

// The terrain facts the visibility queries share, worked out the first time they are needed (one state; the state
// must outlive the view and not change). Up to 32 areas (kMaxTerrainAreas), so a model's areas fit one mask.
class TerrainView {
public:
    explicit TerrainView(const GameState& state);
    [[nodiscard]] const GameState& state() const noexcept { return state_; }
    [[nodiscard]] bool empty() const noexcept { return state_.terrain.empty(); }
    // Bit i: the model is within area i (D-009).
    [[nodiscard]] std::uint32_t within_mask(const Model& model);
    // 13.09 for the model of `unit`.
    [[nodiscard]] bool hidden(const Unit& unit, const Model& model);
    // 13.10 and 13.11 for two models within the areas `mask` (both masks or'ed): the obscuring areas neither is within
    // and the Dense features of the areas either is within. (A Dense feature of any other area is inside that
    // obscuring area, which blocks every line the feature would: D-087 keeps features wholly within their areas.)
    [[nodiscard]] std::span<const TerrainShape* const> sight_blockers(std::uint32_t mask);
    // D-088 Fully Visible (13.08 "intervening terrain"): the obscuring areas neither model is within and the Light and
    // Dense features of the areas either is within (the others are inside those obscuring areas, as above).
    [[nodiscard]] std::span<const TerrainShape* const> cover_blockers(std::uint32_t mask);

private:
    const GameState& state_;
    std::uint32_t obscuring_ = 0;  // the obscuring areas
    std::uint32_t dense_ = 0;      // the areas with a Dense feature
    std::vector<Rect> bounds_;     // each area's box
    std::map<std::uint32_t, std::uint32_t> within_;                    // by model id
    std::map<std::uint32_t, std::vector<const TerrainShape*>> sight_;  // by mask
    std::map<std::uint32_t, std::vector<const TerrainShape*>> cover_;  // by mask
};

// 06.01 with 13.09-13.11 (D-088): `observer` can see `observed` (a model of `observed_unit`) past the model
// `blockers` (06.01: not those of the two units) and the terrain: a hidden model is visible only within 15" (edge to
// edge, D-009), and then only with a line of sight past the sight_blockers. Without terrain: has_line_of_sight.
[[nodiscard]] bool model_visible(TerrainView& view, const Model& observer, const Unit& observed_unit,
                                 const Model& observed, std::span<const PosedModel> blockers);

// Why `target` (an enemy unit) is not visible to `observer`, for the reasons the engine gives and the client shows.
enum class VisibilityProblemKind : std::uint8_t {
    None,     // visible
    Hidden,   // 13.09: every model of the target is hidden and further than 15" from the observer
    Terrain,  // 13.10 / 13.11: a line of sight would exist without terrain
    Models,   // 06.01: other models block every line of sight
};
struct VisibilityProblem {
    VisibilityProblemKind kind = VisibilityProblemKind::None;
    std::string text;  // rule-cited; empty when None
};
[[nodiscard]] VisibilityProblem visibility_problem(const GameState& state, const Model& observer, const Unit& target);

// 13.08 (D-089): `target` has the benefit of cover against ranged attacks by `attacker`: every model of the target is
// INFANTRY/BEASTS/SWARM within a terrain area (any category), or is not fully visible to `attacker` due to the
// intervening terrain (D-088 Fully Visible past the cover_blockers; models don't count). False without terrain or
// models.
[[nodiscard]] bool benefit_of_cover(TerrainView& view, const Model& attacker, const Unit& target);
[[nodiscard]] bool benefit_of_cover(const GameState& state, const Model& attacker, const Unit& target);

}  // namespace wh40k::core
