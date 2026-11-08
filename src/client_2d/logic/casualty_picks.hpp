#pragma once

// D-051 (M35): picking casualties on the board, one model at a time. Each pick is the model that takes the
// next attack (05.04) or mortal wound (06.02); a picked model keeps taking the damage forced onto it. Core's
// preview_casualties says who may be picked next and what each pick does, so there are no rules here
// (D-024). The answer is still one OrderCasualties (D-013): the picks, then the rest of the unit in the default
// order (D-058, M47: default_casualty_order continues from the picks).

#include <optional>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/engine/casualties.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

// What the picks so far do to one model.
struct CasualtyMark {
    core::ModelId model;
    int wounds_lost = 0;
    bool destroyed = false;
    // D-069: a random D's damage, at its maximum (the preview's stand-in): "up to" wounds_lost (drawn "-7?").
    bool stand_in = false;
    // D-069: the model is first reached after an earlier random roll, so with a lower roll it may not be (thin ring).
    bool provisional = false;
    friend bool operator==(const CasualtyMark&, const CasualtyMark&) = default;
};

class CasualtyPicks {
public:
    CasualtyPicks(const core::GameState& state, core::UnitId target);

    [[nodiscard]] core::UnitId target() const noexcept { return target_; }
    [[nodiscard]] const std::vector<core::ModelId>& picks() const noexcept { return picks_; }
    // Core's preview for the picks so far; nullopt only if no casualty decision is pending.
    [[nodiscard]] const std::optional<core::CasualtyPreview>& preview() const noexcept { return preview_; }
    // Who may be picked next (empty when done).
    [[nodiscard]] std::vector<core::ModelId> options() const;
    [[nodiscard]] bool can_pick(core::ModelId model) const;
    // Every damaging step is resolved: the casualties can be confirmed.
    [[nodiscard]] bool done() const noexcept { return preview_ && preview_->done(); }

    // Picks `model` if it may be picked next; whether it was.
    bool pick(const core::GameState& state, core::ModelId model);
    // Takes back the last pick; whether there was one.
    bool undo_pick(const core::GameState& state);

    // Per model, in the order first hit: the wounds the picks so far take off, and whether it dies.
    [[nodiscard]] std::vector<CasualtyMark> marks() const;
    // The answer: the picks, then the unit's other models in the default order (D-058; D-013: every model once).
    [[nodiscard]] core::action::OrderCasualties action() const;

private:
    core::UnitId target_;
    std::vector<core::ModelId> picks_;
    std::vector<core::ModelId> order_;  // default_casualty_order(state, target_, picks_), kept with the picks
    std::optional<core::CasualtyPreview> preview_;
};

}  // namespace wh40k::client
