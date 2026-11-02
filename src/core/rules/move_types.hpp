#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace wh40k::core {

// 09.02 Select Move Type (the V1 move types; disembark/ingress are post-V1).
enum class MoveType : std::uint8_t {
    RemainStationary,  // 09.04
    Normal,            // 09.05
    Advance,           // 09.06
    FallBack,          // 09.07
    Charge,            // 11.04 (Charge phase only; not selectable in 09.02)
    PileIn,            // 12.03 (Fight phase only)
    Consolidation,     // 12.08 (Fight phase only)
};

// 09.07 Before Moving: select fall-back mode.
enum class FallBackMode : std::uint8_t {
    OrderedRetreat,   // only if the unit is not battle-shocked
    DesperateEscape,  // hazard roll per model; may move through enemy models; battle-shock roll after
};

// 12.08 Before Moving: select consolidation mode (mandatory, in this priority order).
enum class ConsolidationMode : std::uint8_t {
    Ongoing,    // the unit is engaged: every enemy unit it is engaged with
    Engaging,   // otherwise, one or more enemy units within 3"
    Objective,  // otherwise, one objective within 3" (V1 has no objectives)
};

[[nodiscard]] std::string_view move_type_name(MoveType t);  // also the JSON name
[[nodiscard]] std::string_view move_type_rule(MoveType t);  // "09.05"
[[nodiscard]] std::optional<MoveType> move_type_from_name(std::string_view name);
[[nodiscard]] std::string_view fall_back_mode_name(FallBackMode m);
[[nodiscard]] std::optional<FallBackMode> fall_back_mode_from_name(std::string_view name);
[[nodiscard]] std::string_view consolidation_mode_name(ConsolidationMode m);
[[nodiscard]] std::optional<ConsolidationMode> consolidation_mode_from_name(std::string_view name);

}  // namespace wh40k::core
