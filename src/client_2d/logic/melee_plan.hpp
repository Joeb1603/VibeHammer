#pragma once

// The melee editor's model: one melee weapon profile and target for the whole unit by default, expanded
// into a declaration for every model that can fight (D-021); a single model can be overridden, including
// splitting its attacks (04.02). Choices come from core's melee_options (D-024).

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
};

class MeleePlan {
public:
    MeleePlan(const core::GameState& state, core::UnitId unit);

    [[nodiscard]] core::UnitId unit() const noexcept { return unit_; }
    [[nodiscard]] const std::vector<MeleeOption>& options() const noexcept { return options_; }
    [[nodiscard]] std::vector<MeleeOption> options_for(core::ModelId model) const;
    [[nodiscard]] std::vector<MeleeChoice> choices() const;

    [[nodiscard]] const std::optional<MeleeChoice>& unit_default() const noexcept { return default_; }
    void set_default(const MeleeChoice& choice) { default_ = choice; }

    void override_model(core::ModelId model, core::MeleeDeclaration declaration);
    void clear_override(core::ModelId model) { overrides_.erase(model); }
    [[nodiscard]] bool is_overridden(core::ModelId model) const { return overrides_.count(model) > 0; }

    // Every model that can fight, in unit order: its override, else the default if it is one of its
    // options, else its first option.
    [[nodiscard]] core::action::Fight expand() const;
    [[nodiscard]] std::optional<core::MeleeDeclaration> declaration_for(core::ModelId model) const;

private:
    core::UnitId unit_;
    std::vector<core::ModelId> models_;
    std::vector<MeleeOption> options_;
    std::optional<MeleeChoice> default_;
    std::map<core::ModelId, core::MeleeDeclaration> overrides_;
};

}  // namespace wh40k::client
