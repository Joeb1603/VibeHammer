#pragma once

// M61 (D-072): the attacker's choice of a [TWIN-LINKED] batch's wound rolls to re-roll (24.38), made in the client
// before it is submitted as one RerollWoundRolls. Board-free and raylib-free: the pop-up draws these dice and
// toggles them; the action bar offers "Re-roll all failed", "Re-roll selected" and "Keep". Client state only.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"

namespace wh40k::client {

struct RerollDie {
    int value = 0;          // the unmodified roll (1-6)
    int modified = 0;       // after the batch's wound modifier
    bool failed = false;    // 05.02: the roll failed (highlighted)
    bool critical = false;  // an unmodified 6
    bool selected = false;  // picked to be re-rolled
    friend bool operator==(const RerollDie&, const RerollDie&) = default;
};

class RerollPicks {
public:
    RerollPicks() = default;
    // The pending RerollWoundRolls decision's dice, none selected. Inactive when that is not the decision.
    explicit RerollPicks(const core::GameState& state);

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] core::UnitId attacker() const noexcept { return attacker_; }
    [[nodiscard]] const std::vector<RerollDie>& dice() const noexcept { return dice_; }
    [[nodiscard]] int required() const noexcept { return required_; }  // the 05.02 result needed to wound
    // "Hive Tyrant fighting Havocs (Monstrous bonesword and lash whip)", as the dice cards say it.
    [[nodiscard]] const std::string& purpose() const noexcept { return purpose_; }
    [[nodiscard]] std::size_t failed_count() const;

    // A click on a die: picks it, or drops it again. Out-of-range indices are ignored.
    void toggle(std::size_t index);
    void select_failed();  // exactly the failed dice
    void clear();          // none

    // The picked dice (ascending), and the actions: the picks, every failed die, none.
    [[nodiscard]] std::vector<std::size_t> selected() const;
    [[nodiscard]] core::action::RerollWoundRolls action() const;
    [[nodiscard]] core::action::RerollWoundRolls all_failed() const;
    [[nodiscard]] static core::action::RerollWoundRolls keep() { return {}; }
    // Whether the picks are other than "every failed die" and "none" (then "Re-roll selected" is worth showing).
    [[nodiscard]] bool custom() const;

private:
    bool active_ = false;
    core::UnitId attacker_{};
    int required_ = 0;
    std::string purpose_;
    std::vector<RerollDie> dice_;
};

}  // namespace wh40k::client
