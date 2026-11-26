#include "session.hpp"

#include <algorithm>
#include <fstream>
#include <utility>
#include <variant>

#include "core/data/load_error.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"

namespace wh40k::client {

namespace {

std::string side_name(core::Side s) { return s == core::Side::A ? "Side A" : "Side B"; }

}  // namespace

GameSession::GameSession(core::GameState initial, std::array<bool, 2> auto_roll, std::array<bool, 2> auto_decline)
    : match_(std::move(initial), core::RollMode::Paced), auto_roll_(auto_roll), auto_decline_(auto_decline) {
    log_.push_back(LogEntry{"Start", match_.events()});  // Match starts the game if it hasn't started
    roll_automatically();                                  // D-095
}

GameSession::GameSession(core::GameState initial, NoAutoRoll) : match_(std::move(initial), core::RollMode::Paced) {
    log_.push_back(LogEntry{"Start", match_.events()});
}

GameSession GameSession::new_game(core::GameState scenario, std::optional<std::uint64_t> seed,
                                  std::array<bool, 2> auto_roll, std::array<bool, 2> auto_decline) {
    if (seed) {
        scenario.dice.seed = *seed;  // M13: seed override at game start
        scenario.dice.position = 0;
    }
    return GameSession(std::move(scenario), auto_roll, auto_decline);
}

GameSession GameSession::from_steps(core::GameState initial, const std::vector<core::ReplayStep>& steps) {
    GameSession s(std::move(initial), NoAutoRoll{});
    for (std::size_t i = 0; i < steps.size(); ++i) {
        if (s.deciding_side() != steps[i].side) {
            throw core::DataError("step " + std::to_string(i + 1) + " is for " + side_name(steps[i].side) +
                                  ", but " + side_name(s.deciding_side()) + " must act");
        }
        const core::ValidationResult r = s.submit_one(steps[i].action);
        if (!r.ok()) {
            throw core::DataError("step " + std::to_string(i + 1) + " is invalid: " + r.reasons.front());
        }
    }
    return s;
}

nlohmann::json GameSession::to_save_json() const {
    return nlohmann::json{{"format", core::kReplayFormatReactionsRecorded},  // D-096, D-097
                          {"kind", "vibehammer-save"},
                          {"initial", initial()},
                          {"steps", history()},
                          {"state", state()}};
}

GameSession GameSession::from_save_json(const nlohmann::json& j) {
    try {
        if (!j.is_object() || !j.contains("kind")) {
            return GameSession(j.get<core::GameState>(), NoAutoRoll{});  // a bare GameState: no history
        }
        const nlohmann::json& format = j.at("format");
        const bool rolls_inline = format == core::kReplayFormatRollsInline;
        const bool rolls_recorded = format == core::kReplayFormatRollsRecorded;
        const bool windows_recorded = format == core::kReplayFormatWindowsRecorded;
        if (j.at("kind") != "vibehammer-save" ||
            (!rolls_inline && !rolls_recorded && !windows_recorded && format != core::kReplayFormatReactionsRecorded)) {
            throw core::DataError("not a VibeHammer save (format 1, 2, 3 or 4)");
        }
        const core::GameState initial = j.at("initial").get<core::GameState>();
        std::vector<core::ReplayStep> steps = j.at("steps").get<std::vector<core::ReplayStep>>();
        if (rolls_inline) steps = core::with_roll_steps(initial, steps);  // D-095: format 1, every roll inside its step
        // D-096: format 2, without its windows and with a batch's damage in one roll.
        if (rolls_recorded) steps = core::with_roll_steps_from_format2(initial, steps);
        // D-097: format 3, without the windows that follow no roll (15.08, 15.11, 15.12).
        if (windows_recorded) steps = core::with_roll_steps_from_format3(initial, steps);
        GameSession s = from_steps(initial, steps);
        if (!(s.state() == j.at("state").get<core::GameState>())) {
            throw core::DataError("the save's state does not match its initial state and steps");
        }
        return s;
    } catch (const nlohmann::json::exception& e) {
        throw core::DataError(std::string("not a valid VibeHammer save: ") + e.what());  // missing or bad fields
    }
}

void GameSession::save(const std::filesystem::path& path) const {
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw core::DataError("cannot write '" + path.string() + "'");
    out << to_save_json().dump(2) << '\n';
}

GameSession GameSession::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw core::DataError("cannot read '" + path.string() + "'");
    nlohmann::json j;
    try {
        in >> j;
    } catch (const nlohmann::json::exception& e) {
        throw core::DataError("'" + path.string() + "' is not valid JSON: " + e.what());
    }
    return from_save_json(j);
}

core::Replay GameSession::to_replay(std::string description) const { return match_.to_replay(std::move(description)); }

bool GameSession::undo() {
    if (history().empty()) return false;
    truncate(history().size() - 1);
    return true;
}

void GameSession::truncate(std::size_t n) {
    const std::vector<core::ReplayStep>& h = history();
    if (n >= h.size()) return;
    // D-095: a roll is never taken back on its own: back to before the last step that isn't a roll. D-096: nor a
    // declined window.
    const auto automatic = [](const core::GameAction& a) {
        return std::holds_alternative<core::action::RollDice>(a) ||
               std::holds_alternative<core::action::DeclineStratagems>(a);
    };
    while (n > 0 && automatic(h[n].action)) --n;
    const std::vector<core::ReplayStep> kept(h.begin(), h.begin() + static_cast<std::ptrdiff_t>(n));
    const std::array<bool, 2> auto_roll = auto_roll_;
    const std::array<bool, 2> auto_decline = auto_decline_;
    const std::array<std::vector<core::Stratagem>, 2> declined = declined_;
    *this = from_steps(initial(), kept);  // deterministic: the dice state is part of GameState
    auto_roll_ = auto_roll;
    auto_decline_ = auto_decline;
    declined_ = declined;
    roll_automatically();
}

bool GameSession::declines_window() const {
    if (!core::window_pending(state())) return false;
    const core::Side side = state().pending->side;
    if (auto_decline(side)) return true;
    const std::vector<core::Stratagem>& declined = declined_[core::side_index(side)];
    if (declined.empty()) return false;
    // D-097: every stratagem the window offers is one the side doesn't play.
    const std::optional<core::WindowInfo> info = core::window_info(state());
    if (!info) return false;
    return std::all_of(info->options.begin(), info->options.end(), [&](const core::StratagemOption& o) {
        return std::find(declined.begin(), declined.end(), o.stratagem) != declined.end();
    });
}

std::vector<core::GameAction> GameSession::roll_automatically() {
    std::vector<core::GameAction> made;
    while (state().pending) {
        const core::Side side = state().pending->side;
        core::GameAction answer;
        if (const auto* d = std::get_if<core::decision::RollDice>(&state().pending->kind); d && auto_roll(side)) {
            answer = core::action::RollDice{d->roll, d->unit};
        } else if (declines_window()) {
            answer = core::action::DeclineStratagems{};  // D-096, D-097
        } else {
            break;
        }
        if (!submit_one(answer).ok()) break;  // never refused; an engine error stops here
        made.push_back(answer);
    }
    return made;
}

std::vector<core::GameAction> GameSession::legal() const {
    if (!state().pending) return {};
    return core::legal_actions(state(), state().pending->side);
}

core::Side GameSession::deciding_side() const {
    return state().pending ? state().pending->side : core::active_side(state());
}

core::ValidationResult GameSession::check(const core::GameAction& action) const {
    return core::validate_action(state(), deciding_side(), action);
}

core::ValidationResult GameSession::submit(const core::GameAction& action) {
    const std::size_t before = history().size();
    const core::ValidationResult v = submit_one(action);
    if (!v.ok()) return v;
    roll_automatically();  // D-095, D-096
    const bool stuck = (core::roll_pending(state()) && auto_roll(state().pending->side)) || declines_window();
    if (stuck) {
        // An automatic answer failed (an engine error): take the action and its answers back, as if it had been refused.
        const core::ValidationResult failed = core::ValidationResult::failure(
            "Engine error (the action was not applied): a roll after it could not be made");
        const std::array<bool, 2> auto_roll = auto_roll_;
        const std::array<bool, 2> auto_decline = auto_decline_;
        const std::array<std::vector<core::Stratagem>, 2> declined = declined_;
        *this = from_steps(initial(), std::vector<core::ReplayStep>(history().begin(),
                                                                    history().begin() + static_cast<std::ptrdiff_t>(before)));
        auto_roll_ = auto_roll;
        auto_decline_ = auto_decline;
        declined_ = declined;
        return failed;
    }
    return v;
}

core::ValidationResult GameSession::submit_one(const core::GameAction& action) {
    const core::Side side = deciding_side();
    const std::size_t before = match_.events().size();
    core::ValidationResult v;
    try {
        v = match_.submit(side, action);
    } catch (const std::exception& e) {
        // apply_action commits only on success, so the game is unchanged; report instead of crashing.
        return core::ValidationResult::failure(std::string("Engine error (the action was not applied): ") + e.what());
    }
    if (!v.ok()) return v;
    LogEntry entry;
    entry.label = "Step " + std::to_string(history().size()) + ": " + side_name(side) + " " +
                  std::string(core::action_name(action));
    if (const auto* r = std::get_if<core::action::RollDice>(&action)) {
        entry.label += " (" + std::string(core::roll_kind_name(r->roll)) + ")";  // D-095: which roll
    }
    entry.events.assign(match_.events().begin() + static_cast<std::ptrdiff_t>(before), match_.events().end());
    log_.push_back(std::move(entry));
    return v;
}

}  // namespace wh40k::client
