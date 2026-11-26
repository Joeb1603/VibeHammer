#pragma once

// D-096 (M95): stratagems (15) and the reaction windows they are used in (01.03.02 Rules Sequencing, app-only). A window
// is a timing at which a side can use one or more of its rules; the engine offers it as a UseStratagems decision only
// when that side can use a stratagem there.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "core/state/decision.hpp"
#include "core/types/ids.hpp"

namespace wh40k::core {

// 15: the stratagems the engine implements. Appended at the end only (JSON names, the bots' state_hash).
enum class Stratagem : std::uint8_t {
    CommandReroll,  // 15.02 Command Re-roll (1CP)
    InsaneBravery,  // 15.04 Insane Bravery (1CP, once per battle)
};

[[nodiscard]] std::string_view stratagem_name(Stratagem s);  // "Command Re-roll", as the rules name it
[[nodiscard]] std::string_view stratagem_key(Stratagem s);   // "CommandReroll"; also the JSON name
[[nodiscard]] std::optional<Stratagem> stratagem_from_key(std::string_view key);
[[nodiscard]] std::string_view stratagem_rule(Stratagem s);  // "15.02"
[[nodiscard]] int stratagem_cost(Stratagem s);               // its CP cost (15.01)
[[nodiscard]] bool once_per_battle(Stratagem s);             // its RESTRICTIONS say once per battle

// 15.01: one side's record of the stratagems it has used. The per-phase lists end with the phase (D-096: cleared on
// leaving each End of Phase step); the once-per-battle list never ends.
struct StratagemRecord {
    std::vector<Stratagem> used_this_phase;   // 15.01: the same stratagem at most once per phase
    std::vector<UnitId> targeted_this_phase;  // 15.01: one stratagem per unit per phase
    std::vector<Stratagem> used_this_battle;  // the once-per-battle stratagems used (15.04)
    [[nodiscard]] bool empty() const noexcept {
        return used_this_phase.empty() && targeted_this_phase.empty() && used_this_battle.empty();
    }
    friend bool operator==(const StratagemRecord&, const StratagemRecord&) = default;
};

// D-096: an open window. The roll it comes after (AfterRoll) or before (BeforeRoll), for `unit` (its controlling player
// rolls; for a Save the target unit, for a Damage roll the attacker, as the RollDice was). Set only while its
// UseStratagems decision waits, never at another decision.
struct StratagemWindow {
    WindowTiming timing = WindowTiming::AfterRoll;
    RollKind roll = RollKind::Hit;
    UnitId unit;
    std::size_t batch = 0;          // the attack batch the roll is for (attack rolls only; 0 otherwise)
    std::vector<Side> sides;        // 01.03.02: the sides still to answer, in order; the first answers now
    std::vector<int> faces;         // AfterRoll: the roll's dice, as rolled (and as re-rolled)
    std::vector<std::size_t> rerolled{};  // 01.05.02: the dice already re-rolled, ascending (Twin-linked's too)
    friend bool operator==(const StratagemWindow&, const StratagemWindow&) = default;
};

// 01.03.02 Rules Sequencing, for the optional rules (stratagems) of one timing: the active player's first, then the
// opposing player's, each only if they have one to use. (Mandatory rules are the engine's own flow and come first.)
[[nodiscard]] std::vector<Side> sequence_sides(Side active, std::array<bool, 2> has_rule);

}  // namespace wh40k::core
