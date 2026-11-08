// M67 (D-076): the bots' state_hash (D-054) with the points tiebreak. A unit's points and starting wounds, and the
// result rule, are hashed only when set, so a state without them (every scenario before M67) hashes exactly as
// before: the bots' own dice and plan memo, and so their play, are unchanged. Points and the rule change the hash.
// Also: the arena counts a game decided on points by its result's winner (it reads the result, as for D-007).

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <variant>
#include <vector>

#include "arena.hpp"
#include "bot.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/state/points_destroyed.hpp"
#include "session.hpp"
#include "state_hash.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::ai::Fnv1a;
using wh40k::ai::state_hash;

namespace {

// The M66 state_hash, copied verbatim (src/ai/state_hash.cpp at 05a63aa) as the reference.
namespace core = wh40k::core;

namespace m66_detail {

void add(Fnv1a& h, const core::Position& p) {
    h.f32(p.x);
    h.f32(p.y);
    h.f32(p.z);
}

void add(Fnv1a& h, core::UnitId id) { h.u64(id.v); }
void add(Fnv1a& h, core::ModelId id) { h.u64(id.v); }

// M65 (D-074 part 2): a facing only when it isn't 0, behind a tag, so a state with only round bases (every facing 0)
// hashes as before.
void add_facing(Fnv1a& h, float facing_deg) {
    if (facing_deg == 0.0f) return;
    h.u64(0x66616365ull);  // "face": apart from the fields around it
    h.f32(facing_deg);
}

template <typename T>
void add_opt_int(Fnv1a& h, const std::optional<T>& v) {
    h.flag(v.has_value());
    if (v) h.i32(static_cast<int>(*v));
}

void add(Fnv1a& h, const core::Unit& u) {
    add(h, u.id);
    h.u64(static_cast<std::uint64_t>(u.side));
    h.flag(u.battle_shocked);
    h.i32(u.starting_strength);
    const core::UnitTurnFlags& t = u.turn_flags;
    for (const bool b : {t.advanced, t.fell_back, t.moved_over_3in, t.made_charge_move, t.fights_first}) h.flag(b);
    const core::UnitPhaseFlags& p = u.phase_flags;
    for (const bool b : {p.selected_to_move, p.desperate_escape_hazards_made, p.selected_to_shoot, p.declared_charge,
                         p.piled_in, p.engaged_at_fight_step_start, p.selected_to_fight,
                         p.eligible_to_fight_this_phase, p.selected_for_overrun, p.forced_to_fight, p.consolidated}) {
        h.flag(b);
    }
    add_opt_int(h, p.advance_roll);
    add_opt_int(h, p.charge_roll);
    h.u64(u.models.size());
    for (const core::Model& m : u.models) {
        add(h, m.id);
        add(h, m.position);
        add_facing(h, m.facing_deg);
        h.i32(m.wounds_remaining);
    }
}

void add(Fnv1a& h, const core::FlowState& f) {
    h.u64(static_cast<std::uint64_t>(f.stage));
    h.i32(f.battle_round);
    h.i32(f.turn_in_round);
    h.flag(f.activation.has_value());
    if (f.activation) {
        add(h, f.activation->unit);
        h.u64(static_cast<std::uint64_t>(f.activation->kind));
    }
    h.flag(f.attack.has_value());
    if (f.attack) {
        const core::AttackFlow& a = *f.attack;
        add(h, a.attacker);
        h.u64(a.batches.size());
        for (const core::AttackBatch& b : a.batches) {
            add(h, b.target);
            h.i32(b.attack_dice);
            // M62 (D-069): only while a random A waits to be rolled, so other states hash as before.
            if (!b.random_attacks.empty()) {
                h.u64(0x72616e64ull);  // "rand": apart from the fields above
                h.u64(b.random_attacks.size());
                for (const core::RandomAttackDice& r : b.random_attacks) {
                    h.u64(r.selection);
                    h.i32(r.attacks.num_dice);
                    h.i32(r.attacks.sides);
                    h.i32(r.attacks.modifier);
                    h.i32(r.bonus);
                }
            }
        }
        for (const bool d : a.batch_done) h.flag(d);
        h.flag(a.current_target.has_value());
        if (a.current_target) add(h, *a.current_target);
        h.flag(a.current.has_value());
        if (a.current) {
            h.u64(a.current->batch);
            h.i32(a.current->wounds);
            h.u64(a.current->groups.size());
            h.flag(a.current->order.has_value());
            if (a.current->order) {
                for (const std::size_t i : *a.current->order) h.u64(i);
            }
            h.flag(a.current->save_rolls.has_value());
            if (a.current->save_rolls) {
                for (const int r : *a.current->save_rolls) h.i32(r);
            }
        }
        // M60 (24.15): only when set, so a state without hazard rolls hashes as before.
        if (a.hazard_rolls != 0 || a.pending_mortal_wounds != 0) {
            h.i32(a.hazard_rolls);
            h.i32(a.pending_mortal_wounds);
        }
        // M61 (24.38): only when set, so a state without pending re-rolls hashes as before.
        if (a.wound_rerolls) {
            h.u64(0x7477696eull);  // "twin": apart from the fields above
            h.u64(a.wound_rerolls->batch);
            h.u64(a.wound_rerolls->rolls.rolls.size());
            for (const core::WoundRoll& r : a.wound_rerolls->rolls.rolls) h.i32(r.unmodified);
        }
    }
    h.flag(f.move.has_value());
    if (f.move) {
        const core::MoveInProgress& m = *f.move;
        add(h, m.unit);
        h.u64(static_cast<std::uint64_t>(m.type));
        add_opt_int(h, m.mode);
        for (const core::ModelStart& s : m.starts) {
            add(h, s.model);
            add(h, s.position);
            add_facing(h, s.facing_deg);
        }
        h.u64(m.paths.size());
        for (const core::ModelPath& p : m.paths) {
            add(h, p.model);
            h.u64(p.points.size());
            for (const core::Position& pt : p.points) add(h, pt);
            if (!p.facings.empty()) {  // M65: only a path that turns its model
                h.u64(0x7475726eull);  // "turn"
                for (const float f : p.facings) h.f32(f);
            }
        }
        h.i32(m.pending_mortal_wounds);
        for (const core::UnitId t : m.targets) add(h, t);
        add_opt_int(h, m.consolidation_mode);
    }
    h.flag(f.charge.has_value());
    if (f.charge) add(h, f.charge->unit);
    h.flag(f.step_done[0]);
    h.flag(f.step_done[1]);
    h.flag(f.fight_sequence.has_value());
    if (f.fight_sequence) {
        h.u64(static_cast<std::uint64_t>(f.fight_sequence->sub_step));
        h.u64(static_cast<std::uint64_t>(f.fight_sequence->selecting));
        add_opt_int(h, f.fight_sequence->last_pass);
    }
    h.flag(f.fight.has_value());
    if (f.fight) {
        add(h, f.fight->unit);
        h.u64(static_cast<std::uint64_t>(f.fight->type));
        h.flag(f.fight->overrun_pile_in_pending);
        h.flag(f.fight->attacks_started);
        h.flag(f.fight->forced);
    }
    h.u64(f.forced_fights.size());
    for (const core::UnitId u : f.forced_fights) add(h, u);
}

// The decision's kind and the unit it is about (every kind holds at most one id).
void add(Fnv1a& h, const core::PendingDecision& d) {
    h.u64(static_cast<std::uint64_t>(d.side));
    h.u64(d.kind.index());
    std::visit(
        [&](const auto& k) {
            using K = std::decay_t<decltype(k)>;
            if constexpr (requires { k.unit; }) add(h, k.unit);
            if constexpr (requires { k.target; }) add(h, k.target);
            if constexpr (requires { k.attacker; }) add(h, k.attacker);
            (void)sizeof(K);
        },
        d.kind);
}

}  // namespace m66_detail

using namespace m66_detail;

std::uint64_t m66_state_hash(const core::GameState& state) {
    Fnv1a h;
    h.i32(state.battle_rounds);
    h.u64(static_cast<std::uint64_t>(state.first_turn));
    h.u64(state.units.size());
    for (const core::Unit& u : state.units) add(h, u);
    h.i32(state.command_points[0]);
    h.i32(state.command_points[1]);
    add(h, state.flow);
    h.flag(state.pending.has_value());
    if (state.pending) add(h, *state.pending);
    h.flag(state.result.has_value());
    if (state.result) add_opt_int(h, state.result->winner);
    // state.dice is deliberately not hashed (D-054: the bot never peeks at the rolls to come).
    return h.value();
}


// Random-play states (each decision's state) from `initial`, at most `actions` of them.
std::vector<GameState> states_from(GameState initial, std::uint64_t seed, int actions) {
    std::vector<GameState> out;
    initial.dice.seed = seed;
    start_game(initial);
    wh40k::test::TestRng rng(seed);
    for (int i = 0; i < actions && initial.pending; ++i) {
        out.push_back(initial);
        const GameAction a = wh40k::test::random_action(initial, rng);
        apply_action(initial, wh40k::test::decider(initial), a);
    }
    return out;
}

// The roster scenario without its points and the option: what M66 would have loaded.
GameState without_points(GameState gs) {
    gs.result_rule = ResultRule::D007Draw;
    for (Unit& u : gs.units) {
        u.points = 0;
        u.starting_wounds = 0;
    }
    if (gs.result) gs.result->points.reset();
    return gs;
}

}  // namespace

TEST(M67_StateHash, StatesWithoutPointsHashAsBefore) {
    const std::filesystem::path root = wh40k::test::repo_root();
    int states = 0;
    for (const GameState& initial : {wh40k::test::load_v1_scenario(),
                                     load_scenario(root / "data/scenarios/strike_44x60.json", root),
                                     wh40k::test::load_tyrannofex_fixture_closer(26.0f)}) {
        for (std::uint64_t seed = 1; seed <= 3; ++seed) {
            for (const GameState& gs : states_from(initial, seed, 300)) {
                ASSERT_EQ(state_hash(gs), m66_state_hash(gs)) << "seed " << seed << " state " << states;
                ++states;
            }
        }
    }
    EXPECT_GT(states, 1000);
    // The roster scenario's states with the points and the option taken out hash as M66 hashed them.
    for (const GameState& gs : states_from(wh40k::test::load_rosters_scenario_closer(26.0f), 4, 300)) {
        ASSERT_EQ(state_hash(without_points(gs)), m66_state_hash(gs));
        ++states;
    }
}

TEST(M67_StateHash, PointsAndTheResultRuleChangeTheHash) {
    const GameState gs = wh40k::test::load_rosters_scenario();
    const GameState plain = without_points(gs);
    EXPECT_NE(state_hash(gs), state_hash(plain));
    GameState rule_only = plain;
    rule_only.result_rule = ResultRule::PointsDestroyed;
    EXPECT_NE(state_hash(rule_only), state_hash(plain));
    GameState other_points = gs;
    other_points.units[3].points += 1;
    EXPECT_NE(state_hash(other_points), state_hash(gs));
    GameState other_wounds = gs;
    other_wounds.units[3].starting_wounds += 1;
    EXPECT_NE(state_hash(other_wounds), state_hash(gs));
}

TEST(M67_Arena, AGameDecidedOnPointsIsCountedByItsWinner) {
    // An Easy bot game on the roster scenario: the game ends (wipe-out or points), and the arena's record and its
    // totals follow the engine's result. A game decided on points counts as a win (or a draw) like any other.
    const GameState scenario = wh40k::test::load_rosters_scenario();
    wh40k::ai::BotConfig easy;
    easy.level = wh40k::ai::BotLevel::Easy;
    bool saw_points = false;
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        wh40k::client::GameSession session = wh40k::client::GameSession::new_game(scenario, seed);
        const wh40k::ai::GameRecord rec = wh40k::ai::play_bot_session(session, easy, easy);
        ASSERT_TRUE(rec.finished) << "seed " << seed;
        EXPECT_EQ(rec.rejected, 0u);
        EXPECT_EQ(rec.reverted, 0u);
        EXPECT_EQ(rec.fallbacks, 0u);
        const GameState& end = session.state();
        ASSERT_TRUE(end.result.has_value());
        EXPECT_EQ(rec.winner, end.result->winner);
        EXPECT_EQ(rec.on_points, end.result->points.has_value());
        if (end.result->points) {
            saw_points = true;
            const PointsTally t = points_destroyed(end);
            EXPECT_EQ(*end.result->points, t);
            EXPECT_EQ(rec.winner, t.destroyed[0] > t.destroyed[1]   ? std::optional<Side>(Side::A)
                                  : t.destroyed[1] > t.destroyed[0] ? std::optional<Side>(Side::B)
                                                                    : std::nullopt);
        }
    }
    // The arena's totals count the games decided on points among the wins and draws.
    const wh40k::ai::ArenaResult r = wh40k::ai::run_arena(scenario, 2, 1, easy, easy);
    int points_games = 0;
    for (const wh40k::ai::GameRecord& g : r.games) points_games += g.on_points ? 1 : 0;
    EXPECT_EQ(r.points_wins[0] + r.points_wins[1] + r.points_draws, points_games);
    EXPECT_EQ(r.wins[0] + r.wins[1] + r.draws + r.unfinished, 2);
    const std::string text = wh40k::ai::format_arena(r, scenario);
    EXPECT_NE(text.find("decided on points destroyed after the last battle round (D-076)"), std::string::npos);
    EXPECT_EQ(wh40k::ai::format_arena(r, wh40k::test::load_v1_scenario()).find("D-076"), std::string::npos);
    std::printf("Easy vs Easy on the roster scenario, seeds 1-3: a game decided on points: %s\n", saw_points ? "yes" : "no");
}
