// M90 (D-091): 12.08 Objective Consolidation and 24.18 [IGNORES COVER]. ScriptedDice and inch coordinates throughout;
// the objective fixtures are wh40k::test::objective_consolidation_state (support/engine_helpers.hpp).

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

#include "core/data/load_error.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/attacks.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/objectives.hpp"
#include "core/state/queries.hpp"
#include "core/geometry/terrain_shape.hpp"
#include "core/state/terrain.hpp"
#include "core/data/datasheet.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::at;
using wh40k::test::events_of;
using wh40k::test::in;
using wh40k::test::is_rule_cited;
using wh40k::test::objective_area;
using wh40k::test::objective_centre;
using wh40k::test::objective_consolidation_state;
using wh40k::test::rejection;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;
constexpr float kMarineRadius = 16.0f;
constexpr float kGantRadius = 14.25f;

UnitId marines(const GameState& gs) { return gs.units[kMarines].id; }
const Model& marine(const GameState& gs, std::size_t i) { return gs.units[kMarines].models[i]; }

bool has_substring(const std::vector<std::string>& reasons, const std::string& text) {
    for (const std::string& r : reasons) {
        if (r.find(text) != std::string::npos) return true;
    }
    return false;
}

// Side A starts its Objective Consolidation onto objective 0.
GameState started(float gap_in = 2.0f, float diameter_in = 4.0f) {
    GameState gs = objective_consolidation_state(gap_in, diameter_in);
    apply_action(gs, Side::A, action::StartConsolidation{marines(gs), {}, 0});
    EXPECT_TRUE(gs.flow.move.has_value());
    return gs;
}

ValidationResult propose(const GameState& gs, std::size_t i, Position to) {
    return validate_action(gs, Side::A, action::ProposeModelMove{marine(gs, i).id, {to}});
}

void move(GameState& gs, std::size_t i, Position to) {
    const GameAction a = action::ProposeModelMove{marine(gs, i).id, {to}};
    ASSERT_TRUE(validate_action(gs, Side::A, a).ok()) << validate_action(gs, Side::A, a).reasons.front();
    apply_action(gs, Side::A, a);
}

Position shifted(const Model& m, float dx_in, float dy_in) {
    return Position{m.position.x + in(dx_in), m.position.y + in(dy_in), 0.0f};
}

}  // namespace

// ==== Before Moving: the mode and the choice ====

TEST(R12_08_ObjectiveConsolidation, TheModesKeepTheirOrder) {
    // Engaged: Ongoing, even with an objective under the unit.
    GameState engaged = wh40k::test::consolidate_step_state();
    Mission mission;
    mission.objectives.push_back(objective_area("Centre", marine(engaged, 2).position, 6.0f));
    engaged.mission = mission;
    EXPECT_EQ(consolidation_mode(engaged, engaged.units[kMarines]), ConsolidationMode::Ongoing);

    // Unengaged with an enemy unit within 3": Engaging, though an objective is within 3" too.
    GameState close = objective_consolidation_state();
    close.units[kGants].models[0].position =
        Position{marine(close, 0).position.x, marine(close, 0).position.y + kMarineRadius + in(2.5f) + kGantRadius, 0};
    EXPECT_EQ(consolidation_mode(close, close.units[kMarines]), ConsolidationMode::Engaging);
    EXPECT_TRUE(consolidation_objective_options(close, close.units[kMarines]).empty());

    // Otherwise, an objective within 3": Objective, which selects that objective and no enemy unit.
    const GameState gs = objective_consolidation_state();
    EXPECT_EQ(consolidation_mode(gs, gs.units[kMarines]), ConsolidationMode::Objective);
    EXPECT_TRUE(consolidation_target_options(gs, gs.units[kMarines]).empty());
    EXPECT_EQ(consolidation_objective_options(gs, gs.units[kMarines]), (std::vector<std::size_t>{0}));
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Consolidate{}}));
}

TEST(R12_08_ObjectiveConsolidation, AnAreaAt2Point9InchesCountsAndAt3Point1InchesDoesNot) {
    // 14.01: measured to the closest part of the area (D-009, edge to edge from the base).
    const GameState near = objective_consolidation_state(2.9f);
    EXPECT_NEAR(mm_to_inches(objective_gap_mm(pose_of(marine(near, 2)), near.mission->objectives[0])), 2.9f, 1e-3f);
    EXPECT_EQ(consolidation_mode(near, near.units[kMarines]), ConsolidationMode::Objective);
    EXPECT_TRUE(eligible_to_consolidate(near, near.units[kMarines]));

    const GameState far = objective_consolidation_state(3.1f);
    EXPECT_FALSE(consolidation_mode(far, far.units[kMarines]).has_value());
    EXPECT_FALSE(eligible_to_consolidate(far, far.units[kMarines]));
    EXPECT_FALSE(far.pending && std::holds_alternative<decision::Consolidate>(far.pending->kind) &&
                 far.pending->side == Side::A);

    // A unit already within range of the area is within 3" of it.
    const GameState inside = objective_consolidation_state(-1.0f);
    EXPECT_EQ(consolidation_mode(inside, inside.units[kMarines]), ConsolidationMode::Objective);
}

TEST(R12_08_ObjectiveConsolidation, OnlyAnObjectiveWithin3InchesMayBeChosen) {
    GameState gs = objective_consolidation_state();
    gs.mission->objectives.push_back(objective_area("Far", at(20.0f, 14.0f), 2.0f));
    gs.pending = compute_decision(gs);
    const UnitId u = marines(gs);
    // One action per objective within 3", and the end of the step.
    EXPECT_EQ(legal_actions(gs, Side::A),
              (std::vector<GameAction>{action::EndConsolidations{}, action::StartConsolidation{u, {}, 0}}));
    const std::string unit = "unit " + std::to_string(u.v);
    EXPECT_EQ(rejection(gs, Side::A, action::StartConsolidation{u, {}, 1}).at(0),
              "12.08: Far is not an objective within 3\" of " + unit + " (Objective Consolidation)");
    EXPECT_EQ(rejection(gs, Side::A, action::StartConsolidation{u, {}, 7}).at(0),
              "12.08: objective 7 is not an objective within 3\" of " + unit + " (Objective Consolidation)");
    EXPECT_EQ(rejection(gs, Side::A, action::StartConsolidation{u, {}}).at(0),
              "12.08: select one of the objectives within 3\" of " + unit);
    EXPECT_EQ(rejection(gs, Side::A, action::StartConsolidation{u, {gs.units[kGants].id}, 0}).at(0),
              "12.08: " + unit + " has no enemy unit within 3\" (Objective Consolidation), so it selects an objective, "
                                 "not enemy units");
    for (const auto& reason : rejection(gs, Side::A, action::StartConsolidation{u, {}, 1})) {
        EXPECT_TRUE(is_rule_cited(reason)) << reason;
    }
}

TEST(R12_08_ObjectiveConsolidation, TheOtherModesRefuseAnObjective) {
    GameState gs = wh40k::test::consolidate_step_state();
    Mission mission;
    mission.objectives.push_back(objective_area("Centre", marine(gs, 2).position, 6.0f));
    gs.mission = mission;
    const UnitId u = marines(gs);
    EXPECT_EQ(rejection(gs, Side::A, action::StartConsolidation{u, {gs.units[kGants].id}, 0}).at(0),
              "12.08: unit " + std::to_string(u.v) +
                  " is engaged (Ongoing Consolidation), so it selects enemy units, not an objective");
    EXPECT_TRUE(validate_action(gs, Side::A, action::StartConsolidation{u, {gs.units[kGants].id}}).ok());
}

TEST(R12_08_ObjectiveConsolidation, StartingRecordsTheObjective) {
    GameState gs = objective_consolidation_state();
    const auto events = apply_action(gs, Side::A, action::StartConsolidation{marines(gs), {}, 0});
    const auto started = events_of<event::ConsolidationStarted>(events);
    ASSERT_EQ(started.size(), 1u);
    EXPECT_EQ(started[0], (event::ConsolidationStarted{marines(gs), ConsolidationMode::Objective, {}, 0, "Centre"}));
    ASSERT_TRUE(gs.flow.move.has_value());
    EXPECT_EQ(gs.flow.move->consolidation_mode, ConsolidationMode::Objective);
    EXPECT_EQ(gs.flow.move->objective, std::optional<std::size_t>{0});
    EXPECT_TRUE(gs.flow.move->targets.empty());
    EXPECT_EQ(move_objective(gs, *gs.flow.move), &gs.mission->objectives[0]);
}

// ==== While Moving ====

TEST(R12_08_ObjectiveConsolidation, AModelThatEndsFurtherFromTheAreaIsRefused) {
    const GameState gs = started(2.0f);
    const ValidationResult back = propose(gs, 0, shifted(marine(gs, 0), 0.0f, -1.0f));
    ASSERT_FALSE(back.ok());
    EXPECT_TRUE(has_substring(back.reasons, "12.08: model " + std::to_string(marine(gs, 0).id.v) +
                                                " must end its move within range of Centre or closer to it "
                                                "(Objective Consolidation)"));
    // Sideways along the row, the gap to the area grows too.
    EXPECT_FALSE(propose(gs, 0, shifted(marine(gs, 0), -1.0f, 0.0f)).ok());
    // Closer but not within range, and within range, are both allowed (TODO(if-possible): "within range if
    // possible" is left to the player).
    EXPECT_TRUE(propose(gs, 0, shifted(marine(gs, 0), 0.0f, 1.0f)).ok());
    EXPECT_TRUE(propose(gs, 2, shifted(marine(gs, 2), 0.0f, 2.1f)).ok());
}

TEST(R12_08_ObjectiveConsolidation, AModelWithinRangeThatLeavesTheAreaIsRefused) {
    const GameState gs = started(-0.5f);  // the middle Intercessor's base overlaps the area by 0.5"
    ASSERT_TRUE(model_within_objective(marine(gs, 2), gs.mission->objectives[0]));
    EXPECT_FALSE(propose(gs, 2, shifted(marine(gs, 2), 0.0f, -1.0f)).ok());  // out of the area
    EXPECT_TRUE(propose(gs, 2, shifted(marine(gs, 2), 0.0f, 1.0f)).ok());    // further in
    EXPECT_TRUE(propose(gs, 2, shifted(marine(gs, 2), 0.2f, 0.0f)).ok());    // still within range
}

TEST(R12_08_ObjectiveConsolidation, TheReachAreaIsWithinRangeOrCloser) {
    const GameState gs = started(2.0f);
    const auto reach = move_reach(gs, marine(gs, 0).id);
    ASSERT_TRUE(reach.has_value());
    ASSERT_EQ(reach->required_any.size(), 1u);
    EXPECT_TRUE(reach->required_any_shapes.empty());
    const ObjectiveArea& area = gs.mission->objectives[0];
    const float before = objective_gap_mm(pose_of(marine(gs, 0)), area);
    const float touching = kMarineRadius + area.diameter_mm / 2.0f;
    EXPECT_EQ(reach->required_any[0],
              (std::vector<ReachCircle>{ReachCircle{area.centre, touching + EPS_MM, false, ModelId{}},
                                        ReachCircle{area.centre, touching + before - EPS_MM, false, ModelId{}}}));
}

// ==== After Moving ====

TEST(R12_08_ObjectiveConsolidation, TheUnitMustEndWithinRange) {
    GameState gs = started(2.0f);
    move(gs, 0, shifted(marine(gs, 0), 0.0f, 1.0f));  // closer, but no model within range
    const std::string problem =
        "12.08: unit " + std::to_string(marines(gs).v) + " must end its Objective Consolidation within range of Centre";
    EXPECT_EQ(move_end_problems(gs), (std::vector<std::string>{problem}));
    // Committing reverts the move (03.01, D-022): the unit may try again.
    const auto reverted = apply_action(gs, Side::A, action::CommitUnitMove{});
    EXPECT_EQ(events_of<event::UnitMoveReverted>(reverted).size(), 1u);
    EXPECT_FALSE(gs.units[kMarines].phase_flags.consolidated);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::Consolidate{}}));

    apply_action(gs, Side::A, action::StartConsolidation{marines(gs), {}, 0});
    move(gs, 2, shifted(marine(gs, 2), 0.0f, 2.05f));  // within range; still in coherency with its neighbours
    EXPECT_TRUE(move_end_problems(gs).empty());
    const auto committed = apply_action(gs, Side::A, action::CommitUnitMove{});
    EXPECT_TRUE(events_of<event::UnitMoveReverted>(committed).empty());
    EXPECT_TRUE(events_of<event::UnitsForcedToFight>(committed).empty());  // only Engaging forces fights
    EXPECT_TRUE(gs.units[kMarines].phase_flags.consolidated);
    EXPECT_TRUE(model_within_objective(marine(gs, 2), gs.mission->objectives[0]));
}

TEST(R12_08_ObjectiveConsolidation, WithoutAMissionPlayIsAsBefore) {
    GameState gs = objective_consolidation_state();
    gs.mission.reset();
    gs.result_rule = ResultRule::D007Draw;
    EXPECT_FALSE(consolidation_mode(gs, gs.units[kMarines]).has_value());
    EXPECT_TRUE(objectives_within(gs, gs.units[kMarines], 3.0f).empty());
    for (const Unit& u : gs.units) EXPECT_FALSE(eligible_to_consolidate(gs, u)) << u.id.v;
    for (const GameAction& a : legal_actions(gs, Side::A)) {
        EXPECT_FALSE(std::holds_alternative<action::StartConsolidation>(a));
    }
}

// ==== JSON (M90: every new field round-trips; old files load and save byte for byte) ====

TEST(D091_Serialization, TheObjectiveFieldsRoundTripAndAreOmittedWhenEmpty) {
    const action::StartConsolidation with{UnitId{1}, {}, 2};
    Json j;
    action::to_json(j, GameAction{with});
    EXPECT_EQ(j.dump(), R"({"kind":"StartConsolidation","objective":2,"targets":[],"unit":1})");
    GameAction back;
    action::from_json(Json::parse(j.dump()), back);
    EXPECT_EQ(back, GameAction{with});

    const char* old_action = R"({"kind":"StartConsolidation","targets":[2],"unit":1})";
    action::from_json(Json::parse(old_action), back);
    EXPECT_EQ(back, GameAction{(action::StartConsolidation{UnitId{1}, {UnitId{2}}})});
    action::to_json(j, back);
    EXPECT_EQ(j.dump(), old_action);

    const event::ConsolidationStarted started{UnitId{1}, ConsolidationMode::Objective, {}, 0, "Centre"};
    event::to_json(j, GameEvent{started});
    EXPECT_EQ(j.dump(), R"({"kind":"ConsolidationStarted","mode":"Objective","objective":0,"objective_name":"Centre","rule":"12.08","targets":[],"unit":1})");
    GameEvent e;
    event::from_json(Json::parse(j.dump()), e);
    EXPECT_EQ(e, GameEvent{started});
    const char* old_event = R"({"kind":"ConsolidationStarted","mode":"Engaging","rule":"12.08","targets":[2],"unit":1})";
    event::from_json(Json::parse(old_event), e);
    event::to_json(j, e);
    EXPECT_EQ(j.dump(), old_event);

    MoveInProgress mv;
    mv.unit = UnitId{1};
    mv.type = MoveType::Consolidation;
    mv.consolidation_mode = ConsolidationMode::Engaging;
    mv.targets = {UnitId{2}};
    const std::string old_move = Json(mv).dump();
    EXPECT_EQ(old_move.find("objective"), std::string::npos);
    EXPECT_EQ(Json(Json::parse(old_move).get<MoveInProgress>()).dump(), old_move);
    mv.consolidation_mode = ConsolidationMode::Objective;
    mv.targets.clear();
    mv.objective = 0;
    EXPECT_EQ(Json::parse(Json(mv).dump()).get<MoveInProgress>(), mv);
}

TEST(D091_Serialization, AStateMidObjectiveConsolidationRoundTrips) {
    GameState gs = started(2.0f);
    move(gs, 2, shifted(marine(gs, 2), 0.0f, 2.05f));
    const Json j = gs;
    const GameState back = j.get<GameState>();
    EXPECT_EQ(back, gs);
    EXPECT_EQ(Json(back).dump(), j.dump());
    // The move's objective must be one of the mission's.
    Json bad = j;
    bad["flow"]["move"]["objective"] = 5;
    EXPECT_THROW((void)bad.get<GameState>(), DataError);
    Json none = j;
    none.erase("mission");
    EXPECT_THROW((void)none.get<GameState>(), DataError);
}

// ==== 24.18 [IGNORES COVER] ====

TEST(R24_18_IgnoresCover, ParsesAndRoundTrips) {
    EXPECT_EQ(parse_weapon_ability("Ignores Cover"), WeaponAbility{ability::IgnoresCover{}});
    EXPECT_EQ(parse_weapon_ability("[IGNORES COVER]"), WeaponAbility{ability::IgnoresCover{}});
    Json j;
    ability::to_json(j, WeaponAbility{ability::IgnoresCover{}});
    EXPECT_EQ(j.dump(), R"({"kind":"IgnoresCover"})");
    // D-071: a file saved before M90 holds it as Unknown text, and reads it as the ability now.
    WeaponAbility a;
    ability::from_json(Json::parse(R"({"kind":"Unknown","text":"Ignores Cover"})"), a);
    EXPECT_EQ(a, WeaponAbility{ability::IgnoresCover{}});
}

// The V1 Shoot step with the Termagants (unit 1) in a row along y = 17" inside a crater field (an Exposed area: 13.08
// INFANTRY within a terrain area), the Intercessors (unit 0) in a row along y = 6"; the first `n` Intercessors fire
// their Bolt Rifle's first profile at the Termagants, the first of them with [IGNORES COVER] added (a fixture: the
// Bolt Rifle has a BS, unlike the flamers, which are [TORRENT]).
struct CoverShots {
    GameState gs;
    std::vector<WeaponSelection> sels;
    std::vector<AttackBatch> batches;
};
CoverShots cover_shots(std::size_t n, bool terrain) {
    GameState gs = wh40k::test::shoot_step_state();
    for (std::size_t i = 0; i < gs.units[0].models.size(); ++i) {
        gs.units[0].models[i].position = at(10.0f + 1.5f * static_cast<float>(i), 6.0f);
    }
    for (std::size_t i = 0; i < gs.units[1].models.size(); ++i) {
        gs.units[1].models[i].position = at(4.0f + 1.3f * static_cast<float>(i), 17.0f);
    }
    if (terrain) {
        TerrainFeature crater;
        crater.name = "Crater";
        crater.category = TerrainCategory::Exposed;
        crater.shape = circle_shape(at(20, 17), in(1));
        TerrainArea craters;
        craters.name = "Craters";
        craters.shape = polygon_shape({at(0.5f, 15.5f), at(23.5f, 15.5f), at(23.5f, 18.5f), at(0.5f, 18.5f)});
        craters.features = {crater};
        gs.terrain = {craters};
    }
    gs.units[0].models[0].weapons[0].profiles[0].abilities.push_back(ability::IgnoresCover{});
    action::Shoot a{gs.units[0].id, ShootingType::Normal, {}};
    for (std::size_t i = 0; i < n; ++i) a.shots.push_back(ShotDeclaration{gs.units[0].models[i].id, 0, 0, gs.units[1].id});
    EXPECT_TRUE(validate_action(gs, Side::A, a).reasons.empty());
    CoverShots out{gs, make_weapon_selections(gs, gs.units[0], ShootingType::Normal, a.shots), {}};
    out.batches = gather_attack_batches(out.gs, out.sels, shooting_context(out.gs, out.gs.units[0]));
    return out;
}

TEST(R24_18_IgnoresCover, AWeaponWithItKeepsItsBsAgainstATargetInCover) {
    const CoverShots shots = cover_shots(2, true);
    ASSERT_EQ(shots.sels.size(), 2u);
    EXPECT_FALSE(shots.sels[0].benefit_of_cover);  // [IGNORES COVER]: the target can't have it
    EXPECT_TRUE(shots.sels[1].benefit_of_cover);   // the same weapon without it: 13.08 applies (D-089)
    const int bs = *selected_profile(shots.gs, shots.sels[1]).skill;
    ASSERT_EQ(shots.batches.size(), 2u);  // the BS differs, so they are not identical attacks (04.03)
    for (const AttackBatch& b : shots.batches) {
        ASSERT_TRUE(b.profile.skill.has_value());
        const bool ignores = std::find(b.selections.begin(), b.selections.end(), std::size_t{0}) !=
                             b.selections.end();
        EXPECT_EQ(*b.profile.skill, ignores ? bs : bs + 1);
    }
}

TEST(R24_18_IgnoresCover, IsUsedUpAtTargetSelectionSoItDoesNotSplitBatches) {
    // D-006: without terrain the two Intercessors' attacks are identical; [IGNORES COVER] doesn't tell them apart.
    const CoverShots shots = cover_shots(2, false);
    EXPECT_EQ(shots.batches.size(), 1u);
    WeaponProfile p;
    p.abilities = {ability::IgnoresCover{}};
    EXPECT_FALSE(unimplemented_ability_problem(p).has_value());
}

TEST(R24_18_IgnoresCover, TheFlamersCanBeSelected) {
    for (const auto& [file, weapon] : std::vector<std::pair<std::string, std::string>>{
             {"data/units/legionaries.json", "Flamer"},
             {"data/units/havocs.json", "Flamer"},
             {"data/units/chaos_terminator_squad.json", "Heavy flamer"}}) {
        const Datasheet ds = load_datasheet(wh40k::test::repo_root() / file);
        const Weapon* w = ds.find_weapon(weapon);
        ASSERT_NE(w, nullptr) << file;
        for (const WeaponProfile& p : w->profiles) {
            EXPECT_TRUE(p.has<ability::IgnoresCover>()) << p.name;
            EXPECT_TRUE(p.has<ability::Torrent>()) << p.name;  // so the cover's BS never mattered for them
            EXPECT_FALSE(unimplemented_ability_problem(p).has_value()) << p.name;
        }
    }
}
