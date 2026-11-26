#include "core/engine/replay.hpp"

#include <fstream>
#include <stdexcept>
#include <utility>
#include <variant>

#include "core/data/datasheet.hpp"
#include "core/data/load_error.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"

namespace wh40k::core {

// ---- JSON ----

void to_json(nlohmann::json& j, const ReplayStep& v) { j = Json{{"side", v.side}, {"action", v.action}}; }
void from_json(const nlohmann::json& j, ReplayStep& v) {
    j.at("side").get_to(v.side);
    action::from_json(j.at("action"), v.action);
}

void to_json(nlohmann::json& j, const ReplayInitial& v) {
    if (v.state) {
        j = Json{{"state", *v.state}};
    } else {
        j = Json{{"scenario", v.scenario.value_or("")}, {"seed", v.seed}};
    }
}
void from_json(const nlohmann::json& j, ReplayInitial& v) {
    v = ReplayInitial{};
    if (j.contains("state")) {
        v.state = j.at("state").get<GameState>();
    } else {
        v.scenario = j.at("scenario").get<std::string>();
        j.at("seed").get_to(v.seed);
    }
}

void to_json(nlohmann::json& j, const Replay& v) {
    j = Json{{"format", v.format},
             {"description", v.description},
             {"initial", v.initial},
             {"steps", v.steps},
             {"expect", v.expect}};
}
void from_json(const nlohmann::json& j, Replay& v) {
    j.at("format").get_to(v.format);
    if (v.format != kReplayFormatRollsInline && v.format != kReplayFormatRollsRecorded &&
        v.format != kReplayFormatWindowsRecorded) {
        throw DataError("replay: unsupported format " + std::to_string(v.format));
    }
    j.at("description").get_to(v.description);
    j.at("initial").get_to(v.initial);
    j.at("steps").get_to(v.steps);
    v.expect = j.contains("expect") ? j.at("expect") : Json();
}

Replay load_replay(const std::filesystem::path& path) {
    try {
        return read_json_file(path).get<Replay>();
    } catch (const Json::exception& e) {
        throw DataError("replay '" + path.string() + "': " + e.what());
    }
}

void save_replay(const Replay& replay, const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw DataError("cannot write replay '" + path.string() + "'");
    }
    out << Json(replay).dump(2) << '\n';
}

// ---- running ----

GameState replay_initial_state(const Replay& replay, const std::filesystem::path& repo_root) {
    if (replay.initial.state) {
        return *replay.initial.state;
    }
    if (!replay.initial.scenario) {
        throw DataError("replay: initial state needs a scenario or a state");
    }
    GameState state = load_scenario(repo_root / *replay.initial.scenario, repo_root);
    state.dice = DiceState{replay.initial.seed, 0};
    return state;
}

namespace {

bool damage_roll_pending(const GameState& state) {
    return roll_pending(state) && std::get<decision::RollDice>(state.pending->kind).roll == RollKind::Damage;
}

// D-095 / D-096: answers, in `match` (a paced one), every roll and window its last step led to: each roll made, each
// window declined, each recorded as a step (what format 1 leaves out).
void answer_all(Match& match) {
    while (roll_pending(match.state()) || window_pending(match.state())) {
        const PendingDecision& p = *match.state().pending;
        GameAction answer = action::DeclineStratagems{};
        if (const auto* d = std::get_if<decision::RollDice>(&p.kind)) answer = action::RollDice{d->roll, d->unit};
        const ValidationResult v = match.submit(p.side, answer);
        if (!v.ok()) throw std::logic_error("D-096: an unrecorded roll or window was refused: " + v.reasons.front());
    }
}

// D-096: declines every window waiting in `match` (format 2 recorded none).
void decline_windows(Match& match) {
    while (window_pending(match.state())) {
        const ValidationResult v = match.submit(match.state().pending->side, action::DeclineStratagems{});
        if (!v.ok()) throw std::logic_error("D-096: a window could not be declined: " + v.reasons.front());
    }
}

// D-096: after a recorded format-2 Damage roll (a batch's in one), the batch's further per-attack Damage rolls.
void more_damage_rolls(Match& match) {
    for (decline_windows(match); damage_roll_pending(match.state()); decline_windows(match)) {
        const auto& d = std::get<decision::RollDice>(match.state().pending->kind);
        const ValidationResult v = match.submit(match.state().pending->side, action::RollDice{d.roll, d.unit});
        if (!v.ok()) throw std::logic_error("D-096: a damage roll was refused: " + v.reasons.front());
    }
}

bool is_damage_roll(const GameAction& a) {
    const auto* r = std::get_if<action::RollDice>(&a);
    return r != nullptr && r->roll == RollKind::Damage;
}

// D-096: plays a format-2 step list in `match` step by step (see with_roll_steps_from_format2). The first step that
// doesn't validate is returned with its reasons; the match stops before it.
std::optional<std::pair<std::size_t, ValidationResult>> play_format2(Match& match, const std::vector<ReplayStep>& steps) {
    for (std::size_t i = 0; i < steps.size(); ++i) {
        decline_windows(match);
        ValidationResult v = match.submit(steps[i].side, steps[i].action);
        if (!v.ok()) return std::make_pair(i, std::move(v));
        if (is_damage_roll(steps[i].action)) more_damage_rolls(match);
    }
    decline_windows(match);
    return std::nullopt;
}

}  // namespace

ReplayResult run_replay(const Replay& replay, const std::filesystem::path& repo_root) {
    ReplayResult r;
    if (replay.format == kReplayFormatRollsRecorded) {
        // D-096: format 2, played as its format-3 form (the same game).
        Match match(replay_initial_state(replay, repo_root), RollMode::Paced);
        if (auto failed = play_format2(match, replay.steps)) {
            r.failed_step = failed->first;
            r.reasons = std::move(failed->second.reasons);
        }
        r.final_state = match.state();
        r.events = match.events();
        return r;
    }
    const bool paced = replay.format == kReplayFormatWindowsRecorded;  // D-095, D-096
    r.final_state = replay_initial_state(replay, repo_root);
    if (r.final_state.flow.stage == Stage::NotStarted) {
        auto events = paced ? start_game_paced(r.final_state) : start_game(r.final_state);
        r.events.insert(r.events.end(), events.begin(), events.end());
    }
    for (std::size_t i = 0; i < replay.steps.size(); ++i) {
        const ReplayStep& step = replay.steps[i];
        ValidationResult v = validate_action(r.final_state, step.side, step.action);
        if (!v.ok()) {
            r.failed_step = i;
            r.reasons = std::move(v.reasons);
            return r;
        }
        auto events = paced ? apply_action_paced(r.final_state, step.side, step.action)
                            : apply_action(r.final_state, step.side, step.action);
        r.events.insert(r.events.end(), events.begin(), events.end());
    }
    return r;
}

std::vector<ReplayStep> with_roll_steps(GameState initial, const std::vector<ReplayStep>& steps) {
    Match match(std::move(initial), RollMode::Paced);
    answer_all(match);
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const ValidationResult v = match.submit(steps[i].side, steps[i].action);
        if (!v.ok()) {
            throw DataError("step " + std::to_string(i + 1) + " is invalid: " + v.reasons.front());
        }
        answer_all(match);
    }
    return match.steps();
}

std::vector<ReplayStep> with_roll_steps_from_format2(GameState initial, const std::vector<ReplayStep>& steps) {
    Match match(std::move(initial), RollMode::Paced);
    if (auto failed = play_format2(match, steps)) {
        throw DataError("step " + std::to_string(failed->first + 1) + " is invalid: " +
                        failed->second.reasons.front());
    }
    return match.steps();
}

Replay with_roll_steps(const Replay& replay, const std::filesystem::path& repo_root) {
    if (replay.format == kReplayFormatWindowsRecorded) return replay;
    Replay out = replay;
    out.format = kReplayFormatWindowsRecorded;
    out.steps = replay.format == kReplayFormatRollsRecorded
                    ? with_roll_steps_from_format2(replay_initial_state(replay, repo_root), replay.steps)
                    : with_roll_steps(replay_initial_state(replay, repo_root), replay.steps);
    return out;
}

nlohmann::json replay_expectation(const GameState& state) {
    Json models = Json::array();
    for (const Unit& u : state.units) {
        for (const Model& m : u.models) {
            Json e{{"unit", u.id},
                   {"model", m.id},
                   {"wounds_remaining", m.wounds_remaining},
                   {"x_mm", m.position.x},
                   {"y_mm", m.position.y}};
            if (m.facing_deg != 0.0f) e["facing_deg"] = m.facing_deg;  // D-074: omitted when 0 (old files unchanged)
            models.push_back(std::move(e));
        }
    }
    Json result = nullptr;
    if (state.result) {
        result = *state.result;
    }
    return Json{{"stage", state.flow.stage},
                {"battle_round", state.flow.battle_round},
                {"result", result},
                {"models", models}};
}

// ---- Match ----

Match::Match(GameState initial, RollMode mode) : initial_(std::move(initial)), state_(initial_), mode_(mode) {
    if (state_.flow.stage == Stage::NotStarted) {
        events_ = mode_ == RollMode::Paced ? start_game_paced(state_) : start_game(state_);
    }
}

ValidationResult Match::submit(Side side, const GameAction& action) {
    ValidationResult v = validate_action(state_, side, action);
    if (!v.ok()) {
        return v;
    }
    SeededDice dice(state_.dice);  // as apply_action's 3-argument overload, with the reused scratch copy (M54)
    auto events = mode_ == RollMode::Paced ? apply_action_paced(state_, side, action, dice, scratch_.state)
                                           : apply_action(state_, side, action, dice, scratch_.state);
    events_.insert(events_.end(), events.begin(), events.end());
    steps_.push_back(ReplayStep{side, action});
    return v;
}

Replay Match::to_replay(std::string description) const {
    Replay r;
    r.format = mode_ == RollMode::Paced ? kReplayFormatWindowsRecorded : kReplayFormatRollsInline;
    r.description = std::move(description);
    r.initial.state = initial_;
    r.steps = steps_;
    r.expect = replay_expectation(state_);
    return r;
}

}  // namespace wh40k::core
