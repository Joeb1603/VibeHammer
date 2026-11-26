#include "core/state/stratagems.hpp"

namespace wh40k::core {

std::string_view stratagem_name(Stratagem s) {
    switch (s) {
        case Stratagem::CommandReroll: return "Command Re-roll";
        case Stratagem::InsaneBravery: return "Insane Bravery";
        case Stratagem::FireOverwatch: return "Fire Overwatch";
        case Stratagem::HeroicIntervention: return "Heroic Intervention";
        case Stratagem::Counteroffensive: return "Counteroffensive";
    }
    return "?";
}

std::string_view stratagem_key(Stratagem s) {
    switch (s) {
        case Stratagem::CommandReroll: return "CommandReroll";
        case Stratagem::InsaneBravery: return "InsaneBravery";
        case Stratagem::FireOverwatch: return "FireOverwatch";
        case Stratagem::HeroicIntervention: return "HeroicIntervention";
        case Stratagem::Counteroffensive: return "Counteroffensive";
    }
    return "?";
}

std::optional<Stratagem> stratagem_from_key(std::string_view key) {
    for (const Stratagem s : {Stratagem::CommandReroll, Stratagem::InsaneBravery, Stratagem::FireOverwatch,
                              Stratagem::HeroicIntervention, Stratagem::Counteroffensive}) {
        if (stratagem_key(s) == key) return s;
    }
    return std::nullopt;
}

std::string_view stratagem_rule(Stratagem s) {
    switch (s) {
        case Stratagem::CommandReroll: return "15.02";
        case Stratagem::InsaneBravery: return "15.04";
        case Stratagem::FireOverwatch: return "15.08";
        case Stratagem::HeroicIntervention: return "15.11";
        case Stratagem::Counteroffensive: return "15.12";
    }
    return "?";
}

int stratagem_cost(Stratagem s) {
    switch (s) {
        case Stratagem::CommandReroll: return 1;       // 15.02: 1CP
        case Stratagem::InsaneBravery: return 1;       // 15.04: 1CP
        case Stratagem::FireOverwatch: return 1;       // 15.08: 1CP
        case Stratagem::HeroicIntervention: return 1;  // 15.11: 1CP (Into the Fray: +1CP, stratagem_use_cost)
        case Stratagem::Counteroffensive: return 2;    // 15.12: 2CP
    }
    return 0;
}

bool once_per_battle(Stratagem s) { return s == Stratagem::InsaneBravery; }  // 15.04 RESTRICTIONS

std::string_view stratagem_mode_name(StratagemMode m) {
    switch (m) {
        case StratagemMode::LeapToDefend: return "Leap to Defend";
        case StratagemMode::IntoTheFray: return "Into the Fray";
    }
    return "?";
}

std::string_view stratagem_mode_key(StratagemMode m) {
    switch (m) {
        case StratagemMode::LeapToDefend: return "LeapToDefend";
        case StratagemMode::IntoTheFray: return "IntoTheFray";
    }
    return "?";
}

std::optional<StratagemMode> stratagem_mode_from_key(std::string_view key) {
    for (const StratagemMode m : {StratagemMode::LeapToDefend, StratagemMode::IntoTheFray}) {
        if (stratagem_mode_key(m) == key) return m;
    }
    return std::nullopt;
}

int stratagem_use_cost(Stratagem s, std::optional<StratagemMode> mode) {
    // 15.11: Into the Fray's section carries +1CP; FAQ Q45: its original cost is then 2CP, not a modified 1CP.
    return stratagem_cost(s) + (s == Stratagem::HeroicIntervention && mode == StratagemMode::IntoTheFray ? 1 : 0);
}

bool has_modes(Stratagem s) { return s == Stratagem::HeroicIntervention; }

std::vector<Side> sequence_sides(Side active, std::array<bool, 2> has_rule) {
    std::vector<Side> out;
    for (const Side s : {active, opponent(active)}) {
        if (has_rule[side_index(s)]) out.push_back(s);  // 01.03.02: active player first, then the opposing player
    }
    return out;
}

}  // namespace wh40k::core
