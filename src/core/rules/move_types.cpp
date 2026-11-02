#include "core/rules/move_types.hpp"

#include <array>
#include <stdexcept>

namespace wh40k::core {

namespace {

struct MoveTypeInfo {
    MoveType type;
    std::string_view name;
    std::string_view rule;
};

constexpr std::array<MoveTypeInfo, 5> kMoveTypes{{
    {MoveType::RemainStationary, "RemainStationary", "09.04"},
    {MoveType::Normal, "Normal", "09.05"},
    {MoveType::Advance, "Advance", "09.06"},
    {MoveType::FallBack, "FallBack", "09.07"},
    {MoveType::Charge, "Charge", "11.04"},
}};

const MoveTypeInfo& info(MoveType t) {
    for (const auto& i : kMoveTypes) {
        if (i.type == t) return i;
    }
    throw std::logic_error("invalid MoveType");
}

}  // namespace

std::string_view move_type_name(MoveType t) { return info(t).name; }
std::string_view move_type_rule(MoveType t) { return info(t).rule; }

std::optional<MoveType> move_type_from_name(std::string_view name) {
    for (const auto& i : kMoveTypes) {
        if (i.name == name) return i.type;
    }
    return std::nullopt;
}

std::string_view fall_back_mode_name(FallBackMode m) {
    return m == FallBackMode::OrderedRetreat ? "OrderedRetreat" : "DesperateEscape";
}

std::optional<FallBackMode> fall_back_mode_from_name(std::string_view name) {
    if (name == "OrderedRetreat") return FallBackMode::OrderedRetreat;
    if (name == "DesperateEscape") return FallBackMode::DesperateEscape;
    return std::nullopt;
}

}  // namespace wh40k::core
