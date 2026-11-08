#pragma once

// The shooting editor's model (D-001 UI rule): one weapon profile and target for the whole unit by
// default, expanded into per-model declarations; a single model can be overridden. The choices come from
// core's shot_options (D-024); the expanded action is validated by the engine before it is applied.
//
// D-071 (M60): a unit whose models carry different ranged weapons (a mixed loadout, e.g. Havocs with
// lascannons and a champion with a plasma gun) has no one choice that suits every model, so by default each
// model fires its own weapons at the unit default's target (another target only for a weapon that can't
// reach it), keeping 24.07. A unit whose models all carry the same ranged weapons keeps the M12 default
// exactly: the unit-wide choice, and a model that can't make it doesn't fire. The default never picks a
// [HAZARDOUS] profile (24.15): a non-Hazardous profile of the same weapon, or another weapon, instead.

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
    bool hazardous = false;       // 24.15: never part of a default (D-071)
    bool close_quarters = false;  // 24.07 / 24.27: a [CLOSE-QUARTERS] or [PISTOL] profile
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
    // A unit-wide choice picked by the player: every model that can make it does. In a mixed loadout the other
    // models fire their own weapons at its target (D-071).
    void set_default(const ShotChoice& choice) {
        default_ = choice;
        own_weapons_ = false;
    }
    // D-071: the models' different ranged weapons (by name).
    [[nodiscard]] bool mixed_loadout() const noexcept { return mixed_; }
    // D-071: the default is each model's own weapons at the unit default's target (a mixed loadout, before
    // set_default).
    [[nodiscard]] bool own_weapons_default() const noexcept { return own_weapons_; }

    // A single model's own choice (nullopt: it does not fire), replacing the unit default for it.
    void override_model(core::ModelId model, std::optional<core::ShotDeclaration> choice);
    void clear_override(core::ModelId model) { overrides_.erase(model); }
    [[nodiscard]] bool is_overridden(core::ModelId model) const { return overrides_.count(model) > 0; }

    // Every model in unit order, each with declarations_for.
    [[nodiscard]] core::action::Shoot expand() const;
    // What `model` will do in expand(): its override; else, for the unit-wide choice, that choice if it is one
    // of its options; else (a mixed loadout) its own weapons (D-071); else nothing.
    [[nodiscard]] std::vector<core::ShotDeclaration> declarations_for(core::ModelId model) const;
    // The first of declarations_for (nullopt: nothing), for the per-model editor (D-031).
    [[nodiscard]] std::optional<core::ShotDeclaration> declaration_for(core::ModelId model) const;

private:
    // D-071: `model`'s own non-Hazardous weapons, at `target` where they can reach it, keeping 24.07.
    [[nodiscard]] std::vector<core::ShotDeclaration> own_weapons(core::ModelId model, core::UnitId target) const;

    core::UnitId unit_;
    core::ShootingType type_;
    std::vector<core::ModelId> models_;
    std::vector<core::ModelId> one_set_models_;  // 24.07 applies to them (non-MONSTER/VEHICLE, not 10.06)
    std::vector<ShotOption> options_;
    std::optional<ShotChoice> default_;
    bool mixed_ = false;
    bool own_weapons_ = false;
    std::map<core::ModelId, std::optional<core::ShotDeclaration>> overrides_;
};

}  // namespace wh40k::client
