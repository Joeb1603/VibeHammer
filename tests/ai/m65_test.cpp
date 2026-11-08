// M65 (D-074 part 2): the bots' state_hash (D-054) with facings. A facing is hashed only when it isn't 0, so a state
// with only round bases (every facing 0) hashes exactly as before M65: the bots' own dice and plan memo, and so their
// play, are unchanged. A turn changes the hash.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <variant>
#include <vector>

#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "state_hash.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::ai::Fnv1a;
using wh40k::ai::state_hash;

namespace {

// The M64 state_hash, copied verbatim (src/ai/state_hash.cpp at 424b332) as the reference.
namespace core = wh40k::core;

void add(Fnv1a& h, const core::Position& p) {
    h.f32(p.x);
    h.f32(p.y);
    h.f32(p.z);
}

void add(Fnv1a& h, core::UnitId id) { h.u64(id.v); }
void add(Fnv1a& h, core::ModelId id) { h.u64(id.v); }

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
        }
        h.u64(m.paths.size());
        for (const core::ModelPath& p : m.paths) {
            add(h, p.model);
            h.u64(p.points.size());
            for (const core::Position& pt : p.points) add(h, pt);
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

std::uint64_t m64_state_hash(const core::GameState& state) {
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

}  // namespace

TEST(M65_StateHash, RoundOnlyStatesHashAsBefore) {
    const std::filesystem::path root = wh40k::test::repo_root();
    int states = 0;
    int moving = 0;
    for (const GameState& initial : {wh40k::test::load_v1_scenario(),
                                     load_scenario(root / "data/scenarios/strike_44x60.json", root),
                                     wh40k::test::load_rosters_fixture_closer(26.0f)}) {
        for (std::uint64_t seed = 1; seed <= 3; ++seed) {
            for (const GameState& gs : states_from(initial, seed, 400)) {
                ASSERT_EQ(state_hash(gs), m64_state_hash(gs)) << "seed " << seed << " state " << states;
                ++states;
                if (gs.flow.move && !gs.flow.move->paths.empty()) ++moving;
            }
        }
    }
    EXPECT_GT(states, 1000);
    EXPECT_GT(moving, 50);  // states with a move in progress and its paths
    // A state with the Tyrannofex at facing 0 (a facing of 0 is not hashed) also hashes as before.
    const GameState oval = wh40k::test::tyrannofex::fixture();
    EXPECT_EQ(state_hash(oval), m64_state_hash(oval));
}

TEST(M65_StateHash, ATurnChangesTheHash) {
    GameState gs = wh40k::test::tyrannofex::fixture();
    const std::uint64_t before = state_hash(gs);
    gs.units[wh40k::test::tyrannofex::kTyrannofex].models[0].facing_deg = 45.0f;
    const std::uint64_t turned = state_hash(gs);
    EXPECT_NE(turned, before);
    // The start facing and a path's facings of a move in progress count too.
    GameState moving = wh40k::test::tyrannofex::at_movement(Side::B);
    const UnitId t = moving.units[wh40k::test::tyrannofex::kTyrannofex].id;
    const ModelId m = moving.units[wh40k::test::tyrannofex::kTyrannofex].models[0].id;
    apply_action(moving, Side::B, action::StartMove{t, MoveType::Normal, std::nullopt});
    const Position s = moving.units[wh40k::test::tyrannofex::kTyrannofex].models[0].position;
    GameState straight = moving;
    apply_action(straight, Side::B, action::ProposeModelMove{m, {Position{s.x, s.y - 20.0f, 0.0f}}, {}});
    GameState turning = moving;
    apply_action(turning, Side::B, action::ProposeModelMove{m, {Position{s.x, s.y - 20.0f, 0.0f}}, {30.0f}});
    EXPECT_NE(state_hash(straight), state_hash(turning));
    GameState started_turned = moving;
    started_turned.flow.move->starts.back().facing_deg = 30.0f;
    EXPECT_NE(state_hash(started_turned), state_hash(moving));
}
