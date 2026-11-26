// D-097 (M96): the opponent-turn core stratagems, used in reaction windows that follow no roll: Fire Overwatch with
// Snap Shooting (15.08, 15.09), Heroic Intervention (15.11) and Counteroffensive (15.12); objective control first at a
// phase's end (14.02.01); format-4 replays.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/distance.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/charge.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/queries.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::add_unit_copy;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::first_melee_options;
using wh40k::test::in;
using wh40k::test::is_rule_cited;
using wh40k::test::model_ids;
using wh40k::test::rejection;
using wh40k::test::repo_root;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

// Applies `a` for the deciding side, paced (up to the next decision, a roll or a window included).
std::vector<GameEvent> paced(GameState& gs, const GameAction& a, DiceSource& dice) {
    return apply_action_paced(gs, gs.pending->side, a, dice);
}
std::vector<GameEvent> paced(GameState& gs, const GameAction& a) {
    ScriptedDice none{std::vector<int>{}};
    return paced(gs, a, none);
}

// Makes the pending roll, paced.
std::vector<GameEvent> roll(GameState& gs, DiceSource& dice) {
    EXPECT_TRUE(roll_pending(gs)) << (gs.pending ? describe(gs, *gs.pending) : std::string("no decision"));
    const auto& d = std::get<decision::RollDice>(gs.pending->kind);
    return paced(gs, action::RollDice{d.roll, d.unit}, dice);
}
std::vector<GameEvent> roll(GameState& gs, std::vector<int> faces) {
    ScriptedDice dice(std::move(faces));
    return roll(gs, dice);
}

std::vector<GameEvent> decline(GameState& gs) { return paced(gs, action::DeclineStratagems{}); }

PendingDecision window(Side side, WindowTiming timing, std::optional<UnitId> unit = std::nullopt) {
    return PendingDecision{side, decision::UseStratagems{timing, unit}};
}

action::UseStratagem use(Stratagem s, UnitId unit, std::optional<StratagemMode> mode = std::nullopt) {
    return action::UseStratagem{s, unit, std::nullopt, mode};
}

std::string label(UnitId id) { return "unit " + std::to_string(id.v); }  // as the engine's reasons name a unit

template <typename E>
std::size_t count(const std::vector<GameEvent>& events) {
    return events_of<E>(events).size();
}

// Answers casualty orders with the unit's own model order (D-013), paced.
void order_casualties(GameState& gs) {
    while (gs.pending && std::holds_alternative<decision::OrderCasualties>(gs.pending->kind)) {
        const UnitId target = std::get<decision::OrderCasualties>(gs.pending->kind).target;
        paced(gs, action::OrderCasualties{target, model_ids(*gs.find_unit(target))});
    }
}

// Paced neutral answers (neutral_action: rolls made from `dice`, windows declined) until `done(gs)`.
template <typename Done>
void play_paced_until(GameState& gs, DiceSource& dice, Done done, int guard = 400) {
    for (int i = 0; i < guard && gs.pending && !done(gs); ++i) {
        const GameAction a = window_pending(gs) ? GameAction{action::DeclineStratagems{}} : wh40k::test::neutral_action(gs);
        apply_action_paced(gs, decider(gs), a, dice);
    }
    ASSERT_TRUE(done(gs)) << (gs.pending ? describe(gs, *gs.pending) : std::string("no decision"));
}

// V1 started paced and played to Side A's Move Units step (round 1: each side has 1 CP).
GameState a_move_units_step() {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game_paced(gs);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::MoveUnits{}}));
    return gs;
}

// The Intercessors remain stationary: the Movement phase ends (15.08's window opens for Side B when it can use it).
std::vector<GameEvent> marines_remain_stationary(GameState& gs) {
    return paced(gs, action::StartMove{gs.units[kMarines].id, MoveType::RemainStationary, std::nullopt});
}

// Every Termagant's fleshborer at the Intercessors, as one shooting type.
action::Shoot gants_shoot(const GameState& gs, ShootingType type) {
    action::Shoot s{gs.units[kGants].id, type, {}};
    for (const Model& m : gs.units[kGants].models) s.shots.push_back(ShotDeclaration{m.id, 0, 0, gs.units[kMarines].id});
    return s;
}

// The Fire Overwatch window at the end of Side A's first Movement phase (the Termagants 12" from the Intercessors),
// each Termagant's fleshborer changed by `change`, Side B with `cp_b` CP.
template <typename Change>
GameState fire_overwatch_window(int cp_b, Change change) {
    GameState gs = a_move_units_step();
    gs.command_points[1] = cp_b;
    for (Model& m : gs.units[kGants].models) change(m.weapons[0].profiles[0]);
    marines_remain_stationary(gs);
    return gs;
}
GameState fire_overwatch_window(int cp_b = 1) {
    return fire_overwatch_window(cp_b, [](WeaponProfile&) {});
}

// Fire Overwatch used on the Termagants and their snap shot declared at the Intercessors: the hit roll waits.
template <typename Change>
GameState snap_hit_roll_waits(int cp_b, Change change) {
    GameState gs = fire_overwatch_window(cp_b, change);
    paced(gs, use(Stratagem::FireOverwatch, gs.units[kGants].id));
    paced(gs, gants_shoot(gs, ShootingType::Snap));
    return gs;
}

}  // namespace

// ==== 15.08 Fire Overwatch ====

TEST(R15_08_FireOverwatch, TheWindowOpensAtTheEndOfTheOpponentsMovementPhaseForThePlayerWhoseTurnItIsNot) {
    GameState gs = a_move_units_step();
    const auto events = marines_remain_stationary(gs);
    EXPECT_EQ(gs.flow.stage, Stage::EndOfMovementPhase);
    EXPECT_EQ(count<event::PhaseEnded>(events), 0u);  // the phase hasn't ended: the window is at its end
    EXPECT_EQ(gs.pending, window(Side::B, WindowTiming::EndOfPhase));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "15.08");
    EXPECT_EQ(active_side(gs), Side::A);
    const UnitId gants = gs.units[kGants].id;
    EXPECT_EQ(legal_actions(gs, Side::B),
              (std::vector<GameAction>{action::DeclineStratagems{}, use(Stratagem::FireOverwatch, gants)}));
    EXPECT_TRUE(legal_actions(gs, Side::A).empty());
    const WindowInfo info = window_info(gs).value();
    EXPECT_EQ(info.timing, WindowTiming::EndOfPhase);
    EXPECT_EQ(info.roll, std::nullopt);
    EXPECT_EQ(info.unit, std::nullopt);
    EXPECT_EQ(info.side, Side::B);
    EXPECT_EQ(info.command_points, 1);
    ASSERT_EQ(info.options.size(), 1u);
    EXPECT_EQ(info.options[0].stratagem, Stratagem::FireOverwatch);
    EXPECT_EQ(info.options[0].name, "Fire Overwatch");
    EXPECT_EQ(info.options[0].rule, "15.08");
    EXPECT_EQ(info.options[0].cost, 1);
    EXPECT_EQ(info.options[0].mode, std::nullopt);
    EXPECT_EQ(info.options[0].targets, std::vector<UnitId>{gants});
    EXPECT_TRUE(info.faces.empty());
    EXPECT_FALSE(info.result.has_value());
}

TEST(R15_08_FireOverwatch, ADeclinedWindowPlaysAsBefore) {
    GameState gs = fire_overwatch_window();
    const auto events = decline(gs);
    EXPECT_FALSE(gs.flow.window.has_value());
    EXPECT_EQ(count<event::PhaseEnded>(events), 1u);
    EXPECT_EQ(gs.flow.stage, Stage::Shoot);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
    EXPECT_EQ(gs.command_points, (std::array<int, 2>{1, 1}));
    // The same game inline (every window declined there).
    GameState inline_gs = wh40k::test::load_v1_scenario();
    start_game(inline_gs);
    apply_action(inline_gs, Side::A, action::StartMove{inline_gs.units[kMarines].id, MoveType::RemainStationary, std::nullopt});
    EXPECT_EQ(inline_gs, gs);
}

TEST(R15_08_FireOverwatch, NoWindowWithoutCpOrAUnitThatCanSnapShoot) {
    {
        SCOPED_TRACE("no CP");
        GameState gs = fire_overwatch_window(0);
        EXPECT_EQ(gs.flow.stage, Stage::Shoot);
        EXPECT_FALSE(gs.flow.window.has_value());
    }
    {
        SCOPED_TRACE("01.07: battle-shocked");
        GameState gs = a_move_units_step();
        gs.units[kGants].battle_shocked = true;
        marines_remain_stationary(gs);
        EXPECT_EQ(gs.flow.stage, Stage::Shoot);
    }
    {
        SCOPED_TRACE("TITANIC");
        GameState gs = a_move_units_step();
        gs.units[kGants].keywords.push_back("Titanic");
        marines_remain_stationary(gs);
        EXPECT_EQ(gs.flow.stage, Stage::Shoot);
    }
    {
        SCOPED_TRACE("15.09: no enemy within 24\"");
        GameState gs = a_move_units_step();
        for (Model& m : gs.units[kGants].models) m.weapons[0].profiles[0].range_mm = in(36);
        for (Model& m : gs.units[kMarines].models) m.position.y -= in(13);  // more than 24" away (off the zone: test only)
        ASSERT_GT(*unit_distance(gs.units[kMarines], gs.units[kGants]), in(24));
        marines_remain_stationary(gs);
        EXPECT_EQ(gs.flow.stage, Stage::Shoot);
    }
    {
        SCOPED_TRACE("15.01: already used this phase");
        GameState gs = a_move_units_step();
        gs.stratagems[1].used_this_phase.push_back(Stratagem::FireOverwatch);
        marines_remain_stationary(gs);
        EXPECT_EQ(gs.flow.stage, Stage::Shoot);
    }
}

TEST(R15_08_FireOverwatch, AnEngagedUnitIsNoTargetAndAnEngagedEnemyIsNoSnapTarget) {
    GameState gs = a_move_units_step();
    // A one-model Termagants unit and a one-model Intercessors unit 12" apart on the west side (x = 2"), clear of the
    // others; then the Termagants' main unit 1" from the main Intercessors (engaged with them).
    const UnitId west_gants = add_unit_copy(gs, kGants);
    const UnitId west_marines = add_unit_copy(gs, kMarines);
    Unit& wg = *gs.find_unit(west_gants);
    Unit& wm = *gs.find_unit(west_marines);
    wg.models.resize(1);
    wm.models.resize(1);
    wg.models[0].position.x = in(2);
    wm.models[0].position.x = in(2);
    const float gap = mm_to_inches(*unit_distance(gs.units[kMarines], gs.units[kGants]));
    for (Model& m : gs.units[kGants].models) m.position.y -= in(gap - 1.0f);
    ASSERT_TRUE(is_engaged(gs, gs.units[kGants]));
    ASSERT_FALSE(is_engaged(gs, *gs.find_unit(west_gants)));
    gs.pending = compute_decision(gs);
    for (const UnitId u : {gs.units[kMarines].id, west_marines}) {
        paced(gs, action::StartMove{u, MoveType::RemainStationary, std::nullopt});
    }
    ASSERT_EQ(gs.pending, window(Side::B, WindowTiming::EndOfPhase));
    // Only the western Termagants can be Fire Overwatch's target, and only the western Intercessors their snap target.
    EXPECT_EQ(window_info(gs)->options.at(0).targets, std::vector<UnitId>{west_gants});
    const auto refused = rejection(gs, Side::B, use(Stratagem::FireOverwatch, gs.units[kGants].id));
    ASSERT_FALSE(refused.empty());
    EXPECT_EQ(refused.front(), "15.08: Fire Overwatch targets an unengaged unit, and unit 2 is engaged");
    paced(gs, use(Stratagem::FireOverwatch, west_gants));
    const Unit& shooter = *gs.find_unit(west_gants);
    const std::vector<ShotDeclaration> at_main{ShotDeclaration{shooter.models[0].id, 0, 0, gs.units[kMarines].id}};
    const auto engaged = shot_problems(gs, shooter, ShootingType::Snap, at_main);
    ASSERT_FALSE(engaged.empty());
    EXPECT_TRUE(is_rule_cited(engaged.front())) << engaged.front();
    const std::vector<ShotDeclaration> at_west{ShotDeclaration{shooter.models[0].id, 0, 0, west_marines}};
    EXPECT_TRUE(shot_problems(gs, shooter, ShootingType::Snap, at_west).empty());
}

TEST(R15_08_FireOverwatch, NoWindowInYourOwnMovementPhase) {
    // Side B's Movement phase in round 1: the window at its end is Side A's (the Intercessors can snap shoot), never
    // Side B's.
    GameState gs = a_move_units_step();
    ScriptedDice dice(std::vector<int>(400, 3));
    play_paced_until(gs, dice, [](const GameState& g) {
        return g.flow.stage == Stage::EndOfMovementPhase && turn_side(g) == Side::B && window_pending(g);
    });
    EXPECT_EQ(gs.pending, window(Side::A, WindowTiming::EndOfPhase));
    const WindowInfo info = window_info(gs).value();
    ASSERT_EQ(info.options.size(), 1u);
    EXPECT_EQ(info.options[0].targets, std::vector<UnitId>{gs.units[kMarines].id});
    EXPECT_TRUE(legal_actions(gs, Side::B).empty());
}

TEST(R15_08_FireOverwatch, TheUnitShootsUsingSnapShootingAndItsPlayerIsTheActivePlayer) {
    GameState gs = fire_overwatch_window(2);
    const UnitId gants = gs.units[kGants].id;
    const auto used = paced(gs, use(Stratagem::FireOverwatch, gants));
    ASSERT_EQ(count<event::StratagemUsed>(used), 1u);
    EXPECT_EQ(events_of<event::StratagemUsed>(used).front(),
              (event::StratagemUsed{Side::B, Stratagem::FireOverwatch, gants, 1, 1}));
    EXPECT_EQ(gs.command_points[1], 1);
    EXPECT_FALSE(gs.flow.window.has_value());
    EXPECT_EQ(gs.flow.snap_shooter, gants);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::SnapShoot{gants}}));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "15.09");
    EXPECT_EQ(legal_actions(gs, Side::B), (std::vector<GameAction>{action::Shoot{gants, ShootingType::Snap, {}}}));
    EXPECT_TRUE(legal_actions(gs, Side::A).empty());
    EXPECT_EQ(compute_decision(gs), gs.pending);
    // Only the Termagants' snap shot answers it.
    EXPECT_FALSE(rejection(gs, Side::B, gants_shoot(gs, ShootingType::Normal)).empty());
    const auto refused = rejection(gs, Side::B, gants_shoot(gs, ShootingType::Normal));
    EXPECT_EQ(refused.front(), "15.08: Fire Overwatch's unit shoots using snap shooting (15.09)");
    const auto events = paced(gs, gants_shoot(gs, ShootingType::Snap));
    ASSERT_EQ(count<event::ShootingStarted>(events), 1u);
    EXPECT_EQ(events_of<event::ShootingStarted>(events).front().type, ShootingType::Snap);
    EXPECT_FALSE(gs.flow.snap_shooter.has_value());
    ASSERT_TRUE(gs.flow.attack.has_value());
    EXPECT_TRUE(gs.flow.attack->context.snap_shooting);
    EXPECT_FALSE(gs.flow.attack->context.heavy_conditions_met);
    EXPECT_EQ(active_side(gs), Side::B);  // 01.03: the unit's player, while it shoots
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Hit, gants}}));
    EXPECT_EQ(gs.flow.stage, Stage::EndOfMovementPhase);
    // 15.01: the Termagants were targeted by Fire Overwatch this phase.
    EXPECT_EQ(gs.stratagems[1].targeted_this_phase, std::vector<UnitId>{gants});
}

TEST(R15_08_FireOverwatch, TheTargetsSavesCanBeReRolledButNotTheShootersRolls) {
    GameState gs = snap_hit_roll_waits(3, [](WeaponProfile&) {});
    gs.command_points[0] = 1;
    const UnitId gants = gs.units[kGants].id;
    const UnitId marines = gs.units[kMarines].id;
    // Hits: four 6s; no window after them (15.09; and the Termagants were targeted this phase, 15.01).
    roll(gs, {6, 6, 6, 6, 1, 2, 3, 4, 5, 5});
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Wound, gants}}));
    // Wounds: S5 against T4 wounds on 3+ (two fail): no window (15.01).
    roll(gs, {6, 5, 1, 2});
    EXPECT_FALSE(window_pending(gs));
    // The saves are Side A's (05.03): the allocation order first, then the roll; Side A can re-roll a save.
    while (gs.pending && !roll_pending(gs)) paced(gs, legal_actions(gs, decider(gs)).front());
    ASSERT_EQ(gs.pending, (PendingDecision{Side::A, decision::RollDice{RollKind::Save, marines}}));
    roll(gs, {1, 6});
    EXPECT_EQ(gs.pending, window(Side::A, WindowTiming::AfterRoll, marines));
}

TEST(R15_08_FireOverwatch, AfterTheSnapShotThePhaseEndsAndTheGameGoesOn) {
    GameState gs = snap_hit_roll_waits(1, [](WeaponProfile&) {});
    roll(gs, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1});  // every snap shot misses: the attack ends
    EXPECT_FALSE(gs.flow.attack.has_value());
    EXPECT_EQ(gs.flow.stage, Stage::Shoot);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::Shoot{}}));
    EXPECT_TRUE(gs.stratagems[1].used_this_phase.empty());  // 15.01: the phase ended
    // The Termagants shot in Side A's turn; in Side B's own Shooting phase they shoot again (10.02: "this phase").
    EXPECT_FALSE(gs.units[kGants].phase_flags.selected_to_shoot);
}

// ==== 15.09 Snap Shooting ====

TEST(R15_09_SnapShooting, OnlyAnUnmodifiedSixHitsAndItIsNotACriticalHit) {
    GameState gs = snap_hit_roll_waits(1, [](WeaponProfile& p) { p.abilities.push_back(ability::SustainedHits{2}); });
    gs.flow.attack->selections[0].hit_modifier = 1;  // even +1 to hit changes nothing: only an unmodified 6
    const auto events = roll(gs, {6, 5, 4, 3, 2, 1, 6, 5, 4, 3});
    const auto hits = events_of<event::HitRollsMade>(events);
    ASSERT_EQ(hits.size(), 1u);
    const HitRolls& r = hits.front().rolls;
    ASSERT_EQ(r.rolls.size(), 10u);
    for (const HitRoll& h : r.rolls) {
        EXPECT_EQ(h.outcome, h.unmodified == 6 ? HitOutcome::Hit : HitOutcome::Fail) << *h.unmodified;
    }
    EXPECT_EQ(r.hits(), 2);
    EXPECT_EQ(r.critical_hits(), 0);
    EXPECT_EQ(r.sustained_hits, 0);  // 24.36: no critical hit, so no sustained hits
}

TEST(R15_09_SnapShooting, ATorrentWeaponStillHitsAutomatically) {
    GameState gs = snap_hit_roll_waits(1, [](WeaponProfile& p) {
        p.abilities.push_back(ability::Torrent{});
        p.skill.reset();
    });
    // No hit roll: the wound roll waits.
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Wound, gs.units[kGants].id}}));
}

TEST(R15_09_SnapShooting, OneTargetWithin24InchesOnly) {
    GameState gs = a_move_units_step();
    gs.command_points[1] = 1;
    for (Model& m : gs.units[kGants].models) m.weapons[0].profiles[0].range_mm = in(36);
    // A second Intercessor unit, 4" east of the first (as far from the Termagants).
    const UnitId second = add_unit_copy(gs, kMarines);
    for (Model& m : gs.find_unit(second)->models) m.position.x += in(4);
    gs.units[kMarines].models.resize(2);
    gs.find_unit(second)->models.erase(gs.find_unit(second)->models.begin(), gs.find_unit(second)->models.begin() + 3);
    gs.pending = compute_decision(gs);
    paced(gs, action::StartMove{gs.units[kMarines].id, MoveType::RemainStationary, std::nullopt});
    paced(gs, action::StartMove{second, MoveType::RemainStationary, std::nullopt});
    ASSERT_EQ(gs.pending, window(Side::B, WindowTiming::EndOfPhase));
    const UnitId gants = gs.units[kGants].id;
    paced(gs, use(Stratagem::FireOverwatch, gants));
    // Two targets: refused.
    action::Shoot both{gants, ShootingType::Snap, {}};
    for (std::size_t i = 0; i < gs.units[kGants].models.size(); ++i) {
        const UnitId target = i % 2 == 0 ? gs.units[kMarines].id : second;
        both.shots.push_back(ShotDeclaration{gs.units[kGants].models[i].id, 0, 0, target});
    }
    const auto refused = rejection(gs, Side::B, both);
    ASSERT_FALSE(refused.empty());
    EXPECT_TRUE(wh40k::test::starts_with(refused.front(), "15.09: snap shooting can only target one enemy unit"))
        << refused.front();
    // 24": the Intercessors exactly 24" away can be targeted; 24.1" can't (unit to unit, D-009).
    const auto at_gap = [&](float gap_in) {
        GameState g = gs;
        const float now = mm_to_inches(*unit_distance(g.units[kMarines], g.units[kGants]));
        for (Model& m : g.units[kMarines].models) m.position.y -= in(gap_in - now);
        return g;
    };
    {
        const GameState g = at_gap(24.0f);
        EXPECT_TRUE(validate_action(g, Side::B, gants_shoot(g, ShootingType::Snap)).ok());
    }
    {
        const GameState g = at_gap(24.1f);
        const auto r = rejection(g, Side::B, gants_shoot(g, ShootingType::Snap));
        ASSERT_FALSE(r.empty());
        EXPECT_NE(r.front().find("15.09"), std::string::npos) << r.front();
        EXPECT_NE(r.front().find("24\""), std::string::npos) << r.front();
        EXPECT_TRUE(is_rule_cited(r.front())) << r.front();
    }
}

TEST(R15_09_SnapShooting, AnEngagedMonsterCantBeTargetedOutsideYourShootingPhase) {
    // 07.02.01 Out of Phase Rules (app): 17.03 is "in your Shooting phase", so an engaged MONSTER is no target.
    GameState gs = wh40k::test::load_v1_scenario();
    gs.units[kMarines].keywords.push_back("Monster");
    // A one-model Termagants unit 1" east of the easternmost Intercessor (engaged with them, out of the way).
    const UnitId engaged = add_unit_copy(gs, kGants);
    Position east = gs.units[kMarines].models[0].position;
    for (const Model& m : gs.units[kMarines].models) {
        if (m.position.x > east.x) east = m.position;
    }
    gs.find_unit(engaged)->models.resize(1);
    gs.find_unit(engaged)->models[0].position = Position{east.x + 16.0f + 14.25f + in(1), east.y, 0.0f};
    ASSERT_TRUE(is_engaged(gs, gs.units[kMarines]));
    ASSERT_FALSE(is_engaged(gs, gs.units[kGants]));
    const Unit& gants = gs.units[kGants];
    const action::Shoot normal = gants_shoot(gs, ShootingType::Normal);
    EXPECT_TRUE(shot_problems(gs, gants, ShootingType::Normal, normal.shots).empty());  // 17.03
    const auto snap = shot_problems(gs, gants, ShootingType::Snap, normal.shots);
    ASSERT_FALSE(snap.empty());
    EXPECT_NE(snap.front().find("engaged"), std::string::npos) << snap.front();
    EXPECT_TRUE(is_rule_cited(snap.front())) << snap.front();
    EXPECT_FALSE(has_shot_option(gs, gants, ShootingType::Snap));
}

TEST(R15_09_SnapShooting, HeavyIsOffAndRapidFireStillApplies) {
    // The Intercessors snap shoot at the end of Side B's Movement phase: they haven't moved this turn, but [HEAVY]
    // (24.16) is "in your Shooting phase" (07.02.01). [RAPID FIRE 1] (24.30) still applies within half range.
    GameState gs = wh40k::test::load_v1_scenario();
    const Unit& marines = gs.units[kMarines];
    EXPECT_TRUE(shooting_context(gs, marines, ShootingType::Normal).heavy_conditions_met);
    EXPECT_FALSE(shooting_context(gs, marines, ShootingType::Snap).heavy_conditions_met);
    EXPECT_TRUE(shooting_context(gs, marines, ShootingType::Snap).snap_shooting);
    const float gap = mm_to_inches(*unit_distance(gs.units[kMarines], gs.units[kGants]));
    for (Model& m : gs.units[kGants].models) m.position.y -= in(gap - 10.0f);  // within half range (12")
    AttackContext context = shooting_context(gs, gs.units[kMarines], ShootingType::Snap);
    std::vector<WeaponSelection> selections;
    for (std::size_t i = 0; i < gs.units[kMarines].models.size(); ++i) {
        selections.push_back(wh40k::test::select(gs, kMarines, i, 0, 0, gs.units[kGants].id));  // Focused Fire
    }
    const auto batches = gather_attack_batches(gs, selections, context);
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].attack_dice, 10);  // A1 + [RAPID FIRE 1], 5 models
    EXPECT_TRUE(batches[0].profile.snap_shooting);
    EXPECT_EQ(batches[0].profile.hit_modifier, 0);  // no [HEAVY] +1
}

TEST(R15_09_SnapShooting, HazardousStillApplies) {
    GameState gs = snap_hit_roll_waits(1, [](WeaponProfile& p) { p.abilities.push_back(ability::Hazardous{}); });
    roll(gs, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1});
    // 24.15: the hazard rolls after the attacks (a snap shot is an attack like any other).
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Hazard, gs.units[kGants].id}}));
}

TEST(R15_09_SnapShooting, AttacksWithoutATargetAreLostNotReTargeted) {
    // Two fleshborer profiles (two batches); the first destroys the last Intercessor. Another Intercessor unit is in
    // range, but a snap shot targets one unit (15.09): the second batch is lost (04.03.03 gives it no new target).
    GameState gs = a_move_units_step();
    gs.command_points[1] = 1;
    const UnitId second = add_unit_copy(gs, kMarines);
    for (Model& m : gs.find_unit(second)->models) m.position.x += in(2);
    gs.units[kMarines].models.resize(1);
    gs.units[kMarines].models[0].wounds_remaining = 1;
    gs.find_unit(second)->models.erase(gs.find_unit(second)->models.begin());
    for (std::size_t i = 5; i < gs.units[kGants].models.size(); ++i) {
        gs.units[kGants].models[i].weapons[0].profiles[0].strength = 6;
    }
    gs.pending = compute_decision(gs);
    paced(gs, action::StartMove{gs.units[kMarines].id, MoveType::RemainStationary, std::nullopt});
    paced(gs, action::StartMove{second, MoveType::RemainStationary, std::nullopt});
    ASSERT_EQ(gs.pending, window(Side::B, WindowTiming::EndOfPhase));
    paced(gs, use(Stratagem::FireOverwatch, gs.units[kGants].id));
    paced(gs, gants_shoot(gs, ShootingType::Snap));
    // Batch 0 (the S5 fleshborers): one 6 hits, wounds, the save fails: the last Intercessor is destroyed.
    ASSERT_TRUE(std::holds_alternative<decision::SelectAttackBatch>(gs.pending->kind));
    std::vector<GameEvent> events = paced(gs, action::SelectAttackBatch{0});
    const auto add = [&events](const std::vector<GameEvent>& more) { events.insert(events.end(), more.begin(), more.end()); };
    add(roll(gs, {6, 1, 1, 1, 1}));  // hits
    add(roll(gs, {6}));              // wound
    while (gs.pending && !roll_pending(gs) && !window_pending(gs)) add(paced(gs, legal_actions(gs, decider(gs)).front()));
    add(roll(gs, {1}));  // the save fails
    for (int guard = 0; guard < 10 && gs.pending; ++guard) {
        if (window_pending(gs)) {
            add(decline(gs));
        } else if (const auto* d = std::get_if<decision::OrderCasualties>(&gs.pending->kind)) {
            add(paced(gs, action::OrderCasualties{d->target, model_ids(*gs.find_unit(d->target))}));
        } else {
            break;
        }
    }
    ASSERT_TRUE(is_destroyed(gs.units[kMarines]));
    // The second batch's five attacks are lost (D-093's AttacksLost), with no RetargetAttacks decision.
    const auto lost = events_of<event::AttacksLost>(events);
    ASSERT_EQ(lost.size(), 1u);
    EXPECT_EQ(lost.front().batch, 1u);
    EXPECT_EQ(lost.front().attack_dice, 5);
    EXPECT_EQ(lost.front().target, gs.units[kMarines].id);
    EXPECT_FALSE(gs.pending && std::holds_alternative<decision::RetargetAttacks>(gs.pending->kind));
    EXPECT_FALSE(gs.flow.attack.has_value());
    EXPECT_EQ(gs.find_unit(second)->models.size(), 4u);
    EXPECT_EQ(gs.flow.stage, Stage::Shoot);
}

TEST(R15_09_SnapShooting, ItIsRefusedAtTheNormalShootStep) {
    GameState gs = wh40k::test::shoot_step_state(Side::B);
    const auto refused = rejection(gs, Side::B, gants_shoot(gs, ShootingType::Snap));
    ASSERT_FALSE(refused.empty());
    EXPECT_EQ(refused.front(), "15.09: snap shooting is made only as a rule allows it (15.08 Fire Overwatch)");
    EXPECT_EQ(eligible_shooting_types(gs, gs.units[kGants]),
              (std::vector<ShootingType>{ShootingType::Normal}));  // never one of 10.02's types
}

// ==== 15.11 Heroic Intervention ====

namespace {

constexpr float kMarineRadius = 16.0f;
constexpr float kGantRadius = 14.25f;

Position westmost(const Unit& u) {
    Position best = u.models.at(0).position;
    for (const Model& m : u.models) {
        if (m.position.x < best.x) best = m.position;
    }
    return best;
}

// A one-model copy of units[index] at `pos`, with fresh turn and phase flags (it did not charge).
UnitId add_one_model_unit(GameState& gs, std::size_t index, Position pos) {
    const UnitId id = add_unit_copy(gs, index);
    Unit& u = *gs.find_unit(id);
    u.models.resize(1);
    u.models[0].position = pos;
    u.turn_flags = UnitTurnFlags{};
    u.phase_flags = UnitPhaseFlags{};
    return id;
}

// Side A's Charge step after the Intercessors' charge (after_successful_charge: they made a charge move and are engaged
// with the Termagants), with a one-model Termagants unit `gap_in` west of the westernmost Intercessor (unengaged) and
// Side B with `cp_b` CP. `before` changes the state first; then Side A ends the step (15.11's window opens for Side B
// when it can use it).
struct Heroic {
    GameState gs;
    UnitId lone;
};
template <typename Before>
Heroic heroic_window(float gap_in, int cp_b, Before before) {
    Heroic h{wh40k::test::after_successful_charge(), {}};
    GameState& gs = h.gs;
    const Position w = westmost(gs.units[kMarines]);
    h.lone = add_one_model_unit(gs, kGants, Position{w.x - kMarineRadius - kGantRadius - in(gap_in), w.y, 0.0f});
    gs.command_points[1] = cp_b;
    before(h);
    gs.pending = compute_decision(gs);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::A, decision::Charge{}}));
    paced(gs, action::EndChargeStep{});
    return h;
}
Heroic heroic_window(float gap_in, int cp_b) {
    return heroic_window(gap_in, cp_b, [](Heroic&) {});
}

// The lone Termagant's charge move: `inches` straight east.
std::vector<GameEvent> lone_moves_east(GameState& gs, UnitId lone, float inches) {
    const Model& m = gs.find_unit(lone)->models.at(0);
    const Position s = *move_start_position(gs, m.id);
    paced(gs, action::ProposeModelMove{m.id, {Position{s.x + in(inches), s.y, 0.0f}}});
    return paced(gs, action::CommitUnitMove{});
}

}  // namespace

TEST(R15_11_HeroicIntervention, TheWindowOpensAtTheEndOfTheOpponentsChargePhaseWithEachModeAndItsCost) {
    Heroic h = heroic_window(5.0f, 3);
    GameState& gs = h.gs;
    EXPECT_EQ(gs.flow.stage, Stage::EndOfChargePhase);
    ASSERT_EQ(gs.pending, window(Side::B, WindowTiming::EndOfPhase));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "15.11");
    EXPECT_EQ(legal_actions(gs, Side::B),
              (std::vector<GameAction>{action::DeclineStratagems{},
                                       use(Stratagem::HeroicIntervention, h.lone, StratagemMode::LeapToDefend),
                                       use(Stratagem::HeroicIntervention, h.lone, StratagemMode::IntoTheFray)}));
    const WindowInfo info = window_info(gs).value();
    ASSERT_EQ(info.options.size(), 2u);
    EXPECT_EQ(info.options[0].name, "Heroic Intervention (Leap to Defend)");
    EXPECT_EQ(info.options[0].cost, 1);
    EXPECT_EQ(info.options[0].mode, StratagemMode::LeapToDefend);
    EXPECT_EQ(info.options[0].targets, std::vector<UnitId>{h.lone});  // the engaged Termagants can't (11.02)
    EXPECT_EQ(info.options[1].name, "Heroic Intervention (Into the Fray)");
    EXPECT_EQ(info.options[1].cost, 2);  // FAQ Q45: 2CP outright
    EXPECT_EQ(info.options[1].rule, "15.11");
    // The mode is chosen with the stratagem (before the charge roll).
    const auto no_mode = rejection(gs, Side::B, use(Stratagem::HeroicIntervention, h.lone));
    ASSERT_FALSE(no_mode.empty());
    EXPECT_TRUE(wh40k::test::starts_with(no_mode.front(), "15.11: select Heroic Intervention's mode")) << no_mode.front();
    const auto engaged = rejection(gs, Side::B, use(Stratagem::HeroicIntervention, gs.units[kGants].id,
                                                    StratagemMode::LeapToDefend));
    ASSERT_FALSE(engaged.empty());
    EXPECT_EQ(engaged.front(), "11.02: unit 2 is engaged and cannot declare a charge");
    const auto mode_elsewhere = rejection(gs, Side::B, action::UseStratagem{Stratagem::FireOverwatch, h.lone,
                                                                            std::nullopt, StratagemMode::IntoTheFray});
    ASSERT_FALSE(mode_elsewhere.empty());
    EXPECT_EQ(mode_elsewhere.front(), "15.08: Fire Overwatch has no modes");
}

TEST(R15_11_HeroicIntervention, WithOneCpOnlyLeapToDefendIsOffered) {
    Heroic h = heroic_window(5.0f, 1);
    const WindowInfo info = window_info(h.gs).value();
    ASSERT_EQ(info.options.size(), 1u);
    EXPECT_EQ(info.options[0].mode, StratagemMode::LeapToDefend);
    const auto refused =
        rejection(h.gs, Side::B, use(Stratagem::HeroicIntervention, h.lone, StratagemMode::IntoTheFray));
    ASSERT_FALSE(refused.empty());
    EXPECT_EQ(refused.front(), "15.01: Side B has 1 CP; Heroic Intervention (Into the Fray) costs 2CP");
}

TEST(R15_11_HeroicIntervention, TheTargetLimits) {
    // Two lone Termagant units; the second changed by each case. Only the first can then be the target.
    const auto check = [](const char* what, auto change, const std::string& reason) {
        SCOPED_TRACE(what);
        UnitId second{};
        Heroic h = heroic_window(5.0f, 1, [&](Heroic& hh) {
            const Position w = westmost(hh.gs.units[kMarines]);
            second = add_one_model_unit(hh.gs, kGants, Position{w.x - kMarineRadius - kGantRadius - in(5.0f),
                                                                w.y - in(2.0f), 0.0f});
            change(*hh.gs.find_unit(second));
        });
        ASSERT_EQ(h.gs.pending, window(Side::B, WindowTiming::EndOfPhase));
        EXPECT_EQ(window_info(h.gs)->options.at(0).targets, std::vector<UnitId>{h.lone});
        const auto refused =
            rejection(h.gs, Side::B, use(Stratagem::HeroicIntervention, second, StratagemMode::LeapToDefend));
        ASSERT_FALSE(refused.empty());
        EXPECT_EQ(refused.front(), reason);
    };
    check("a VEHICLE", [](Unit& u) { u.keywords.push_back("Vehicle"); },
          "15.11: Heroic Intervention can select a VEHICLE unit only if it is a CHARACTER or WALKER unit (unit 4 is not)");
    check("01.07", [](Unit& u) { u.battle_shocked = true; },
          "01.07: unit 4 is battle-shocked, so its player can't target it with stratagems");
    check("11.02: it advanced", [](Unit& u) { u.turn_flags.advanced = true; },
          "11.02: unit 4 made an advance move this turn and cannot declare a charge");
    // A VEHICLE that is a WALKER can.
    Heroic walker = heroic_window(5.0f, 1, [](Heroic& hh) {
        Unit& u = *hh.gs.find_unit(hh.lone);
        u.keywords.push_back("Vehicle");
        u.keywords.push_back("Walker");
    });
    EXPECT_EQ(walker.gs.pending, window(Side::B, WindowTiming::EndOfPhase));
}

TEST(R15_11_HeroicIntervention, NoWindowWhenNeitherModeCanSelectATarget) {
    {
        SCOPED_TRACE("no enemy unit within 12\"");
        Heroic h = heroic_window(12.5f, 3);
        EXPECT_FALSE(window_pending(h.gs));
        EXPECT_NE(h.gs.flow.stage, Stage::EndOfChargePhase);
    }
    {
        SCOPED_TRACE("no enemy charged, none within 6\"");
        Heroic h = heroic_window(7.0f, 3, [](Heroic& hh) { hh.gs.units[kMarines].turn_flags.made_charge_move = false; });
        EXPECT_FALSE(window_pending(h.gs));
    }
    {
        SCOPED_TRACE("no enemy charged: only Into the Fray (an enemy within 6\")");
        Heroic h = heroic_window(5.0f, 3, [](Heroic& hh) { hh.gs.units[kMarines].turn_flags.made_charge_move = false; });
        const WindowInfo info = window_info(h.gs).value();
        ASSERT_EQ(info.options.size(), 1u);
        EXPECT_EQ(info.options[0].mode, StratagemMode::IntoTheFray);
    }
    {
        SCOPED_TRACE("in your own Charge phase");
        // Side A ends its own Charge step: Side A is never offered Heroic Intervention there (the window is Side B's).
        Heroic h = heroic_window(5.0f, 3);
        EXPECT_TRUE(legal_actions(h.gs, Side::A).empty());
    }
}

TEST(R15_11_HeroicIntervention, LeapToDefendChargesOnlyAUnitThatMadeAChargeMove) {
    // Another Intercessor unit 4" west of the lone Termagant: it did not charge.
    UnitId other{};
    Heroic h = heroic_window(5.0f, 1, [&](Heroic& hh) {
        const Position g = hh.gs.find_unit(hh.lone)->models[0].position;
        other = add_one_model_unit(hh.gs, kMarines, Position{g.x - kMarineRadius - kGantRadius - in(4.0f), g.y, 0.0f});
    });
    GameState& gs = h.gs;
    const auto used = paced(gs, use(Stratagem::HeroicIntervention, h.lone, StratagemMode::LeapToDefend));
    ASSERT_EQ(count<event::StratagemUsed>(used), 1u);
    EXPECT_EQ(events_of<event::StratagemUsed>(used).front(),
              (event::StratagemUsed{Side::B, Stratagem::HeroicIntervention, h.lone, 1, 0, StratagemMode::LeapToDefend}));
    ASSERT_EQ(count<event::ChargeDeclared>(used), 1u);
    EXPECT_EQ(events_of<event::ChargeDeclared>(used).front().unit, h.lone);
    EXPECT_FALSE(gs.flow.window.has_value());
    ASSERT_TRUE(gs.flow.charge.has_value());
    EXPECT_EQ(gs.flow.charge->heroic, StratagemMode::LeapToDefend);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::RollDice{RollKind::Charge, h.lone}}));
    EXPECT_EQ(active_side(gs), Side::B);  // 01.03: selected to move (charge)
    const auto rolled = roll(gs, {6, 6});
    const auto charge = events_of<event::ChargeRolled>(rolled);
    ASSERT_EQ(charge.size(), 1u);
    EXPECT_EQ(charge.front().eligible_targets, std::vector<UnitId>{gs.units[kMarines].id});  // not `other`
    EXPECT_EQ(charge.front().capped, std::nullopt);
    ASSERT_EQ(gs.pending, (PendingDecision{Side::B, decision::SelectChargeTargets{h.lone}}));
    const auto refused = rejection(gs, Side::B, action::StartChargeMove{h.lone, {other}});
    ASSERT_FALSE(refused.empty());
    EXPECT_EQ(refused.front(), "15.11: Leap to Defend: " + label(other) +
                                   " is not an enemy unit that made a charge move this phase, within the charge roll "
                                   "(12\") of " + label(h.lone));
}

TEST(R15_11_HeroicIntervention, IntoTheFrayCostsTwoCpCapsTheRollAtSixAndTargetsAnyEnemyWithin6Inches) {
    UnitId other{};
    Heroic h = heroic_window(5.0f, 3, [&](Heroic& hh) {
        const Position g = hh.gs.find_unit(hh.lone)->models[0].position;
        other = add_one_model_unit(hh.gs, kMarines, Position{g.x - kMarineRadius - kGantRadius - in(4.0f), g.y, 0.0f});
    });
    GameState& gs = h.gs;
    const auto used = paced(gs, use(Stratagem::HeroicIntervention, h.lone, StratagemMode::IntoTheFray));
    EXPECT_EQ(events_of<event::StratagemUsed>(used).at(0),
              (event::StratagemUsed{Side::B, Stratagem::HeroicIntervention, h.lone, 2, 1, StratagemMode::IntoTheFray}));
    EXPECT_EQ(gs.command_points[1], 1);
    const auto rolled = roll(gs, {6, 6});
    const auto charge = events_of<event::ChargeRolled>(rolled);
    ASSERT_EQ(charge.size(), 1u);
    EXPECT_EQ(charge.front().roll.total(), 12);
    EXPECT_EQ(charge.front().capped, 6);  // "if the result is greater than 6, change it to 6"
    EXPECT_EQ(charge.front().eligible_targets, (std::vector<UnitId>{gs.units[kMarines].id, other}));
    EXPECT_EQ(gs.find_unit(h.lone)->phase_flags.charge_roll, 6);
    // 15.01: the Termagant was targeted by Heroic Intervention this phase, so Command Re-roll can't re-roll its charge
    // roll (Side B has 1 CP left): no window.
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::SelectChargeTargets{h.lone}}));
    // The maximum distance is 6": a 6.5" charge move is too far.
    paced(gs, action::StartChargeMove{h.lone, {gs.units[kMarines].id}});
    const Model& m = gs.find_unit(h.lone)->models.at(0);
    const Position s = *move_start_position(gs, m.id);
    const auto too_far = rejection(gs, Side::B, action::ProposeModelMove{m.id, {Position{s.x + in(6.5f), s.y, 0.0f}}});
    EXPECT_FALSE(too_far.empty());
}

TEST(R15_11_HeroicIntervention, TheChargeMoveGivesFightsFirstAndTheGameGoesOnToTheFightPhase) {
    Heroic h = heroic_window(5.0f, 1);
    GameState& gs = h.gs;
    paced(gs, use(Stratagem::HeroicIntervention, h.lone, StratagemMode::LeapToDefend));
    roll(gs, {3, 3});
    paced(gs, action::StartChargeMove{h.lone, {gs.units[kMarines].id}});
    const auto events = lone_moves_east(gs, h.lone, 4.0f);  // ends 1" from the westernmost Intercessor
    const auto resolved = events_of<event::ChargeResolved>(events);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_TRUE(resolved.front().charge_move_made);
    const Unit& lone = *gs.find_unit(h.lone);
    EXPECT_TRUE(lone.turn_flags.made_charge_move);
    EXPECT_TRUE(lone.turn_flags.fights_first);  // 11.04: until the end of the turn
    EXPECT_TRUE(is_engaged(gs, lone));
    // No second window: the Charge phase ends.
    EXPECT_EQ(count<event::PhaseEnded>(events), 1u);
    EXPECT_NE(gs.flow.stage, Stage::EndOfChargePhase);
    EXPECT_TRUE(gs.stratagems[1].used_this_phase.empty());
    // In the Fight step the Termagant is a Fights First unit, eligible to fight (12.04: it made a charge move).
    ScriptedDice dice(std::vector<int>(64, 1));
    play_paced_until(gs, dice, [](const GameState& g) { return g.flow.stage == Stage::Fight; });
    EXPECT_TRUE(eligible_to_fight(gs, *gs.find_unit(h.lone)));
    EXPECT_TRUE(is_fights_first_unit(*gs.find_unit(h.lone)));
}

TEST(R15_11_HeroicIntervention, AFailedHeroicChargeMovesNothing) {
    Heroic h = heroic_window(5.0f, 1);
    GameState& gs = h.gs;
    paced(gs, use(Stratagem::HeroicIntervention, h.lone, StratagemMode::LeapToDefend));
    const Position before = gs.find_unit(h.lone)->models.at(0).position;
    const auto events = roll(gs, {1, 2});
    const auto resolved = events_of<event::ChargeResolved>(events);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_FALSE(resolved.front().charge_move_made);
    EXPECT_TRUE(wh40k::test::starts_with(resolved.front().reason, "15.11: Leap to Defend:")) << resolved.front().reason;
    EXPECT_EQ(gs.find_unit(h.lone)->models.at(0).position, before);
    EXPECT_FALSE(gs.find_unit(h.lone)->turn_flags.made_charge_move);
    EXPECT_NE(gs.flow.stage, Stage::EndOfChargePhase);
}

// ==== 15.12 Counteroffensive ====

namespace {

// From Side A's Charge step: end it (any window there declined) and make no pile-in moves, reaching the Fight step.
void to_fight_step(GameState& gs) {
    gs.pending = compute_decision(gs);
    paced(gs, action::EndChargeStep{});
    ScriptedDice none{std::vector<int>{}};
    play_paced_until(gs, none, [](const GameState& g) { return g.flow.stage == Stage::Fight; });
}

// The unit is selected to fight (Normal) and every one of its attacks misses; the windows after its own rolls are
// declined. Returns every event, up to the window after the fight (if any).
std::vector<GameEvent> fight_and_miss(GameState& gs, UnitId unit, FightType type = FightType::Normal) {
    std::vector<GameEvent> events = paced(gs, action::SelectToFight{unit, type});
    const auto add = [&events](const std::vector<GameEvent>& more) { events.insert(events.end(), more.begin(), more.end()); };
    ScriptedDice misses(std::vector<int>(64, 1));
    for (int guard = 0; guard < 20 && gs.flow.fight; ++guard) {
        if (roll_pending(gs)) {
            add(roll(gs, misses));
        } else if (window_pending(gs)) {
            add(decline(gs));
        } else {
            add(paced(gs, wh40k::test::neutral_action(gs)));  // first melee options, casualties in unit order, ...
        }
    }
    EXPECT_FALSE(gs.flow.fight.has_value());
    return events;
}

const FightSequence& sequence(const GameState& gs) { return gs.flow.fight_sequence.value(); }

}  // namespace

TEST(R15_12_Counteroffensive, TheWindowOpensJustAfterAnEnemyUnitHasFought) {
    GameState gs = wh40k::test::after_successful_charge();
    gs.command_points[1] = 2;
    to_fight_step(gs);
    const UnitId marines = gs.units[kMarines].id;
    const UnitId gants = gs.units[kGants].id;
    ASSERT_EQ(gs.pending, (PendingDecision{Side::A, decision::SelectUnitToFight{}}));
    fight_and_miss(gs, marines);
    ASSERT_EQ(gs.pending, window(Side::B, WindowTiming::AfterAttacks, marines));
    EXPECT_EQ(decision_rule(gs, *gs.pending), "15.12");
    EXPECT_EQ(legal_actions(gs, Side::B),
              (std::vector<GameAction>{action::DeclineStratagems{}, use(Stratagem::Counteroffensive, gants)}));
    const WindowInfo info = window_info(gs).value();
    EXPECT_EQ(info.timing, WindowTiming::AfterAttacks);
    EXPECT_EQ(info.unit, marines);
    EXPECT_EQ(info.roll, std::nullopt);
    ASSERT_EQ(info.options.size(), 1u);
    EXPECT_EQ(info.options[0].name, "Counteroffensive");
    EXPECT_EQ(info.options[0].cost, 2);
    EXPECT_EQ(info.options[0].targets, std::vector<UnitId>{gants});
    // Declined: the sequence goes on as before (Side B selects next, D-020).
    decline(gs);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::SelectUnitToFight{}}));
    EXPECT_EQ(sequence(gs).sub_step, FightSubStep::Remaining);
}

TEST(R15_12_Counteroffensive, NoWindowWithout2CpAfterAFriendlyFightOrASecondTime) {
    {
        SCOPED_TRACE("1 CP");
        GameState gs = wh40k::test::after_successful_charge();
        gs.command_points[1] = 1;
        to_fight_step(gs);
        fight_and_miss(gs, gs.units[kMarines].id);
        EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::SelectUnitToFight{}}));
    }
    {
        SCOPED_TRACE("after Side B's own unit fights, and once used");
        GameState gs = wh40k::test::after_successful_charge();
        gs.command_points[1] = 4;
        to_fight_step(gs);
        fight_and_miss(gs, gs.units[kMarines].id);
        paced(gs, use(Stratagem::Counteroffensive, gs.units[kGants].id));
        fight_and_miss(gs, gs.units[kGants].id);  // Side B's own fight: no window for anyone (Side A's turn)
        EXPECT_FALSE(window_pending(gs));
        EXPECT_EQ(gs.command_points[1], 2);
    }
}

TEST(R15_12_Counteroffensive, TheUnitHasFightsFirstUntilTheEndOfThePhaseAndMustBeSelectedNext) {
    GameState gs = wh40k::test::after_successful_charge();
    gs.command_points[1] = 2;
    to_fight_step(gs);
    const UnitId gants = gs.units[kGants].id;
    fight_and_miss(gs, gs.units[kMarines].id);
    const auto used = paced(gs, use(Stratagem::Counteroffensive, gants));
    EXPECT_EQ(events_of<event::StratagemUsed>(used).at(0),
              (event::StratagemUsed{Side::B, Stratagem::Counteroffensive, gants, 2, 0}));
    EXPECT_TRUE(gs.find_unit(gants)->phase_flags.fights_first);
    EXPECT_TRUE(is_fights_first_unit(*gs.find_unit(gants)));
    EXPECT_EQ(sequence(gs).next_to_fight, gants);
    EXPECT_EQ(sequence(gs).sub_step, FightSubStep::FightsFirst);  // play returns to Fights First at once
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::SelectUnitToFight{}}));
    EXPECT_EQ(legal_actions(gs, Side::B), (std::vector<GameAction>{action::SelectToFight{gants, FightType::Normal}}));
    fight_and_miss(gs, gants);
    EXPECT_FALSE(gs.flow.fight_sequence && gs.flow.fight_sequence->next_to_fight);
    EXPECT_TRUE(gs.find_unit(gants)->phase_flags.fights_first);
    // Until the end of the phase (FAQ Q62: through its end step, left on leaving it), then gone.
    ScriptedDice dice(std::vector<int>(64, 1));
    play_paced_until(gs, dice, [](const GameState& g) { return g.flow.stage == Stage::Consolidate; });
    EXPECT_TRUE(gs.find_unit(gants)->phase_flags.fights_first);
    play_paced_until(gs, dice, [](const GameState& g) { return phase_of(g.flow.stage) != Phase::Fight; });
    EXPECT_FALSE(gs.find_unit(gants)->phase_flags.fights_first);
}

TEST(R15_12_Counteroffensive, ItFightsBetweenTheTurnPlayersTwoFightsFirstUnits) {
    // A second Intercessor unit, engaged with the Termagants' front-west model, with Fights First (test-only: as if
    // it had charged too).
    const auto setup = [] {
        GameState gs = wh40k::test::after_successful_charge();
        Position g = gs.units[kGants].models[0].position;
        for (const Model& m : gs.units[kGants].models) {
            if (m.position.y < g.y - 0.01f || (std::abs(m.position.y - g.y) <= 0.01f && m.position.x < g.x)) g = m.position;
        }
        const UnitId second = add_one_model_unit(gs, kMarines, Position{g.x - kGantRadius - kMarineRadius - in(1), g.y, 0});
        gs.find_unit(second)->turn_flags.made_charge_move = true;
        gs.find_unit(second)->turn_flags.fights_first = true;
        gs.command_points[1] = 2;
        to_fight_step(gs);
        return std::make_pair(gs, second);
    };
    const auto order = [](const std::vector<GameEvent>& events) {
        std::vector<UnitId> out;
        for (const auto& s : events_of<event::UnitSelectedToFight>(events)) out.push_back(s.unit);
        return out;
    };
    {
        SCOPED_TRACE("declined");
        auto [gs, second] = setup();
        std::vector<GameEvent> events = fight_and_miss(gs, gs.units[kMarines].id);
        decline(gs);
        ASSERT_EQ(gs.pending, (PendingDecision{Side::A, decision::SelectUnitToFight{}}));  // Side B has no Fights First unit
        const auto more = fight_and_miss(gs, second);
        events.insert(events.end(), more.begin(), more.end());
        EXPECT_EQ(order(events), (std::vector<UnitId>{gs.units[kMarines].id, second}));
    }
    {
        SCOPED_TRACE("used");
        auto [gs, second] = setup();
        const UnitId gants = gs.units[kGants].id;
        std::vector<GameEvent> events = fight_and_miss(gs, gs.units[kMarines].id);
        paced(gs, use(Stratagem::Counteroffensive, gants));
        auto more = fight_and_miss(gs, gants);
        events.insert(events.end(), more.begin(), more.end());
        ASSERT_EQ(gs.pending, (PendingDecision{Side::A, decision::SelectUnitToFight{}}));
        more = fight_and_miss(gs, second);
        events.insert(events.end(), more.begin(), more.end());
        EXPECT_EQ(order(events), (std::vector<UnitId>{gs.units[kMarines].id, gants, second}));
    }
}

TEST(R15_12_Counteroffensive, UsedInRemainingCombatsPlayReturnsToFightsFirst) {
    GameState gs = wh40k::test::after_successful_charge();
    gs.units[kMarines].turn_flags.fights_first = false;  // test-only: no Fights First unit
    gs.command_points[1] = 2;
    to_fight_step(gs);
    ASSERT_EQ(sequence(gs).sub_step, FightSubStep::Remaining);
    fight_and_miss(gs, gs.units[kMarines].id);
    ASSERT_EQ(gs.pending, window(Side::B, WindowTiming::AfterAttacks, gs.units[kMarines].id));
    paced(gs, use(Stratagem::Counteroffensive, gs.units[kGants].id));
    EXPECT_EQ(sequence(gs).sub_step, FightSubStep::FightsFirst);
    EXPECT_EQ(gs.pending, (PendingDecision{Side::B, decision::SelectUnitToFight{}}));
}

TEST(R15_12_Counteroffensive, APassStaysAllowedAndEndsTheRequirement) {
    GameState gs = wh40k::test::after_successful_charge();
    gs.command_points[1] = 2;
    // A second Termagants unit, also engaged with the Intercessors (east of them).
    Position east = gs.units[kMarines].models[0].position;
    for (const Model& m : gs.units[kMarines].models) {
        if (m.position.x > east.x) east = m.position;
    }
    const UnitId second = add_one_model_unit(gs, kGants, Position{east.x + kMarineRadius + kGantRadius + in(1), east.y, 0});
    to_fight_step(gs);
    fight_and_miss(gs, gs.units[kMarines].id);
    const UnitId gants = gs.units[kGants].id;
    paced(gs, use(Stratagem::Counteroffensive, second));
    EXPECT_EQ(legal_actions(gs, Side::B), (std::vector<GameAction>{action::SelectToFight{second, FightType::Normal}}));
    const auto refused = rejection(gs, Side::B, action::SelectToFight{gants, FightType::Normal});
    ASSERT_FALSE(refused.empty());
    EXPECT_EQ(refused.front(), "15.12: Counteroffensive: " + label(second) +
                                   " must be the next unit Side B selects to fight");
    // Test-only: Side B's units now more than 5" from every enemy unit (still eligible: engaged at the step's start),
    // so Side B may pass (D-020).
    for (const UnitId u : {gants, second}) {
        for (Model& m : gs.find_unit(u)->models) m.position.y += in(8);
    }
    gs.pending = compute_decision(gs);
    const auto legal = legal_actions(gs, Side::B);
    ASSERT_NE(std::find(legal.begin(), legal.end(), GameAction{action::PassFight{}}), legal.end());
    const auto passed = paced(gs, action::PassFight{});
    EXPECT_EQ(count<event::FightPassed>(passed), 1u);
    EXPECT_FALSE(gs.flow.fight_sequence && gs.flow.fight_sequence->next_to_fight);
}

TEST(R15_12_Counteroffensive, TheWindowOpensAfterAFightWithNoAttacks) {
    // FAQ Q2: "just after an enemy unit has resolved its attacks" includes a fight with none. Both units eligible to
    // fight but 12" apart (test-only: made_charge_move set by hand); the Intercessors make an overrun fight with no
    // attack.
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (gs.flow.stage != Stage::Charge) apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    gs.units[kMarines].turn_flags.made_charge_move = true;
    gs.units[kMarines].turn_flags.fights_first = true;
    gs.units[kGants].turn_flags.made_charge_move = true;
    gs.command_points[1] = 2;
    to_fight_step(gs);
    const auto events = fight_and_miss(gs, gs.units[kMarines].id, FightType::Overrun);
    ASSERT_EQ(count<event::MeleeAttacksDeclared>(events), 1u);
    EXPECT_TRUE(events_of<event::MeleeAttacksDeclared>(events).front().selections.empty());
    EXPECT_EQ(gs.pending, window(Side::B, WindowTiming::AfterAttacks, gs.units[kMarines].id));
}

TEST(R15_12_Counteroffensive, NoWindowOnceTheGameHasEnded) {
    // D-007: the Intercessors destroy the last Termagant; the game is over, so no window.
    GameState gs = wh40k::test::after_successful_charge();
    gs.units[kGants].models.resize(1);
    gs.command_points[1] = 2;
    to_fight_step(gs);
    paced(gs, action::SelectToFight{gs.units[kMarines].id, FightType::Normal});
    paced(gs, first_melee_options(gs, gs.units[kMarines].id));
    ScriptedDice dice(std::vector<int>(64, 6));
    for (int guard = 0; guard < 20 && gs.pending; ++guard) {
        if (roll_pending(gs)) {
            const auto& d = std::get<decision::RollDice>(gs.pending->kind);
            ScriptedDice fail(std::vector<int>(64, 1));
            if (d.roll == RollKind::Save) {
                roll(gs, fail);
            } else {
                roll(gs, dice);
            }
        } else if (window_pending(gs)) {
            ASSERT_NE(std::get<decision::UseStratagems>(gs.pending->kind).timing, WindowTiming::AfterAttacks);
            decline(gs);
        } else {
            paced(gs, wh40k::test::neutral_action(gs));
        }
    }
    EXPECT_TRUE(gs.result.has_value());
    EXPECT_FALSE(gs.flow.window.has_value());
}

// ==== 14.02.01: objective control first at the end of a phase ====

namespace {

// D-084: a one-objective mission ("Centre", 3" across, centred on `centre`), with each unit's points set (as a mission
// needs).
void add_objective(GameState& gs, Position centre) {
    Mission mission;
    mission.objectives.push_back(wh40k::test::objective_area("Centre", centre, 3.0f));
    gs.mission = mission;
    gs.result_rule = ResultRule::Mission;
    for (Unit& u : gs.units) {
        u.points = u.side == Side::A ? 80 : 55;
        u.starting_wounds = 0;
        for (const Model& m : u.models) u.starting_wounds += m.characteristics.wounds;
    }
}

}  // namespace

TEST(R14_02_01_ObjectiveControl, ControlIsDeterminedOnEnteringTheEndOfPhaseStepBeforeTheWindow) {
    // The objective 3" north of the first Intercessor: the Intercessors move 3" north onto it.
    GameState gs = wh40k::test::load_v1_scenario();
    const Position first = gs.units[kMarines].models[0].position;
    add_objective(gs, Position{first.x, first.y + in(3), 0.0f});
    start_game_paced(gs);
    ASSERT_FALSE(gs.mission->objectives[0].controller.has_value());
    paced(gs, action::StartMove{gs.units[kMarines].id, MoveType::Normal, std::nullopt});
    for (const Model& m : gs.units[kMarines].models) {
        const Position s = *move_start_position(gs, m.id);
        paced(gs, action::ProposeModelMove{m.id, {Position{s.x, s.y + in(3), 0.0f}}});
    }
    const auto events = paced(gs, action::CommitUnitMove{});
    const auto changed = events_of<event::ObjectiveControlChanged>(events);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed.front().controller, Side::A);
    EXPECT_EQ(gs.mission->objectives[0].controller, Side::A);
    EXPECT_EQ(gs.pending, window(Side::B, WindowTiming::EndOfPhase));  // the window comes after the check
    EXPECT_EQ(count<event::PhaseEnded>(events), 0u);
    const auto declined = decline(gs);
    EXPECT_EQ(count<event::PhaseEnded>(declined), 1u);
    EXPECT_TRUE(events_of<event::ObjectiveControlChanged>(declined).empty());  // not determined again on leaving
}

TEST(R14_02_01_ObjectiveControl, FireOverwatchKillsChangeControlOnlyAtTheEndOfTheShootingPhase) {
    // One Intercessor (1 wound left) on the objective, the others 6" east of it; the snap shot destroys it.
    GameState gs = wh40k::test::load_v1_scenario();
    const UnitId rest = add_unit_copy(gs, kMarines);
    gs.units[kMarines].models.resize(1);
    gs.units[kMarines].models[0].wounds_remaining = 1;
    for (Model& m : gs.find_unit(rest)->models) m.position.x += in(6);
    gs.find_unit(rest)->models.erase(gs.find_unit(rest)->models.begin());
    add_objective(gs, gs.units[kMarines].models[0].position);
    start_game_paced(gs);
    ScriptedDice passes(std::vector<int>(8, 6));  // the lone Intercessor's battle-shock test (below half-strength)
    play_paced_until(gs, passes, [](const GameState& g) { return g.flow.stage == Stage::MoveUnits; });
    paced(gs, action::StartMove{gs.units[kMarines].id, MoveType::RemainStationary, std::nullopt});
    paced(gs, action::StartMove{rest, MoveType::RemainStationary, std::nullopt});
    ASSERT_EQ(gs.pending, window(Side::B, WindowTiming::EndOfPhase));
    ASSERT_EQ(gs.mission->objectives[0].controller, Side::A);
    paced(gs, use(Stratagem::FireOverwatch, gs.units[kGants].id));
    action::Shoot at_one{gs.units[kGants].id, ShootingType::Snap, {}};
    for (const Model& m : gs.units[kGants].models) at_one.shots.push_back(ShotDeclaration{m.id, 0, 0, gs.units[kMarines].id});
    paced(gs, at_one);
    roll(gs, {6, 1, 1, 1, 1, 1, 1, 1, 1, 1});  // one hit
    roll(gs, {6});                              // it wounds
    while (gs.pending && !roll_pending(gs)) paced(gs, legal_actions(gs, decider(gs)).front());
    roll(gs, {1});  // the save fails
    for (int guard = 0; guard < 10 && gs.pending && gs.flow.stage == Stage::EndOfMovementPhase; ++guard) {
        if (window_pending(gs)) {
            decline(gs);
        } else {
            paced(gs, wh40k::test::neutral_action(gs));
        }
    }
    ASSERT_TRUE(is_destroyed(gs.units[kMarines]));
    EXPECT_EQ(gs.flow.stage, Stage::Shoot);
    EXPECT_EQ(gs.mission->objectives[0].controller, Side::A);  // still: determined at the end of the phase only
    const auto ended = paced(gs, action::EndShootStep{});
    const auto changed = events_of<event::ObjectiveControlChanged>(ended);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed.front().controller, std::nullopt);
}

// ==== D-097: the new state, its JSON and the windows ====

TEST(D097_Windows, EachNewFieldRoundTripsAndIsOmittedOtherwise) {
    const auto round_trip = [](const GameState& gs) { return Json(gs).get<GameState>(); };
    // A window after no roll (no roll, no unit), the snap shooter, a heroic charge, Counteroffensive's Fights First and
    // next unit, and a snap attack.
    {
        GameState gs = fire_overwatch_window();
        ASSERT_TRUE(gs.flow.window.has_value());
        EXPECT_EQ(round_trip(gs), gs);
        const Json j = Json(gs).at("flow").at("window");
        EXPECT_FALSE(j.contains("roll"));
        EXPECT_FALSE(j.contains("unit"));
        EXPECT_FALSE(j.contains("faces"));
        EXPECT_FALSE(j.contains("batch"));
        paced(gs, use(Stratagem::FireOverwatch, gs.units[kGants].id));
        ASSERT_TRUE(gs.flow.snap_shooter.has_value());
        EXPECT_EQ(round_trip(gs), gs);
        EXPECT_TRUE(Json(gs).at("flow").contains("snap_shooter"));
        paced(gs, gants_shoot(gs, ShootingType::Snap));
        ASSERT_TRUE(gs.flow.attack->context.snap_shooting);
        EXPECT_EQ(round_trip(gs), gs);
        EXPECT_TRUE(Json(gs).at("flow").at("attack").at("context").at("snap_shooting").get<bool>());
    }
    {
        Heroic h = heroic_window(5.0f, 3);
        paced(h.gs, use(Stratagem::HeroicIntervention, h.lone, StratagemMode::IntoTheFray));
        ASSERT_TRUE(h.gs.flow.charge->heroic.has_value());
        EXPECT_EQ(round_trip(h.gs), h.gs);
        EXPECT_EQ(Json(h.gs).at("flow").at("charge").at("heroic"), "IntoTheFray");
    }
    {
        GameState gs = wh40k::test::after_successful_charge();
        gs.command_points[1] = 2;
        to_fight_step(gs);
        fight_and_miss(gs, gs.units[kMarines].id);
        EXPECT_EQ(round_trip(gs), gs);  // the AfterAttacks window (a unit, no roll)
        paced(gs, use(Stratagem::Counteroffensive, gs.units[kGants].id));
        EXPECT_EQ(round_trip(gs), gs);
        const Json j = Json(gs);
        EXPECT_TRUE(j.at("flow").at("fight_sequence").contains("next_to_fight"));
        EXPECT_TRUE(j.at("units").at(kGants).at("phase_flags").at("fights_first").get<bool>());
    }
    // None of them in a state without the new stratagems.
    GameState plain = wh40k::test::shoot_step_state(Side::A);
    const std::string text = Json(plain).dump();
    for (const char* key : {"snap_shooter", "heroic", "next_to_fight", "snap_shooting"}) {
        EXPECT_EQ(text.find(key), std::string::npos) << key;
    }
    for (const Json& u : Json(plain).at("units")) EXPECT_FALSE(u.at("phase_flags").contains("fights_first"));
}

TEST(D097_Windows, TheActionsAndEventsRoundTripWithTheirNewFields) {
    const std::vector<GameAction> actions{
        use(Stratagem::FireOverwatch, UnitId{2}),
        use(Stratagem::HeroicIntervention, UnitId{2}, StratagemMode::LeapToDefend),
        use(Stratagem::HeroicIntervention, UnitId{2}, StratagemMode::IntoTheFray),
        use(Stratagem::Counteroffensive, UnitId{2}),
        action::Shoot{UnitId{2}, ShootingType::Snap, {}},
    };
    for (const GameAction& a : actions) {
        Json j;
        action::to_json(j, a);
        GameAction back;
        action::from_json(j, back);
        EXPECT_EQ(back, a) << j.dump();
    }
    Json fo;
    action::to_json(fo, actions[0]);
    EXPECT_FALSE(fo.contains("mode"));
    const std::vector<GameEvent> events{
        event::StratagemUsed{Side::B, Stratagem::HeroicIntervention, UnitId{2}, 2, 1, StratagemMode::IntoTheFray},
        event::StratagemUsed{Side::B, Stratagem::FireOverwatch, UnitId{2}, 1, 0},
        event::ChargeRolled{UnitId{2}, TwoD6{{6, 6}}, {UnitId{1}}, 6},
        event::ChargeRolled{UnitId{2}, TwoD6{{3, 4}}, {UnitId{1}}},
    };
    for (const GameEvent& e : events) {
        const Json j = e;
        EXPECT_EQ(j.get<GameEvent>(), e) << j.dump();
    }
    EXPECT_FALSE(Json(events[1]).contains("mode"));
    EXPECT_FALSE(Json(events[3]).contains("capped"));
    EXPECT_EQ(Json(events[2]).at("capped"), 6);
    for (const StratagemMode m : {StratagemMode::LeapToDefend, StratagemMode::IntoTheFray}) {
        EXPECT_EQ(stratagem_mode_from_key(stratagem_mode_key(m)), m);
    }
    EXPECT_EQ(stratagem_mode_from_key("Sideways"), std::nullopt);
    EXPECT_EQ(stratagem_use_cost(Stratagem::HeroicIntervention, StratagemMode::IntoTheFray), 2);
    EXPECT_EQ(stratagem_use_cost(Stratagem::HeroicIntervention, StratagemMode::LeapToDefend), 1);
    EXPECT_EQ(stratagem_use_cost(Stratagem::Counteroffensive, std::nullopt), 2);
    EXPECT_EQ(stratagem_use_cost(Stratagem::FireOverwatch, std::nullopt), 1);
}

// ==== Random play that uses the new stratagems (D-004 style): every answer valid, the invariants hold ====

namespace {

// Random play rarely charges, so a third start: Side A's Charge step after the Intercessors' charge, with a lone
// Termagant 5" west of them (heroic_window's state before the step ends).
GameState melee_start() {
    GameState gs = wh40k::test::after_successful_charge();
    const Position w = westmost(gs.units[kMarines]);
    add_one_model_unit(gs, kGants, Position{w.x - kMarineRadius - kGantRadius - in(5.0f), w.y, 0.0f});
    gs.pending = compute_decision(gs);
    return gs;
}

}  // namespace

TEST(D097_RandomPlay, GamesThatUseTheNewStratagemsStayValidAndReplay) {
    const std::vector<GameState> scenarios{wh40k::test::load_v1_scenario(), wh40k::test::load_rosters_scenario(),
                                           melee_start()};
    std::map<Stratagem, int> used;
    int snap_shots = 0;
    for (std::size_t n = 0; n < scenarios.size(); ++n) {
        for (std::uint64_t seed = 1; seed <= 6; ++seed) {
            SCOPED_TRACE("scenario " + std::to_string(n) + " seed " + std::to_string(seed));
            GameState initial = scenarios[n];
            initial.dice = DiceState{seed, 0};
            initial.command_points = {6, 6};  // enough CP for every stratagem to come up
            if (initial.flow.stage != Stage::NotStarted) initial.pending = compute_decision(initial);
            Match match(initial, RollMode::Paced);
            wh40k::test::TestRng rng(seed * 31 + n);
            for (int i = 0; i < 900 && match.state().pending; ++i) {
                const GameState& gs = match.state();
                ASSERT_EQ(window_pending(gs), gs.flow.window.has_value());
                ASSERT_EQ(gs.pending, compute_decision(gs));
                ASSERT_GE(gs.command_points[0], 0);
                ASSERT_GE(gs.command_points[1], 0);
                if (gs.flow.snap_shooter) {
                    ASSERT_TRUE(std::holds_alternative<decision::SnapShoot>(gs.pending->kind));
                    ++snap_shots;
                }
                if (window_pending(gs)) {
                    for (const GameAction& a : legal_actions(gs, gs.pending->side)) {
                        EXPECT_TRUE(validate_action(gs, gs.pending->side, a).ok()) << action_name(a);
                    }
                }
                const std::size_t before = match.events().size();
                const GameAction a = wh40k::test::random_action(gs, rng);
                ASSERT_TRUE(match.submit(decider(gs), a).ok()) << action_name(a);
                for (std::size_t k = before; k < match.events().size(); ++k) {
                    if (const auto* s = std::get_if<event::StratagemUsed>(&match.events()[k])) ++used[s->stratagem];
                }
            }
            // The game as a format-4 replay plays the same.
            const Replay replay = match.to_replay("random play with the M96 stratagems");
            EXPECT_EQ(replay.format, kReplayFormatReactionsRecorded);
            const ReplayResult again = run_replay(Json(replay).get<Replay>(), repo_root());
            ASSERT_TRUE(again.ok()) << "step " << *again.failed_step << ": " << again.reasons.front();
            EXPECT_EQ(again.final_state, match.state());
            EXPECT_EQ(again.events, match.events());
        }
    }
    EXPECT_GT(used[Stratagem::FireOverwatch], 2);
    EXPECT_GT(used[Stratagem::HeroicIntervention], 0);
    EXPECT_GT(used[Stratagem::Counteroffensive], 0);
    EXPECT_GT(snap_shots, 2);
}

TEST(D097_Closure, GamesWithTheNewWindowsDeclinedPlayTheSamePacedAndInline) {
    // As D095_Closure's random games, counting the windows that follow no roll: each declined, so the paced game is the
    // inline one.
    const std::vector<GameState> scenarios{wh40k::test::load_v1_scenario(), wh40k::test::load_rosters_scenario(),
                                           melee_start()};
    std::map<WindowTiming, int> windows;
    for (std::size_t n = 0; n < scenarios.size(); ++n) {
        for (std::uint64_t seed = 1; seed <= 3; ++seed) {
            SCOPED_TRACE("scenario " + std::to_string(n) + " seed " + std::to_string(seed));
            GameState initial = scenarios[n];
            initial.dice = DiceState{seed, 0};
            initial.command_points = {4, 4};
            if (initial.flow.stage != Stage::NotStarted) initial.pending = compute_decision(initial);
            Match paced_match(initial, RollMode::Paced);
            wh40k::test::TestRng rng(seed * 11 + n);
            for (int i = 0; i < 500 && paced_match.state().pending; ++i) {
                const GameState& gs = paced_match.state();
                if (window_pending(gs)) ++windows[std::get<decision::UseStratagems>(gs.pending->kind).timing];
                const GameAction a = window_pending(gs) ? GameAction{action::DeclineStratagems{}}
                                                        : wh40k::test::random_action(gs, rng);
                ASSERT_TRUE(paced_match.submit(decider(gs), a).ok());
            }
            if (roll_pending(paced_match.state()) || window_pending(paced_match.state())) continue;
            Match at_once(initial);
            for (const ReplayStep& s : paced_match.steps()) {
                if (std::holds_alternative<action::RollDice>(s.action) ||
                    std::holds_alternative<action::DeclineStratagems>(s.action)) {
                    continue;
                }
                ASSERT_TRUE(at_once.submit(s.side, s.action).ok());
            }
            EXPECT_EQ(at_once.state(), paced_match.state());
            EXPECT_EQ(at_once.events(), paced_match.events());
        }
    }
    EXPECT_GT(windows[WindowTiming::EndOfPhase], 5);
    EXPECT_GT(windows[WindowTiming::AfterAttacks], 0);
}

// ==== The M96 replay: tests/replays/m96_reactions.json (format 4: each new stratagem used, each window answered) ====

namespace {

const std::filesystem::path kM96Replay = std::filesystem::path(WH40K_REPLAY_DIR) / "m96_reactions.json";
constexpr std::uint64_t kM96Seed = 1;  // picked by D097_Replay.DISABLED_SearchM96Seed

// The milestone's replay, played through a paced match. `ok` says whether the seed shows what the replay is about: a
// snap shot's 6 hits; the Intercessors' charge reaches; Into the Fray's roll above 6 is capped at 6 and still reaches;
// Counteroffensive's unit fights between the two Fights First units. `problem`: the first refused step.
Replay m96_replay(std::uint64_t seed, bool& ok, std::string& problem) {
    ok = false;
    Replay r;
    r.format = kReplayFormatReactionsRecorded;
    r.description =
        "M96 (D-097): the opponent-turn stratagems, each a step of its own. The V1 scenario with the Termagants 4.9\" "
        "north of the Intercessors and a one-Termagant unit 4\" west of where the Intercessors' charge ends; Side B "
        "starts with 4 CP. Round 1, Side A: the Intercessors remain stationary; at the end of the Movement phase Side B "
        "uses Fire Overwatch (15.08) and the Termagants snap shoot at the Intercessors (15.09: only a 6 hits, and it is "
        "not a critical hit). The Intercessors charge the Termagants (11.02, 11.04: 3.9\" north). At the end of the "
        "Charge phase Side B uses Heroic Intervention, Into the Fray (15.11, 2CP): the lone Termagant's charge roll "
        "above 6 becomes 6 and it charges the Intercessors (3\" east). Fight step: the Intercessors fight first; Side B "
        "uses Counteroffensive (15.12, 2CP) on the Termagants, which fight next, before the lone Termagant (Fights First "
        "from its charge). Every other window declined, casualties in unit order. Stops at the Consolidate step.";
    GameState init = wh40k::test::load_v1_scenario();
    const float gap = mm_to_inches(*unit_distance(init.units[kMarines], init.units[kGants]));
    for (Model& m : init.units[kGants].models) m.position.y -= in(gap - 4.9f);
    const Position w = westmost(init.units[kMarines]);
    const UnitId lone = add_one_model_unit(init, kGants, Position{w.x - kMarineRadius - kGantRadius - in(4.0f),
                                                                  w.y + in(3.9f), 0.0f});
    init.command_points = {0, 4};
    init.dice = DiceState{seed, 0};
    r.initial.state = init;
    const UnitId marines = init.units[kMarines].id;
    const UnitId gants = init.units[kGants].id;
    Match match(init, RollMode::Paced);
    bool failed = false;
    auto act = [&](const GameAction& a) {
        if (failed) return;
        const ValidationResult v = match.submit(decider(match.state()), a);
        if (!v.ok()) {
            failed = true;
            problem = v.reasons.front();
        }
    };
    // The builder's answers: Fire Overwatch on the Termagants, Into the Fray on the lone Termagant, Counteroffensive on
    // the Termagants, each when offered; every other window declined; rolls made; neutral answers otherwise.
    auto answer = [&](const GameState& gs) -> GameAction {
        if (window_pending(gs)) {
            for (const GameAction& a : legal_actions(gs, gs.pending->side)) {
                const auto* u = std::get_if<action::UseStratagem>(&a);
                if (u == nullptr) continue;
                if ((u->stratagem == Stratagem::FireOverwatch && u->unit == gants) ||
                    (u->stratagem == Stratagem::HeroicIntervention && u->unit == lone &&
                     u->mode == StratagemMode::IntoTheFray) ||
                    (u->stratagem == Stratagem::Counteroffensive && u->unit == gants)) {
                    return a;
                }
            }
            return action::DeclineStratagems{};
        }
        if (std::holds_alternative<decision::SnapShoot>(gs.pending->kind)) {
            action::Shoot s{gants, ShootingType::Snap, {}};
            for (const Model& m : gs.find_unit(gants)->models) s.shots.push_back(ShotDeclaration{m.id, 0, 0, marines});
            return s;
        }
        return wh40k::test::neutral_action(gs);
    };
    auto play_until = [&](auto done) {
        for (int i = 0; i < 400 && !failed && match.state().pending && !done(match.state()); ++i) act(answer(match.state()));
    };
    const auto waiting = [](const GameState& gs) { return roll_pending(gs) || window_pending(gs); };
    // Side A's Charge step: the Intercessors charge the Termagants, 3.9" north.
    play_until([&](const GameState& gs) { return gs.flow.stage == Stage::Charge && !waiting(gs); });
    if (failed || match.state().flow.stage != Stage::Charge) return r;
    act(action::DeclareCharge{marines});
    play_until([&](const GameState& gs) { return !waiting(gs); });
    if (failed || match.state().pending != PendingDecision{Side::A, decision::SelectChargeTargets{marines}}) return r;
    act(action::StartChargeMove{marines, {gants}});
    for (const Model& m : match.state().find_unit(marines)->models) {
        const Position s = *move_start_position(match.state(), m.id);
        act(action::ProposeModelMove{m.id, {Position{s.x, s.y + in(3.9f), 0.0f}}});
    }
    act(action::CommitUnitMove{});
    // Side A ends the step; Into the Fray: the lone Termagant charges the Intercessors, 3" east.
    play_until([&](const GameState& gs) {
        return gs.pending == PendingDecision{Side::B, decision::SelectChargeTargets{lone}} || gs.flow.stage == Stage::PileIn;
    });
    if (failed || match.state().pending != PendingDecision{Side::B, decision::SelectChargeTargets{lone}}) return r;
    act(action::StartChargeMove{lone, {marines}});
    const Model& lm = match.state().find_unit(lone)->models.at(0);
    const Position ls = *move_start_position(match.state(), lm.id);
    act(action::ProposeModelMove{lm.id, {Position{ls.x + in(3.0f), ls.y, 0.0f}}});
    act(action::CommitUnitMove{});
    // The Fight step, to the Consolidate step.
    play_until([&](const GameState& gs) { return gs.flow.stage == Stage::Consolidate; });
    r.steps = match.steps();
    r.expect = replay_expectation(match.state());
    if (failed || match.state().flow.stage != Stage::Consolidate) return r;
    // What the seed shows.
    std::vector<Stratagem> used;
    bool snap_six_hits = false;
    bool capped_and_reaches = false;
    bool charged = false;
    bool lone_charged = false;
    std::vector<UnitId> fought;
    for (const GameEvent& e : match.events()) {
        if (const auto* s = std::get_if<event::StratagemUsed>(&e)) used.push_back(s->stratagem);
        if (const auto* h = std::get_if<event::HitRollsMade>(&e); h != nullptr && used.size() == 1 && !charged) {
            snap_six_hits = snap_six_hits || h->rolls.hits() > 0;
        }
        if (const auto* c = std::get_if<event::ChargeRolled>(&e); c != nullptr && c->unit == lone) {
            capped_and_reaches = c->capped == 6 && !c->eligible_targets.empty();
        }
        if (const auto* c = std::get_if<event::ChargeResolved>(&e)) {
            if (c->unit == marines) charged = c->charge_move_made;
            if (c->unit == lone) lone_charged = c->charge_move_made;
        }
        if (const auto* f = std::get_if<event::UnitSelectedToFight>(&e)) fought.push_back(f->unit);
    }
    ok = used == std::vector<Stratagem>{Stratagem::FireOverwatch, Stratagem::HeroicIntervention,
                                        Stratagem::Counteroffensive} &&
         snap_six_hits && charged && capped_and_reaches && lone_charged &&
         fought == std::vector<UnitId>{marines, gants, lone};
    return r;
}

}  // namespace

TEST(D097_Replay, TheReactionsReplayUsesEachNewStratagemAsAStep) {
    const Replay saved = load_replay(kM96Replay);
    EXPECT_EQ(saved.format, kReplayFormatReactionsRecorded);
    const ReplayResult run = run_replay(saved, repo_root());
    ASSERT_TRUE(run.ok()) << "step " << *run.failed_step << ": " << run.reasons.front();
    EXPECT_EQ(replay_expectation(run.final_state), saved.expect);
    std::vector<std::pair<Side, Stratagem>> used;
    for (const GameEvent& e : run.events) {
        if (const auto* s = std::get_if<event::StratagemUsed>(&e)) {
            used.emplace_back(s->side, s->stratagem);
            EXPECT_GE(s->cp_left, 0);
        }
    }
    EXPECT_EQ(used, (std::vector<std::pair<Side, Stratagem>>{{Side::B, Stratagem::FireOverwatch},
                                                              {Side::B, Stratagem::HeroicIntervention},
                                                              {Side::B, Stratagem::Counteroffensive}}));
    EXPECT_EQ(run.final_state.command_points[1], 0);
    // The snap shot: every hit an unmodified 6, none critical.
    bool snap = false;
    for (const GameEvent& e : run.events) {
        if (const auto* s = std::get_if<event::ShootingStarted>(&e)) snap = s->type == ShootingType::Snap;
        if (const auto* h = std::get_if<event::HitRollsMade>(&e); h != nullptr && snap) {
            for (const HitRoll& roll : h->rolls.rolls) {
                EXPECT_EQ(roll.outcome, roll.unmodified == 6 ? HitOutcome::Hit : HitOutcome::Fail);
            }
            EXPECT_GT(h->rolls.hits(), 0);
            snap = false;
        }
    }
    // The file is what the builder plays now.
    bool ok = false;
    std::string problem;
    const Replay again = m96_replay(kM96Seed, ok, problem);
    EXPECT_TRUE(problem.empty()) << problem;
    EXPECT_TRUE(ok);
    EXPECT_EQ(Json(again).dump(2), Json(saved).dump(2));
}

TEST(D097_Replay, DISABLED_SearchM96Seed) {
    for (std::uint64_t seed = 1; seed <= 1000; ++seed) {
        bool ok = false;
        std::string problem;
        (void)m96_replay(seed, ok, problem);
        if (!problem.empty()) std::printf("seed %llu: %s\n", static_cast<unsigned long long>(seed), problem.c_str());
        if (ok) {
            std::printf("M96 seed %llu\n", static_cast<unsigned long long>(seed));
            return;
        }
    }
    ADD_FAILURE() << "no seed in 1..1000";
}

// Writes tests/replays/m96_reactions.json. Run on purpose only:
//   wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D097_Replay.DISABLED_RegenerateM96Replay
TEST(D097_Replay, DISABLED_RegenerateM96Replay) {
    bool ok = false;
    std::string problem;
    const Replay r = m96_replay(kM96Seed, ok, problem);
    ASSERT_TRUE(problem.empty()) << problem;
    ASSERT_TRUE(ok);
    const ReplayResult run = run_replay(r, repo_root());
    ASSERT_TRUE(run.ok()) << "step " << *run.failed_step << ": " << run.reasons.front();
    save_replay(r, kM96Replay);
}

// ==== D-097: the regeneration check (format 3 -> 4 adds only declines) ====

// M96 regenerated every replay as format 4. Each new file must be exactly its format-3 file (git show HEAD:) converted
// by with_roll_steps: the same description, initial state and expect block, and the same steps with only the answers
// to D-097's windows added (each declined). Run on purpose only, with the old files in $WH40K_FORMAT3_DIR:
//   WH40K_FORMAT3_DIR=<dir> wh40k_tests --gtest_also_run_disabled_tests --gtest_filter=D097_Replays.DISABLED_CompareWithFormat3Files
TEST(D097_Replays, DISABLED_CompareWithFormat3Files) {
    const char* dir = std::getenv("WH40K_FORMAT3_DIR");
    if (dir == nullptr) GTEST_SKIP() << "set WH40K_FORMAT3_DIR";
    int compared = 0;
    for (const auto& entry : std::filesystem::directory_iterator(WH40K_REPLAY_DIR)) {
        if (entry.path().extension() != ".json") continue;
        const std::filesystem::path old_path = std::filesystem::path(dir) / entry.path().filename();
        if (!std::filesystem::exists(old_path)) continue;
        SCOPED_TRACE(entry.path().filename().string());
        const Replay old_file = load_replay(old_path);
        const Replay new_file = load_replay(entry.path());
        EXPECT_EQ(old_file.format, kReplayFormatWindowsRecorded);
        EXPECT_EQ(new_file.format, kReplayFormatReactionsRecorded);
        EXPECT_EQ(Json(with_roll_steps(old_file, repo_root())).dump(2), Json(new_file).dump(2));
        ++compared;
    }
    std::printf("compared %d files\n", compared);
    EXPECT_GT(compared, 0);
}
