// M74 (D-081): the bots in the deployment step. M74's policy (deploy_policy) sets the lowest-id unit waiting up at the
// engine's proposal (or declares it not set up when the engine finds no legal spot), so an army hand-deployed by it ends
// exactly where Auto-place puts it and plays on as the auto-placed game does. state_hash covers the units waiting to be
// set up only when there are some.
// M77 (D-081 "Bot deployment"): the bot deploys by a heuristic (m77_test.cpp); M74's policy is its answer when the
// heuristic has none, and the fallback's. D-085 (M85): M74's policy was level Easy's; with one bot the tests below call
// it directly, and check the heuristic's properties for the bot.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "arena.hpp"
#include "bot.hpp"
#include "bot_driver.hpp"
#include "core/data/army_list.hpp"
#include "core/data/datasheet.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/engine.hpp"
#include "core/state/deployment.hpp"
#include "deploy_policy.hpp"
#include "fallback.hpp"
#include "session.hpp"
#include "state_hash.hpp"
#include "support/army_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::decider;
using wh40k::test::repo_root;

// The bots' state_hash as M73 left it (src/ai/state_hash.cpp at commit 5eb02f5), so the tests can show that every
// state without units waiting to be set up hashes exactly as before (the bots' seeds and plan memo are unchanged).
namespace m73 {
namespace core = wh40k::core;
using wh40k::ai::Fnv1a;
namespace {

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
    // M67 (D-076): a unit's points and starting wounds only when it has points, behind a tag, so a state without
    // them (every scenario before M67) hashes as before.
    if (u.points != 0 || u.starting_wounds != 0) {
        h.u64(0x706e7473ull);  // "pnts": apart from the fields around it
        h.i32(u.points);
        h.i32(u.starting_wounds);
    }
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

}  // namespace

std::uint64_t m73_state_hash(const core::GameState& state) {
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
    // M67 (D-076): the result rule only when it isn't D-007's, so older states hash as before.
    if (state.result_rule != core::ResultRule::D007Draw) {
        h.u64(0x72756c65ull);  // "rule"
        h.u64(static_cast<std::uint64_t>(state.result_rule));
    }
    // state.dice is deliberately not hashed (D-054: the bot never peeks at the rolls to come).
    return h.value();
}

}  // namespace m73

namespace {

std::vector<GameState> states_from(GameState initial, std::uint64_t seed, int actions) {
    std::vector<GameState> out;
    initial.dice.seed = seed;
    start_game(initial);
    wh40k::test::TestRng rng(seed);
    for (int i = 0; i < actions && initial.pending; ++i) {
        out.push_back(initial);
        const GameAction a = wh40k::test::random_action(initial, rng);
        apply_action(initial, decider(initial), a);
    }
    return out;
}

GameState stock_battle(Side first, DeploymentMode mode) {
    return wh40k::test::custom_battle(wh40k::test::stock_army(wh40k::test::csm()),
                                      wh40k::test::stock_army(wh40k::test::tyranids()), first, mode);
}

// M74's policy (once level Easy's) as a player: its answer to every set-up.
BotChoice m74_choice(const GameState& gs, Side side) {
    const std::optional<PolicyResult> r = deploy_policy(gs, side, legal_actions(gs, side));
    EXPECT_TRUE(r.has_value());
    return r ? BotChoice{r->action, r->reason} : BotChoice{};
}

// D-081: a 12" x 24" battlefield whose south zone (2.5" deep) holds two lines of 10 Termagants but not a third (side A,
// units 1-3), against one Intercessor Squad (side B, unit 4).
GameState narrow_zone() {
    const nlohmann::json termagants{{"unit_file", "data/units/termagants.json"},
                                    {"models", {{{"type", "Termagants"}, {"count", 10},
                                                 {"weapons", {"Fleshborer", "Chitinous claws and teeth"}}}}}};
    const nlohmann::json intercessors{{"unit_file", "data/units/intercessor_squad.json"},
                                      {"models",
                                       {{{"type", "Intercessor Sergeant"}, {"count", 1},
                                         {"weapons", {"Bolt Rifle", "Bolt pistol", "Knives and Fists"}}},
                                        {{"type", "Intercessor"}, {"count", 4},
                                         {"weapons", {"Bolt Rifle", "Bolt pistol", "Knives and Fists"}}}}}};
    const nlohmann::json s{
        {"name", "M74 narrow zone"},
        {"battlefield", {{"width_in", 12}, {"depth_in", 24}}},
        {"battle_rounds", 5},
        {"first_turn", "A"},
        {"deployment", "alternating"},
        {"sides",
         {{"A", {{"name", "South"}, {"deployment_zone", {{"edge", "south"}, {"depth_in", 2.5}}},
                 {"units", {termagants, termagants, termagants}}}},
          {"B", {{"name", "North"}, {"deployment_zone", {{"edge", "north"}, {"depth_in", 3}}},
                 {"units", {intercessors}}}}}}};
    return build_game_state(s, [](const std::string& f) { return load_datasheet(repo_root() / f); });
}

}  // namespace

TEST(M74_StateHash, StatesWithoutUnitsWaitingHashAsBefore) {
    // Every shipped kind of game, and a hand-deployed one once its deployment is over, hashes as M73's state_hash.
    const std::filesystem::path root = repo_root();
    int states = 0;
    for (const GameState& initial :
         {wh40k::test::load_v1_scenario(), load_scenario(root / "data/scenarios/strike_44x60.json", root),
          wh40k::test::load_rosters_scenario(), stock_battle(Side::A, DeploymentMode::Auto)}) {
        for (const GameState& gs : states_from(initial, 3, 150)) {
            ASSERT_EQ(state_hash(gs), m73::m73_state_hash(gs)) << "state " << states;
            ++states;
        }
    }
    for (const GameState& gs : states_from(stock_battle(Side::B, DeploymentMode::Hand), 5, 200)) {
        if (!gs.undeployed.empty()) continue;
        ASSERT_EQ(state_hash(gs), m73::m73_state_hash(gs)) << "state " << states;
        ++states;
    }
    EXPECT_GT(states, 600);
}

TEST(M74_StateHash, UnitsWaitingToBeSetUpChangeTheHash) {
    GameState gs = stock_battle(Side::A, DeploymentMode::Hand);
    start_game(gs);
    const std::uint64_t h = state_hash(gs);
    EXPECT_NE(h, m73::m73_state_hash(gs));  // M73 didn't see them
    GameState fewer = gs;
    fewer.undeployed.pop_back();
    EXPECT_NE(state_hash(fewer), h);
    EXPECT_EQ(m73::m73_state_hash(fewer), m73::m73_state_hash(gs));  // what the tag adds
    GameState changed = gs;
    changed.undeployed.front().models.front().wounds_remaining -= 1;
    EXPECT_NE(state_hash(changed), h);
    // Setting a unit up changes it too (the unit moves from one list to the other).
    GameState after = gs;
    apply_action(after, decider(after), legal_actions(after, decider(after)).front());
    EXPECT_NE(state_hash(after), h);
}

// M77 (D-081 "Bot deployment"): this test pinned "every level sets up the lowest-id unit at its proposal" while M74's
// policy was every level's. It pins that for M74's policy (D-085, M85: once level Easy's, now called directly), and for
// the bot checks the heuristic's properties at each decision (m77_test.cpp has the rest): a legal set-up of the first unit
// of deploy_order, no fallback.
TEST(M74_Bots, M74sPolicySetsUpTheLowestIdUnitAtItsProposal) {
    for (const bool policy : {true, false}) {
        GameState gs = stock_battle(Side::A, DeploymentMode::Hand);
        start_game(gs);
        Bot bot(BotConfig{});
        int decisions = 0;
        while (gs.flow.stage == Stage::Deploy) {
            const Side side = decider(gs);
            const std::vector<GameAction> legal = legal_actions(gs, side);
            const BotChoice choice = policy ? m74_choice(gs, side) : bot.decide(gs, side);
            ASSERT_FALSE(choice.stopped);
            UnitId lowest{0};
            for (const Unit& u : gs.undeployed) {
                if (u.side == side) {
                    lowest = u.id;
                    break;
                }
            }
            if (policy) {
                ASSERT_EQ(choice.action, GameAction{*propose_set_up(gs, lowest)});
                EXPECT_EQ(choice.action, legal.front());
                EXPECT_TRUE(wh40k::test::starts_with(choice.reason, "D-081: set up ")) << choice.reason;
            } else {
                const auto* d = std::get_if<action::DeployUnit>(&choice.action);
                ASSERT_NE(d, nullptr);
                EXPECT_EQ(d->unit, deploy_order(gs, side).front());
                EXPECT_TRUE(set_up_problems(gs, side, *d).empty());
                EXPECT_TRUE(wh40k::test::starts_with(choice.reason, "D-081 (M77): set up ")) << choice.reason;
            }
            apply_action(gs, side, choice.action);
            ++decisions;
        }
        EXPECT_EQ(decisions, 14);
        EXPECT_EQ(bot.fallbacks(), 0u);
        if (policy) {
            EXPECT_EQ(gs, [] {
                GameState p = stock_battle(Side::A, DeploymentMode::Auto);
                start_game(p);
                return p;
            }());
        }
    }
}

// M77 (D-081 "Bot deployment"): this test ran a Normal bot and required the fallback to give the same answer, which held
// while every level answered with M74's policy. The fallback keeps M74's policy, so that requirement is M74's policy's
// (D-085: called directly); the bot and the policy must both set up both lines and declare only the third not set up.
TEST(M74_Bots, ABotDeclaresAUnitNotSetUpOnlyWhenNothingFits) {
    for (const bool policy : {true, false}) {
        GameState gs = narrow_zone();
        start_game(gs);
        Bot bot(BotConfig{});
        std::vector<GameAction> taken;
        while (gs.flow.stage == Stage::Deploy) {
            const Side side = decider(gs);
            const BotChoice choice = policy ? m74_choice(gs, side) : bot.decide(gs, side);
            ASSERT_FALSE(choice.stopped);
            if (policy) {
                // The fallback gives the same answer.
                const FallbackResult fb = fallback_action(gs, side);
                ASSERT_TRUE(fb.action.has_value());
                EXPECT_EQ(*fb.action, choice.action);
            }
            taken.push_back(choice.action);
            apply_action(gs, side, choice.action);
        }
        ASSERT_EQ(taken.size(), 4u);
        EXPECT_TRUE(std::holds_alternative<action::DeployUnit>(taken[0]));  // B's Intercessors
        EXPECT_TRUE(std::holds_alternative<action::DeployUnit>(taken[1]));  // A's first line of Termagants
        EXPECT_TRUE(std::holds_alternative<action::DeployUnit>(taken[2]));  // the second, behind it
        EXPECT_EQ(taken[3], GameAction{(action::CannotSetUp{UnitId{3}})});  // no room for the third
        EXPECT_EQ(bot.fallbacks(), 0u);
    }
}

// M77 (D-081 "Bot deployment"): the bot nudges its units (m77_test.cpp: M77_BotDeploy.HandDeployedGamesFinishClean
// checks its hand-deployed games), so the equality is M74's policy's (D-085, M85: once level Easy's, which also played
// the games to the end; the bot plays 120 actions after the deployment, slow in Debug).
TEST(M74_Bots, GamesHandDeployedByM74sPolicyPlayOnAsAutoPlacedOnesDo) {
    // --deploy hand against --deploy auto: once M74's policy has set everything up, the state is the auto-placed game's,
    // so the rest of the game, decided from state hashes, plays the same (D-054) to the same state.
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        const Side first = seed % 2 == 0 ? Side::B : Side::A;
        const GameState hand = wh40k::test::random_armies_battle(seed, DeploymentMode::Hand, first);
        const GameState autos = wh40k::test::random_armies_battle(seed, DeploymentMode::Auto, first);
        wh40k::client::GameSession h = wh40k::client::GameSession::new_game(hand, seed);
        wh40k::client::GameSession a = wh40k::client::GameSession::new_game(autos, seed);
        std::size_t set_ups = 0;
        while (h.state().flow.stage == Stage::Deploy) {
            const Side side = h.state().pending->side;
            const BotChoice c = m74_choice(h.state(), side);
            ASSERT_TRUE(h.submit(c.action).ok());
            ++set_ups;
        }
        EXPECT_EQ(set_ups, hand.undeployed.size());
        EXPECT_EQ(h.state(), a.state()) << "seed " << seed;
        const std::size_t cap = 120;
        const GameRecord rh = play_bot_session(h, BotConfig{}, BotConfig{}, cap + set_ups);
        const GameRecord ra = play_bot_session(a, BotConfig{}, BotConfig{}, cap);
        EXPECT_EQ(rh.rejected + rh.reverted + rh.fallbacks, 0u);
        EXPECT_EQ(ra.rejected + ra.reverted + ra.fallbacks, 0u);
        EXPECT_EQ(h.history().size(), a.history().size() + set_ups);
        EXPECT_EQ(h.state(), a.state()) << "seed " << seed;
    }
}

TEST(M74_Bots, ABotGameWithHandDeploymentFromTheStartFinishesClean) {
    // play_bot_game from the hand-deployment state itself (the bots set up too): it finishes clean, and the arena counts
    // the starting wounds of the units waiting to be set up. (D-085, M85: with level Easy, which set up where Auto-place
    // does, it also ended as the auto-placed game; the bot's heuristic sets up elsewhere.)
    const GameState hand = wh40k::test::random_armies_battle(7, DeploymentMode::Hand);
    const GameRecord rh = play_bot_game(hand, 7, BotConfig{}, BotConfig{});
    EXPECT_TRUE(rh.finished);
    EXPECT_EQ(rh.rejected + rh.reverted + rh.fallbacks, 0u);
    EXPECT_GE(rh.wounds_lost[0], 0);
    EXPECT_GE(rh.wounds_lost[1], 0);
    EXPECT_GT(rh.wounds_lost[0] + rh.wounds_lost[1], 0);
}

TEST(M74_Bots, TheDriverPacesASetUpLikeAUnitsActivation) {
    const double move = base_pause_seconds(action::StartMove{UnitId{1}, MoveType::Normal, std::nullopt});
    EXPECT_EQ(base_pause_seconds(action::DeployUnit{UnitId{1}, {}}), move);
    EXPECT_EQ(base_pause_seconds(action::CannotSetUp{UnitId{1}}), move);
    EXPECT_EQ(move, 0.8);
}

TEST(M74_Arena, CustomBattlesTakeTheDeploymentMode) {
    const ArmySource source = load_army_source(repo_root());
    const GameState hand = random_armies_battle(source, 4, Side::A, DeploymentMode::Hand);
    const GameState autos = random_armies_battle(source, 4, Side::A);
    EXPECT_TRUE(hand.units.empty());
    EXPECT_FALSE(hand.undeployed.empty());
    EXPECT_TRUE(autos.undeployed.empty());
    EXPECT_EQ(autos, random_armies_battle(source, 4, Side::A, DeploymentMode::Auto));
    EXPECT_EQ(hand.undeployed.size(), autos.units.size());
}
