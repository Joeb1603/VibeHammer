#include "arena.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <variant>

#include "auto_actions.hpp"
#include "core/engine/actions.hpp"
#include "core/serialization/json.hpp"
#include "session.hpp"
#include "state_hash.hpp"

namespace wh40k::ai {

namespace {

std::size_t reverts_in(const client::LogEntry& entry) {
    return static_cast<std::size_t>(std::count_if(entry.events.begin(), entry.events.end(), [](const core::GameEvent& e) {
        return std::holds_alternative<core::event::UnitMoveReverted>(e);
    }));
}

// Every model's wounds left, by side (a destroyed model is gone, so it counts 0).
std::array<int, 2> wounds_left(const core::GameState& state) {
    std::array<int, 2> out{};
    for (const core::Unit& u : state.units) {
        for (const core::Model& m : u.models) out[core::side_index(u.side)] += m.wounds_remaining;
    }
    return out;
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t i = std::min(v.size() - 1, static_cast<std::size_t>(p * static_cast<double>(v.size() - 1) + 0.5));
    return v[i];
}

}  // namespace

GameRecord play_bot_game(const core::GameState& scenario, std::uint64_t seed, BotConfig a, BotConfig b,
                         std::size_t step_cap, std::vector<double>* decision_ms, std::vector<std::string>* trace) {
    client::GameSession session = client::GameSession::new_game(scenario, seed);
    return play_bot_session(session, a, b, step_cap, decision_ms, trace);
}

GameRecord play_bot_session(client::GameSession& session, BotConfig a, BotConfig b, std::size_t step_cap,
                            std::vector<double>* decision_ms, std::vector<std::string>* trace) {
    GameRecord rec;
    rec.seed = session.seed();
    const std::array<int, 2> wounds_at_start = wounds_left(session.state());
    std::array<Bot, 2> bots{Bot(a), Bot(b)};
    // As the client runs a bot game: skip steps with nothing to do, never auto-allocate for a bot (D-054).
    client::AutoSettings autos;
    autos.skip_empty_steps = true;
    autos.auto_allocate = false;
    autos.bot_controlled = {true, true};
    std::size_t logged = 0;
    const auto count_new = [&] {
        for (; logged < session.log().size(); ++logged) {
            const client::LogEntry& entry = session.log()[logged];
            rec.reverted += reverts_in(entry);
            for (const core::GameEvent& e : entry.events) {
                if (const auto* r = std::get_if<core::event::ModelRemovedForCoherency>(&e)) {
                    if (const core::Unit* u = session.state().find_unit(r->unit)) ++rec.coherency_removed[core::side_index(u->side)];
                }
                if (const auto* c = std::get_if<core::event::UnitMoveCommitted>(&e)) {
                    const bool fight_move =
                        c->move_type == core::MoveType::PileIn || c->move_type == core::MoveType::Consolidation;
                    const core::Unit* u = session.state().find_unit(c->unit);
                    if (fight_move && u != nullptr) ++rec.fight_moves[core::side_index(u->side)];
                }
            }
        }
    };
    while (session.pending() && session.history().size() < step_cap) {
        (void)client::run_auto_actions(session, autos);
        count_new();
        if (!session.pending()) break;
        const core::Side side = session.pending()->side;
        if (std::holds_alternative<core::decision::PileIn>(session.pending()->kind) ||
            std::holds_alternative<core::decision::Consolidate>(session.pending()->kind)) {
            ++rec.fight_move_steps[core::side_index(side)];
        }
        const auto t0 = std::chrono::steady_clock::now();
        const BotChoice choice = bots[core::side_index(side)].decide(session.state(), side);
        const auto t1 = std::chrono::steady_clock::now();
        if (decision_ms != nullptr) decision_ms->push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        if (trace != nullptr) {
            trace->push_back("R" + std::to_string(session.state().flow.battle_round) + " " +
                             (side == core::Side::A ? "A " : "B ") + std::string(core::action_name(choice.action)) +
                             ": " + choice.reason);
        }
        if (choice.stopped) {
            rec.problems = choice.problems;
            rec.problems.insert(rec.problems.begin(), "the bot stopped: " + choice.reason);
            break;
        }
        const core::ValidationResult r = session.submit(choice.action);
        if (!r.ok()) {
            ++rec.rejected;
            rec.problems.push_back("rejected " + std::string(core::action_name(choice.action)) + ": " + r.reasons.front());
            break;
        }
        count_new();
    }
    const core::GameState& end = session.state();
    rec.steps = session.history().size();
    rec.finished = end.result.has_value();
    if (end.result) rec.winner = end.result->winner;
    rec.on_points = end.result && end.result->points.has_value();  // M67 (D-076)
    if (!rec.finished && rec.problems.empty()) rec.problems.push_back("step cap reached");
    rec.end_round = end.flow.battle_round;
    for (const core::Unit& u : end.units) rec.surviving_models[core::side_index(u.side)] += static_cast<int>(u.models.size());
    const std::array<int, 2> wounds_at_end = wounds_left(end);
    for (std::size_t i = 0; i < 2; ++i) rec.wounds_lost[i] = wounds_at_start[i] - wounds_at_end[i];
    rec.fallbacks = bots[0].fallbacks() + bots[1].fallbacks();
    return rec;
}

ArenaResult run_arena(const core::GameState& scenario, int games, std::uint64_t first_seed, BotConfig a, BotConfig b,
                      std::size_t step_cap) {
    ArenaResult r;
    std::vector<double> ms;
    for (int i = 0; i < games; ++i) {
        r.games.push_back(play_bot_game(scenario, first_seed + static_cast<std::uint64_t>(i), a, b, step_cap, &ms));
    }
    for (const GameRecord& g : r.games) {
        if (!g.finished) {
            ++r.unfinished;
        } else if (g.winner) {
            ++r.wins[core::side_index(*g.winner)];
            if (g.on_points) ++r.points_wins[core::side_index(*g.winner)];  // M67 (D-076)
        } else {
            ++r.draws;
            if (g.on_points) ++r.points_draws;
        }
        r.rejected += g.rejected;
        r.reverted += g.reverted;
        r.mean_end_round += g.end_round;
        r.mean_surviving[0] += g.surviving_models[0];
        r.mean_surviving[1] += g.surviving_models[1];
        r.mean_wounds_lost[0] += g.wounds_lost[0];
        r.mean_wounds_lost[1] += g.wounds_lost[1];
        r.fallbacks += g.fallbacks;
        r.coherency_removed[0] += g.coherency_removed[0];
        r.coherency_removed[1] += g.coherency_removed[1];
        for (std::size_t s = 0; s < 2; ++s) {
            r.fight_move_steps[s] += g.fight_move_steps[s];
            r.fight_moves[s] += g.fight_moves[s];
        }
    }
    if (!r.games.empty()) {
        const double n = static_cast<double>(r.games.size());
        r.mean_end_round /= n;
        r.mean_surviving[0] /= n;
        r.mean_surviving[1] /= n;
        r.mean_wounds_lost[0] /= n;
        r.mean_wounds_lost[1] /= n;
    }
    r.p50_ms = percentile(ms, 0.50);
    r.p95_ms = percentile(ms, 0.95);
    r.max_ms = ms.empty() ? 0.0 : *std::max_element(ms.begin(), ms.end());
    return r;
}

PerGameReport run_per_game(const core::GameState& scenario, int games, std::uint64_t first_seed, BotConfig a,
                           BotConfig b, std::size_t step_cap) {
    const auto hash_text = [](const std::string& text) {
        Fnv1a h;
        h.bytes(text.data(), text.size());
        return static_cast<unsigned long long>(h.value());
    };
    PerGameReport out;
    std::vector<double> ms;
    char buf[1024];
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < games; ++i) {
        const std::uint64_t s = first_seed + static_cast<std::uint64_t>(i);
        client::GameSession session = client::GameSession::new_game(scenario, s);
        std::vector<std::string> lines;
        const GameRecord g = play_bot_session(session, a, b, step_cap, &ms, &lines);
        std::string trace;
        for (const std::string& l : lines) trace += l + "\n";
        std::snprintf(buf, sizeof buf,
                      "game %llu: finished %d winner %s round %d steps %zu surviving %d/%d wounds lost %d/%d coherency "
                      "%d/%d fight steps %d/%d fight moves %d/%d rejected %zu reverted %zu fallbacks %zu "
                      "actions %016llx state %016llx trace %016llx\n",
                      static_cast<unsigned long long>(s), g.finished ? 1 : 0,
                      g.winner ? (*g.winner == core::Side::A ? "A" : "B") : "none", g.end_round, g.steps,
                      g.surviving_models[0], g.surviving_models[1], g.wounds_lost[0], g.wounds_lost[1],
                      g.coherency_removed[0], g.coherency_removed[1], g.fight_move_steps[0], g.fight_move_steps[1],
                      g.fight_moves[0], g.fight_moves[1], g.rejected, g.reverted, g.fallbacks,
                      hash_text(nlohmann::json(session.history()).dump()),
                      hash_text(nlohmann::json(session.state()).dump()), hash_text(trace));
        out.text += buf;
        out.clean = out.clean && g.finished && g.rejected == 0 && g.reverted == 0;
    }
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double max_ms = ms.empty() ? 0.0 : *std::max_element(ms.begin(), ms.end());
    std::snprintf(buf, sizeof buf, "decisions %zu: p50 %.3f ms, p95 %.3f ms, max %.3f ms; wall %.2f s\n", ms.size(),
                  percentile(ms, 0.50), percentile(ms, 0.95), max_ms, wall);
    out.text += buf;
    return out;
}

std::string format_arena(const ArenaResult& r, const core::GameState& scenario) {
    char buf[1024];
    const std::string a = scenario.sides[0].name;
    const std::string b = scenario.sides[1].name;
    std::snprintf(buf, sizeof buf,
                  "games %zu: side A (%s) wins %d, side B (%s) wins %d, draws %d, unfinished %d\n"
                  "mean end round %.2f; mean surviving models: A %.2f, B %.2f\n"
                  "mean wounds lost: A %.2f, B %.2f; models removed for coherency: A %d, B %d\n"
                  "rejected actions %zu, UnitMoveReverted %zu, fallbacks %zu\n"
                  "decision time p50 %.3f ms, p95 %.3f ms, max %.3f ms\n"
                  "mean result per game (win +1, draw 0, loss -1): A %+.3f, B %+.3f\n",
                  r.games.size(), a.c_str(), r.wins[0], b.c_str(), r.wins[1], r.draws, r.unfinished, r.mean_end_round,
                  r.mean_surviving[0], r.mean_surviving[1], r.mean_wounds_lost[0], r.mean_wounds_lost[1],
                  r.coherency_removed[0], r.coherency_removed[1], r.rejected,
                  r.reverted, r.fallbacks, r.p50_ms, r.p95_ms, r.max_ms, r.mean_result(core::Side::A),
                  r.mean_result(core::Side::B));
    std::string out = buf;
    if (scenario.result_rule == core::ResultRule::PointsDestroyed) {  // M67 (D-076): only for such a scenario
        std::snprintf(buf, sizeof buf,
                      "decided on points destroyed after the last battle round (D-076): A wins %d, B wins %d, draws %d\n",
                      r.points_wins[0], r.points_wins[1], r.points_draws);
        out += buf;
    }
    std::snprintf(buf, sizeof buf,
                  "pile-in / consolidate steps answered: A %d, B %d; pile-ins and consolidations made: A %d, B %d\n",
                  r.fight_move_steps[0], r.fight_move_steps[1], r.fight_moves[0], r.fight_moves[1]);
    out += buf;
    std::string coherency;  // M43: which games lost models to coherency (for --trace)
    for (const GameRecord& g : r.games) {
        if (g.coherency_removed[0] + g.coherency_removed[1] > 0) coherency += " " + std::to_string(g.seed);
    }
    if (!coherency.empty()) out += "coherency removals in seeds:" + coherency + "\n";
    std::string reverted;  // M44: which games had a UnitMoveReverted (for --trace)
    for (const GameRecord& g : r.games) {
        if (g.reverted > 0) reverted += " " + std::to_string(g.seed);
    }
    if (!reverted.empty()) out += "UnitMoveReverted in seeds:" + reverted + "\n";
    for (const GameRecord& g : r.games) {
        if (g.problems.empty()) continue;
        out += "  seed " + std::to_string(g.seed) + ": " + g.problems.front() + "\n";
    }
    return out;
}

}  // namespace wh40k::ai
