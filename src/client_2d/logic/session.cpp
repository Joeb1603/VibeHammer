#include "session.hpp"

#include <fstream>
#include <utility>

#include "core/data/load_error.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"

namespace wh40k::client {

namespace {

std::string side_name(core::Side s) { return s == core::Side::A ? "Side A" : "Side B"; }

}  // namespace

GameSession::GameSession(core::GameState initial) : match_(std::move(initial)) {
    log_.push_back(LogEntry{"Start", match_.events()});  // Match starts the game if it hasn't started
}

GameSession GameSession::new_game(core::GameState scenario, std::optional<std::uint64_t> seed) {
    if (seed) {
        scenario.dice.seed = *seed;  // M13: seed override at game start
        scenario.dice.position = 0;
    }
    return GameSession(std::move(scenario));
}

GameSession GameSession::from_steps(core::GameState initial, const std::vector<core::ReplayStep>& steps) {
    GameSession s(std::move(initial));
    for (std::size_t i = 0; i < steps.size(); ++i) {
        if (s.deciding_side() != steps[i].side) {
            throw core::DataError("step " + std::to_string(i + 1) + " is for " + side_name(steps[i].side) +
                                  ", but " + side_name(s.deciding_side()) + " must act");
        }
        const core::ValidationResult r = s.submit(steps[i].action);
        if (!r.ok()) {
            throw core::DataError("step " + std::to_string(i + 1) + " is invalid: " + r.reasons.front());
        }
    }
    return s;
}

nlohmann::json GameSession::to_save_json() const {
    return nlohmann::json{{"format", 1},
                          {"kind", "vibehammer-save"},
                          {"initial", initial()},
                          {"steps", history()},
                          {"state", state()}};
}

GameSession GameSession::from_save_json(const nlohmann::json& j) {
    try {
        if (!j.is_object() || !j.contains("kind")) {
            return GameSession(j.get<core::GameState>());  // a bare GameState: no history
        }
        if (j.at("kind") != "vibehammer-save" || j.at("format") != 1) {
            throw core::DataError("not a VibeHammer save (format 1)");
        }
        GameSession s = from_steps(j.at("initial").get<core::GameState>(),
                                   j.at("steps").get<std::vector<core::ReplayStep>>());
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
    std::vector<core::ReplayStep> steps = history();
    steps.pop_back();
    *this = from_steps(initial(), steps);  // deterministic: the dice state is part of GameState
    return true;
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
    entry.events.assign(match_.events().begin() + static_cast<std::ptrdiff_t>(before), match_.events().end());
    log_.push_back(std::move(entry));
    return v;
}

}  // namespace wh40k::client
