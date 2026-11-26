#include "core/state/stratagems.hpp"

namespace wh40k::core {

std::string_view stratagem_name(Stratagem s) {
    switch (s) {
        case Stratagem::CommandReroll: return "Command Re-roll";
        case Stratagem::InsaneBravery: return "Insane Bravery";
    }
    return "?";
}

std::string_view stratagem_key(Stratagem s) {
    switch (s) {
        case Stratagem::CommandReroll: return "CommandReroll";
        case Stratagem::InsaneBravery: return "InsaneBravery";
    }
    return "?";
}

std::optional<Stratagem> stratagem_from_key(std::string_view key) {
    for (const Stratagem s : {Stratagem::CommandReroll, Stratagem::InsaneBravery}) {
        if (stratagem_key(s) == key) return s;
    }
    return std::nullopt;
}

std::string_view stratagem_rule(Stratagem s) {
    switch (s) {
        case Stratagem::CommandReroll: return "15.02";
        case Stratagem::InsaneBravery: return "15.04";
    }
    return "?";
}

int stratagem_cost(Stratagem s) {
    switch (s) {
        case Stratagem::CommandReroll: return 1;  // 15.02: 1CP
        case Stratagem::InsaneBravery: return 1;  // 15.04: 1CP
    }
    return 0;
}

bool once_per_battle(Stratagem s) { return s == Stratagem::InsaneBravery; }  // 15.04 RESTRICTIONS

std::vector<Side> sequence_sides(Side active, std::array<bool, 2> has_rule) {
    std::vector<Side> out;
    for (const Side s : {active, opponent(active)}) {
        if (has_rule[side_index(s)]) out.push_back(s);  // 01.03.02: active player first, then the opposing player
    }
    return out;
}

}  // namespace wh40k::core
