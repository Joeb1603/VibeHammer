#pragma once

// The melee editor's model: one melee weapon profile and target for the whole unit by default, expanded
// into a declaration for every model that can fight (D-021); a single model can be overridden, including
// splitting its attacks (04.02). Choices come from core's melee_options (D-024).
//
// D-071 (M60):
// - A model with [EXTRA ATTACKS] weapons fights with all of them as well as its main weapon (24.11). The plan
//   picks each model's main weapon as before (its override, the unit default, ...) from its other melee
//   weapons, or from its [EXTRA ATTACKS] weapons if it can use no other; the rest are added at the main
//   weapon's target where they reach it.
// - A model without the unit default fights with its own weapon at the unit default's target if it is engaged
//   with it, else at another target; a non-[HAZARDOUS] profile is preferred where there is one.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/rules/fight_types.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

struct MeleeChoice {
    std::string weapon;
    std::size_t profile_index = 0;
    core::UnitId target;
    friend bool operator==(const MeleeChoice&, const MeleeChoice&) = default;
};

struct MeleeOption {
    core::MeleeDeclaration declaration;  // a single target with all of the weapon's attacks
    std::string weapon;
    std::string profile;
    bool extra_attacks = false;  // 24.11: the weapon is an [EXTRA ATTACKS] weapon
    bool hazardous = false;      // 24.15: avoided by the default where possible (D-071)
};

class MeleePlan {
public:
    MeleePlan(const core::GameState& state, core::UnitId unit);

    [[nodiscard]] core::UnitId unit() const noexcept { return unit_; }
    [[nodiscard]] const std::vector<MeleeOption>& options() const noexcept { return options_; }
    [[nodiscard]] std::vector<MeleeOption> options_for(core::ModelId model) const;
    // The options a model's main weapon is picked from: its other melee weapons' options, or its [EXTRA ATTACKS]
    // weapons' if it is offered no other (24.11, D-071). Every option for a model without [EXTRA ATTACKS].
    [[nodiscard]] std::vector<MeleeOption> main_options_for(core::ModelId model) const;
    // The distinct unit-wide choices over the models' main options.
    [[nodiscard]] std::vector<MeleeChoice> choices() const;

    [[nodiscard]] const std::optional<MeleeChoice>& unit_default() const noexcept { return default_; }
    void set_default(const MeleeChoice& choice) { default_ = choice; }

    // Replaces the model's main declaration (its [EXTRA ATTACKS] weapons are still added, 24.11).
    void override_model(core::ModelId model, core::MeleeDeclaration declaration);
    void clear_override(core::ModelId model) { overrides_.erase(model); }
    [[nodiscard]] bool is_overridden(core::ModelId model) const { return overrides_.count(model) > 0; }

    // Every model that can fight, in unit order, each with declarations_for.
    [[nodiscard]] core::action::Fight expand() const;
    // The model's main declaration: its override, else the default if it is one of its main options, else its
    // first main option at the default's target, else its first main option (non-[HAZARDOUS] first).
    [[nodiscard]] std::optional<core::MeleeDeclaration> declaration_for(core::ModelId model) const;
    // 24.11: the model's [EXTRA ATTACKS] weapons other than its main one, each at the main declaration's first
    // target if it reaches it, else at its first target.
    [[nodiscard]] std::vector<core::MeleeDeclaration> extra_declarations_for(core::ModelId model) const;
    // declaration_for, then extra_declarations_for.
    [[nodiscard]] std::vector<core::MeleeDeclaration> declarations_for(core::ModelId model) const;

private:
    core::UnitId unit_;
    std::vector<core::ModelId> models_;
    std::vector<MeleeOption> options_;
    std::optional<MeleeChoice> default_;
    std::map<core::ModelId, core::MeleeDeclaration> overrides_;
};

}  // namespace wh40k::client
