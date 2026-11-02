#pragma once

#include <optional>

namespace wh40k::core {

// 02.02 Profiles. Characteristics a datasheet can show as '-' are optional.
// Dice-result characteristics (Sv, InSv, Ld) store the target number: Sv 3+ => 3.
struct Characteristics {
    std::optional<float> move_mm;  // M; '-' => cannot move after set up (02.02)
    int toughness = 0;             // T
    std::optional<int> save;       // Sv
    std::optional<int> invuln_save;  // InSv; most models have none
    int wounds = 0;                // W
    std::optional<int> leadership;  // Ld
    std::optional<int> objective_control;  // OC; '-' => cannot control objectives

    friend bool operator==(const Characteristics&, const Characteristics&) = default;
};

}  // namespace wh40k::core
