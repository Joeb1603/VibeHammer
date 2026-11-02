#pragma once

// The shooting editor's model (D-001 UI rule): one weapon profile and target for the whole unit by
// default, expanded into per-model declarations; a single model can be overridden. The choices come from
// core's shot_options (D-024); the expanded action is validated by the engine before it is applied.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/rules/shooting_types.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

// A unit-wide choice: the weapon by name (models carry their own copies), a profile and a target.
struct ShotChoice {
    std::string weapon;
    std::size_t profile_index = 0;
    core::UnitId target;
    friend bool operator==(const ShotChoice&, const ShotChoice&) = default;
};

// One valid single declaration and its display names.
struct ShotOption {
    core::ShotDeclaration declaration;
    std::string weapon;
    std::string profile;
};

class ShotPlan {
public:
    ShotPlan(const core::GameState& state, core::UnitId unit, core::ShootingType type);

    [[nodiscard]] core::UnitId unit() const noexcept { return unit_; }
    [[nodiscard]] core::ShootingType type() const noexcept { return type_; }
    [[nodiscard]] const std::vector<ShotOption>& options() const noexcept { return options_; }
    [[nodiscard]] std::vector<ShotOption> options_for(core::ModelId model) const;
    // The distinct unit-wide choices (weapon, profile, target) available to at least one model.
    [[nodiscard]] std::vector<ShotChoice> choices() const;

    [[nodiscard]] const std::optional<ShotChoice>& unit_default() const noexcept { return default_; }
    void set_default(const ShotChoice& choice) { default_ = choice; }

    // A single model's own choice (nullopt: it does not fire), replacing the unit default for it.
    void override_model(core::ModelId model, std::optional<core::ShotDeclaration> choice);
    void clear_override(core::ModelId model) { overrides_.erase(model.v); }
    [[nodiscard]] bool is_overridden(core::ModelId model) const { return overrides_.count(model.v) > 0; }

    // Every model in unit order: its override, else the default if it is one of its options, else nothing.
    [[nodiscard]] core::action::Shoot expand() const;
    // What `model` will do in expand() (nullopt: nothing).
    [[nodiscard]] std::optional<core::ShotDeclaration> declaration_for(core::ModelId model) const;

private:
    core::UnitId unit_;
    core::ShootingType type_;
    std::vector<core::ModelId> models_;
    std::vector<ShotOption> options_;
    std::optional<ShotChoice> default_;
    std::map<std::uint32_t, std::optional<core::ShotDeclaration>> overrides_;
};

}  // namespace wh40k::client
