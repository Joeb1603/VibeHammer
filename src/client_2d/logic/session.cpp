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

GameSession::GameSession(core::GameState initial) : initial_(initial), state_(std::move(initial)) {
    LogEntry start{"Start", {}};
    if (state_.flow.stage == core::Stage::NotStarted) {
        start.events = core::start_game(state_);
    }
    log_.push_back(std::move(start));
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
                          {"initial", initial_},
                          {"steps", history_},
                          {"state", state_}};
}

GameSession GameSession::from_save_json(const nlohmann::json& j) {
    if (!j.contains("kind")) {
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

core::Replay GameSession::to_replay(std::string description) const {
    core::Replay r;
    r.description = std::move(description);
    r.initial.state = initial_;
    r.steps = history_;
    r.expect = core::replay_expectation(state_);
    return r;
}

bool GameSession::undo() {
    if (history_.empty()) return false;
    std::vector<core::ReplayStep> steps = history_;
    steps.pop_back();
    *this = from_steps(initial_, steps);  // deterministic: the dice state is part of GameState
    return true;
}

std::vector<core::GameAction> GameSession::legal() const {
    if (!state_.pending) return {};
    return core::legal_actions(state_, state_.pending->side);
}

core::Side GameSession::deciding_side() const {
    return state_.pending ? state_.pending->side : core::active_side(state_);
}

core::ValidationResult GameSession::check(const core::GameAction& action) const {
    return core::validate_action(state_, deciding_side(), action);
}

core::ValidationResult GameSession::submit(const core::GameAction& action) {
    const core::Side side = deciding_side();
    core::ValidationResult v = core::validate_action(state_, side, action);
    if (!v.ok()) return v;
    LogEntry entry;
    entry.label = "Step " + std::to_string(history_.size() + 1) + ": " + side_name(side) + " " +
                  std::string(core::action_name(action));
    entry.events = core::apply_action(state_, side, action);
    history_.push_back(core::ReplayStep{side, action});
    log_.push_back(std::move(entry));
    return v;
}

}  // namespace wh40k::client
