// Replays (D-014): an initial state plus (side, action) steps reproduce the game exactly.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/data/army_list.hpp"
#include "core/data/datasheet.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/oval.hpp"
#include "core/rules/attack_sequence.hpp"
#include "core/rules/shooting.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/deployment.hpp"
#include "core/state/queries.hpp"
#include "support/army_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::repo_root;
using wh40k::test::select;
using wh40k::test::shoot_step_state;
using wh40k::test::TestRng;

namespace {

const std::filesystem::path kReplayDir{WH40K_REPLAY_DIR};
constexpr std::uint64_t kM60Seed = 2;  // M60: picked by Replay.DISABLED_SearchM60Seed
constexpr std::uint64_t kM61Seed = 1;  // M61: picked by Replay.DISABLED_SearchM61Seed
constexpr std::uint64_t kM62Seed = 10;  // M62: picked by Replay.DISABLED_SearchM62Seed
constexpr std::uint64_t kM63Seed = 1;   // M63: picked by Replay.DISABLED_SearchM63Seed
constexpr std::uint64_t kM65Seed = 1;   // M65: picked by Replay.DISABLED_SearchM65Seed
constexpr std::uint64_t kM67Seed = 1;   // M67: picked by Replay.DISABLED_SearchM67Seed
constexpr std::uint64_t kM72Seed = 10;  // M72: picked by Replay.DISABLED_SearchM72Seed (5 profiles fired)

// Plays random actions through a Match until GameOver or `max_actions`.
void random_play(Match& match, TestRng& rng, int max_actions) {
    for (int i = 0; i < max_actions && match.state().pending; ++i) {
        const GameAction a = wh40k::test::random_action(match.state(), rng);
        const auto r = match.submit(decider(match.state()), a);
        ASSERT_TRUE(r.ok()) << r.reasons[0];
    }
}

void expect_replays_identically(const Match& match) {
    const Replay replay = Json::parse(Json(match.to_replay("test")).dump()).get<Replay>();  // via JSON
    const ReplayResult result = run_replay(replay, repo_root());
    ASSERT_TRUE(result.ok()) << "step " << *result.failed_step << ": " << result.reasons[0];
    EXPECT_EQ(Json(result.final_state).dump(), Json(match.state()).dump());
    EXPECT_EQ(result.events, match.events());
    EXPECT_EQ(replay_expectation(result.final_state), replay.expect);
}

// Writes a regenerated replay only if nothing has failed so far: an ASSERT inside a scripting lambda
// returns from the lambda, not the test, so a failed step must not let a different game overwrite a
// committed regression replay.
void save_if_clean(const Replay& replay, const std::filesystem::path& path) {
    if (::testing::Test::HasFailure()) {
        ADD_FAILURE() << "not writing " << path << ": an earlier step failed";
        return;
    }
    save_replay(replay, path);
}

// M60 (D-071): the Havocs vs Hive Tyrant fixture with the Tyrant's bonesword replaced by a heavy venom cannon (a
// datasheet option), so its only melee weapon is its scything talons ([EXTRA ATTACKS], selected alone: still legal
// once M61 makes the bonesword selectable). Embedded initial state, so the file needs no tests/data scenario.
// `ok` says whether the seed shows what the replay is about: the hazard roll fails and the talons destroy a Havoc;
// `problem`, the first refused step (none expected).
Replay m60_replay(std::uint64_t seed, bool& ok, std::string& problem) {
    ok = false;
    Replay r;
    r.description =
        "M60: the Havocs vs Hive Tyrant fixture (D-070), the Tyrant with a heavy venom cannon instead of its bonesword, "
        "so its scything talons ([EXTRA ATTACKS], 24.11) are its only melee weapon. Round 1, Side A: the Havocs remain "
        "stationary; the champion fires its plasma gun on supercharge ([HAZARDOUS]) at the Tyrant, 8\" away (2 shots, "
        "Rapid Fire 1). After the attacks the Havocs make one hazard roll (24.15, 06.03); it fails, and Side A gives "
        "the mortal wound to the last lascannon Havoc (06.02, D-013). Side B: the Tyrant moves 4\" south, declares a "
        "charge at the Havocs and moves into base contact with the middle Havoc; it fights first with its talons "
        "alone (the heavy venom cannon is a ranged weapon; D-071) and slays the wounded Havoc (05.04: a model that "
        "has lost wounds first), then the four Havocs left fight back. Other casualties in unit order. Stops at Side "
        "B's first Consolidate decision.";
    GameState init = load_scenario(repo_root() / wh40k::test::kHavocsVsTyrantFixture, repo_root());
    const Datasheet tyrant_sheet = load_datasheet(repo_root() / "data/units/hive_tyrant.json");
    init.units[1].models[0].weapons[0] = *tyrant_sheet.find_weapon("Heavy venom cannon");
    init.dice.seed = seed;
    r.initial.state = init;
    GameState g = replay_initial_state(r, repo_root());
    start_game(g);
    bool failed = false;
    auto act = [&](const GameAction& a) {
        const Side side = decider(g);
        const auto v = validate_action(g, side, a);
        if (!v.ok()) {
            if (!failed) problem = v.reasons[0];
            failed = true;
            return;
        }
        r.steps.push_back(ReplayStep{side, a});
        apply_action(g, side, a);
    };
    const UnitId havocs = g.units[0].id;
    const UnitId tyrant = g.units[1].id;
    auto first_legal = [&]() -> GameAction {
        GameAction a = legal_actions(g, decider(g)).front();
        if (auto* oc = std::get_if<action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
            oc->priority = wh40k::test::model_ids(*g.find_unit(oc->target));
        }
        return a;
    };
    act(action::StartMove{havocs, MoveType::RemainStationary, std::nullopt});
    const Weapon& plasma = g.units[0].models[0].weapons[0];
    const std::size_t supercharge = plasma.profiles[1].has<ability::Hazardous>() ? 1 : 0;
    act(action::Shoot{havocs, ShootingType::Normal, {ShotDeclaration{g.units[0].models[0].id, 0, supercharge, tyrant}}});
    bool hazard_failed = false;
    while (!failed && g.flow.attack) {
        if (g.flow.attack->pending_mortal_wounds > 0) {
            hazard_failed = true;
            std::vector<ModelId> priority = wh40k::test::model_ids(g.units[0]);
            std::reverse(priority.begin(), priority.end());  // the last lascannon Havoc takes it
            act(action::OrderCasualties{havocs, priority});
        } else {
            act(first_legal());
        }
    }
    act(action::EndShootStep{});
    act(action::EndChargeStep{});
    while (!failed && !(g.pending && *g.pending == PendingDecision{Side::B, decision::MoveUnits{}})) act(first_legal());
    act(action::StartMove{tyrant, MoveType::Normal, std::nullopt});
    const Position p = *move_start_position(g, g.units[1].models[0].id);
    act(action::ProposeModelMove{g.units[1].models[0].id, {Position{p.x, p.y - wh40k::test::in(4), 0}}});
    act(action::CommitUnitMove{});
    act(action::EndShootStep{});
    act(action::DeclareCharge{tyrant});
    if (failed || !g.pending || !std::holds_alternative<decision::SelectChargeTargets>(g.pending->kind)) return r;
    act(action::StartChargeMove{tyrant, {havocs}});
    {
        // Straight at the middle Havoc: into base contact if the roll allows, else as far as it goes.
        const Model& t = g.units[1].models[0];
        const Model& h = g.units[0].models[2];
        const Position from = *move_start_position(g, t.id);
        const float dx = h.position.x - from.x;
        const float dy = h.position.y - from.y;
        const float centre = std::sqrt(dx * dx + dy * dy);
        const float contact = centre - base_radius(t.base) - base_radius(h.base);
        const float step = std::min(contact, max_move_distance_mm(g, t.id).value_or(0.0f));
        act(action::ProposeModelMove{t.id, {Position{from.x + dx / centre * step, from.y + dy / centre * step, 0}}});
    }
    act(action::CommitUnitMove{});
    if (failed || g.flow.stage != Stage::Charge) return r;
    act(action::EndChargeStep{});
    while (!failed && g.pending && std::holds_alternative<decision::PileIn>(g.pending->kind)) act(action::EndPileIns{});
    if (failed || !g.pending || *g.pending != PendingDecision{Side::B, decision::SelectUnitToFight{}}) return r;
    act(action::SelectToFight{tyrant, FightType::Normal});
    const std::size_t havoc_models = g.units[0].models.size();
    act(action::Fight{tyrant, {MeleeDeclaration{g.units[1].models[0].id, 1, 0, {MeleeTarget{havocs, 4}}}}});
    while (!failed && g.flow.attack) act(first_legal());
    const bool talons_killed = g.units[0].models.size() < havoc_models;
    if (failed || !g.pending || *g.pending != PendingDecision{Side::A, decision::SelectUnitToFight{}}) return r;
    act(action::SelectToFight{havocs, FightType::Normal});
    act(wh40k::test::first_melee_options(g, havocs));
    while (!failed && g.flow.attack) act(first_legal());
    while (!failed && g.pending && !std::holds_alternative<decision::Consolidate>(g.pending->kind)) act(first_legal());
    if (failed || !g.pending || g.pending->side != Side::B) return r;
    r.expect = replay_expectation(g);
    ok = hazard_failed && talons_killed;
    return r;
}

// M61 (D-072): the Havocs vs Hive Tyrant fixture as it is, so the Tyrant fights with its [TWIN-LINKED] bonesword and
// its scything talons (24.11). Embedded initial state, so the file needs no tests/data scenario. `ok` says whether the
// seed shows what the replay is about: a bonesword wound roll fails, the Tyrant re-rolls every failed one (24.38) and
// at least one re-roll wounds; `problem`, the first refused step (none expected).
Replay m61_replay(std::uint64_t seed, bool& ok, std::string& problem) {
    ok = false;
    Replay r;
    r.description =
        "M61: the Havocs vs Hive Tyrant fixture (D-070). Round 1, Side A: the Havocs remain stationary and don't shoot. "
        "Side B: the Tyrant moves 4\" south, declares a charge at the Havocs and moves into base contact with the middle "
        "Havoc; it fights first with its bonesword and lash whip ([TWIN-LINKED], 24.38) and its scything talons "
        "([EXTRA ATTACKS], 24.11), resolving the bonesword first (04.03). A bonesword wound roll fails, so Side B is "
        "asked which wound rolls to re-roll (D-072) and re-rolls every failed one (D-011: once, before modifiers); "
        "then the saves, casualties in unit order, the talons, and the Havocs fighting back. Stops at Side B's first "
        "Consolidate decision.";
    GameState init = load_scenario(repo_root() / wh40k::test::kHavocsVsTyrantFixture, repo_root());
    init.dice.seed = seed;
    r.initial.state = init;
    GameState g = replay_initial_state(r, repo_root());
    start_game(g);
    bool failed = false;
    std::vector<GameEvent> events;
    auto act = [&](const GameAction& a) {
        if (failed || !g.pending) {
            failed = true;
            return;
        }
        const Side side = decider(g);
        const auto v = validate_action(g, side, a);
        if (!v.ok()) {
            if (problem.empty()) problem = v.reasons[0];
            failed = true;
            return;
        }
        r.steps.push_back(ReplayStep{side, a});
        const auto e = apply_action(g, side, a);
        events.insert(events.end(), e.begin(), e.end());
    };
    auto first_legal = [&]() -> GameAction {
        GameAction a = legal_actions(g, decider(g)).front();
        if (auto* oc = std::get_if<action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
            oc->priority = wh40k::test::model_ids(*g.find_unit(oc->target));
        }
        return a;
    };
    const UnitId havocs = g.units[0].id;
    const UnitId tyrant = g.units[1].id;
    act(action::StartMove{havocs, MoveType::RemainStationary, std::nullopt});
    act(action::EndShootStep{});
    act(action::EndChargeStep{});
    while (!failed && g.pending && !(*g.pending == PendingDecision{Side::B, decision::MoveUnits{}})) act(first_legal());
    act(action::StartMove{tyrant, MoveType::Normal, std::nullopt});
    if (failed) return r;
    const Position p = *move_start_position(g, g.units[1].models[0].id);
    act(action::ProposeModelMove{g.units[1].models[0].id, {Position{p.x, p.y - wh40k::test::in(4), 0}}});
    act(action::CommitUnitMove{});
    while (!failed && g.pending && std::holds_alternative<decision::Shoot>(g.pending->kind)) act(first_legal());
    act(action::DeclareCharge{tyrant});
    if (failed || !g.pending || !std::holds_alternative<decision::SelectChargeTargets>(g.pending->kind)) return r;
    act(action::StartChargeMove{tyrant, {havocs}});
    {
        // Straight at the middle Havoc: into base contact if the roll allows, else as far as it goes.
        const Model& t = g.units[1].models[0];
        const Model& h = g.units[0].models[2];
        const Position from = *move_start_position(g, t.id);
        const float dx = h.position.x - from.x;
        const float dy = h.position.y - from.y;
        const float centre = std::sqrt(dx * dx + dy * dy);
        const float contact = centre - base_radius(t.base) - base_radius(h.base);
        const float step = std::min(contact, max_move_distance_mm(g, t.id).value_or(0.0f));
        act(action::ProposeModelMove{t.id, {Position{from.x + dx / centre * step, from.y + dy / centre * step, 0}}});
    }
    act(action::CommitUnitMove{});
    if (failed || g.flow.stage != Stage::Charge) return r;
    act(action::EndChargeStep{});
    while (!failed && g.pending && std::holds_alternative<decision::PileIn>(g.pending->kind)) act(action::EndPileIns{});
    if (failed || !g.pending || *g.pending != PendingDecision{Side::B, decision::SelectUnitToFight{}}) return r;
    act(action::SelectToFight{tyrant, FightType::Normal});
    const Model& t = g.units[1].models[0];
    act(action::Fight{tyrant,
                      {MeleeDeclaration{t.id, 0, 0, {MeleeTarget{havocs, t.weapons[0].profiles[0].attacks.fixed_value()}}},
                       MeleeDeclaration{t.id, 1, 0, {MeleeTarget{havocs, t.weapons[1].profiles[0].attacks.fixed_value()}}}}});
    bool asked = false;
    bool a_reroll_wounded = false;
    if (!failed && g.pending && std::holds_alternative<decision::SelectAttackBatch>(g.pending->kind)) {
        std::size_t bonesword = 0;
        for (std::size_t i = 0; i < g.flow.attack->batches.size(); ++i) {
            if (twin_linked(g.flow.attack->batches[i].profile)) bonesword = i;
        }
        act(action::SelectAttackBatch{bonesword});
    }
    while (!failed && g.flow.attack) {
        if (g.pending && std::holds_alternative<decision::RerollWoundRolls>(g.pending->kind)) {
            asked = true;
            const std::size_t before = events.size();
            act(first_legal());  // 24.38: every failed wound roll (listed first)
            for (std::size_t i = before; i < events.size(); ++i) {
                if (const auto* w = std::get_if<event::WoundRollsRerolled>(&events[i])) {
                    for (const WoundReroll& x : w->rerolls) a_reroll_wounded |= x.after.outcome != WoundOutcome::Fail;
                }
            }
        } else {
            act(first_legal());
        }
    }
    if (failed || !g.pending || *g.pending != PendingDecision{Side::A, decision::SelectUnitToFight{}}) return r;
    act(action::SelectToFight{havocs, FightType::Normal});
    act(wh40k::test::first_melee_options(g, havocs));
    while (!failed && g.flow.attack) act(first_legal());
    while (!failed && g.pending && !std::holds_alternative<decision::Consolidate>(g.pending->kind)) act(first_legal());
    if (failed || !g.pending || g.pending->side != Side::B) return r;
    r.expect = replay_expectation(g);
    ok = asked && a_reroll_wounded;
    return r;
}

// M62 (D-069): the Havocs vs Hive Tyrant fixture with the Tyrant's bonesword replaced by a heavy venom cannon (A D3, D3,
// [BLAST]; a datasheet option, as in m60). Embedded initial state. `ok` says whether the seed shows what the replay is
// about: two or more lascannon attacks inflict damage, each rolling its D6+1 on the Tyrant, which survives; `problem`, the
// first refused step (none expected).
Replay m62_replay(std::uint64_t seed, bool& ok, std::string& problem) {
    ok = false;
    Replay r;
    r.description =
        "M62: the Havocs vs Hive Tyrant fixture (D-070), the Tyrant with a heavy venom cannon (A D3, D3, [BLAST]) instead "
        "of its bonesword. Round 1, Side A: the Havocs remain stationary and their four lascannons (D D6+1) fire at the "
        "Tyrant, 8\" away. Side B orders the casualties once for the batch (D-013); each attack that inflicts damage then "
        "rolls its D6+1 after the Tyrant is selected (D-069, 05.04), and the wounded Tyrant (W10) takes every roll and "
        "survives. Side B: the Tyrant remains stationary and fires its heavy venom cannon at the five Havocs: its A is "
        "rolled (D3) when the attacks are resolved, then +1 [BLAST] for the five models (04.03, 24.05); casualties in "
        "unit order. Every other decision takes the first legal action. Stops at Side B's first Charge decision.";
    GameState init = load_scenario(repo_root() / wh40k::test::kHavocsVsTyrantFixture, repo_root());
    const Datasheet tyrant_sheet = load_datasheet(repo_root() / "data/units/hive_tyrant.json");
    init.units[1].models[0].weapons[0] = *tyrant_sheet.find_weapon("Heavy venom cannon");
    init.dice.seed = seed;
    r.initial.state = init;
    GameState g = replay_initial_state(r, repo_root());
    start_game(g);
    bool failed = false;
    std::vector<GameEvent> events;
    auto act = [&](const GameAction& a) {
        if (failed || !g.pending) {
            failed = true;
            return;
        }
        const Side side = decider(g);
        const auto v = validate_action(g, side, a);
        if (!v.ok()) {
            if (problem.empty()) problem = v.reasons[0];
            failed = true;
            return;
        }
        r.steps.push_back(ReplayStep{side, a});
        const auto e = apply_action(g, side, a);
        events.insert(events.end(), e.begin(), e.end());
    };
    auto first_legal = [&]() -> GameAction {
        GameAction a = legal_actions(g, decider(g)).front();
        if (auto* oc = std::get_if<action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
            oc->priority = wh40k::test::model_ids(*g.find_unit(oc->target));
        }
        return a;
    };
    const UnitId havocs = g.units[0].id;
    const UnitId tyrant = g.units[1].id;
    act(action::StartMove{havocs, MoveType::RemainStationary, std::nullopt});
    {
        action::Shoot lascannons{havocs, ShootingType::Normal, {}};
        for (const Model& m : g.units[0].models) {
            if (m.weapons[0].name == "Havoc lascannon") lascannons.shots.push_back(ShotDeclaration{m.id, 0, 0, tyrant});
        }
        act(lascannons);
    }
    while (!failed && g.flow.attack) act(first_legal());
    int damage_rolls = 0;
    for (const GameEvent& e : events) {
        if (const auto* s = std::get_if<event::SavesResolved>(&e)) {
            for (const SaveStep& step : s->result.steps) damage_rolls += step.damage_roll ? 1 : 0;
        }
    }
    const bool tyrant_alive = !g.units[1].models.empty();
    if (failed || !tyrant_alive) return r;
    act(action::EndShootStep{});
    act(action::EndChargeStep{});
    while (!failed && g.pending && !(*g.pending == PendingDecision{Side::B, decision::MoveUnits{}})) act(first_legal());
    act(action::StartMove{tyrant, MoveType::RemainStationary, std::nullopt});
    if (failed || !g.pending || !std::holds_alternative<decision::Shoot>(g.pending->kind)) return r;
    const std::size_t before = events.size();
    act(action::Shoot{tyrant, ShootingType::Normal, {ShotDeclaration{g.units[1].models[0].id, 0, 0, havocs}}});
    while (!failed && g.flow.attack) act(first_legal());
    bool cannon_rolled = false;
    for (std::size_t i = before; i < events.size(); ++i) {
        if (const auto* a = std::get_if<event::AttackDiceRolled>(&events[i])) {
            cannon_rolled = a->rolls.size() == 1 && a->rolls[0].bonus == 1;
        }
    }
    act(action::EndShootStep{});
    if (failed || !g.pending || *g.pending != PendingDecision{Side::B, decision::Charge{}}) return r;
    r.expect = replay_expectation(g);
    ok = damage_rolls >= 2 && cannon_rolled;
    return r;
}

// M63 (D-073): the monster fixture (tests/support/monster_fixtures.hpp: a few roster units placed by hand), embedded.
// `ok` says whether the seed shows what the replay is about: the Tyrant's charge ends engaged with the Legionaries, it
// is still engaged (and alive) in Side A's Shooting phase, the lascannons roll at -1 (17.03) and the Legionaries' bolt
// pistols at the Tyrant they are engaged with at 0 (the close-quarters exception); `problem`, the first refused step
// (none expected).
Replay m63_replay(std::uint64_t seed, bool& ok, std::string& problem) {
    namespace mf = wh40k::test::monsters;
    ok = false;
    Replay r;
    r.description =
        "M63: Monsters and Vehicles (17.01, 17.03, D-073) on the monster fixture (roster units placed by hand on the "
        "44\"x60\" board; embedded state). Round 1, Side B first: the Hive Tyrant makes a Normal move 7.7\" south "
        "straight through the chainsword Legionaries' rank (17.01: a MONSTER moves through enemy models that aren't "
        "MONSTER/VEHICLE models), ending 2.04\" beyond it, unengaged (09.05). It declares a charge at the Legionaries "
        "and moves into base contact (or as far as its roll allows); it fights first, then the Legionaries fight back "
        "(every other decision takes the first legal action; casualties in unit order). Side A: every unit remains "
        "stationary; the unengaged Havocs fire their four lascannons at the engaged Tyrant at -1 to hit (17.03), then "
        "the engaged Legionaries fire their bolt pistols at it in close-quarters shooting with no penalty ([PISTOL] = "
        "[CLOSE-QUARTERS] weapons of a unit engaged with the target, 10.06, 17.03). Stops at Side A's first Charge "
        "decision.";
    GameState init = mf::fixture(false, Side::B);
    init.dice.seed = seed;
    r.initial.state = init;
    GameState g = replay_initial_state(r, repo_root());
    start_game(g);
    bool failed = false;
    std::vector<GameEvent> events;
    auto act = [&](const GameAction& a) {
        if (failed || !g.pending) {
            failed = true;
            return;
        }
        const Side side = decider(g);
        const auto v = validate_action(g, side, a);
        if (!v.ok()) {
            if (problem.empty()) problem = v.reasons[0];
            failed = true;
            return;
        }
        r.steps.push_back(ReplayStep{side, a});
        const auto e = apply_action(g, side, a);
        events.insert(events.end(), e.begin(), e.end());
    };
    // The scripted answer to anything not asked about below.
    auto answer = [&]() -> GameAction {
        const DecisionKind& k = g.pending->kind;
        if (std::holds_alternative<decision::Shoot>(k)) return action::EndShootStep{};
        if (std::holds_alternative<decision::Charge>(k)) return action::EndChargeStep{};
        if (std::holds_alternative<decision::PileIn>(k)) return action::EndPileIns{};
        if (std::holds_alternative<decision::Consolidate>(k)) return action::EndConsolidations{};
        if (const auto* o = std::get_if<decision::OverrunPileIn>(&k)) return action::SkipPileIn{o->unit};
        if (const auto* d = std::get_if<decision::DeclareMeleeAttacks>(&k)) return wh40k::test::first_melee_options(g, d->unit);
        GameAction a = legal_actions(g, decider(g)).front();
        if (auto* oc = std::get_if<action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
            oc->priority = wh40k::test::model_ids(*g.find_unit(oc->target));
        }
        return a;
    };
    const UnitId tyrant = g.units[mf::kTyrant].id;
    const UnitId legionaries = g.units[mf::kLegionaries].id;
    const UnitId havocs = g.units[mf::kHavocs].id;
    const ModelId tyrant_model = g.units[mf::kTyrant].models[0].id;
    while (!failed && g.pending && !(*g.pending == PendingDecision{Side::B, decision::MoveUnits{}})) act(answer());
    act(action::StartMove{tyrant, MoveType::Normal, std::nullopt});
    act(action::ProposeModelMove{tyrant_model, {wh40k::test::at(22.0f, mf::kThroughY)}});
    act(action::CommitUnitMove{});
    if (failed || g.units[mf::kTyrant].models[0].position != wh40k::test::at(22.0f, mf::kThroughY)) return r;
    while (!failed && g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind)) act(answer());
    act(action::EndShootStep{});
    act(action::DeclareCharge{tyrant});
    if (failed || !g.pending || !std::holds_alternative<decision::SelectChargeTargets>(g.pending->kind)) return r;
    act(action::StartChargeMove{tyrant, {legionaries}});
    {
        // Straight north at the rank: into base contact if the roll allows, else as far as it goes (2" or more: engaged).
        const float gap = wh40k::test::in(mf::kRankY) - wh40k::test::in(mf::kThroughY) - base_radius(g.units[mf::kTyrant].models[0].base) -
                          base_radius(g.units[mf::kLegionaries].models[0].base);
        const float step = std::min(gap, max_move_distance_mm(g, tyrant_model).value_or(0.0f));
        const Position from = *move_start_position(g, tyrant_model);
        act(action::ProposeModelMove{tyrant_model, {Position{from.x, from.y + step, 0.0f}}});
    }
    act(action::CommitUnitMove{});
    if (failed || !g.units[mf::kTyrant].turn_flags.made_charge_move) return r;
    while (!failed && g.pending && !(*g.pending == PendingDecision{Side::A, decision::MoveUnits{}})) act(answer());
    while (!failed && g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind)) act(answer());
    if (failed || !g.pending || !std::holds_alternative<decision::Shoot>(g.pending->kind)) return r;
    if (g.units[mf::kTyrant].models.empty() || !is_engaged(g, g.units[mf::kTyrant])) return r;
    std::size_t before = events.size();
    {
        action::Shoot lascannons{havocs, ShootingType::Normal, {}};
        for (const Model& m : g.units[mf::kHavocs].models) {
            if (m.weapons[0].name == "Havoc lascannon") lascannons.shots.push_back(ShotDeclaration{m.id, 0, 0, tyrant});
        }
        act(lascannons);
    }
    while (!failed && g.flow.attack) act(answer());
    bool lascannons_at_minus_one = false;
    for (std::size_t i = before; i < events.size(); ++i) {
        if (const auto* h = std::get_if<event::HitRollsMade>(&events[i])) {
            lascannons_at_minus_one = !h->rolls.rolls.empty();
            for (const HitRoll& x : h->rolls.rolls) {
                if (x.unmodified && *x.unmodified != 1 && *x.unmodified != 6) {
                    lascannons_at_minus_one = lascannons_at_minus_one && x.modified == std::max(1, *x.unmodified - 1);
                }
            }
        }
    }
    if (failed || g.units[mf::kTyrant].models.empty() || g.units[mf::kLegionaries].models.empty()) return r;
    before = events.size();
    {
        action::Shoot pistols{legionaries, ShootingType::CloseQuarters, {}};
        for (const ShotDeclaration& d : shot_options(g, g.units[mf::kLegionaries], ShootingType::CloseQuarters)) {
            const bool taken = std::any_of(pistols.shots.begin(), pistols.shots.end(),
                                           [&](const ShotDeclaration& x) { return x.model == d.model; });
            if (d.target == tyrant && !taken) pistols.shots.push_back(d);
        }
        if (pistols.shots.empty()) return r;
        act(pistols);
    }
    while (!failed && g.flow.attack) act(answer());
    bool pistols_unmodified = false;
    for (std::size_t i = before; i < events.size(); ++i) {
        if (const auto* s = std::get_if<event::ShootingStarted>(&events[i])) {
            pistols_unmodified = !s->selections.empty() &&
                                 std::all_of(s->selections.begin(), s->selections.end(),
                                             [](const WeaponSelection& w) { return w.hit_modifier == 0; });
        }
    }
    act(action::EndShootStep{});
    if (failed || !g.pending || *g.pending != PendingDecision{Side::A, decision::Charge{}}) return r;
    r.expect = replay_expectation(g);
    ok = lascannons_at_minus_one && pistols_unmodified;
    return r;
}

// M65 (D-074 part 2): the Tyrannofex layout (tests/support/monster_fixtures.hpp: a few units of the Tyrannofex fixture
// placed by hand), embedded. `ok` says whether the seed shows what the replay is about: the Tyrannofex ends its Normal
// move turned to 45 degrees after 3.00" (so [HEAVY] still applies: the turn doesn't count, 24.16), fires both guns with
// the fleshborer hive's batch at +1, makes its charge move turned back to 0 and engaged, and fights; `problem`, the
// first refused step (none expected).
Replay m65_replay(std::uint64_t seed, bool& ok, std::string& problem) {
    namespace tf = wh40k::test::tyrannofex;
    ok = false;
    Replay r;
    r.description =
        "M65: Oval bases in the engine (01.04, 03.01, 11.04, D-074 part 2) on the Tyrannofex layout (roster units placed by "
        "hand on the 44\"x60\" board; embedded state). Round 1, Side B first: the Tyrannofex (a 120 x 92 mm oval, facing "
        "0, its length south to north) turns 45 degrees and makes a 3.00\" Normal move south (03.01: the turn adds "
        "nothing to the distance, so no model has moved more than 3\" and its [HEAVY] fleshborer hive gets +1, 24.16); "
        "it fires its stinger salvoes and fleshborer hive at the chainsword Legionaries; it declares a charge at them, "
        "turns back to 0 and moves straight at them, ending engaged (11.04, closer than its start at either facing); it "
        "fights first and the Legionaries fight back (every other decision takes the first legal action; casualties in "
        "unit order). Stops at Side A's first Move Units decision.";
    GameState init = tf::fixture(Side::B);
    init.dice.seed = seed;
    r.initial.state = init;
    GameState g = replay_initial_state(r, repo_root());
    start_game(g);
    bool failed = false;
    std::vector<GameEvent> events;
    auto act = [&](const GameAction& a) {
        if (failed || !g.pending) {
            failed = true;
            return;
        }
        const Side side = decider(g);
        const auto v = validate_action(g, side, a);
        if (!v.ok()) {
            if (problem.empty()) problem = v.reasons[0];
            failed = true;
            return;
        }
        r.steps.push_back(ReplayStep{side, a});
        const auto e = apply_action(g, side, a);
        events.insert(events.end(), e.begin(), e.end());
    };
    auto answer = [&]() -> GameAction {
        const DecisionKind& k = g.pending->kind;
        if (std::holds_alternative<decision::Shoot>(k)) return action::EndShootStep{};
        if (std::holds_alternative<decision::Charge>(k)) return action::EndChargeStep{};
        if (std::holds_alternative<decision::PileIn>(k)) return action::EndPileIns{};
        if (std::holds_alternative<decision::Consolidate>(k)) return action::EndConsolidations{};
        if (const auto* o = std::get_if<decision::OverrunPileIn>(&k)) return action::SkipPileIn{o->unit};
        if (const auto* d = std::get_if<decision::DeclareMeleeAttacks>(&k)) return wh40k::test::first_melee_options(g, d->unit);
        GameAction a = legal_actions(g, decider(g)).front();
        if (auto* oc = std::get_if<action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
            oc->priority = wh40k::test::model_ids(*g.find_unit(oc->target));
        }
        return a;
    };
    const UnitId tyrannofex = g.units[tf::kTyrannofex].id;
    const UnitId legionaries = g.units[tf::kLegionaries].id;
    const ModelId tyrannofex_model = g.units[tf::kTyrannofex].models[0].id;
    while (!failed && g.pending && !(*g.pending == PendingDecision{Side::B, decision::MoveUnits{}})) act(answer());
    // The Normal move: a turn to 45, then 3" south.
    const Position start = g.units[tf::kTyrannofex].models[0].position;
    const Position moved{start.x, start.y - wh40k::test::in(3.0f), 0.0f};
    act(action::StartMove{tyrannofex, MoveType::Normal, std::nullopt});
    act(action::ProposeModelMove{tyrannofex_model, {moved}, {45.0f}});
    act(action::CommitUnitMove{});
    if (failed || g.units[tf::kTyrannofex].models[0].position != moved ||
        g.units[tf::kTyrannofex].models[0].facing_deg != 45.0f || g.units[tf::kTyrannofex].turn_flags.moved_over_3in) {
        return r;
    }
    while (!failed && g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind)) act(answer());
    if (failed || !g.pending || !std::holds_alternative<decision::Shoot>(g.pending->kind)) return r;
    // Both guns at the Legionaries.
    std::size_t before = events.size();
    {
        action::Shoot guns{tyrannofex, ShootingType::Normal, {}};
        for (const ShotDeclaration& d : shot_options(g, g.units[tf::kTyrannofex], ShootingType::Normal)) {
            const bool taken = std::any_of(guns.shots.begin(), guns.shots.end(),
                                           [&](const ShotDeclaration& x) { return x.weapon_index == d.weapon_index; });
            if (d.target == legionaries && !taken) guns.shots.push_back(d);
        }
        if (guns.shots.size() != 2) return r;
        act(guns);
    }
    while (!failed && g.flow.attack) act(answer());
    bool heavy = false;
    for (std::size_t i = before; i < events.size(); ++i) {
        if (const auto* b = std::get_if<event::AttackBatchesGathered>(&events[i])) {
            for (const AttackBatch& batch : b->batches) {
                // The fleshborer hive's batch (its [TWIN-LINKED] marks it): [HEAVY] +1 (24.16).
                const bool hive = std::any_of(batch.profile.attack_time_abilities.begin(),
                                              batch.profile.attack_time_abilities.end(), [](const WeaponAbility& a) {
                                                  return std::holds_alternative<ability::TwinLinked>(a);
                                              });
                if (hive) heavy = batch.profile.hit_modifier == 1;
            }
        }
    }
    act(action::EndShootStep{});
    if (failed || g.units[tf::kLegionaries].models.empty()) return r;
    // The charge: turned back to 0, then straight at them by the gap at that facing (never onto a model: no line of
    // that length can make the gap negative).
    act(action::DeclareCharge{tyrannofex});
    if (failed || !g.pending || !std::holds_alternative<decision::SelectChargeTargets>(g.pending->kind)) return r;
    act(action::StartChargeMove{tyrannofex, {legionaries}});
    {
        const Position from = *move_start_position(g, tyrannofex_model);
        const float gap = *model_distance_to_unit(BasePose{from, g.units[tf::kTyrannofex].models[0].base, 0.0f},
                                                  g.units[tf::kLegionaries]);
        const float step = std::min(gap, max_move_distance_mm(g, tyrannofex_model).value_or(0.0f));
        act(action::ProposeModelMove{tyrannofex_model, {Position{from.x, from.y - step, 0.0f}}, {0.0f}});
    }
    act(action::CommitUnitMove{});
    if (failed || !g.units[tf::kTyrannofex].turn_flags.made_charge_move || g.units[tf::kTyrannofex].models[0].facing_deg != 0.0f) {
        return r;
    }
    before = events.size();
    while (!failed && g.pending && !(*g.pending == PendingDecision{Side::A, decision::MoveUnits{}})) act(answer());
    bool fought = false;
    bool fought_back = false;
    for (std::size_t i = before; i < events.size(); ++i) {
        if (const auto* m = std::get_if<event::MeleeAttacksDeclared>(&events[i])) {
            fought = fought || (m->unit == tyrannofex && !m->selections.empty());
            fought_back = fought_back || (m->unit == legionaries && !m->selections.empty());
        }
    }
    if (failed || !g.pending || *g.pending != PendingDecision{Side::A, decision::MoveUnits{}}) return r;
    r.expect = replay_expectation(g);
    ok = heavy && fought && fought_back;
    return r;
}

// M67 (D-076): the playable roster scenario (data/scenarios/rosters_44x60.json, a scenario-file replay). No unit can
// reach a charge in battle round 1 (36" between the zones, M 8" at most), so the replay runs to the start of battle
// round 3. `ok` says whether the seed shows what it is about: the Havocs' lascannons damage their target in round 1,
// the west Genestealers' charge in round 2 succeeds, and both they and the chainsword Legionaries fight; `problem`,
// the first refused step (none expected).
Replay m67_replay(std::uint64_t seed, bool& ok, std::string& problem) {
    ok = false;
    Replay r;
    r.description =
        "M67: the portrait roster scenario (data/scenarios/rosters_44x60.json; D-076's points tiebreak, D-064, D-070, "
        "D-074), battle rounds 1 and 2 (no unit can reach a charge in round 1: 36\" between the zones). Round 1: the "
        "chainsword Legionaries (unit 1) move 6\" north; every other Chaos Space Marine unit remains stationary; the "
        "Havocs (unit 4) fire their four lascannons at the Tyranid unit their first shot option names (48\", about "
        "39\" away), casualties in unit order. The west Genestealers (unit 7) move 8\" south; the rest remain "
        "stationary. Round 2: the chainsword Legionaries move 6\" north again, nothing shoots; the Genestealers move "
        "8\" south, declare a charge at the Legionaries and move straight at them (each model the charge roll or 1\" "
        "short of the closest gap, whichever is less), fight first, and the Legionaries fight back (first melee options, "
        "casualties in unit order, no pile-ins or consolidations). Every other decision takes the first legal action. "
        "Stops at Side A's first Move Units decision of battle round 3.";
    r.initial.scenario = wh40k::test::kRostersScenario;
    r.initial.seed = seed;
    GameState g = replay_initial_state(r, repo_root());
    std::vector<GameEvent> events = start_game(g);
    bool failed = false;
    auto act = [&](const GameAction& a) {
        if (failed || !g.pending) {
            failed = true;
            return;
        }
        const Side side = decider(g);
        const auto v = validate_action(g, side, a);
        if (!v.ok()) {
            if (problem.empty()) problem = v.reasons[0];
            failed = true;
            return;
        }
        r.steps.push_back(ReplayStep{side, a});
        const auto e = apply_action(g, side, a);
        events.insert(events.end(), e.begin(), e.end());
    };
    auto until = [&](Side side, int round) {
        while (!failed && g.pending && !(g.flow.battle_round == round && *g.pending == PendingDecision{side, decision::MoveUnits{}})) {
            act(wh40k::test::neutral_action(g));
        }
    };
    // A straight move of every model by dy inches, the leading rank first.
    auto move = [&](std::uint32_t id, MoveType type, float dy_in) {
        act(action::StartMove{UnitId{id}, type, std::nullopt});
        std::vector<Model> models = g.find_unit(UnitId{id})->models;
        std::stable_sort(models.begin(), models.end(), [&](const Model& a, const Model& b) {
            return dy_in > 0.0f ? a.position.y > b.position.y : a.position.y < b.position.y;
        });
        for (const Model& m : models) {
            const Position p = *move_start_position(g, m.id);
            act(action::ProposeModelMove{m.id, {Position{p.x, p.y + wh40k::test::in(dy_in), 0.0f}}, {}});
        }
        act(action::CommitUnitMove{});
    };
    constexpr std::uint32_t kLegionaries = 1;
    constexpr std::uint32_t kHavocs = 4;
    constexpr std::uint32_t kGenestealers = 7;
    // Round 1, Side A.
    until(Side::A, 1);
    move(kLegionaries, MoveType::Normal, 6.0f);
    while (!failed && g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind)) {
        act(wh40k::test::neutral_action(g));  // the rest remain stationary
    }
    if (failed || !g.pending || !std::holds_alternative<decision::Shoot>(g.pending->kind)) return r;
    action::Shoot volley{UnitId{kHavocs}, ShootingType::Normal, {}};
    {
        const Unit& havocs = *g.find_unit(UnitId{kHavocs});
        std::optional<UnitId> target;
        std::vector<std::pair<ModelId, std::size_t>> taken;
        for (const ShotDeclaration& d : shot_options(g, havocs, ShootingType::Normal)) {
            if (find_model(havocs, d.model)->weapons.at(d.weapon_index).name != "Havoc lascannon") continue;
            if (!target) target = d.target;
            const std::pair<ModelId, std::size_t> key{d.model, d.weapon_index};
            if (d.target != *target || std::find(taken.begin(), taken.end(), key) != taken.end()) continue;
            taken.push_back(key);
            volley.shots.push_back(d);
        }
        if (volley.shots.size() != 4) return r;
    }
    const UnitId target = volley.shots.front().target;
    const int target_wounds_before = [&] {
        int w = 0;
        for (const Model& m : g.find_unit(target)->models) w += m.wounds_remaining;
        return w;
    }();
    act(volley);
    while (!failed && g.flow.attack) act(wh40k::test::neutral_action(g));
    int target_wounds_after = 0;
    for (const Model& m : g.find_unit(target)->models) target_wounds_after += m.wounds_remaining;
    const bool shot_hurt = target_wounds_after < target_wounds_before;
    // Round 1, Side B.
    until(Side::B, 1);
    move(kGenestealers, MoveType::Normal, -8.0f);
    // Round 2.
    until(Side::A, 2);
    move(kLegionaries, MoveType::Normal, 6.0f);
    until(Side::B, 2);
    move(kGenestealers, MoveType::Normal, -8.0f);
    while (!failed && g.pending && !std::holds_alternative<decision::Charge>(g.pending->kind)) {
        act(wh40k::test::neutral_action(g));
    }
    if (failed || !g.pending) return r;
    act(action::DeclareCharge{UnitId{kGenestealers}});
    if (failed || !g.pending || !std::holds_alternative<decision::SelectChargeTargets>(g.pending->kind)) return r;
    const int roll = g.find_unit(UnitId{kGenestealers})->phase_flags.charge_roll.value_or(0);
    const float gap = *unit_distance(*g.find_unit(UnitId{kGenestealers}), *g.find_unit(UnitId{kLegionaries}));
    const float step = std::min(wh40k::test::in(static_cast<float>(roll)), gap - wh40k::test::in(1.0f));
    if (gap - step > wh40k::test::in(2.0f) - 0.1f) {  // the roll is too short to end engaged
        act(action::EndChargeStep{});
        return r;
    }
    act(action::StartChargeMove{UnitId{kGenestealers}, {UnitId{kLegionaries}}});
    for (const Model& m : g.find_unit(UnitId{kGenestealers})->models) {
        const Position p = *move_start_position(g, m.id);
        act(action::ProposeModelMove{m.id, {Position{p.x, p.y - step, 0.0f}}, {}});
    }
    act(action::CommitUnitMove{});
    if (failed || !g.find_unit(UnitId{kGenestealers})->turn_flags.made_charge_move) return r;
    const std::size_t before = events.size();
    until(Side::A, 3);
    if (failed || !g.pending || g.flow.battle_round != 3) return r;
    bool fought = false;
    bool fought_back = false;
    for (std::size_t i = before; i < events.size(); ++i) {
        if (const auto* m = std::get_if<event::MeleeAttacksDeclared>(&events[i])) {
            fought = fought || (m->unit == UnitId{kGenestealers} && !m->selections.empty());
            fought_back = fought_back || (m->unit == UnitId{kLegionaries} && !m->selections.empty());
        }
    }
    r.expect = replay_expectation(g);
    ok = shot_hurt && fought && fought_back;
    return r;
}

// M72 (D-079 part 2): a custom battle of the two showcase armies (tests/support/army_fixtures.hpp), auto-placed, played
// at random (TestRng(seed)) through the end of battle round 2. Initial-state form, so the file needs no army or
// scenario file. `shots` gets the weapon profiles fired.
Replay m72_replay(std::uint64_t seed, std::set<std::string>& shots) {
    GameState init = wh40k::test::custom_battle(wh40k::test::showcase_csm(), wh40k::test::showcase_tyranids());
    init.dice.seed = seed;
    Match match(init);
    TestRng rng(seed);
    while (match.state().pending && match.state().flow.battle_round < 3) {
        const GameAction a = wh40k::test::random_action(match.state(), rng);
        if (const auto* sh = std::get_if<action::Shoot>(&a)) {
            for (const ShotDeclaration& d : sh->shots) {
                const Model* m = find_model(match.state(), d.model);
                if (m != nullptr) shots.insert(m->weapons.at(d.weapon_index).profiles.at(d.profile_index).name);
            }
        }
        const auto r = match.submit(decider(match.state()), a);
        EXPECT_TRUE(r.ok()) << r.reasons.at(0);
        if (!r.ok()) break;
    }
    return match.to_replay(
        "M72: a custom battle (D-079 part 2) of two non-stock legal armies, auto-placed by the D-079 layout on the 44\" "
        "x 60\" portrait battlefield (Incursion, decided on points destroyed, D-076). Side A, Chaos Space Marines (875 "
        "points): a Chaos Lord with an accursed weapon and a power fist; Havocs with a champion's two power fists and an "
        "autocannon, a reaper chaincannon, a missile launcher and a heavy bolter; 10 Terminators with 5 power fists and "
        "two paired accursed weapons; 10 Legionaries with a reaper chaincannon, a missile launcher and a heavy melee "
        "weapon; 5 Legionaries with the plasma pistol and chainsword special. Side B, Tyranids (900 points): a Hive "
        "Tyrant with two scything talons; 20 Termagants with spinefists, devourers, two Shardlaunchers and two Spike "
        "rifles; 6 Tyrant Guard, two of each loadout; a Tyrannofex with a rupture cannon and one with acid spray; 10 "
        "Termagants. Random legal play (the tests' TestRng, seed " +
        std::to_string(seed) + ") through the end of battle round 2; stops at the first decision of battle round 3. The "
        "initial state is embedded.");
}

// M74 (D-081): the stock armies' custom battle set up by hand, B (Tyranids) taking the first turn, so A (Chaos Space
// Marines) sets up first. The scenario file is custom_battle_scenario of the two stock army lists in Hand mode (written
// by the regenerate test, checked by D081_Replay.TheFixtureIsTheStockArmiesSetUpByHand).
constexpr const char* kM74Fixture = "tests/data/scenarios/hand_deployment_44x60.json";
constexpr std::uint64_t kM74Seed = 1;

nlohmann::json m74_fixture() {
    return custom_battle_scenario(wh40k::test::stock_army(wh40k::test::csm()),
                                  wh40k::test::stock_army(wh40k::test::tyranids()), Side::B, wh40k::test::factions(),
                                  wh40k::test::army_resolver(), DeploymentMode::Hand);
}

// Each side sets up its lowest-id unit waiting, at the engine's proposal, except the Tyrannofex (unit 11): Side B sets
// it up by hand, turned 30 degrees, on the spot the auto-layout gives the Screamer-killer after it (unit 12; stepped
// back 1/4" at a time until it is wholly within the zone), so that unit's proposal comes from the scan. Then Side B's
// first turn with neutral answers (every unit remains stationary, every step ended), to Side A's first Move Units
// decision of battle round 1. `ok`: the Tyrannofex stands turned and the Screamer-killer's proposal came from the scan;
// `problem`: the first refused step (none expected).
Replay m74_replay(bool& ok, std::string& problem) {
    ok = false;
    Replay r;
    r.description =
        "M74: deployment by hand (D-081) on the stock armies' custom battle (tests/data/scenarios/hand_deployment_44x60.json: "
        "custom_battle_scenario of the two stock Incursion lists with \"deployment\": \"alternating\"; D-079, D-076). "
        "Side B (Tyranids) takes the first turn, so Side A (Chaos Space Marines) sets up first: A and B alternate one "
        "unit at a time until A's five units are down, then B sets up its other four. Each unit is set up at the "
        "engine's proposal (propose_set_up: its auto-layout spot) except the Tyrannofex (unit 11), which Side B sets up "
        "by hand turned 30 degrees on the spot the auto-layout gives the Screamer-killer after it (unit 12), stepped "
        "back until it is wholly within the zone; the Screamer-killer's proposal then comes from the scan. Battle "
        "round 1 then starts as an auto-placed game's would (D-003); Side B's turn passes (every unit remains "
        "stationary, every step ended). Stops at Side A's first Move Units decision of battle round 1.";
    r.initial.scenario = kM74Fixture;
    r.initial.seed = kM74Seed;
    GameState g = replay_initial_state(r, repo_root());
    start_game(g);
    GameState placed = build_game_state(custom_battle_scenario(wh40k::test::stock_army(wh40k::test::csm()),
                                                               wh40k::test::stock_army(wh40k::test::tyranids()), Side::B,
                                                               wh40k::test::factions(), wh40k::test::army_resolver()),
                                        wh40k::test::army_resolver());
    bool failed = false;
    auto act = [&](const GameAction& a) {
        if (failed || !g.pending) {
            failed = true;
            return;
        }
        const Side side = decider(g);
        const auto v = validate_action(g, side, a);
        if (!v.ok()) {
            if (problem.empty()) problem = v.reasons[0];
            failed = true;
            return;
        }
        r.steps.push_back(ReplayStep{side, a});
        (void)apply_action(g, side, a);
    };
    bool turned = false;
    bool scanned = false;
    const Position screamer_slot = placed.find_unit(UnitId{12})->models.front().position;
    while (!failed && g.flow.stage == Stage::Deploy) {
        const Side side = decider(g);
        UnitId next{0};
        for (const Unit& u : g.undeployed) {
            if (u.side == side) {
                next = u.id;
                break;
            }
        }
        if (next == UnitId{11}) {
            const ModelId tyrannofex = find_undeployed(g, next)->models.front().id;
            for (int step = 0; step <= 24 && !turned; ++step) {
                // B's zone is the north one: back is north.
                const action::DeployUnit a{
                    next, {ModelPlacement{tyrannofex,
                                          Position{screamer_slot.x, screamer_slot.y + wh40k::test::in(0.25f * static_cast<float>(step)), 0.0f},
                                          30.0f}}};
                if (validate_action(g, side, a).ok()) {
                    act(a);
                    turned = true;
                }
            }
            if (!turned) failed = true;
            continue;
        }
        const std::optional<action::DeployUnit> p = propose_set_up(g, next);
        if (next == UnitId{12} && p && p->models.front().position != screamer_slot) scanned = true;
        act(p ? GameAction{*p} : GameAction{action::CannotSetUp{next}});
    }
    while (!failed && g.pending &&
           !(g.flow.battle_round == 1 && *g.pending == PendingDecision{Side::A, decision::MoveUnits{}})) {
        act(wh40k::test::neutral_action(g));
    }
    if (failed || !g.pending) return r;
    r.expect = replay_expectation(g);
    ok = turned && scanned;
    return r;
}

// M86 (D-084): a short game on the objective scenario (a scenario-file replay). `ok` says whether the seed shows what
// it is about: the Terminators take the Centre at the end of Side A's round-2 Movement phase, Side B scores its home
// in its round-2 Command phase and Side A the Centre in its round-3 one; `problem`, the first refused step.
// M87 (D-086) tuned the shipped objective scenario's numbers; the replay plays the scenario as M86 shipped it (the test
// fixture, the same file at M86).
constexpr const char* kM86Scenario = "tests/data/scenarios/m86_objectives_44x60.json";
constexpr std::uint64_t kM86Seed = 86;

Replay m86_replay(std::uint64_t seed, bool& ok, std::string& problem) {
    ok = false;
    Replay r;
    r.description =
        "M86: the objective scenario as M86 shipped it (tests/data/scenarios/m86_objectives_44x60.json; D-084, 14.02): "
        "the rosters "
        "armies with a 12\" Centre and a 6\" home per side. Round 1: the Chaos Terminator Squad (unit 3) advances "
        "straight north (5\" + the advance roll); every other Chaos Space Marine unit remains stationary. The east "
        "Genestealers (unit 13) move 8\" south; the rest of Side B remains stationary (its held-back Tyrant Guard and "
        "Tyrannofex stand on B's home, which Side B controls from the end of Side A's first Command phase). Round 2: "
        "the Terminators move 5\" north onto the Centre, which Side A controls from the end of that Movement phase; "
        "Side B scores its home (2 VP) in its Command phase. Nothing shoots or charges. Every other decision takes the "
        "first legal action. Stops at Side A's first Move Units decision of battle round 3, after Side A scores the "
        "Centre (5 VP) in its Command phase.";
    r.initial.scenario = kM86Scenario;
    r.initial.seed = seed;
    GameState g = replay_initial_state(r, repo_root());
    std::vector<GameEvent> events = start_game(g);
    bool failed = false;
    auto act = [&](const GameAction& a) {
        if (failed || !g.pending) {
            failed = true;
            return;
        }
        const Side side = decider(g);
        const auto v = validate_action(g, side, a);
        if (!v.ok()) {
            if (problem.empty()) problem = v.reasons[0];
            failed = true;
            return;
        }
        r.steps.push_back(ReplayStep{side, a});
        const auto e = apply_action(g, side, a);
        events.insert(events.end(), e.begin(), e.end());
    };
    auto until = [&](Side side, int round) {
        while (!failed && g.pending && !(g.flow.battle_round == round && *g.pending == PendingDecision{side, decision::MoveUnits{}})) {
            act(wh40k::test::neutral_action(g));
        }
    };
    // A straight move of every model by dy inches (or, for an Advance, by its M plus the roll), leading rank first.
    auto move = [&](std::uint32_t id, MoveType type, float dy_in) {
        act(action::StartMove{UnitId{id}, type, std::nullopt});
        if (failed) return;
        const Unit& u = *g.find_unit(UnitId{id});
        if (type == MoveType::Advance) {
            const float m_in = mm_to_inches(*u.models.front().characteristics.move_mm);
            dy_in = (m_in + static_cast<float>(u.phase_flags.advance_roll.value_or(0))) * (dy_in < 0.0f ? -1.0f : 1.0f);
        }
        std::vector<Model> models = u.models;
        std::stable_sort(models.begin(), models.end(), [&](const Model& a, const Model& b) {
            return dy_in > 0.0f ? a.position.y > b.position.y : a.position.y < b.position.y;
        });
        for (const Model& m : models) {
            const Position p = *move_start_position(g, m.id);
            act(action::ProposeModelMove{m.id, {Position{p.x, p.y + wh40k::test::in(dy_in), 0.0f}}, {}});
        }
        act(action::CommitUnitMove{});
    };
    auto rest_stationary = [&] {
        while (!failed && g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind)) {
            act(wh40k::test::neutral_action(g));
        }
    };
    constexpr std::uint32_t kTerminators = 3;
    constexpr std::uint32_t kGenestealers = 13;
    until(Side::A, 1);
    move(kTerminators, MoveType::Advance, 1.0f);
    rest_stationary();
    until(Side::B, 1);
    move(kGenestealers, MoveType::Normal, -8.0f);
    until(Side::A, 2);
    move(kTerminators, MoveType::Normal, 5.0f);
    rest_stationary();
    until(Side::A, 3);
    if (failed || !g.pending) return r;
    r.expect = replay_expectation(g);
    const auto changed = events_of<event::ObjectiveControlChanged>(events);
    const auto scored = events_of<event::VictoryPointsScored>(events);
    const bool centre_taken = std::any_of(changed.begin(), changed.end(), [](const event::ObjectiveControlChanged& c) {
        return c.name == "Centre" && c.controller == Side::A;
    });
    ok = centre_taken && scored.size() == 2 &&
         scored[0] == event::VictoryPointsScored{Side::B, 2, "B home", 2, 2} &&
         scored[1] == event::VictoryPointsScored{Side::A, 0, "Centre", 5, 5};
    return r;
}

}  // namespace

TEST(Replay, RandomGamesFromTheScenarioReplayIdentically) {
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        GameState initial = wh40k::test::load_v1_scenario();
        initial.dice.seed = seed;
        Match match(initial);
        TestRng rng(seed);
        random_play(match, rng, 200);
        expect_replays_identically(match);
    }
}

TEST(Replay, RandomGamesFromTheStrikeScenarioReplayIdentically) {
    // M53: seven units on the 60"x44" field (D-064), played to the end. A few seeds keep the Debug run short.
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        GameState initial = load_scenario(repo_root() / "data/scenarios/strike_44x60.json", repo_root());
        initial.dice.seed = seed;
        Match match(initial);
        TestRng rng(seed);
        random_play(match, rng, 1000);
        EXPECT_EQ(match.state().flow.stage, Stage::GameOver) << "seed " << seed;
        expect_replays_identically(match);
    }
}

TEST(Replay, RandomGamesFromTheRostersFixtureReplayIdentically) {
    // M59 / D-070: the two rosters (13 units) from an embedded initial state, 36" and 10" apart.
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        GameState initial = seed == 1 ? wh40k::test::load_rosters_fixture()
                                      : wh40k::test::load_rosters_fixture_closer(seed == 2 ? 26.0f : 35.5f);
        initial.dice.seed = seed;
        Match match(initial);
        TestRng rng(seed);
        random_play(match, rng, 6000);
        EXPECT_EQ(match.state().flow.stage, Stage::GameOver) << "seed " << seed;
        expect_replays_identically(match);
    }
}

TEST(Replay, RandomGamesOnRandomLegalArmiesReplayIdentically) {
    // M72 (D-079 part 2): custom battles on random legal armies (as the arena's --random-armies), auto-placed, played
    // to the end from an embedded initial state.
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        GameState initial = wh40k::test::random_armies_battle(seed);
        initial.dice.seed = seed;
        Match match(initial);
        TestRng rng(seed);
        random_play(match, rng, 10000);
        EXPECT_EQ(match.state().flow.stage, Stage::GameOver) << "seed " << seed;
        expect_replays_identically(match);
    }
}

TEST(Replay, AttackFlowGamesReplayIdentically) {
    int steps = 0;
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        GameState gs = shoot_step_state(Side::A, seed);
        std::vector<WeaponSelection> sel;
        for (std::size_t i = 0; i < 5; ++i) {
            sel.push_back(select(gs, 0, i, 0, i % 2, gs.units[1].id));
        }
        begin_attacks(gs, gs.units[0].id, AttackContext{AttackType::Shooting, true}, sel);  // seeded dice
        Match match(gs);  // starts mid-attack, at the first decision
        TestRng rng(seed);
        random_play(match, rng, 200);
        steps += static_cast<int>(match.steps().size());
        expect_replays_identically(match);
    }
    EXPECT_GT(steps, 0);
}

TEST(Replay, InvalidStepIsReportedWithItsReasons) {
    Replay replay;
    replay.initial.scenario = "data/scenarios/v1_skirmish.json";
    replay.initial.seed = 3;
    replay.steps.push_back(ReplayStep{Side::B, action::EndShootStep{}});
    const ReplayResult result = run_replay(replay, repo_root());
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(*result.failed_step, 0u);
    EXPECT_EQ(result.reasons.at(0).rfind("09.02: ", 0), 0u) << result.reasons[0];
}

TEST(Replay, JsonRoundTripsBothInitialForms) {
    Replay from_scenario;
    from_scenario.description = "scenario form";
    from_scenario.initial.scenario = "data/scenarios/v1_skirmish.json";
    from_scenario.initial.seed = 0xFFFFFFFFFFFFFFFFull;
    from_scenario.steps = {ReplayStep{Side::A, action::StartMove{UnitId{1}, MoveType::RemainStationary}},
                           ReplayStep{Side::B, action::OrderCasualties{UnitId{2}, {ModelId{7}, ModelId{6}}}}};
    from_scenario.expect = Json{{"stage", "GameOver"}};
    EXPECT_EQ(Json(from_scenario).get<Replay>(), from_scenario);

    Replay from_state;
    from_state.initial.state = wh40k::test::load_v1_scenario();
    EXPECT_EQ(Json(from_state).get<Replay>(), from_state);
}

// Every saved replay is a regression test: it must run clean and match its expect block.
TEST(Replay, SavedReplayFilesPass) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(kReplayDir)) {
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    ASSERT_FALSE(files.empty()) << "no replays in " << kReplayDir;
    for (const auto& file : files) {
        SCOPED_TRACE(file.filename().string());
        const Replay replay = load_replay(file);
        const ReplayResult first = run_replay(replay, repo_root());
        ASSERT_TRUE(first.ok()) << "step " << *first.failed_step << ": " << first.reasons[0];
        EXPECT_EQ(replay_expectation(first.final_state), replay.expect);
        const ReplayResult second = run_replay(replay, repo_root());
        EXPECT_EQ(first.final_state, second.final_state);  // deterministic
    }
}

// Rewrites tests/replays/. Run on purpose only:
//   wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=Replay.DISABLED_RegenerateReplays
TEST(Replay, DISABLED_RegenerateReplays) {
    // M5: a full five-round game where every decision passes.
    Replay replay;
    replay.description =
        "M5: full game from the V1 scenario where every decision passes (remain stationary, end shooting, "
        "end charges); ends in a draw after battle round 5 (D-007).";
    replay.initial.scenario = "data/scenarios/v1_skirmish.json";
    replay.initial.seed = 1;
    GameState gs = replay_initial_state(replay, repo_root());
    start_game(gs);
    while (gs.pending) {
        const Side side = decider(gs);
        const GameAction a = legal_actions(gs, side).front();
        replay.steps.push_back(ReplayStep{side, a});
        apply_action(gs, side, a);
    }
    replay.expect = replay_expectation(gs);
    save_if_clean(replay, kReplayDir / "m5_full_game_all_pass.json");

    // M7: movement. A Normal move, an Advance (seeded roll), a move reverted for coherency (03.01), then
    // every later decision passes.
    Replay moves;
    moves.description =
        "M7: Side A Normal-moves its Intercessors 3\" north; Side B advances its Termagants 2\" south; "
        "Side A then moves one Intercessor 3\" west, which breaks coherency so the move reverts (03.01, "
        "03.03), and remains stationary instead. Every later decision passes; the game is a draw.";
    moves.initial.scenario = "data/scenarios/v1_skirmish.json";
    moves.initial.seed = 7;
    GameState g = replay_initial_state(moves, repo_root());
    start_game(g);
    auto act = [&](const GameAction& a) {
        const Side side = decider(g);
        const auto v = validate_action(g, side, a);
        ASSERT_TRUE(v.ok()) << v.reasons[0];
        moves.steps.push_back(ReplayStep{side, a});
        apply_action(g, side, a);
    };
    auto shift_unit = [&](std::size_t unit, float dx_in, float dy_in) {
        for (const Model& m : g.units[unit].models) {
            const Position s = *move_start_position(g, m.id);
            act(action::ProposeModelMove{m.id, {Position{s.x + wh40k::test::in(dx_in), s.y + wh40k::test::in(dy_in), 0}}});
        }
    };
    act(action::StartMove{g.units[0].id, MoveType::Normal, std::nullopt});
    shift_unit(0, 0, 3);
    act(action::CommitUnitMove{});
    act(action::EndShootStep{});
    act(action::EndChargeStep{});
    act(action::StartMove{g.units[1].id, MoveType::Advance, std::nullopt});
    shift_unit(1, 0, -2);
    act(action::CommitUnitMove{});
    act(action::EndShootStep{});
    act(action::EndChargeStep{});
    act(action::StartMove{g.units[0].id, MoveType::Normal, std::nullopt});
    const Model& first = g.units[0].models[0];
    const Position s = *move_start_position(g, first.id);
    act(action::ProposeModelMove{first.id, {Position{s.x - wh40k::test::in(3), s.y, 0}}});
    act(action::CommitUnitMove{});  // reverts: model 1 is more than 2" from the others
    while (g.pending) {
        act(legal_actions(g, decider(g)).front());
    }
    moves.expect = replay_expectation(g);
    save_if_clean(moves, kReplayDir / "m7_movement.json");

    // M8: shooting. Normal shooting after a 3" move (Heavy and Rapid Fire apply), then the Termagants
    // shoot back, both resolved with seeded dice; the defender orders casualties (D-013).
    Replay shots;
    shots.description =
        "M8: Side A Normal-moves its Intercessors 3\" north (no model more than 3\", so [HEAVY] applies) and "
        "fires every Bolt Rifle on Focused Fire at the Termagants, 9\" away (within half range: [RAPID FIRE 1]). "
        "Side B remains stationary and fires every Fleshborer back (Normal shooting, 18\"). Casualties are "
        "removed in unit order (D-013). Every later decision passes.";
    shots.initial.scenario = "data/scenarios/v1_skirmish.json";
    shots.initial.seed = 8;
    GameState s8 = replay_initial_state(shots, repo_root());
    start_game(s8);
    auto play = [&](const GameAction& a) {
        const Side side = decider(s8);
        const auto v = validate_action(s8, side, a);
        ASSERT_TRUE(v.ok()) << v.reasons[0];
        shots.steps.push_back(ReplayStep{side, a});
        apply_action(s8, side, a);
    };
    // The first legal action, with a casualty-order template filled in unit order.
    auto first_legal = [&]() -> GameAction {
        GameAction a = legal_actions(s8, decider(s8)).front();
        if (auto* oc = std::get_if<action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
            oc->priority = wh40k::test::model_ids(*s8.find_unit(oc->target));
        }
        return a;
    };
    auto volley = [&](std::size_t unit, std::size_t weapon, std::size_t profile, std::size_t target) {
        action::Shoot a{s8.units[unit].id, ShootingType::Normal, {}};
        for (const Model& m : s8.units[unit].models) {
            a.shots.push_back(ShotDeclaration{m.id, weapon, profile, s8.units[target].id});
        }
        return a;
    };
    auto resolve_attacks = [&]() {
        while (s8.flow.attack) play(first_legal());
    };
    play(action::StartMove{s8.units[0].id, MoveType::Normal, std::nullopt});
    for (const Model& m : s8.units[0].models) {
        const Position p = *move_start_position(s8, m.id);
        play(action::ProposeModelMove{m.id, {Position{p.x, p.y + wh40k::test::in(3), 0}}});
    }
    play(action::CommitUnitMove{});
    play(volley(0, 0, 0, 1));  // Bolt Rifle, Focused Fire
    resolve_attacks();
    ASSERT_FALSE(s8.units[1].models.empty()) << "pick a seed where the Termagants survive";
    play(action::EndShootStep{});
    play(action::EndChargeStep{});
    play(action::StartMove{s8.units[1].id, MoveType::RemainStationary, std::nullopt});
    play(volley(1, 0, 0, 0));  // Fleshborer
    resolve_attacks();
    while (s8.pending) play(first_legal());
    shots.expect = replay_expectation(s8);
    save_if_clean(shots, kReplayDir / "m8_shooting.json");

    // M9: a charge. The replay stops when the charge is resolved, before the Fight phase (M10), so it
    // stays valid when M10 adds fight decisions.
    Replay charge;
    charge.description =
        "M9: Side A Normal-moves its Intercessors 6\" north (6\" from the Termagants), skips shooting and "
        "declares a charge (seeded 2D6 of 6 or more). It selects the Termagants as the charge target and moves "
        "every model 2\" north: not engaged, so the move reverts (03.01, 11.04 After Moving) and the unit goes "
        "back to selecting targets with the same roll (D-018). It selects the Termagants again and moves every "
        "model 5\" north, ending 1\" away: the charge succeeds and the Intercessors have Fights First (24.13).";
    charge.initial.scenario = "data/scenarios/v1_skirmish.json";
    GameState c9;
    auto c_act = [&](const GameAction& a) {
        const Side side = decider(c9);
        const auto v = validate_action(c9, side, a);
        ASSERT_TRUE(v.ok()) << v.reasons[0];
        charge.steps.push_back(ReplayStep{side, a});
        apply_action(c9, side, a);
    };
    auto c_move_north = [&](float dy_in) {
        for (const Model& m : c9.units[0].models) {
            const Position p = *move_start_position(c9, m.id);
            c_act(action::ProposeModelMove{m.id, {Position{p.x, p.y + wh40k::test::in(dy_in), 0}}});
        }
    };
    // The first seed whose charge roll is 6 or more.
    for (std::uint64_t seed = 1;; ++seed) {
        charge.initial.seed = seed;
        charge.steps.clear();
        c9 = replay_initial_state(charge, repo_root());
        start_game(c9);
        c_act(action::StartMove{c9.units[0].id, MoveType::Normal, std::nullopt});
        c_move_north(6);
        c_act(action::CommitUnitMove{});
        c_act(action::EndShootStep{});
        c_act(action::DeclareCharge{c9.units[0].id});
        if (c9.flow.charge) break;  // a target was within the roll
        ASSERT_LT(seed, 100u);
    }
    c_act(action::StartChargeMove{c9.units[0].id, {c9.units[1].id}});
    c_move_north(2);
    c_act(action::CommitUnitMove{});  // reverts: not engaged with the charge target
    c_act(action::StartChargeMove{c9.units[0].id, {c9.units[1].id}});
    c_move_north(5);
    c_act(action::CommitUnitMove{});
    ASSERT_TRUE(c9.units[0].turn_flags.fights_first);
    charge.expect = replay_expectation(c9);
    save_if_clean(charge, kReplayDir / "m9_charge.json");

    // M10: the Fight phase after that charge, stopping when it ends (the next turn's Command phase is M11).
    Replay fight;
    fight.description =
        "M10: the M9 charge, then the Fight phase. No pile-in moves. The Intercessors fight first (Fights First, "
        "12.04) with Knives and Fists; casualties are removed in unit order (D-013), front rank first. The "
        "surviving Termagants fight back in Resolve Remaining Combats (an Overrun Fight with a 1.5\" pile-in if "
        "they are no longer engaged, 12.06). The Intercessors make an Ongoing Consolidation moving one model 0.5\" "
        "closer (12.08); the Termagants don't consolidate. Stops at Side B's first Move Units decision.";
    fight.initial = charge.initial;
    fight.steps = charge.steps;
    GameState f10 = replay_initial_state(fight, repo_root());
    start_game(f10);
    for (const ReplayStep& step : fight.steps) apply_action(f10, step.side, step.action);
    auto f_act = [&](const GameAction& a) {
        const Side side = decider(f10);
        const auto v = validate_action(f10, side, a);
        ASSERT_TRUE(v.ok()) << v.reasons[0];
        fight.steps.push_back(ReplayStep{side, a});
        apply_action(f10, side, a);
    };
    auto f_resolve = [&]() {
        while (f10.flow.attack) {
            GameAction a = legal_actions(f10, decider(f10)).front();
            if (auto* oc = std::get_if<action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
                oc->priority = wh40k::test::model_ids(*f10.find_unit(oc->target));
            }
            f_act(a);
        }
    };
    const UnitId marines = f10.units[0].id;
    const UnitId gants = f10.units[1].id;
    f_act(action::EndChargeStep{});
    f_act(action::EndPileIns{});
    f_act(action::EndPileIns{});
    f_act(action::SelectToFight{marines, FightType::Normal});
    f_act(wh40k::test::first_melee_options(f10, marines));
    f_resolve();
    ASSERT_FALSE(f10.units[1].models.empty()) << "the Termagants must survive to fight back";
    if (is_engaged(f10, f10.units[1])) {
        f_act(action::SelectToFight{gants, FightType::Normal});
    } else {
        // 12.06: the front rank is gone, so the survivors overrun: a 1.5" pile-in south, then they fight.
        f_act(action::SelectToFight{gants, FightType::Overrun});
        f_act(action::StartPileIn{gants, {marines}});
        for (const Model& m : f10.units[1].models) {
            const Position p = *move_start_position(f10, m.id);
            f_act(action::ProposeModelMove{m.id, {Position{p.x, p.y - wh40k::test::in(1.5f), 0}}});
        }
        f_act(action::CommitUnitMove{});
    }
    f_act(wh40k::test::first_melee_options(f10, gants));
    f_resolve();
    ASSERT_EQ(f10.flow.stage, Stage::Consolidate);
    f_act(action::StartConsolidation{marines, engaged_enemy_units(f10, f10.units[0])});
    const Model& west = f10.units[0].models.front();
    f_act(action::ProposeModelMove{west.id, {Position{west.position.x, west.position.y + wh40k::test::in(0.5f), 0}}});
    f_act(action::CommitUnitMove{});
    while (f10.pending && std::holds_alternative<decision::Consolidate>(f10.pending->kind)) {
        f_act(action::EndConsolidations{});
    }
    ASSERT_EQ(*f10.pending, (PendingDecision{Side::B, decision::MoveUnits{}}));
    fight.expect = replay_expectation(f10);
    save_if_clean(fight, kReplayDir / "m10_fight.json");

    // M11: the same game played to the end. Every later decision takes the first legal action (templates
    // filled: every model fights with its first melee option; casualties in unit order).
    Replay full;
    full.description =
        "M11: the M10 game continued to the end. Every later decision takes the first legal action: units "
        "remain stationary, skip shooting and charges, make no pile-in or consolidation moves, and fight "
        "whenever they are eligible (each model's first melee option). Both players gain 1 CP each Command "
        "phase (08.02). In Side B's Command phase the Termagants, below half-strength, make a battle-shock "
        "roll (08.03). The fight goes on in Side B's Fight phase until the Termagants are destroyed: Side A "
        "wins in battle round 1 (D-007).";
    full.initial = fight.initial;
    full.steps = fight.steps;
    GameState g11 = f10;
    while (g11.pending) {
        GameAction a = legal_actions(g11, decider(g11)).front();
        if (auto* oc = std::get_if<action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
            oc->priority = wh40k::test::model_ids(*g11.find_unit(oc->target));
        } else if (auto* f = std::get_if<action::Fight>(&a); f != nullptr && f->declarations.empty()) {
            a = wh40k::test::first_melee_options(g11, f->unit);
        }
        const Side side = decider(g11);
        const auto v = validate_action(g11, side, a);
        ASSERT_TRUE(v.ok()) << v.reasons[0];
        full.steps.push_back(ReplayStep{side, a});
        apply_action(g11, side, a);
    }
    ASSERT_TRUE(g11.result.has_value());
    full.expect = replay_expectation(g11);
    save_if_clean(full, kReplayDir / "m11_full_game.json");

    // M14: the close formation (D-008) and per-proposal overlap (D-027). Stops at Side B's first
    // Shooting decision, so later shooting changes don't touch it.
    Replay close;
    close.description =
        "M14: both units start in base contact (D-008). Side A moves its Intercessors 2\" east, the east-most "
        "model first so no proposal ends on a model yet to move (D-027), and the move is committed. Side A "
        "skips shooting and charges. Side B's two front-rank Termagants on the west swap places in three "
        "proposals: the first steps 1.5\" south, the second takes its place, then the first is proposed again "
        "into the second's place (a new proposal replaces its path). The move is committed. Stops at Side B's "
        "first Shooting decision.";
    close.initial.scenario = "data/scenarios/v1_skirmish.json";
    close.initial.seed = 14;
    GameState g14 = replay_initial_state(close, repo_root());
    start_game(g14);
    auto m_act = [&](const GameAction& a) {
        const Side side = decider(g14);
        const auto v = validate_action(g14, side, a);
        ASSERT_TRUE(v.ok()) << v.reasons[0];
        close.steps.push_back(ReplayStep{side, a});
        apply_action(g14, side, a);
    };
    auto start_of = [&](ModelId id) { return *move_start_position(g14, id); };
    m_act(action::StartMove{g14.units[0].id, MoveType::Normal, std::nullopt});
    for (auto it = g14.units[0].models.rbegin(); it != g14.units[0].models.rend(); ++it) {  // east-most first
        const Position start = start_of(it->id);
        m_act(action::ProposeModelMove{it->id, {Position{start.x + wh40k::test::in(2), start.y, 0}}});
    }
    m_act(action::CommitUnitMove{});
    m_act(action::EndShootStep{});
    m_act(action::EndChargeStep{});
    while (!(g14.pending && std::holds_alternative<decision::MoveUnits>(g14.pending->kind))) {
        m_act(legal_actions(g14, decider(g14)).front());  // Side A's empty Fight phase, Side B's Command phase
    }
    m_act(action::StartMove{g14.units[1].id, MoveType::Normal, std::nullopt});
    const ModelId gant_a = g14.units[1].models[0].id;
    const ModelId gant_b = g14.units[1].models[1].id;
    const Position p1 = start_of(gant_a);
    const Position p2 = start_of(gant_b);
    m_act(action::ProposeModelMove{gant_a, {Position{p1.x, p1.y - wh40k::test::in(1.5f), 0}}});
    m_act(action::ProposeModelMove{gant_b, {p1}});
    m_act(action::ProposeModelMove{gant_a, {p2}});
    m_act(action::CommitUnitMove{});
    ASSERT_EQ(*g14.pending, (PendingDecision{Side::B, decision::Shoot{}}));
    close.expect = replay_expectation(g14);
    save_if_clean(close, kReplayDir / "m14_close_formation.json");

    // M53: battle round 1 on the 60"x44" field with seven units (D-064). Stops at Side A's first Move
    // Units decision of battle round 2; later milestones (M54-M58) change bots, client and speed only.
    Replay strike;
    strike.description =
        "M53: battle round 1 of the Strike scenario (D-064), 3 Intercessor Squads (units 1-3) vs 4 Termagant "
        "units (4-7). Side A: squad 1 Normal-moves 3\" north, squad 2 remains stationary, squad 3 advances 4\" "
        "north (seeded roll). Squads 1 and 2 fire every Bolt Rifle that can on Focused Fire at Termagants 4 and "
        "5 (squad 3 advanced and doesn't shoot); casualties are removed in unit order (D-013). No charges. "
        "Side B: Termagants 4 Normal-move 4\" south, 5 advance 3\" south, 6 (held back 3\") and 7 remain "
        "stationary; 4 and 7 fire every Fleshborer that can at the closest squads. No charges. Every other "
        "decision takes the first legal action. Stops at Side A's first Move Units decision of battle round 2.";
    strike.initial.scenario = "data/scenarios/strike_44x60.json";
    strike.initial.seed = 53;
    GameState g53 = replay_initial_state(strike, repo_root());
    start_game(g53);
    auto s_act = [&](const GameAction& a) {
        const Side side = decider(g53);
        const auto v = validate_action(g53, side, a);
        ASSERT_TRUE(v.ok()) << v.reasons[0];
        strike.steps.push_back(ReplayStep{side, a});
        apply_action(g53, side, a);
    };
    auto s_first_legal = [&]() -> GameAction {
        GameAction a = legal_actions(g53, decider(g53)).front();
        if (auto* oc = std::get_if<action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
            oc->priority = wh40k::test::model_ids(*g53.find_unit(oc->target));
        }
        return a;
    };
    auto s_unit = [&](std::uint32_t id) -> const Unit& { return *g53.find_unit(UnitId{id}); };
    // Every model of the unit moves straight by (dx, dy) inches, in listed order (the front rank first).
    auto s_move = [&](std::uint32_t id, MoveType type, float dy_in) {
        s_act(action::StartMove{UnitId{id}, type, std::nullopt});
        if (type == MoveType::RemainStationary) return;
        for (const Model& m : s_unit(id).models) {
            const Position p = *move_start_position(g53, m.id);
            s_act(action::ProposeModelMove{m.id, {Position{p.x, p.y + wh40k::test::in(dy_in), 0}}});
        }
        s_act(action::CommitUnitMove{});
    };
    // Every model that can fires its first weapon's first profile at `target` (04.02 range, 06.01 visibility).
    auto s_volley = [&](std::uint32_t id, std::uint32_t target) {
        const Unit& u = s_unit(id);
        action::Shoot a{u.id, ShootingType::Normal, {}};
        for (const ShotDeclaration& d : shot_options(g53, u, ShootingType::Normal)) {
            if (d.weapon_index == 0 && d.profile_index == 0 && d.target == UnitId{target}) a.shots.push_back(d);
        }
        ASSERT_FALSE(a.shots.empty()) << "unit " << id << " has no shot at unit " << target;
        s_act(a);
        while (g53.flow.attack) s_act(s_first_legal());
    };
    s_move(1, MoveType::Normal, 3);
    s_move(2, MoveType::RemainStationary, 0);
    s_move(3, MoveType::Advance, 4);
    s_volley(1, 4);
    s_volley(2, 5);
    s_act(action::EndShootStep{});
    s_act(action::EndChargeStep{});
    while (!(g53.pending && *g53.pending == PendingDecision{Side::B, decision::MoveUnits{}})) {
        s_act(s_first_legal());  // Side A's empty Fight phase, Side B's Command phase
    }
    s_move(4, MoveType::Normal, -4);
    s_move(5, MoveType::Advance, -3);
    s_move(6, MoveType::RemainStationary, 0);
    s_move(7, MoveType::RemainStationary, 0);
    ASSERT_FALSE(s_unit(4).models.empty());
    s_volley(4, 1);
    s_volley(7, 3);
    s_act(action::EndShootStep{});
    s_act(action::EndChargeStep{});
    while (!(g53.pending && *g53.pending == PendingDecision{Side::A, decision::MoveUnits{}})) {
        s_act(s_first_legal());  // Side B's empty Fight phase, battle round 2's Command phase
    }
    ASSERT_EQ(g53.flow.battle_round, 2);
    strike.expect = replay_expectation(g53);
    save_if_clean(strike, kReplayDir / "m53_strike_round1.json");

    // M59: the roster fixture (D-070), from an embedded initial state so the file needs no scenario under
    // tests/data (the client's replay list and the package load it as is). Movement in battle round 1 and
    // one boltgun volley in battle round 2; stops at Side B's first Move Units decision of battle round 2,
    // before anything M60-M70 change (no Hazardous, random, Twin-linked or Extra Attacks weapon is used).
    Replay rosters;
    rosters.description =
        "M59: the two New Recruit rosters without the Tyrannofex on the 44\" x 60\" portrait board (D-070). "
        "Units 1-5 Chaos Space Marines (Chaos Lord, Legionaries with chainswords, Legionaries with boltguns, "
        "Chaos Terminators, Havocs), 6-13 Tyranids (Hive Tyrant, Termagants west and east, Genestealers front "
        "and held back, Tyrant Guard, Screamer-killers west and east). Round 1: the Lord moves 5\" north, both "
        "Legionaries squads 6\", the Terminators 5\"; the Havocs remain stationary; only the Havocs' lascannons "
        "(48\") are in range, and Side A ends its Shooting phase without firing. The Hive Tyrant moves 6\" south, the west Termagants 6\", the east Termagants advance 3\", the "
        "front Genestealers 8\" and the held-back ones 6\", the Tyrant Guard 5\"; the Screamer-killers remain "
        "stationary. Round 2: the boltgun Legionaries move 6\" north and fire their boltguns at the front "
        "Genestealers (16\" away); casualties in unit order (D-013). Every other unit remains stationary and "
        "every other decision takes the first legal action. Stops at Side B's first Move Units decision of "
        "battle round 2.";
    rosters.initial.state = wh40k::test::load_rosters_fixture();
    rosters.initial.state->dice.seed = 59;
    GameState g59 = replay_initial_state(rosters, repo_root());
    start_game(g59);
    auto r_act = [&](const GameAction& a) {
        const Side side = decider(g59);
        const auto v = validate_action(g59, side, a);
        ASSERT_TRUE(v.ok()) << v.reasons[0];
        rosters.steps.push_back(ReplayStep{side, a});
        apply_action(g59, side, a);
    };
    auto r_first_legal = [&]() -> GameAction {
        GameAction a = legal_actions(g59, decider(g59)).front();
        if (auto* oc = std::get_if<action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
            oc->priority = wh40k::test::model_ids(*g59.find_unit(oc->target));
        }
        return a;
    };
    auto r_unit = [&](std::uint32_t id) -> const Unit& { return *g59.find_unit(UnitId{id}); };
    auto r_move = [&](std::uint32_t id, MoveType type, float dy_in) {
        r_act(action::StartMove{UnitId{id}, type, std::nullopt});
        if (type == MoveType::RemainStationary) return;
        for (const Model& m : r_unit(id).models) {  // the front rank first
            const Position p = *move_start_position(g59, m.id);
            r_act(action::ProposeModelMove{m.id, {Position{p.x, p.y + wh40k::test::in(dy_in), 0}}});
        }
        r_act(action::CommitUnitMove{});
    };
    auto r_until = [&](Side side) {
        while (!(g59.pending && *g59.pending == PendingDecision{side, decision::MoveUnits{}})) r_act(r_first_legal());
    };
    r_move(1, MoveType::Normal, 5);
    r_move(2, MoveType::Normal, 6);
    r_move(3, MoveType::Normal, 6);
    r_move(4, MoveType::Normal, 5);
    r_move(5, MoveType::RemainStationary, 0);
    for (std::uint32_t id = 1; id <= 4; ++id) {
        ASSERT_TRUE(shot_options(g59, r_unit(id), ShootingType::Normal).empty()) << "unit " << id;
    }
    // M62 (D-069): the Havoc lascannons' random D is rolled now, and they reach the Tyranids (48"); nothing else does.
    ASSERT_FALSE(shot_options(g59, r_unit(5), ShootingType::Normal).empty());
    for (const ShotDeclaration& d : shot_options(g59, r_unit(5), ShootingType::Normal)) {
        ASSERT_EQ(find_model(r_unit(5), d.model)->weapons.at(d.weapon_index).name, "Havoc lascannon");
    }
    r_act(action::EndShootStep{});
    r_act(action::EndChargeStep{});
    r_until(Side::B);
    r_move(6, MoveType::Normal, -6);
    r_move(7, MoveType::Normal, -6);
    r_move(8, MoveType::Advance, -3);
    r_move(9, MoveType::Normal, -8);
    r_move(10, MoveType::Normal, -6);
    r_move(11, MoveType::Normal, -5);
    r_move(12, MoveType::RemainStationary, 0);
    r_move(13, MoveType::RemainStationary, 0);
    r_act(action::EndShootStep{});
    r_act(action::EndChargeStep{});
    r_until(Side::A);
    ASSERT_EQ(g59.flow.battle_round, 2);
    r_move(3, MoveType::Normal, 6);
    for (const std::uint32_t id : {1u, 2u, 4u, 5u}) r_move(id, MoveType::RemainStationary, 0);
    {
        const Unit& bolters = r_unit(3);
        action::Shoot boltgun_volley{bolters.id, ShootingType::Normal, {}};
        for (const ShotDeclaration& d : shot_options(g59, bolters, ShootingType::Normal)) {
            const Model& m = *find_model(bolters, d.model);
            if (m.weapons[d.weapon_index].name == "Boltgun" && d.target == UnitId{9}) boltgun_volley.shots.push_back(d);
        }
        ASSERT_EQ(boltgun_volley.shots.size(), 9u);
        r_act(boltgun_volley);
        while (g59.flow.attack) r_act(r_first_legal());
    }
    r_act(action::EndShootStep{});
    r_act(action::EndChargeStep{});
    r_until(Side::B);
    ASSERT_EQ(g59.flow.battle_round, 2);
    rosters.expect = replay_expectation(g59);
    save_if_clean(rosters, kReplayDir / "m59_rosters_fixture_round1.json");

    // M60: the hazard roll after a supercharged plasma gun's attacks, and the Hive Tyrant's [EXTRA ATTACKS] talons
    // (D-071). Stops at Side B's first Consolidate decision, before anything M61-M70 change.
    bool shows_it = false;
    std::string problem;
    const Replay m60 = m60_replay(kM60Seed, shows_it, problem);
    ASSERT_TRUE(problem.empty()) << problem;
    EXPECT_TRUE(shows_it) << "seed " << kM60Seed << " no longer shows a failed hazard roll and a Havoc slain by the talons";
    save_if_clean(m60, kReplayDir / "m60_hazard_and_extra_attacks.json");

    // M61: the Hive Tyrant's [TWIN-LINKED] bonesword and the re-roll decision (24.38, D-072). Stops at Side B's first
    // Consolidate decision, before anything M62-M70 change (no random A/D, no shooting at the engaged MONSTER, round
    // bases only).
    bool m61_shows_it = false;
    std::string m61_problem;
    const Replay m61 = m61_replay(kM61Seed, m61_shows_it, m61_problem);
    ASSERT_TRUE(m61_problem.empty()) << m61_problem;
    EXPECT_TRUE(m61_shows_it) << "seed " << kM61Seed << " no longer shows a failed bonesword wound roll re-rolled into a wound";
    save_if_clean(m61, kReplayDir / "m61_twin_linked_reroll.json");

    // M62: random characteristics (D-069): the lascannons' D6+1 rolled per attack after the Tyrant is selected, and the
    // heavy venom cannon's D3 attacks rolled when they are resolved, +1 [BLAST]. Stops at Side B's first Charge decision,
    // before anything M63-M70 change (no move through models, no shooting at an engaged MONSTER, round bases only).
    bool m62_shows_it = false;
    std::string m62_problem;
    const Replay m62 = m62_replay(kM62Seed, m62_shows_it, m62_problem);
    ASSERT_TRUE(m62_problem.empty()) << m62_problem;
    EXPECT_TRUE(m62_shows_it) << "seed " << kM62Seed << " no longer shows two lascannon damage rolls and the cannon's roll";
    save_if_clean(m62, kReplayDir / "m62_random_characteristics.json");

    // M63: Monsters and Vehicles (D-073): the Hive Tyrant's Normal move through the Legionaries (17.01), then the Havocs'
    // lascannons at the engaged Tyrant at -1 and the Legionaries' bolt pistols with no penalty (17.03). Stops at Side
    // A's first Charge decision, before anything M64-M70 change (round bases only, no points result).
    bool m63_shows_it = false;
    std::string m63_problem;
    const Replay m63 = m63_replay(kM63Seed, m63_shows_it, m63_problem);
    ASSERT_TRUE(m63_problem.empty()) << m63_problem;
    EXPECT_TRUE(m63_shows_it) << "seed " << kM63Seed << " no longer shows the -1 and the close-quarters exception";
    save_if_clean(m63, kReplayDir / "m63_monsters.json");

    // M65: Oval bases in the engine (D-074 part 2): the Tyrannofex turns and moves 3" ([HEAVY] still applies), shoots,
    // charges turned back and fights. Stops at Side A's first Move Units decision, before anything M66-M70 change (the
    // client and bots, the real scenario and its points result).
    bool m65_shows_it = false;
    std::string m65_problem;
    const Replay m65 = m65_replay(kM65Seed, m65_shows_it, m65_problem);
    ASSERT_TRUE(m65_problem.empty()) << m65_problem;
    EXPECT_TRUE(m65_shows_it) << "seed " << kM65Seed << " no longer shows the [HEAVY] +1, the charge and the fight";
    save_if_clean(m65, kReplayDir / "m65_oval_tyrannofex.json");

    // M67: the playable roster scenario (D-076): a lascannon volley in round 1, a Genestealer charge and the fight in
    // round 2. Stops at Side A's first Move Units decision of round 3, before anything M68-M70 change (the bots).
    bool m67_shows_it = false;
    std::string m67_problem;
    const Replay m67 = m67_replay(kM67Seed, m67_shows_it, m67_problem);
    ASSERT_TRUE(m67_problem.empty()) << m67_problem;
    EXPECT_TRUE(m67_shows_it) << "seed " << kM67Seed << " no longer shows the lascannon damage, the charge and the fight";
    save_if_clean(m67, kReplayDir / "m67_rosters_round1.json");

    // M72: a custom battle of two non-stock legal armies (D-079 part 2), auto-placed, random play through battle round 2.
    // Embedded initial state. Stops at the first decision of battle round 3, before M74's deployment step (an
    // auto-placed battle has none) and the bot changes of M73 and M77.
    std::set<std::string> m72_shots;
    const Replay m72 = m72_replay(kM72Seed, m72_shots);
    EXPECT_EQ(replay_expectation(run_replay(m72, repo_root()).final_state).at("battle_round"), 3);
    EXPECT_GT(m72_shots.size(), 3u) << "seed " << kM72Seed << " no longer shows the new loadouts shooting";
    for (const std::string& p : m72_shots) std::printf("m72 shot: %s\n", p.c_str());
    save_if_clean(m72, kReplayDir / "m72_custom_battle_round2.json");

    // M74: deployment by hand (D-081): the stock armies' custom battle, A setting up first, the Tyrannofex turned by
    // hand, to Side A's first Move Units decision of battle round 1 (before M77's bot deployment, which only changes
    // what bots choose). The scenario file is written first: the replay names it.
    {
        std::ofstream out(repo_root() / kM74Fixture, std::ios::binary);
        out << m74_fixture().dump(2) << "\n";
    }
    bool m74_shows_it = false;
    std::string m74_problem;
    const Replay m74 = m74_replay(m74_shows_it, m74_problem);
    ASSERT_TRUE(m74_problem.empty()) << m74_problem;
    EXPECT_TRUE(m74_shows_it) << "the M74 replay no longer turns the Tyrannofex or scans for the Screamer-killer";
    save_if_clean(m74, kReplayDir / "m74_hand_deployment.json");

    // M86 (D-084): a short game on the objective scenario.
    bool m86_shows_it = false;
    std::string m86_problem;
    const Replay m86 = m86_replay(kM86Seed, m86_shows_it, m86_problem);
    ASSERT_TRUE(m86_problem.empty()) << m86_problem;
    EXPECT_TRUE(m86_shows_it) << "seed " << kM86Seed << " no longer takes the Centre and scores both sides";
    save_if_clean(m86, kReplayDir / "m86_objectives.json");
}

// M74 (D-081): the replay's scenario file is the stock armies' custom battle set up by hand.
TEST(D081_Replay, TheFixtureIsTheStockArmiesSetUpByHand) {
    EXPECT_EQ(read_json_file(repo_root() / kM74Fixture), m74_fixture());
    const GameState gs = load_scenario(repo_root() / kM74Fixture, repo_root());
    EXPECT_EQ(gs.first_turn, Side::B);
    EXPECT_TRUE(gs.units.empty());
    EXPECT_EQ(gs.undeployed.size(), 14u);
}

// M74 (D-081): the saved replay sets every unit up, A first and alternating, the Tyrannofex turned, and battle round 1
// then runs to Side A's first Move Units decision.
TEST(D081_Replay, TheHandDeploymentReplaySetsUpAndStartsBattleRoundOne) {
    const Replay saved = load_replay(kReplayDir / "m74_hand_deployment.json");
    ASSERT_TRUE(saved.initial.scenario.has_value());
    EXPECT_EQ(*saved.initial.scenario, kM74Fixture);
    const ReplayResult run = run_replay(saved, repo_root());
    ASSERT_TRUE(run.ok()) << "step " << *run.failed_step << ": " << run.reasons.front();
    std::string order;
    for (std::size_t i = 0; i < 14; ++i) {
        ASSERT_TRUE(std::holds_alternative<action::DeployUnit>(saved.steps.at(i).action)) << "step " << i;
        order += saved.steps[i].side == Side::A ? 'A' : 'B';
    }
    EXPECT_EQ(order, "ABABABABABBBBB");
    const Model& tyrannofex = run.final_state.find_unit(UnitId{11})->models.front();
    EXPECT_EQ(tyrannofex.facing_deg, 30.0f);
    EXPECT_EQ(run.final_state.flow.battle_round, 1);
    EXPECT_EQ(run.final_state.pending, (PendingDecision{Side::A, decision::MoveUnits{}}));
    EXPECT_TRUE(run.final_state.undeployed.empty());
    EXPECT_EQ(events_of<event::UnitSetUp>(run.events).size(), 14u);
    bool ok = false;
    std::string problem;
    const Replay again = m74_replay(ok, problem);
    EXPECT_TRUE(problem.empty()) << problem;
    EXPECT_TRUE(ok);
    EXPECT_EQ(Json(again).dump(2), Json(saved).dump(2));  // the file is what the builder plays now
}

// M86 (D-084): the saved replay takes the Centre, scores both sides and stops at Side A's round-3 Move Units decision.
TEST(D084_Replay, TheObjectiveReplayTakesTheCentreAndScoresBothSides) {
    const Replay saved = load_replay(kReplayDir / "m86_objectives.json");
    ASSERT_TRUE(saved.initial.scenario.has_value());
    EXPECT_EQ(*saved.initial.scenario, kM86Scenario);
    const ReplayResult run = run_replay(saved, repo_root());
    ASSERT_TRUE(run.ok()) << "step " << *run.failed_step << ": " << run.reasons.front();
    const GameState& end = run.final_state;
    EXPECT_EQ(end.flow.battle_round, 3);
    EXPECT_EQ(end.pending, (PendingDecision{Side::A, decision::MoveUnits{}}));
    ASSERT_TRUE(end.mission.has_value());
    EXPECT_EQ(end.mission->objectives[0].controller, Side::A);
    EXPECT_EQ(end.mission->objectives[1].controller, std::nullopt);
    EXPECT_EQ(end.mission->objectives[2].controller, Side::B);
    EXPECT_EQ(end.mission->objective_vp, (std::array<int, 2>{5, 2}));
    bool ok = false;
    std::string problem;
    const Replay again = m86_replay(kM86Seed, ok, problem);
    EXPECT_TRUE(problem.empty()) << problem;
    EXPECT_TRUE(ok);
    EXPECT_EQ(Json(again).dump(2), Json(saved).dump(2));  // the file is what the builder plays now
}

TEST(Replay, DISABLED_SearchM72Seed) {
    for (std::uint64_t seed = 1; seed <= 60; ++seed) {
        std::set<std::string> shots;
        (void)m72_replay(seed, shots);
        std::printf("seed %llu: %zu profiles fired\n", static_cast<unsigned long long>(seed), shots.size());
    }
}

TEST(Replay, DISABLED_SearchM67Seed) {
    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        bool ok = false;
        std::string problem;
        (void)m67_replay(seed, ok, problem);
        if (!problem.empty()) std::printf("seed %llu: %s\n", static_cast<unsigned long long>(seed), problem.c_str());
        if (ok) {
            std::printf("M67 seed %llu\n", static_cast<unsigned long long>(seed));
            return;
        }
    }
}

TEST(Replay, DISABLED_SearchM65Seed) {
    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        bool ok = false;
        std::string problem;
        (void)m65_replay(seed, ok, problem);
        if (!problem.empty()) std::printf("seed %llu: %s\n", static_cast<unsigned long long>(seed), problem.c_str());
        if (ok) {
            std::printf("M65 seed %llu\n", static_cast<unsigned long long>(seed));
            return;
        }
    }
}

TEST(Replay, RandomGamesFromTheTyrannofexFixtureReplayIdentically) {
    // M65 (D-074 part 2): games with turns (the random fills give the oval facings) replay identically, through JSON.
    int with_facings = 0;
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        GameState initial = seed % 2 == 0 ? wh40k::test::tyrannofex::fixture(Side::B, seed == 4)
                                          : wh40k::test::load_tyrannofex_fixture_closer(26.0f);
        initial.dice.seed = seed;
        Match match(initial);
        TestRng rng(seed);
        random_play(match, rng, 1500);
        for (const ReplayStep& step : match.steps()) {
            if (const auto* p = std::get_if<action::ProposeModelMove>(&step.action); p != nullptr && !p->facings.empty()) {
                ++with_facings;
            }
        }
        expect_replays_identically(match);
    }
    EXPECT_GT(with_facings, 0);
}

TEST(Replay, DISABLED_SearchM63Seed) {
    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        bool ok = false;
        std::string problem;
        (void)m63_replay(seed, ok, problem);
        if (!problem.empty()) std::printf("seed %llu: %s\n", static_cast<unsigned long long>(seed), problem.c_str());
        if (ok) {
            std::printf("M63 seed %llu\n", static_cast<unsigned long long>(seed));
            return;
        }
    }
}

TEST(Replay, DISABLED_SearchM62Seed) {
    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        bool ok = false;
        std::string problem;
        (void)m62_replay(seed, ok, problem);
        if (!problem.empty()) std::printf("seed %llu: %s\n", static_cast<unsigned long long>(seed), problem.c_str());
        if (ok) {
            std::printf("M62 seed %llu\n", static_cast<unsigned long long>(seed));
            return;
        }
    }
}

TEST(Replay, DISABLED_SearchM61Seed) {
    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        bool ok = false;
        std::string problem;
        (void)m61_replay(seed, ok, problem);
        if (!problem.empty()) std::printf("seed %llu: %s\n", static_cast<unsigned long long>(seed), problem.c_str());
        if (ok) {
            std::printf("M61 seed %llu\n", static_cast<unsigned long long>(seed));
            return;
        }
    }
}

TEST(Replay, DISABLED_SearchM60Seed) {
    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        bool ok = false;
        std::string problem;
        (void)m60_replay(seed, ok, problem);
        if (!problem.empty()) std::printf("seed %llu: %s\n", static_cast<unsigned long long>(seed), problem.c_str());
        if (ok) {
            std::printf("M60 seed %llu\n", static_cast<unsigned long long>(seed));
            return;
        }
    }
}
