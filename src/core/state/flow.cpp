#include "core/state/flow.hpp"

#include <array>
#include <stdexcept>

namespace wh40k::core {

namespace {

struct StageInfo {
    Stage stage;
    std::string_view name;
    std::string_view rule;
    Phase phase;
};

constexpr std::array<StageInfo, kStageCount> kStages{{
    {Stage::NotStarted, "NotStarted", "07.01", Phase::None},
    {Stage::StartOfBattleRound, "StartOfBattleRound", "07.01", Phase::None},
    {Stage::StartOfTurn, "StartOfTurn", "07.02", Phase::None},
    {Stage::StartOfCommandPhase, "StartOfCommandPhase", "08.01", Phase::Command},
    {Stage::GainCoreCP, "GainCoreCP", "08.02", Phase::Command},
    {Stage::BattleShock, "BattleShock", "08.03", Phase::Command},
    {Stage::CommandAbilities, "CommandAbilities", "08.04", Phase::Command},
    {Stage::EndOfCommandPhase, "EndOfCommandPhase", "08.05", Phase::Command},
    {Stage::StartOfMovementPhase, "StartOfMovementPhase", "09.01", Phase::Movement},
    {Stage::MoveUnits, "MoveUnits", "09.02", Phase::Movement},
    {Stage::EndOfMovementPhase, "EndOfMovementPhase", "09.03", Phase::Movement},
    {Stage::StartOfShootingPhase, "StartOfShootingPhase", "10.01", Phase::Shooting},
    {Stage::Shoot, "Shoot", "10.02", Phase::Shooting},
    {Stage::EndOfShootingPhase, "EndOfShootingPhase", "10.03", Phase::Shooting},
    {Stage::StartOfChargePhase, "StartOfChargePhase", "11.01", Phase::Charge},
    {Stage::Charge, "Charge", "11.02", Phase::Charge},
    {Stage::EndOfChargePhase, "EndOfChargePhase", "11.03", Phase::Charge},
    {Stage::StartOfFightPhase, "StartOfFightPhase", "12.01", Phase::Fight},
    {Stage::PileIn, "PileIn", "12.02", Phase::Fight},
    {Stage::Fight, "Fight", "12.04", Phase::Fight},
    {Stage::Consolidate, "Consolidate", "12.07", Phase::Fight},
    {Stage::EndOfFightPhase, "EndOfFightPhase", "12.09", Phase::Fight},
    {Stage::EndOfTurn, "EndOfTurn", "07.02", Phase::None},
    {Stage::EndOfBattleRound, "EndOfBattleRound", "07.03", Phase::None},
    {Stage::GameOver, "GameOver", "D-007", Phase::None},
    {Stage::Deploy, "Deploy", "03.02", Phase::None},  // D-081 (M74)
}};

const StageInfo& info(Stage stage) {
    const auto i = static_cast<std::size_t>(stage);
    if (i >= kStages.size() || kStages[i].stage != stage) {
        throw std::logic_error("flow: invalid Stage");
    }
    return kStages[i];
}

struct PhaseInfo {
    Phase phase;
    std::string_view name;
    std::string_view rule;
};

constexpr std::array<PhaseInfo, 6> kPhases{{
    {Phase::None, "None", "07"},
    {Phase::Command, "Command", "08"},
    {Phase::Movement, "Movement", "09"},
    {Phase::Shooting, "Shooting", "10"},
    {Phase::Charge, "Charge", "11"},
    {Phase::Fight, "Fight", "12"},
}};

const PhaseInfo& info(Phase phase) {
    const auto i = static_cast<std::size_t>(phase);
    if (i >= kPhases.size() || kPhases[i].phase != phase) {
        throw std::logic_error("flow: invalid Phase");
    }
    return kPhases[i];
}

}  // namespace

Phase phase_of(Stage stage) { return info(stage).phase; }
std::string_view stage_name(Stage stage) { return info(stage).name; }
std::string_view stage_rule(Stage stage) { return info(stage).rule; }
std::string_view phase_name(Phase phase) { return info(phase).name; }
std::string_view phase_rule(Phase phase) { return info(phase).rule; }

std::optional<Stage> stage_from_name(std::string_view name) {
    for (const StageInfo& s : kStages) {
        if (s.name == name) {
            return s.stage;
        }
    }
    return std::nullopt;
}

std::optional<Phase> phase_from_name(std::string_view name) {
    for (const PhaseInfo& p : kPhases) {
        if (p.name == name) {
            return p.phase;
        }
    }
    return std::nullopt;
}

}  // namespace wh40k::core
