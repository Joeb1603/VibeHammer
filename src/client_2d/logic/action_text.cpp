#include "action_text.hpp"

#include <type_traits>
#include <variant>

#include "core/rules/fight_types.hpp"
#include "core/rules/move_types.hpp"
#include "core/rules/shooting_types.hpp"
#include "unit_style.hpp"

namespace wh40k::client {

namespace {

std::string unit_name(const core::GameState& state, core::UnitId id) {
    // D-065: "Termagants 2 (5)" when a side has several units; "Termagants (5)" otherwise, as before.
    const core::Unit* u = state.find_unit(id);
    return (u != nullptr ? unit_display_name(state, id) : std::string("Unit")) + " (" + std::to_string(id.v) + ")";
}

std::string units(const core::GameState& state, const std::vector<core::UnitId>& ids) {
    std::string out;
    for (const core::UnitId id : ids) out += (out.empty() ? "" : " + ") + unit_name(state, id);
    return out;
}

}  // namespace

std::string action_label(const core::GameState& state, const core::GameAction& action) {
    namespace a = core::action;
    return std::visit(
        [&](const auto& x) -> std::string {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, a::StartMove>) {
                std::string s = std::string(core::move_type_name(x.move_type)) + ": " + unit_name(state, x.unit);
                if (x.fall_back_mode) s += " (" + std::string(core::fall_back_mode_name(*x.fall_back_mode)) + ")";
                // D-045 / D-005: an Advance roll made earlier this phase is kept, not rolled again.
                const core::Unit* u = state.find_unit(x.unit);
                if (x.move_type == core::MoveType::Advance && u != nullptr && u->phase_flags.advance_roll) {
                    s += " (advance roll kept: +" + std::to_string(*u->phase_flags.advance_roll) + "\")";
                }
                return s;
            } else if constexpr (std::is_same_v<T, a::ProposeModelMove>) {
                return "Move model " + std::to_string(x.model.v);
            } else if constexpr (std::is_same_v<T, a::CommitUnitMove>) {
                return "Commit move";
            } else if constexpr (std::is_same_v<T, a::ResetUnitMove>) {
                return "Reset move";
            } else if constexpr (std::is_same_v<T, a::Shoot>) {
                return std::string(core::shooting_type_name(x.type)) + " shooting: " + unit_name(state, x.unit);
            } else if constexpr (std::is_same_v<T, a::EndShootStep>) {
                return "End Shooting phase";
            } else if constexpr (std::is_same_v<T, a::DeclareCharge>) {
                return "Declare charge: " + unit_name(state, x.unit);
            } else if constexpr (std::is_same_v<T, a::StartChargeMove>) {
                return "Charge " + units(state, x.targets);
            } else if constexpr (std::is_same_v<T, a::SkipChargeMove>) {
                return "Make no charge move";
            } else if constexpr (std::is_same_v<T, a::EndChargeStep>) {
                return "End Charge phase";
            } else if constexpr (std::is_same_v<T, a::StartPileIn>) {
                return "Pile in: " + unit_name(state, x.unit) + " towards " + units(state, x.targets);
            } else if constexpr (std::is_same_v<T, a::SkipPileIn>) {
                return "No overrun pile-in";
            } else if constexpr (std::is_same_v<T, a::EndPileIns>) {
                return "End pile-in moves";
            } else if constexpr (std::is_same_v<T, a::SelectToFight>) {
                return "Fight (" + std::string(core::fight_type_name(x.type)) + "): " + unit_name(state, x.unit);
            } else if constexpr (std::is_same_v<T, a::PassFight>) {
                return "Pass";
            } else if constexpr (std::is_same_v<T, a::Fight>) {
                return "Fight with " + unit_name(state, x.unit);
            } else if constexpr (std::is_same_v<T, a::StartConsolidation>) {
                return "Consolidate: " + unit_name(state, x.unit) + " towards " + units(state, x.targets);
            } else if constexpr (std::is_same_v<T, a::EndConsolidations>) {
                return "End consolidation moves";
            } else if constexpr (std::is_same_v<T, a::RemoveModelForCoherency>) {
                return "Remove model " + std::to_string(x.model.v);
            } else if constexpr (std::is_same_v<T, a::SelectAttackBatch>) {
                return "Resolve attack batch " + std::to_string(x.batch);
            } else if constexpr (std::is_same_v<T, a::DeclareAllocationOrder>) {
                std::string order;
                for (const auto i : x.order) order += (order.empty() ? "" : ", ") + std::to_string(i);
                return "Allocation order: groups " + order;
            } else if constexpr (std::is_same_v<T, a::OrderCasualties>) {
                return "Order casualties: " + unit_name(state, x.target);
            } else {
                static_assert(sizeof(T) == 0, "unhandled action");
            }
        },
        action);
}

}  // namespace wh40k::client
