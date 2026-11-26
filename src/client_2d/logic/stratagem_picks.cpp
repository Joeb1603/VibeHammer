#include "stratagem_picks.hpp"

#include <algorithm>


namespace wh40k::client {

StratagemPicks::StratagemPicks(const core::GameState& state) : info_(core::window_info(state)) {
    if (!info_ || info_->whole_roll) return;
    // The only pickable die (a damage or advance roll) is picked already: one click less.
    const auto n = std::count(info_->pickable.begin(), info_->pickable.end(), true);
    if (n == 1) picked_ = static_cast<std::size_t>(std::find(info_->pickable.begin(), info_->pickable.end(), true) -
                                                   info_->pickable.begin());
}

bool StratagemPicks::offers(core::Stratagem s) const {
    if (!info_) return false;
    return std::any_of(info_->options.begin(), info_->options.end(),
                       [s](const core::StratagemOption& o) { return o.stratagem == s; });
}

int StratagemPicks::cost(core::Stratagem s) const {
    if (!info_) return 0;
    for (const core::StratagemOption& o : info_->options) {
        if (o.stratagem == s) return o.cost;
    }
    return 0;
}

bool StratagemPicks::pickable(std::size_t die) const {
    return info_ && !info_->whole_roll && die < info_->pickable.size() && info_->pickable[die];
}

void StratagemPicks::pick(std::size_t die) {
    if (!pickable(die)) return;
    picked_ = picked_ == die ? std::nullopt : std::optional<std::size_t>(die);
}

bool StratagemPicks::reroll_ready() const {
    return offers(core::Stratagem::CommandReroll) && (info_->whole_roll || picked_.has_value());
}

std::optional<core::GameAction> StratagemPicks::command_reroll() const {
    if (!reroll_ready()) return std::nullopt;
    return core::action::UseStratagem{core::Stratagem::CommandReroll, info_->unit.value(),
                                      info_->whole_roll ? std::nullopt : picked_};
}

std::optional<core::GameAction> StratagemPicks::insane_bravery() const {
    if (!offers(core::Stratagem::InsaneBravery)) return std::nullopt;
    return core::action::UseStratagem{core::Stratagem::InsaneBravery, info_->unit.value(), std::nullopt};
}

std::string StratagemPicks::reroll_label() const {
    return "Command Re-roll (" + std::to_string(cost(core::Stratagem::CommandReroll)) + " CP)";
}

std::string StratagemPicks::insane_bravery_label() const {
    return "Insane Bravery (" + std::to_string(cost(core::Stratagem::InsaneBravery)) + " CP)";
}

std::string StratagemPicks::decline_label() const {
    return info_ && info_->timing == core::WindowTiming::BeforeRoll ? "Roll normally" : "Keep the roll";
}

std::string StratagemPicks::headline() const {
    if (!info_) return "";
    if (info_->timing == core::WindowTiming::BeforeRoll) return "Insane Bravery? (15.04)";
    return "Command Re-roll? (15.02)";
}

std::string StratagemPicks::hint() const {
    if (!info_) return "";
    const std::string cp = std::to_string(info_->command_points) + " CP";
    if (info_->timing == core::WindowTiming::BeforeRoll) {
        return "You have " + cp + ". Insane Bravery passes this battle-shock test with no dice rolled, once per battle.";
    }
    if (info_->whole_roll) return "You have " + cp + ". Command Re-roll re-rolls the whole charge roll.";
    return "You have " + cp + ". Click a die to pick the one to re-roll (a die is re-rolled only once).";
}

}  // namespace wh40k::client
