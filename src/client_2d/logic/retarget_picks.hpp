#pragma once

// M93 (D-093, 04.03.03): the attacker's new targets for weapons whose target is gone, picked per weapon in the client
// before they are submitted as one RetargetAttacks. Board-free and raylib-free: the action bar draws one row per
// weapon with its eligible units (from core's retarget_choices) and "lost". Client state only.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

struct RetargetRow {
    std::size_t selection = 0;              // index into the attack's WeaponSelections
    std::string weapon;                     // "Intercessor (5): Bolt Rifle - Focused Fire"
    std::vector<core::UnitId> options;      // eligible new targets (may be empty: the weapon is lost)
    std::optional<std::size_t> chosen;      // index into options; nullopt: lost
    friend bool operator==(const RetargetRow&, const RetargetRow&) = default;
};

class RetargetPicks {
public:
    RetargetPicks() = default;
    // The pending RetargetAttacks decision's weapons, each given its first option. Inactive for any other decision.
    explicit RetargetPicks(const core::GameState& state);

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const std::vector<RetargetRow>& rows() const noexcept { return rows_; }
    // Picks option `option` of row `row`, or none (lost). Out-of-range indices are ignored.
    void choose(std::size_t row, std::optional<std::size_t> option);
    // Every weapon that can target `unit` at it, the others lost.
    void all_at(core::UnitId unit);
    // The units any weapon can target, in first-appearance order.
    [[nodiscard]] std::vector<core::UnitId> units() const;
    [[nodiscard]] core::action::RetargetAttacks action() const;

private:
    bool active_ = false;
    std::vector<RetargetRow> rows_;
};

}  // namespace wh40k::client
