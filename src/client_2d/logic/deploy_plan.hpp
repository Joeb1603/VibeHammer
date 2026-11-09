#pragma once

// M76 (D-082 part 2): placing a unit by hand in the deployment step (D-081). A DeployPlan is the placement in progress
// for one unit of the deploying side: where each of its models would stand and which way an oval faces. It changes
// nothing in the game: the board drags it, and "Place" submits its action() through GameSession, which validates it
// (D-024). There is no rules logic here: whether it can be set up, and which models are at fault, come from core
// (set_up_problem_details, the same check as the engine's validation); the starting spot is core's proposal.
// Deterministic value types; no raylib or ImGui.

#include <optional>
#include <string>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/geometry/pose.hpp"
#include "core/state/deployment.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

class DeployPlan {
public:
    struct Placed {
        core::ModelId model;
        core::Base base;
        core::Position position;
        float facing_deg = 0.0f;  // an oval's footprint facing, in (-90, 90] (D-074); always 0 for a round base
        [[nodiscard]] core::BasePose pose() const { return core::BasePose{position, base, facing_deg}; }
        friend bool operator==(const Placed&, const Placed&) = default;
    };

    // Starts placing `unit`, which must be waiting to be set up (nullopt otherwise): at core's proposal
    // (propose_set_up, Auto-place's spot), or, when core finds no legal spot (CannotSetUp is offered), the unit's
    // formation at the middle of its zone's front line (the fewest coherent ranks, gap 0, facing 0, as the auto-layout
    // measures it), which core refuses: the player may still find a spot by hand (D-081).
    [[nodiscard]] static std::optional<DeployPlan> start(const core::GameState& state, core::UnitId unit);
    // A placement given as an action (one Undo took back): nullopt unless the unit is waiting to be set up and the
    // action places each of its models exactly once.
    [[nodiscard]] static std::optional<DeployPlan> from_action(const core::GameState& state,
                                                               const core::action::DeployUnit& action);

    [[nodiscard]] core::UnitId unit() const noexcept { return unit_; }
    [[nodiscard]] core::Side side() const noexcept { return side_; }
    // Whether it started at core's proposal (false: core found no legal spot for the unit).
    [[nodiscard]] bool from_proposal() const noexcept { return from_proposal_; }
    // The unit's models, in its model order.
    [[nodiscard]] const std::vector<Placed>& models() const noexcept { return models_; }
    [[nodiscard]] const Placed* find(core::ModelId model) const;
    // The mean of the models' positions: the "Move unit" handle, and the centre rotate_block turns round.
    [[nodiscard]] core::Position centre() const;

    // The whole block moves by (dx, dy) mm; its shape and facings are unchanged.
    void translate(float dx_mm, float dy_mm);
    // One model to `to` (z 0). False (nothing changes) if it isn't one of the unit's models.
    bool move_model(core::ModelId model, core::Position to);
    // Turns an oval by `deg` (counter-clockwise positive, D-074), its facing kept as the footprint's, modulo 180
    // (footprint_facing_deg). False (nothing changes) for a round base, which has no facing, or another unit's model.
    bool turn_model(core::ModelId model, float deg);
    // The whole block turns by `deg` round centre(): each position turns with it, and each oval's facing by `deg`
    // (round bases stay at 0).
    void rotate_block(float deg);

    // The model whose base (at its facing) holds `p`, if any; the last in model order when bases overlap.
    [[nodiscard]] std::optional<core::ModelId> model_at(const core::Position& p) const;

    // The DeployUnit that sets the unit up here (every model, in model order).
    [[nodiscard]] core::action::DeployUnit action() const;
    // Core's reasons it can't be set up here, with the models at fault; empty when it can (D-024: the engine's own
    // check, set_up_problem_details).
    [[nodiscard]] std::vector<core::SetUpProblem> problems(const core::GameState& state) const;

    friend bool operator==(const DeployPlan&, const DeployPlan&) = default;

private:
    core::UnitId unit_;
    core::Side side_ = core::Side::A;
    bool from_proposal_ = false;
    std::vector<Placed> models_;
};

// The models named by any of `problems`, in first-mention order (each once): the ones drawn in red.
[[nodiscard]] std::vector<core::ModelId> models_at_fault(const std::vector<core::SetUpProblem>& problems);

// What the engine offers for one waiting unit in this decision (legal_actions, D-004): its proposal ("Auto-place this
// unit"), or, only when core finds no legal spot for it, CannotSetUp ("Can't be set up").
struct DeployChoices {
    std::optional<core::action::DeployUnit> auto_place;
    std::optional<core::action::CannotSetUp> cannot_set_up;
};
[[nodiscard]] DeployChoices deploy_choices(const std::vector<core::GameAction>& legal, core::UnitId unit);

// The lowest-id unit of `side` waiting to be set up (the default selection, as the bots pick, D-081), if any.
[[nodiscard]] std::optional<core::UnitId> first_undeployed(const core::GameState& state, core::Side side);

// "Can't be set up" with its consequence (D-081, D-076): "No legal spot is left for Termagants 2: it counts as
// destroyed and Side A scores its 60 points (D-081, D-076)."
[[nodiscard]] std::string cannot_set_up_text(const core::GameState& state, core::UnitId unit);

// A unit's size and points, as the sidebar lists a unit not set up yet: "10 models, 60 pts" ("1 model"; the points left
// out when it has none).
[[nodiscard]] std::string models_and_points_text(const core::Unit& unit);

}  // namespace wh40k::client
