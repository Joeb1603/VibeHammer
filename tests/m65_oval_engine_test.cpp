// M65 (D-074 part 2): oval bases in the engine. The Tyrannofex (a 92 x 120 mm oval) measured, moved, turned and placed
// by the engine: 01.04 distances in its queries, 03.01 turns and lines, 09.04, the 11.04 / 12.03 / 12.08 "closer" and
// "engaged" tests with a turn, the modulo-180 facing, the round base's refusal, JSON and MoveReach.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/data/load_error.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/engagement.hpp"
#include "core/geometry/oval.hpp"
#include "core/geometry/pose.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/charge.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/queries.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::at;
using wh40k::test::contains;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::expect_valid;
using wh40k::test::in;
using wh40k::test::rejection;
using wh40k::test::starts_with;
namespace tf = wh40k::test::tyrannofex;

namespace {

constexpr float kHalfLength = 60.0f;  // the Tyrannofex's semi-axes (D-070: 120 x 92 mm)
constexpr float kHalfWidth = 46.0f;
constexpr float kLegionaryR = 16.0f;  // 32 mm
const Base kRound32{BaseShape::Round, 32.0f, 0.0f};

UnitId tyr_unit(const GameState& gs) { return gs.units[tf::kTyrannofex].id; }
ModelId tyr_model(const GameState& gs) { return gs.units[tf::kTyrannofex].models[0].id; }
const Model& tyr(const GameState& gs) { return gs.units[tf::kTyrannofex].models[0]; }
Unit& legionaries(GameState& gs) { return gs.units[tf::kLegionaries]; }

bool any_reason(const std::vector<std::string>& reasons, const std::string& part) {
    return std::any_of(reasons.begin(), reasons.end(), [&](const std::string& r) { return r.find(part) != std::string::npos; });
}

// Neutral answers until `done(gs)`; with `all_ones`, every die the actions roll is a 1 (every attack misses).
template <typename Done>
void drive(GameState& gs, Done done, bool all_ones = false, int guard = 800) {
    for (int i = 0; i < guard && gs.pending && !done(gs); ++i) {
        const GameAction a = wh40k::test::neutral_action(gs);
        if (all_ones) {
            ScriptedDice ones(std::vector<int>(400, 1));
            apply_action(gs, decider(gs), a, ones);
        } else {
            apply_action(gs, decider(gs), a);
        }
    }
}

bool pending_is(const GameState& gs, Side side, const DecisionKind& kind) {
    return gs.pending && gs.pending->side == side && gs.pending->kind.index() == kind.index();
}

// The fixture, adjusted by `setup` before the game starts, at Side B's first Move Units decision (B first).
template <typename Setup>
GameState at_b_movement(Setup setup) {
    GameState gs = tf::fixture(Side::B);
    setup(gs);
    start_game(gs);
    drive(gs, [](const GameState& g) { return pending_is(g, Side::B, decision::MoveUnits{}); });
    return gs;
}

GameState at_b_movement() {
    return at_b_movement([](GameState&) {});
}

// Side B's Charge decision (every unit remained stationary, no shooting), the fixture adjusted by `setup`.
template <typename Setup>
GameState at_b_charge(Setup setup) {
    GameState gs = at_b_movement(setup);
    drive(gs, [](const GameState& g) { return pending_is(g, Side::B, decision::Charge{}); });
    return gs;
}

// The Tyrannofex declares a charge at the Legionaries with an 8" roll and selects them as its target.
void charge_the_legionaries(GameState& gs) {
    ScriptedDice dice{4, 4};
    apply_action(gs, Side::B, action::DeclareCharge{tyr_unit(gs)}, dice);
    ASSERT_TRUE(pending_is(gs, Side::B, decision::SelectChargeTargets{}));
    apply_action(gs, Side::B, action::StartChargeMove{tyr_unit(gs), {legionaries(gs).id}});
    ASSERT_TRUE(gs.flow.move && gs.flow.move->type == MoveType::Charge);
}

// The Legionaries' rank keeps its place; one model (or none) is moved by the tests below.
Position offset(const Position& p, float dx_mm, float dy_mm) { return Position{p.x + dx_mm, p.y + dy_mm, 0.0f}; }

}  // namespace

// ---- 01.04: oval distances in the engine's queries ----

TEST(R01_04_OvalEngine, EngagementIsMeasuredToTheClosestPartOfTheOval) {
    // 03.04 / 01.04: 120 mm centre to centre is 120 - 60 - 16 = 44 mm (1.73") along the Tyrannofex's length, engaged,
    // and 120 - 46 - 16 = 58 mm (2.28") along its width, not. A round base 92 mm across would be 58 mm away both ways.
    GameState gs = tf::fixture();
    Unit& leg = legionaries(gs);
    wh40k::test::monsters::place_rank(leg, 10.0f, 4.0f);  // out of the way
    Model& t = tf::the_tyrannofex(gs);
    leg.models[0].position = offset(t.position, 0.0f, -120.0f);
    EXPECT_NEAR(*unit_distance(leg, gs.units[tf::kTyrannofex]), 44.0f, 1e-3f);
    EXPECT_TRUE(is_engaged(gs, leg));
    EXPECT_EQ(engaged_enemy_units(gs, gs.units[tf::kTyrannofex]), std::vector<UnitId>{leg.id});
    leg.models[0].position = offset(t.position, 120.0f, 0.0f);
    EXPECT_NEAR(*unit_distance(leg, gs.units[tf::kTyrannofex]), 58.0f, 1e-3f);
    EXPECT_FALSE(is_engaged(gs, leg));
    EXPECT_FALSE(is_engaged(gs, gs.units[tf::kTyrannofex]));
    t.facing_deg = 90.0f;  // turned: its length lies along x
    EXPECT_NEAR(*unit_distance(leg, gs.units[tf::kTyrannofex]), 44.0f, 1e-3f);
    EXPECT_TRUE(is_engaged(gs, gs.units[tf::kTyrannofex]));
    // The per-model queries measure the facing too (12.03, D-022).
    EXPECT_EQ(enemy_units_engaged_with_model(gs, leg, pose_of(leg.models[0])), std::vector<UnitId>{tyr_unit(gs)});
    EXPECT_NEAR(*model_distance_to_unit(pose_of(leg.models[0]), gs.units[tf::kTyrannofex]), 44.0f, 1e-3f);
    EXPECT_FALSE(in_base_contact_with_enemy(gs, leg, pose_of(leg.models[0])));
    leg.models[0].position = offset(t.position, kHalfLength + kLegionaryR, 0.0f);  // touching its end
    EXPECT_TRUE(in_base_contact_with_enemy(gs, leg, pose_of(leg.models[0])));
    EXPECT_TRUE(in_base_contact_with_enemy(gs, gs.units[tf::kTyrannofex], pose_of(t)));
}

TEST(R01_04_OvalEngine, RangeAndHalfRangeAreMeasuredToTheClosestPartOfTheOval) {
    // 04.02 / 24.30: the Havoc Champion's plasma gun (24", [RAPID FIRE 1]) at the Tyrannofex, measured to the closest
    // part of its base: to its end while it faces the champion, 14 mm further across its width when turned.
    GameState gs = tf::fixture();
    wh40k::test::monsters::place_rank(gs.units[tf::kScreamer], 5.0f, 55.0f);  // out of the way
    const Model& champion = gs.units[tf::kHavocs].models[0];
    ASSERT_EQ(champion.weapons[0].name, "Plasma gun");
    const WeaponProfile& standard = champion.weapons[0].profiles[0];
    Model& t = tf::the_tyrannofex(gs);
    const float champion_r = 20.0f;
    t.position = offset(champion.position, 0.0f, in(11.9f) + kHalfLength + champion_r);
    EXPECT_TRUE(select_weapon_target(gs, champion.id, 0, 0, tyr_unit(gs), std::nullopt).target_within_half_range);
    t.facing_deg = 90.0f;  // 11.9" + 14 mm: beyond half range
    EXPECT_FALSE(select_weapon_target(gs, champion.id, 0, 0, tyr_unit(gs), std::nullopt).target_within_half_range);
    t.facing_deg = 0.0f;
    t.position = offset(champion.position, 0.0f, in(23.9f) + kHalfLength + champion_r);
    EXPECT_TRUE(target_within_range(champion, standard, gs.units[tf::kTyrannofex]));
    t.facing_deg = 90.0f;
    EXPECT_FALSE(target_within_range(champion, standard, gs.units[tf::kTyrannofex]));
}

TEST(R01_04_OvalEngine, TheClosestTargetIsMeasuredToTheOval) {
    // D-010 / D-022: a Legionary 1" from the Tyrannofex's end and 1.2" from a Tyrant Guard. The Guard's centre is nearer
    // (71.5 mm against 101.4 mm), but the Tyrannofex is the closest unit; with a round 92 mm base it would be 1.55"
    // away and the Guard would be. So a move closer to the Guard only is not closer to the closest target.
    GameState gs = tf::fixture();
    Unit& leg = legionaries(gs);
    wh40k::test::monsters::place_rank(leg, 10.0f, 4.0f);
    const Position p = at(20.0f, 20.0f);
    leg.models[0].position = p;
    tf::the_tyrannofex(gs).position = offset(p, 0.0f, -(kLegionaryR + in(1.0f) + kHalfLength));
    Unit& guard = gs.units[tf::kGuard];
    wh40k::test::monsters::place_rank(guard, 30.0f, 50.0f);
    guard.models[0].position = offset(p, 0.0f, kLegionaryR + in(1.2f) + 25.0f);
    const std::vector<UnitId> targets{tyr_unit(gs), guard.id};
    const BasePose start = pose_of(leg.models[0]);
    EXPECT_FALSE(ends_closer_to_closest_target(gs, start, BasePose{offset(p, 0.0f, 5.0f), kRound32, 0.0f}, targets));
    EXPECT_TRUE(ends_closer_to_closest_target(gs, start, BasePose{offset(p, 0.0f, -5.0f), kRound32, 0.0f}, targets));
}

TEST(R03_03_OvalCoherency, CoherencyIsMeasuredToTheClosestPartOfTheOvals) {
    // 03.03: two Tyrannofexes 2" apart end to end are in coherency; side by side at the same centre distance they are
    // 2" + 28 mm apart and are not.
    GameState gs = tf::fixture();
    Unit& u = gs.units[tf::kTyrannofex];
    u.models.push_back(u.models[0]);
    u.models[1].id = ModelId{9001};
    const Position c = at(22.0f, 40.0f);
    u.models[0].position = c;
    u.models[1].position = offset(c, 0.0f, 2.0f * kHalfLength + in(2.0f));
    EXPECT_TRUE(unit_coherency(u).ok());
    u.models[1].position = offset(c, 2.0f * kHalfLength + in(2.0f), 0.0f);
    EXPECT_FALSE(unit_coherency(u).ok());
    u.models[0].facing_deg = u.models[1].facing_deg = 90.0f;
    EXPECT_TRUE(unit_coherency(u).ok());
}

// ---- 06.01: the oval blocks lines of sight by its footprint ----

TEST(R06_01_OvalEngine, TheTyrannofexBlocksLineOfSightByItsFacing) {
    // The Havoc Champion (40 mm) looks at a Tyrant Guard (50 mm) 300 mm north. The Tyrannofex stands half way, 30 mm
    // east of the line: end-on (facing 0) it leaves a 6 mm gap west of it, broadside (facing 90) it closes it.
    GameState gs = tf::fixture();
    wh40k::test::monsters::place_rank(gs.units[tf::kScreamer], 5.0f, 55.0f);
    wh40k::test::monsters::place_rank(legionaries(gs), 10.0f, 4.0f);
    const Model& champion = gs.units[tf::kHavocs].models[0];
    Unit& guard = gs.units[tf::kGuard];
    guard.models.resize(1);
    guard.models[0].position = offset(champion.position, 0.0f, 300.0f);
    Model& t = tf::the_tyrannofex(gs);
    t.position = offset(champion.position, 30.0f, 150.0f);
    t.facing_deg = 0.0f;
    EXPECT_TRUE(unit_visible_to_model(gs, champion, guard));
    t.facing_deg = 90.0f;
    EXPECT_FALSE(unit_visible_to_model(gs, champion, guard));
}

// ---- 03.01: turns and straight lines ----

TEST(R03_01_OvalMoves, ATurnThenAStraightLineIsOneProposal) {
    GameState gs = at_b_movement();
    apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::Normal, std::nullopt});
    const Position start = tyr(gs).position;
    const action::ProposeModelMove move{tyr_model(gs), {offset(start, 0.0f, -in(3.0f))}, {45.0f}};
    expect_valid(gs, Side::B, move);
    const std::vector<GameEvent> events = apply_action(gs, Side::B, move);
    EXPECT_EQ(tyr(gs).position, offset(start, 0.0f, -in(3.0f)));
    EXPECT_EQ(tyr(gs).facing_deg, 45.0f);
    const auto proposed = events_of<event::ModelMoveProposed>(events);
    ASSERT_EQ(proposed.size(), 1u);
    EXPECT_EQ(proposed[0].facings, (std::vector<float>{0.0f, 45.0f}));
    EXPECT_NEAR(proposed[0].distance_mm, in(3.0f), 1e-3f);  // 03.01: the turn adds nothing
    ASSERT_EQ(gs.flow.move->paths.size(), 1u);
    EXPECT_EQ(gs.flow.move->paths[0].facings, (std::vector<float>{0.0f, 45.0f}));
    apply_action(gs, Side::B, action::CommitUnitMove{});
    EXPECT_FALSE(gs.flow.move.has_value());
    EXPECT_EQ(tyr(gs).facing_deg, 45.0f);
    EXPECT_FALSE(gs.units[tf::kTyrannofex].turn_flags.moved_over_3in);  // 24.16: 3.00" moved, the turn not counted
}

TEST(R03_01_OvalMoves, TheDistanceMovedExcludesTurns) {
    // 03.01 Rotating a Model: "rotating a model does not count towards the distance it has moved". M 9".
    GameState gs = at_b_movement();
    apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::Normal, std::nullopt});
    const Position start = tyr(gs).position;
    const action::ProposeModelMove nine{tyr_model(gs), {start, start, start, offset(start, 0.0f, in(9.0f))},
                                        {60.0f, 120.0f, 170.0f, 170.0f}};
    expect_valid(gs, Side::B, nine);
    const action::ProposeModelMove too_far{tyr_model(gs), {start, offset(start, 0.0f, in(9.1f))}, {90.0f, 90.0f}};
    const auto reasons = rejection(gs, Side::B, too_far);
    EXPECT_TRUE(contains(reasons, "09.05: model " + std::to_string(tyr_model(gs).v) +
                                      " would move 9.10\" but the maximum distance is 9.00\""));
    const auto events = apply_action(gs, Side::B, nine);
    const auto proposed = events_of<event::ModelMoveProposed>(events);
    ASSERT_EQ(proposed.size(), 1u);
    EXPECT_NEAR(proposed[0].distance_mm, in(9.0f), 1e-3f);
    EXPECT_EQ(tyr(gs).facing_deg, -10.0f);  // 170 is the footprint at -10 (D-074 part 2)
}

TEST(R03_01_OvalMoves, ATurnIsBlockedByAnEnemyBase) {
    // A Legionary 7 mm from the Tyrannofex's flank: turning far enough to put its length across (more than about 43
    // degrees) passes through it (03.01: its base cannot be moved through enemy models). The unit is engaged, so it
    // falls back; Ordered Retreat may not pass through enemy models, Desperate Escape may (09.07).
    const auto setup = [](GameState& gs) {
        Unit& leg = legionaries(gs);
        wh40k::test::monsters::place_rank(leg, 10.0f, 4.0f);
        leg.models[0].position = offset(tf::the_tyrannofex(gs).position, kHalfWidth + kLegionaryR + 7.0f, 0.0f);
    };
    GameState gs = at_b_movement(setup);
    GameState desperate = gs;
    apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::FallBack, FallBackMode::OrderedRetreat});
    const Position start = tyr(gs).position;
    const ModelId leg0 = legionaries(gs).models[0].id;
    const auto reasons = rejection(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {start}, {90.0f}});
    EXPECT_TRUE(contains(reasons, "03.01: model " + std::to_string(tyr_model(gs).v) +
                                      "'s base cannot be turned through enemy model " + std::to_string(leg0.v) +
                                      " (before segment 1)"));
    expect_valid(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {start}, {10.0f}});  // a small turn clears it
    // Moving away first, then turning, clears it too.
    expect_valid(gs, Side::B,
                 action::ProposeModelMove{tyr_model(gs), {offset(start, -in(2.0f), 0.0f), offset(start, -in(2.0f), 0.0f)},
                                          {0.0f, 90.0f}});
    // The same turn after moving next to it again, at the second point: refused before segment 2.
    const auto later = rejection(gs, Side::B,
                                 action::ProposeModelMove{tyr_model(gs), {offset(start, -in(2.0f), 0.0f), start, start},
                                                          {0.0f, 0.0f, 90.0f}});
    EXPECT_TRUE(any_reason(later, "cannot be turned through enemy model " + std::to_string(leg0.v) + " (before segment 3)"));

    apply_action(desperate, Side::B,
                 action::StartMove{tyr_unit(desperate), MoveType::FallBack, FallBackMode::DesperateEscape});
    if (desperate.pending && std::holds_alternative<decision::OrderCasualties>(desperate.pending->kind)) {
        wh40k::test::answer_casualties(desperate);  // 09.07 hazard rolls (06.03)
    }
    ASSERT_TRUE(pending_is(desperate, Side::B, decision::MoveModels{}));
    expect_valid(desperate, Side::B, action::ProposeModelMove{tyr_model(desperate), {start, offset(start, -in(3.0f), 0.0f)},
                                                              {90.0f, 90.0f}});
}

TEST(R03_01_OvalMoves, ATurnIsBlockedByTheBattlefieldEdge) {
    // 1 mm from the west edge across its width (46 mm): turning its length (60 mm) across crosses the edge.
    const auto setup = [](GameState& gs) { tf::the_tyrannofex(gs).position = Position{kHalfWidth + 1.0f, in(34.0f), 0.0f}; };
    GameState gs = at_b_movement(setup);
    apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::Normal, std::nullopt});
    const Position start = tyr(gs).position;
    const auto reasons = rejection(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {start}, {90.0f}});
    EXPECT_TRUE(contains(reasons, "03.01: model " + std::to_string(tyr_model(gs).v) +
                                      "'s base cannot cross the edge of the battlefield while it turns (before segment 1)"));
    // 20 mm east first: the turn there leaves 7 mm.
    const Position away = offset(start, 20.0f, 0.0f);
    expect_valid(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {away, away}, {0.0f, 90.0f}});
}

TEST(R03_01_OvalMoves, A17_01TurnPassesEnemyInfantryButNotAnEnemyMonster) {
    // 17.01 (D-073): in a Normal move a MONSTER may be moved through enemy models that are not MONSTER/VEHICLE models,
    // turns included. The Tyrannofex moves 6" north to P, turns there from 0 to 90 (its length sweeps through the
    // north-west, where an enemy model stands just clear of it at both ends of the turn), then moves 3" east.
    const Position p = at(22.0f, 40.0f);
    const float diagonal = std::sqrt(0.5f);
    // The oval's radius towards the north-west is 51.6 mm at facings 0 and 90 and 60 mm at 45.
    const auto place = [&](float r) { return offset(p, -(51.63f + r + 3.0f) * diagonal, (51.63f + r + 3.0f) * diagonal); };
    const auto check_geometry = [&](const Position& centre, const Base& base) {
        const BasePose other{centre, base, 0.0f};
        ASSERT_GT(signed_base_distance(BasePose{p, tf::kOval, 0.0f}, other), 1.0f);
        ASSERT_GT(signed_base_distance(BasePose{p, tf::kOval, 90.0f}, other), 1.0f);
        ASSERT_LT(signed_base_distance(BasePose{p, tf::kOval, 45.0f}, other), -1.0f);
    };
    const Position q = offset(p, in(3.0f), 0.0f);
    {
        // An enemy Legionary (INFANTRY) there: the turn passes through it.
        const auto setup = [&](GameState& gs) { legionaries(gs).models[0].position = place(kLegionaryR); };
        GameState gs = at_b_movement(setup);
        check_geometry(legionaries(gs).models[0].position, kRound32);
        apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::Normal, std::nullopt});
        const action::ProposeModelMove move{tyr_model(gs), {p, p, q}, {0.0f, 90.0f, 90.0f}};
        expect_valid(gs, Side::B, move);
        apply_action(gs, Side::B, move);
        EXPECT_TRUE(move_end_problems(gs).empty());
        apply_action(gs, Side::B, action::CommitUnitMove{});
        EXPECT_EQ(tyr(gs).position, q);
        EXPECT_EQ(tyr(gs).facing_deg, 90.0f);
    }
    {
        // An enemy MONSTER there (the Screamer-killer, 90 mm, on Side A for the test): the turn is refused.
        const auto setup = [&](GameState& gs) {
            Unit& s = gs.units[tf::kScreamer];
            s.side = Side::A;
            s.models[0].position = place(45.0f);
        };
        GameState gs = at_b_movement(setup);
        check_geometry(gs.units[tf::kScreamer].models[0].position, gs.units[tf::kScreamer].models[0].base);
        apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::Normal, std::nullopt});
        const auto reasons = rejection(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {p, p, q}, {0.0f, 90.0f, 90.0f}});
        EXPECT_TRUE(contains(reasons, "03.01: model " + std::to_string(tyr_model(gs).v) +
                                          "'s base cannot be turned through enemy model " +
                                          std::to_string(gs.units[tf::kScreamer].models[0].id.v) + " (before segment 2)"));
    }
}

TEST(R03_01_OvalMoves, ATurnMayPassAFriendlyModelButNotEndOnIt) {
    // D-027 / 03.01: a Tyrant Guard touching the Tyrannofex's flank: turning its length across would end on it.
    const auto setup = [](GameState& gs) {
        Unit& guard = gs.units[tf::kGuard];
        guard.models[0].position = offset(tf::the_tyrannofex(gs).position, kHalfWidth + 25.0f, 0.0f);
        guard.models[1].position = offset(guard.models[0].position, 50.0f, 0.0f);
        guard.models[2].position = offset(guard.models[0].position, 100.0f, 0.0f);
    };
    GameState gs = at_b_movement(setup);
    apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::Normal, std::nullopt});
    const Position start = tyr(gs).position;
    const ModelId guard0 = gs.units[tf::kGuard].models[0].id;
    const auto reasons = rejection(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {start}, {90.0f}});
    EXPECT_TRUE(contains(reasons, "03.01: model " + std::to_string(tyr_model(gs).v) + " cannot end its move on model " +
                                      std::to_string(guard0.v)));
    // Turned through it and on 3" west, it ends clear: friendly models may be passed (03.01).
    expect_valid(gs, Side::B,
                 action::ProposeModelMove{tyr_model(gs), {start, offset(start, -in(3.0f), 0.0f)}, {90.0f, 90.0f}});
}

TEST(R03_01_OvalMoves, ResettingOrRevertingTheMoveRestoresTheFacing) {
    // 03.01 Ending a Move: the models return "to their positions at the start of that move", facings included.
    GameState gs = at_b_movement();
    apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::Normal, std::nullopt});
    const Position start = tyr(gs).position;
    apply_action(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {offset(start, in(2.0f), 0.0f)}, {60.0f}});
    EXPECT_EQ(tyr(gs).facing_deg, 60.0f);
    apply_action(gs, Side::B, action::ResetUnitMove{});
    EXPECT_EQ(tyr(gs).position, start);
    EXPECT_EQ(tyr(gs).facing_deg, 0.0f);
    // A move whose end is engaged (09.05 After Moving) is reverted at the commit, facing and all.
    apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::Normal, std::nullopt});
    apply_action(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {offset(start, 0.0f, -in(3.5f))}, {30.0f}});
    ASSERT_FALSE(move_end_problems(gs).empty());
    const auto events = apply_action(gs, Side::B, action::CommitUnitMove{});
    EXPECT_EQ(events_of<event::UnitMoveReverted>(events).size(), 1u);
    EXPECT_EQ(tyr(gs).position, start);
    EXPECT_EQ(tyr(gs).facing_deg, 0.0f);
}

TEST(R03_01_OvalMoves, ProposingAgainReplacesTheTurns) {
    GameState gs = at_b_movement();
    apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::Normal, std::nullopt});
    const Position start = tyr(gs).position;
    const Position to = offset(start, in(2.0f), 0.0f);
    apply_action(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {to}, {45.0f}});
    const auto events = apply_action(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {to}, {}});
    EXPECT_EQ(tyr(gs).facing_deg, 0.0f);  // no facings: no turn, it keeps its start facing
    EXPECT_TRUE(gs.flow.move->paths[0].facings.empty());
    EXPECT_TRUE(events_of<event::ModelMoveProposed>(events)[0].facings.empty());
}

TEST(R03_01_OvalMoves, TheLongerWayRoundSweepsEveryTurn) {
    // The Fall-back set-up above (a Legionary 7 mm from the flank). 170 is the footprint at -10, reached by a 10-degree
    // turn the shorter way, which is clear. Going round through 60 and 120 at the same point sweeps the length across
    // the Legionary: refused (03.01).
    const auto setup = [](GameState& gs) {
        Unit& leg = legionaries(gs);
        wh40k::test::monsters::place_rank(leg, 10.0f, 4.0f);
        leg.models[0].position = offset(tf::the_tyrannofex(gs).position, kHalfWidth + kLegionaryR + 7.0f, 0.0f);
    };
    GameState gs = at_b_movement(setup);
    apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::FallBack, FallBackMode::OrderedRetreat});
    const Position s = tyr(gs).position;
    expect_valid(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {s}, {170.0f}});
    const auto reasons = rejection(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {s, s, s}, {60.0f, 120.0f, 170.0f}});
    EXPECT_TRUE(any_reason(reasons, "cannot be turned through enemy model"));
    apply_action(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {s}, {170.0f}});
    EXPECT_EQ(tyr(gs).facing_deg, -10.0f);
}

// ---- 09.04 Remain Stationary ----

TEST(R09_04_RemainStationary, NoModelIsMovedOrRotated) {
    // 09.04: "No models are moved (either in straight lines or rotated)."
    GameState gs = at_b_movement();
    const std::vector<GameEvent> events =
        apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::RemainStationary, std::nullopt});
    EXPECT_EQ(events_of<event::UnitRemainedStationary>(events).size(), 1u);
    EXPECT_FALSE(gs.flow.move.has_value());
    for (const GameAction& a : legal_actions(gs, Side::B)) {
        EXPECT_FALSE(std::holds_alternative<action::ProposeModelMove>(a));
    }
    const auto reasons = rejection(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {tyr(gs).position}, {45.0f}});
    EXPECT_TRUE(starts_with(reasons[0], "03.01: ProposeModelMove is not possible now")) << reasons[0];
    EXPECT_EQ(tyr(gs).facing_deg, 0.0f);
    EXPECT_TRUE(gs.units[tf::kTyrannofex].phase_flags.selected_to_move);
}

// ---- 11.04: "closer" and "engaged" with a turn ----

namespace {

// The Tyrannofex at (22, 31) facing `facing`, 2.4" (broadside) or more north of the Legionaries, charging them.
GameState charge_from(float y_in, float facing) {
    GameState gs = at_b_charge([&](GameState& g) {
        tf::the_tyrannofex(g).position = at(22.0f, y_in);
        tf::the_tyrannofex(g).facing_deg = facing;
    });
    charge_the_legionaries(gs);
    return gs;
}

float tyr_gap(const GameState& gs, const Position& at_pos, float facing) {
    return *model_distance_to_unit(BasePose{at_pos, tyr(gs).base, facing}, gs.units[tf::kLegionaries]);
}

}  // namespace

TEST(R11_04_OvalCharge, ATurnAloneIsNotCloser) {
    // Broadside (facing 90), turning its end towards the Legionaries brings its base about 14 mm closer, but a rotation
    // does not count as moving closer (D-074 part 2, the agreed reading extended to 11.04): refused.
    GameState gs = charge_from(31.0f, 90.0f);
    const Position s = tyr(gs).position;
    ASSERT_LT(tyr_gap(gs, s, 0.0f), tyr_gap(gs, s, 90.0f) - 10.0f);
    const auto reasons = rejection(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {s}, {0.0f}});
    EXPECT_TRUE(contains(reasons, "11.04: model " + std::to_string(tyr_model(gs).v) +
                                      " must end its move closer to one or more charge targets"));
}

TEST(R11_04_OvalCharge, TheStraightLinesMustBringItCloser) {
    // From broadside: turned to face them and moved 5 mm south it is closer than the start at either facing; moved
    // 0.005 mm (less than EPS) it is 14 mm closer than its broadside start but not closer than the start turned.
    GameState gs = charge_from(31.0f, 90.0f);
    const Position s = tyr(gs).position;
    expect_valid(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {offset(s, 0.0f, -5.0f)}, {0.0f}});
    const auto reasons = rejection(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {offset(s, 0.0f, -0.005f)}, {0.0f}});
    EXPECT_TRUE(any_reason(reasons, "must end its move closer to one or more charge targets"));
    // The query on its own: closer at the end facing only is not enough.
    const BasePose start = pose_of(tyr(gs));
    EXPECT_FALSE(ends_closer_to_a_charge_target(gs, start, BasePose{offset(s, 0.0f, -0.005f), tf::kOval, 0.0f},
                                                std::vector<UnitId>{legionaries(gs).id}));
    EXPECT_TRUE(ends_closer_to_a_charge_target(gs, start, BasePose{offset(s, 0.0f, -5.0f), tf::kOval, 0.0f},
                                               std::vector<UnitId>{legionaries(gs).id}));
}

TEST(R11_04_OvalCharge, ATurnThatSwingsTheNearEndAwayMustBeMadeUp) {
    // Facing them (facing 0), turning broadside moves the near part of its base back about 14 mm: 10 mm south is not
    // closer than the start, 20 mm is.
    GameState gs = charge_from(31.5f, 0.0f);
    const Position s = tyr(gs).position;
    const float before = tyr_gap(gs, s, 0.0f);
    ASSERT_GT(tyr_gap(gs, offset(s, 0.0f, -10.0f), 90.0f), before);
    ASSERT_LT(tyr_gap(gs, offset(s, 0.0f, -20.0f), 90.0f), before - 1.0f);
    EXPECT_TRUE(any_reason(rejection(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {offset(s, 0.0f, -10.0f)}, {90.0f}}),
                           "must end its move closer to one or more charge targets"));
    expect_valid(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {offset(s, 0.0f, -20.0f)}, {90.0f}});
}

TEST(R11_04_OvalCharge, EngagedAfterMovingIsJudgedAtTheEndFacing) {
    // 11.04 After Moving: "Your unit must be engaged with all of the charge targets". 0.3" south from broadside: turned
    // to face them it ends about 1.6" away, engaged; without the turn about 2.1", not engaged (the move is reverted).
    GameState gs = charge_from(31.0f, 90.0f);
    const Position s = tyr(gs).position;
    GameState unturned = gs;
    apply_action(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {offset(s, 0.0f, -in(0.3f))}, {0.0f}});
    EXPECT_TRUE(move_end_problems(gs).empty());
    apply_action(gs, Side::B, action::CommitUnitMove{});
    EXPECT_TRUE(gs.units[tf::kTyrannofex].turn_flags.made_charge_move);
    EXPECT_EQ(tyr(gs).facing_deg, 0.0f);

    apply_action(unturned, Side::B, action::ProposeModelMove{tyr_model(unturned), {offset(s, 0.0f, -in(0.3f))}, {}});
    const auto problems = move_end_problems(unturned);
    EXPECT_TRUE(contains(problems, "11.04: unit " + std::to_string(tyr_unit(unturned).v) +
                                       " must end its charge move engaged with charge target unit " +
                                       std::to_string(legionaries(unturned).id.v)));
}

// ---- 12.03 / 12.08: "closer", base contact and "still engaged" with a turn ----

namespace {

// After a charge that ends the Tyrannofex 1" from the Legionaries facing `end_facing`: Side B's Pile In decision.
GameState at_b_pile_in(float end_facing) {
    GameState gs = charge_from(31.0f, 90.0f);
    const Position s = tyr(gs).position;
    // Straight south until its base is 1" from the closest Legionary (at its end facing).
    float lo = 0.0f;
    float hi = in(3.0f);
    for (int i = 0; i < 60; ++i) {
        const float mid = 0.5f * (lo + hi);
        (tyr_gap(gs, offset(s, 0.0f, -mid), end_facing) > in(1.0f) ? lo : hi) = mid;
    }
    apply_action(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {offset(s, 0.0f, -lo)}, {end_facing}});
    apply_action(gs, Side::B, action::CommitUnitMove{});
    EXPECT_TRUE(gs.units[tf::kTyrannofex].turn_flags.made_charge_move);
    drive(gs, [](const GameState& g) { return pending_is(g, Side::B, decision::PileIn{}); });
    return gs;
}

}  // namespace

TEST(R12_03_OvalPileIn, ATurnAloneIsNotCloser) {
    // 12.03 While Moving: "Each model that is moved must end its move closer to the closest pile-in target". 1" away
    // broadside, turning its end to them is 14 mm closer, but a rotation does not count (D-074 part 2, agreed).
    GameState gs = at_b_pile_in(90.0f);
    apply_action(gs, Side::B, action::StartPileIn{tyr_unit(gs), {legionaries(gs).id}});
    const Position s = tyr(gs).position;
    ASSERT_LT(tyr_gap(gs, s, 0.0f), tyr_gap(gs, s, 90.0f) - 10.0f);
    const auto reasons = rejection(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {s}, {0.0f}});
    EXPECT_TRUE(contains(reasons, "12.03: model " + std::to_string(tyr_model(gs).v) +
                                      " must end its move closer to the closest pile-in target"));
    // Turned and moved 5 mm closer by its centre, it is closer.
    expect_valid(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {offset(s, 0.0f, -5.0f)}, {0.0f}});
}

TEST(R12_03_OvalPileIn, AModelInBaseContactCannotTurn) {
    // 12.03 While Moving: "Models in base-contact with one or more enemy models cannot be moved": moving includes
    // rotating (03.01), so its turn is refused too. Its end touches a Legionary at the start of the game.
    const auto setup = [](GameState& gs) {
        const Model& l = legionaries(gs).models[4];
        tf::the_tyrannofex(gs).position = offset(l.position, 0.0f, kHalfLength + kLegionaryR);
    };
    GameState gs = at_b_movement(setup);
    ASSERT_TRUE(in_base_contact_with_enemy(gs, gs.units[tf::kTyrannofex], pose_of(tyr(gs))));
    drive(gs, [](const GameState& g) { return pending_is(g, Side::B, decision::PileIn{}); });
    apply_action(gs, Side::B, action::StartPileIn{tyr_unit(gs), {legionaries(gs).id}});
    const auto reasons = rejection(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {tyr(gs).position}, {20.0f}});
    EXPECT_TRUE(contains(reasons, "12.03: model " + std::to_string(tyr_model(gs).v) +
                                      " is in base contact with an enemy model and cannot be moved"));
    const auto reach = move_reach(gs, tyr_model(gs), 20.0f);
    ASSERT_TRUE(reach.has_value());
    EXPECT_TRUE(reach->immobile);
}

TEST(R12_03_OvalPileIn, StillEngagedIsJudgedFromTheStartFacing) {
    // 12.03 After Moving: "Each model that started this move engaged with an enemy unit must still be engaged with
    // that enemy unit". A Havoc 1.9" from the Tyrannofex's end: engaged at its start facing (0); turned broadside the
    // Havoc is 1.9" + 14 mm away (not engaged). Started broadside, it was never engaged with the Havoc.
    GameState gs = tf::fixture();
    Unit& havocs = gs.units[tf::kHavocs];
    Model& t = tf::the_tyrannofex(gs);
    havocs.models[0].position = offset(t.position, 0.0f, -(kHalfLength + in(1.9f) + 20.0f));
    const ModelStart facing_them{t.id, t.position, 0.0f};
    t.facing_deg = 90.0f;
    const auto problems = still_engaged_problems(gs, gs.units[tf::kTyrannofex], std::span<const ModelStart>(&facing_them, 1), "12.03");
    EXPECT_TRUE(contains(problems, "12.03: model " + std::to_string(t.id.v) + " started the move engaged with unit " +
                                       std::to_string(havocs.id.v) + " and must still be engaged with it"));
    const ModelStart broadside{t.id, t.position, 90.0f};
    EXPECT_TRUE(still_engaged_problems(gs, gs.units[tf::kTyrannofex], std::span<const ModelStart>(&broadside, 1), "12.03").empty());
}

TEST(R12_08_OvalConsolidation, ATurnAloneIsNotCloserInEitherMode) {
    // 12.08 While Moving (Ongoing and Engaging): "Each model that is moved must end its move closer to the closest
    // selected enemy unit". The Tyrannofex charged and fought (every die a 1: nobody is hurt), broadside 1" away.
    GameState gs = at_b_pile_in(90.0f);
    drive(gs, [](const GameState& g) { return pending_is(g, Side::B, decision::Consolidate{}); }, true);
    ASSERT_TRUE(pending_is(gs, Side::B, decision::Consolidate{}));
    GameState engaging = gs;
    // Ongoing Consolidation (engaged).
    ASSERT_EQ(consolidation_mode(gs, gs.units[tf::kTyrannofex]), ConsolidationMode::Ongoing);
    apply_action(gs, Side::B, action::StartConsolidation{tyr_unit(gs), {legionaries(gs).id}});
    const Position s = tyr(gs).position;
    EXPECT_TRUE(any_reason(rejection(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {s}, {0.0f}}),
                           "12.08: model " + std::to_string(tyr_model(gs).v) +
                               " must end its move closer to the closest selected enemy unit"));
    expect_valid(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {offset(s, 0.0f, -5.0f)}, {0.0f}});
    // Engaging Consolidation: the Legionaries 1.5" further south (unengaged, within 3").
    for (Model& m : legionaries(engaging).models) m.position.y -= in(1.5f);
    ASSERT_EQ(consolidation_mode(engaging, engaging.units[tf::kTyrannofex]), ConsolidationMode::Engaging);
    apply_action(engaging, Side::B, action::StartConsolidation{tyr_unit(engaging), {legionaries(engaging).id}});
    EXPECT_TRUE(any_reason(rejection(engaging, Side::B, action::ProposeModelMove{tyr_model(engaging), {s}, {0.0f}}),
                           "must end its move closer to the closest selected enemy unit"));
    expect_valid(engaging, Side::B, action::ProposeModelMove{tyr_model(engaging), {offset(s, 0.0f, -in(1.0f))}, {0.0f}});
}

// ---- D-074 part 2: facings ----

TEST(D074_Facings, OneFacingPerFootprintModulo180) {
    EXPECT_EQ(footprint_facing_deg(0.0f), 0.0f);
    EXPECT_EQ(footprint_facing_deg(90.0f), 90.0f);
    EXPECT_EQ(footprint_facing_deg(-90.0f), 90.0f);
    EXPECT_EQ(footprint_facing_deg(180.0f), 0.0f);
    EXPECT_EQ(footprint_facing_deg(-180.0f), 0.0f);
    EXPECT_EQ(footprint_facing_deg(170.0f), -10.0f);
    EXPECT_EQ(footprint_facing_deg(270.0f), 90.0f);
    EXPECT_EQ(footprint_facing_deg(-100.0f), 80.0f);
    EXPECT_EQ(footprint_facing_deg(45.5f), 45.5f);
    EXPECT_EQ(footprint_facing_deg(1e10f), -80.0f);  // 1e10 = 180 * 55555555 + 100
    EXPECT_FALSE(std::signbit(footprint_facing_deg(-0.0f)));
    EXPECT_FALSE(std::signbit(footprint_facing_deg(-180.0f)));
    EXPECT_EQ(footprint_facing_deg(std::numeric_limits<float>::quiet_NaN()), 0.0f);
    EXPECT_EQ(footprint_facing_deg(std::numeric_limits<float>::infinity()), 0.0f);
    // Exact: the result differs from the input by a multiple of 180 (in double), for every float tried.
    wh40k::test::TestRng rng(65);
    for (int i = 0; i < 20000; ++i) {
        const float f = static_cast<float>(static_cast<double>(rng.below(2000001)) / 1000.0 - 1000.0) *
                        (i % 7 == 0 ? 1.0f : 1.37f);
        const float out = footprint_facing_deg(f);
        ASSERT_GT(out, -90.0f) << f;
        ASSERT_LE(out, 90.0f) << f;
        const double k = (static_cast<double>(f) - static_cast<double>(out)) / 180.0;
        ASSERT_EQ(k, std::round(k)) << f;
    }
}

TEST(D074_Facings, TurnsGoTheShorterWayBetweenFootprints) {
    EXPECT_EQ(footprint_turn_deg(0.0f, 170.0f), -10.0);
    EXPECT_EQ(footprint_turn_deg(80.0f, -80.0f), 20.0);
    EXPECT_EQ(footprint_turn_deg(-80.0f, 80.0f), -20.0);
    EXPECT_EQ(footprint_turn_deg(0.0f, 90.0f), 90.0);
    EXPECT_EQ(footprint_turn_deg(90.0f, 0.0f), 90.0);  // a quarter turn is +90 (counter-clockwise), as a half turn is +180
    EXPECT_EQ(footprint_turn_deg(0.0f, -90.0f), 90.0);
    EXPECT_EQ(footprint_turn_deg(10.0f, 100.0f), 90.0);
    EXPECT_EQ(footprint_turn_deg(30.0f, 210.0f), 0.0);
    EXPECT_EQ(footprint_turn_deg(45.0f, 45.0f), 0.0);
}

TEST(D074_Facings, TheStateKeepsTheFootprintsFacing) {
    GameState gs = at_b_movement();
    apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::Normal, std::nullopt});
    const Position s = tyr(gs).position;
    for (const auto& [given, stored] : std::vector<std::pair<float, float>>{
             {170.0f, -10.0f}, {-90.0f, 90.0f}, {450.0f, 90.0f}, {-135.0f, 45.0f}, {180.0f, 0.0f}}) {
        apply_action(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {s}, {given}});
        EXPECT_EQ(tyr(gs).facing_deg, stored) << given;
        // A proposal that turns the model nowhere keeps no facings (180 is its start footprint).
        EXPECT_EQ(gs.flow.move->paths[0].facings.empty(), stored == 0.0f) << given;
    }
}

TEST(D074_Facings, ARoundBaseRefusesAFacing) {
    GameState gs = tf::at_movement(Side::A);
    ASSERT_TRUE(pending_is(gs, Side::A, decision::MoveUnits{}));
    apply_action(gs, Side::A, action::StartMove{legionaries(gs).id, MoveType::Normal, std::nullopt});
    const Model& l = legionaries(gs).models[0];
    const Position to = offset(l.position, 0.0f, -in(1.0f));
    const auto reasons = rejection(gs, Side::A, action::ProposeModelMove{l.id, {to}, {30.0f}});
    ASSERT_EQ(reasons.size(), 1u);
    EXPECT_EQ(reasons[0], "D-074: model " + std::to_string(l.id.v) +
                              " has a round base, which has no facing; its facings must be 0");
    expect_valid(gs, Side::A, action::ProposeModelMove{l.id, {to}, {0.0f}});  // zeros change nothing
    const auto events = apply_action(gs, Side::A, action::ProposeModelMove{l.id, {to}, {0.0f}});
    EXPECT_TRUE(events_of<event::ModelMoveProposed>(events)[0].facings.empty());
    EXPECT_TRUE(gs.flow.move->paths[0].facings.empty());
    EXPECT_EQ(legionaries(gs).models[0].facing_deg, 0.0f);
}

TEST(D074_Facings, FacingsMustBeOnePerVertexAndFinite) {
    GameState gs = at_b_movement();
    apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::Normal, std::nullopt});
    const Position s = tyr(gs).position;
    const std::string who = "D-074: model " + std::to_string(tyr_model(gs).v);
    auto reasons = rejection(gs, Side::B, action::ProposeModelMove{tyr_model(gs), {s}, {10.0f, 20.0f}});
    EXPECT_EQ(reasons, std::vector<std::string>{who + "'s move gives 2 facings for 1 path points; give one facing per "
                                                      "path point, or none"});
    reasons = rejection(gs, Side::B,
                        action::ProposeModelMove{tyr_model(gs), {s, s}, {10.0f, std::numeric_limits<float>::quiet_NaN()}});
    EXPECT_EQ(reasons, std::vector<std::string>{who + "'s facing at path point 2 is not a finite number of degrees"});
    reasons = rejection(gs, Side::B,
                        action::ProposeModelMove{tyr_model(gs), {s}, {std::numeric_limits<float>::infinity()}});
    EXPECT_EQ(reasons, std::vector<std::string>{who + "'s facing at path point 1 is not a finite number of degrees"});
}

// ---- D-074 part 2: MoveReach ----

TEST(D074_MoveReach, AnOvalMoverGetsShapesForTheFacingBeingDragged) {
    GameState gs = at_b_movement();
    apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::Normal, std::nullopt});
    const auto reach = move_reach(gs, tyr_model(gs));
    ASSERT_TRUE(reach.has_value());
    std::size_t others = 0;
    for (const Unit& u : gs.units) others += u.models.size();
    --others;
    EXPECT_TRUE(reach->forbidden.empty());  // every pair has the oval in it
    EXPECT_EQ(reach->forbidden_shapes.size(), others);
    EXPECT_TRUE(reach->blocking.empty());
    EXPECT_TRUE(reach->blocking_shapes.empty());  // 17.01: no enemy MONSTER/VEHICLE to block a MONSTER's Normal move
    EXPECT_EQ(reach->facing_deg, 0.0f);
    EXPECT_FALSE(reach->turn_blocked);
    const Rect board{0.0f, 0.0f, gs.battlefield.width_mm, gs.battlefield.depth_mm};
    EXPECT_EQ(reach->centre_bounds, (Rect{kHalfWidth, kHalfLength, board.max_x - kHalfWidth, board.max_y - kHalfLength}));
    for (const ReachShape& s : reach->forbidden_shapes) {
        EXPECT_EQ(s.mover_base, tf::kOval);
        EXPECT_EQ(s.mover_facing_deg, 0.0f);
        const bool enemy = find_unit_of_model(gs, s.model)->side == Side::A;
        EXPECT_EQ(s.engagement, enemy);  // 09.05 After Moving: unengaged
        EXPECT_FLOAT_EQ(s.threshold_mm, enemy ? kEngagementRangeMm + EPS_MM : 0.0f);
    }
    // At facing 90 the bounds and every shape are for the turned base.
    const auto turned = move_reach(gs, tyr_model(gs), 90.0f);
    ASSERT_TRUE(turned.has_value());
    EXPECT_EQ(turned->facing_deg, 90.0f);
    EXPECT_EQ(turned->centre_bounds, (Rect{kHalfLength, kHalfWidth, board.max_x - kHalfLength, board.max_y - kHalfWidth}));
    for (const ReachShape& s : turned->forbidden_shapes) EXPECT_EQ(s.mover_facing_deg, 90.0f);
    EXPECT_EQ(move_reach(gs, tyr_model(gs), 270.0f), turned);  // the same footprint
}

TEST(D074_MoveReach, AShapeHoldsTheCentresTheEngineMeasures) {
    // A Legionary south of the Tyrannofex: its engagement shape holds a centre exactly when the Tyrannofex there, at
    // the reach's facing, would be within 2" of it (03.04), and the end-on and broadside shapes differ by the oval.
    GameState gs = at_b_movement();
    apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::Normal, std::nullopt});
    const Model& l = legionaries(gs).models[4];
    for (const float facing : {0.0f, 90.0f, 30.0f}) {
        const auto reach = move_reach(gs, tyr_model(gs), facing);
        ASSERT_TRUE(reach.has_value());
        const auto it = std::find_if(reach->forbidden_shapes.begin(), reach->forbidden_shapes.end(),
                                     [&](const ReachShape& s) { return s.model == l.id; });
        ASSERT_NE(it, reach->forbidden_shapes.end());
        for (int k = 0; k <= 400; ++k) {
            const Position c = offset(l.position, 0.0f, 50.0f + static_cast<float>(k) * 0.5f);
            const float d = base_distance(BasePose{c, tf::kOval, facing}, pose_of(l));
            if (std::abs(d - kEngagementRangeMm) < 0.05f) continue;  // the boundary itself, within rounding
            EXPECT_EQ(reach_shape_contains(*it, c), within(d, kEngagementRangeMm)) << facing << " " << k;
        }
        // The bounds hold the shape.
        wh40k::test::TestRng rng(7);
        for (int k = 0; k < 500; ++k) {
            const float angle = static_cast<float>(rng.below(3600)) * 0.1f * 3.14159265f / 180.0f;
            const float r = static_cast<float>(rng.below(3000)) * 0.1f;
            const Position c = offset(it->obstacle.position, r * std::cos(angle), r * std::sin(angle));
            if (r < reach_shape_inner_radius_mm(*it) - 0.01f) EXPECT_TRUE(reach_shape_contains(*it, c));
            if (r > reach_shape_outer_radius_mm(*it) + 0.01f) EXPECT_FALSE(reach_shape_contains(*it, c));
        }
    }
}

TEST(D074_MoveReach, ATurnBlockedWhereTheModelIsBlocksTheReachAtThatFacing) {
    // The Fall-back set-up (a Legionary 7 mm from the flank): turning far enough to put its length across is blocked,
    // a small turn is not. Ordered Retreat: every enemy model blocks, as shapes.
    const auto setup = [](GameState& gs) {
        Unit& leg = legionaries(gs);
        wh40k::test::monsters::place_rank(leg, 10.0f, 4.0f);
        leg.models[0].position = offset(tf::the_tyrannofex(gs).position, kHalfWidth + kLegionaryR + 7.0f, 0.0f);
    };
    GameState gs = at_b_movement(setup);
    apply_action(gs, Side::B, action::StartMove{tyr_unit(gs), MoveType::FallBack, FallBackMode::OrderedRetreat});
    EXPECT_FALSE(move_reach(gs, tyr_model(gs))->turn_blocked);
    EXPECT_FALSE(move_reach(gs, tyr_model(gs), 10.0f)->turn_blocked);
    const auto blocked = move_reach(gs, tyr_model(gs), 90.0f);
    EXPECT_TRUE(blocked->turn_blocked);
    std::size_t enemies = 0;
    for (const Unit& u : gs.units) {
        if (u.side == Side::A) enemies += u.models.size();
    }
    EXPECT_EQ(blocked->blocking_shapes.size(), enemies);
    for (const ReachShape& s : blocked->blocking_shapes) EXPECT_FLOAT_EQ(s.threshold_mm, -EPS_MM);
}

TEST(D074_MoveReach, ARoundMoverGetsAShapeOnlyForTheOval) {
    GameState gs = tf::at_movement(Side::A);
    apply_action(gs, Side::A, action::StartMove{legionaries(gs).id, MoveType::Normal, std::nullopt});
    const auto reach = move_reach(gs, legionaries(gs).models[0].id);
    ASSERT_TRUE(reach.has_value());
    ASSERT_EQ(reach->forbidden_shapes.size(), 1u);
    EXPECT_EQ(reach->forbidden_shapes[0].model, tyr_model(gs));
    EXPECT_TRUE(reach->forbidden_shapes[0].engagement);
    EXPECT_EQ(reach->forbidden_shapes[0].mover_base, kRound32);
    ASSERT_EQ(reach->blocking_shapes.size(), 1u);
    EXPECT_EQ(reach->blocking_shapes[0].model, tyr_model(gs));
    std::size_t others = 0;
    for (const Unit& u : gs.units) others += u.models.size();
    EXPECT_EQ(reach->forbidden.size(), others - 2);  // every other model but the Tyrannofex: circles, as before
    EXPECT_EQ(reach->facing_deg, 0.0f);
    EXPECT_EQ(move_reach(gs, legionaries(gs).models[0].id, 45.0f), reach);  // a round base has no facing
}

TEST(D074_MoveReach, TheChargeReachMeasuresCloserWithTheTurnRule) {
    // From broadside, the "closer" threshold at facing 0 is the start distance turned (the smaller), at 90 the start's.
    GameState gs = charge_from(31.0f, 90.0f);
    const Position s = tyr(gs).position;
    const float before90 = tyr_gap(gs, s, 90.0f);
    const float before0 = tyr_gap(gs, s, 0.0f);
    for (const auto& [facing, before] : std::vector<std::pair<float, float>>{{0.0f, before0}, {90.0f, before90}}) {
        const auto reach = move_reach(gs, tyr_model(gs), facing);
        ASSERT_TRUE(reach.has_value());
        ASSERT_EQ(reach->required_any.size(), 1u);
        ASSERT_EQ(reach->required_any_shapes.size(), 1u);
        EXPECT_TRUE(reach->required_any[0].empty());
        EXPECT_EQ(reach->required_any_shapes[0].size(), legionaries(gs).models.size());
        for (const ReachShape& sh : reach->required_any_shapes[0]) EXPECT_FLOAT_EQ(sh.threshold_mm, before - EPS_MM);
    }
}

TEST(D074_MoveReach, ARoundOnlyStateGivesTheReachAsBefore) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (!pending_is(gs, Side::A, decision::MoveUnits{})) apply_action(gs, decider(gs), legal_actions(gs, decider(gs)).front());
    apply_action(gs, Side::A, action::StartMove{gs.units[0].id, MoveType::Normal, std::nullopt});
    const auto reach = move_reach(gs, gs.units[0].models[0].id);
    ASSERT_TRUE(reach.has_value());
    EXPECT_EQ(reach->facing_deg, 0.0f);
    EXPECT_FALSE(reach->turn_blocked);
    EXPECT_TRUE(reach->forbidden_shapes.empty());
    EXPECT_TRUE(reach->required_any_shapes.empty());
    EXPECT_TRUE(reach->blocking_shapes.empty());
    EXPECT_EQ(reach->forbidden.size(), gs.units[0].models.size() - 1 + gs.units[1].models.size());
}

// ---- D-074 part 2: JSON ----

TEST(D074_Json, FacingsRoundTripAndAreOmittedWhenZero) {
    Model m;
    m.id = ModelId{7};
    m.base = tf::kOval;
    EXPECT_FALSE(Json(m).contains("facing_deg"));
    m.facing_deg = -10.0f;
    const Json j = m;
    EXPECT_EQ(j.at("facing_deg"), -10.0f);
    EXPECT_EQ(j.get<Model>(), m);

    EXPECT_FALSE(Json(ModelStart{ModelId{1}, Position{1.0f, 2.0f, 0.0f}, 0.0f}).contains("facing_deg"));
    const ModelStart s{ModelId{1}, Position{1.0f, 2.0f, 0.0f}, 45.0f};
    EXPECT_EQ(Json(s).get<ModelStart>(), s);
    EXPECT_FALSE(Json(ModelPath{ModelId{1}, {Position{}, Position{}}, {}}).contains("facings"));
    const ModelPath p{ModelId{1}, {Position{}, Position{3.0f, 0.0f, 0.0f}}, {0.0f, 90.0f}};
    EXPECT_EQ(Json(p).get<ModelPath>(), p);

    const GameAction none = action::ProposeModelMove{ModelId{3}, {Position{1.0f, 1.0f, 0.0f}}, {}};
    EXPECT_FALSE(Json(none).contains("facings"));
    EXPECT_EQ(Json(none).get<GameAction>(), none);
    const GameAction zero = action::ProposeModelMove{ModelId{3}, {Position{1.0f, 1.0f, 0.0f}}, {0.0f}};
    EXPECT_TRUE(Json(zero).contains("facings"));  // [0] turns an oval to 0; [] keeps its facing
    EXPECT_EQ(Json(zero).get<GameAction>(), zero);
    const GameEvent proposed = event::ModelMoveProposed{UnitId{2}, ModelId{3}, {Position{}, Position{}}, 0.0f, {0.0f, 45.0f}};
    EXPECT_EQ(Json(proposed).get<GameEvent>(), proposed);
    EXPECT_FALSE(Json(GameEvent{event::ModelMoveProposed{UnitId{2}, ModelId{3}, {Position{}}, 0.0f, {}}}).contains("facings"));
}

TEST(D074_Json, AFacingOutsideTheStoredFormOrOnARoundBaseIsRefused) {
    Model oval;
    oval.id = ModelId{7};
    oval.base = tf::kOval;
    Json j = oval;
    for (const Json bad : {Json(-90.0f), Json(120.0f), Json("north")}) {
        j["facing_deg"] = bad;
        EXPECT_THROW((void)j.get<Model>(), DataError) << bad.dump();
    }
    j["facing_deg"] = std::numeric_limits<double>::infinity();
    EXPECT_THROW((void)j.get<Model>(), DataError);
    Model round = oval;
    round.base = kRound32;
    Json r = round;
    r["facing_deg"] = 30.0f;
    try {
        (void)r.get<Model>();
        FAIL() << "expected DataError";
    } catch (const DataError& e) {
        EXPECT_NE(std::string(e.what()).find("D-074: model 7 has a round base, which has no facing"), std::string::npos)
            << e.what();
    }
    Json path = ModelPath{ModelId{1}, {Position{}, Position{}}, {0.0f, 90.0f}};
    path["facings"] = Json::array({0.0f});
    EXPECT_THROW((void)path.get<ModelPath>(), DataError);
    Json start = ModelStart{ModelId{1}, Position{}, 10.0f};
    start["facing_deg"] = 270.0f;
    EXPECT_THROW((void)start.get<ModelStart>(), DataError);
}

TEST(D074_Json, TheReplayExpectationRecordsAFacingOnlyWhenNotZero) {
    GameState gs = tf::fixture();
    const Json round_only = replay_expectation(gs);
    for (const Json& m : round_only.at("models")) EXPECT_FALSE(m.contains("facing_deg"));
    tf::the_tyrannofex(gs).facing_deg = 30.0f;
    int with = 0;
    const Json turned = replay_expectation(gs);
    for (const Json& m : turned.at("models")) {
        if (m.contains("facing_deg")) {
            ++with;
            EXPECT_EQ(m.at("facing_deg"), 30.0f);
        }
    }
    EXPECT_EQ(with, 1);
}

TEST(D074_Json, OldFilesLoadTheSame) {
    // Every saved replay reads and writes back identically; the ones from before M65 hold no facing anywhere, and
    // neither does a round-only state.
    int files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(WH40K_REPLAY_DIR))) {
        if (entry.path().extension() != ".json") continue;
        ++files;
        std::ifstream in_file(entry.path());
        std::stringstream text;
        text << in_file.rdbuf();
        const Json saved = Json::parse(text.str());
        EXPECT_EQ(Json(load_replay(entry.path())), saved) << entry.path().filename();
        // M72: m72_custom_battle_round2.json is a new file whose random play turns the Tyrannofexes (D-074).
        // M74: m74_hand_deployment.json is a new file that sets its Tyrannofex up at an angle (D-081).
        const std::string name = entry.path().filename().string();
        if (name.rfind("m65_", 0) != 0 && name.rfind("m72_", 0) != 0 && name.rfind("m74_", 0) != 0) {
            EXPECT_EQ(text.str().find("facing"), std::string::npos) << entry.path().filename();
        }
    }
    EXPECT_GE(files, 17);
    for (const GameState& gs : {wh40k::test::load_v1_scenario(), wh40k::test::load_rosters_fixture(),
                                load_scenario(wh40k::test::repo_root() / "data/scenarios/strike_44x60.json",
                                              wh40k::test::repo_root())}) {
        EXPECT_EQ(Json(gs).dump().find("facing"), std::string::npos);
    }
}
