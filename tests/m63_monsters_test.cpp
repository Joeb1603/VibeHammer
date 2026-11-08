// M63 (D-073): Monsters and Vehicles. 17.01 (a MONSTER/VEHICLE model's Normal or Advance move passes through enemy
// models that aren't MONSTER/VEHICLE models), 17.03 (an engaged enemy MONSTER/VEHICLE unit can be shot at -1 to hit;
// [BLAST] refused, Rules Appendix FAQ) and the 10.06 fix (its -1 adds to the others; D-011 caps the net). Built on
// the rosters fixture's real units (tests/support/monster_fixtures.hpp); a stand-in MONSTER / VEHICLE keyword is
// added to a CSM unit where a test needs one on Side A.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

#include "core/data/datasheet.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/path.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/shooting.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/queries.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::at;
using wh40k::test::decider;
using wh40k::test::events_of;
using wh40k::test::expect_valid;
using wh40k::test::first_melee_options;
using wh40k::test::in;
using wh40k::test::rejection;
using wh40k::test::repo_root;
using wh40k::test::starts_with;
namespace mf = wh40k::test::monsters;

namespace {

UnitId id_of(const GameState& gs, std::size_t unit) { return gs.units[unit].id; }
ModelId tyrant_model(const GameState& gs) { return gs.units[mf::kTyrant].models[0].id; }

bool any_starts_with(const std::vector<std::string>& reasons, const std::string& prefix) {
    return std::any_of(reasons.begin(), reasons.end(), [&](const std::string& r) { return starts_with(r, prefix); });
}

bool crosses_an_enemy(const std::vector<std::string>& reasons) {
    return std::any_of(reasons.begin(), reasons.end(), [](const std::string& r) {
        return starts_with(r, "03.01: ") && r.find("cannot be moved through enemy model") != std::string::npos;
    });
}

// `move` started for `unit` in the fixture's first Move Units decision (Side B moves first).
GameState moving(std::size_t unit, MoveType type, bool engaged = false, std::optional<FallBackMode> mode = {}) {
    GameState gs = mf::at_movement(engaged);
    ScriptedDice dice{1};  // 09.06: an advance roll of 1 (9" for the Tyrant)
    apply_action(gs, Side::B, action::StartMove{id_of(gs, unit), type, mode}, dice);
    return gs;
}

action::ProposeModelMove straight_to(const GameState& gs, std::size_t unit, Position to) {
    return action::ProposeModelMove{gs.units[unit].models[0].id, {to}};
}

// The Tyrant's Normal move 7.7" south: through the Legionaries' rank, ending 2.04" beyond it.
Position through_the_rank() { return at(22.0f, mf::kThroughY); }

void add_keyword(GameState& gs, std::size_t unit, const char* keyword) { gs.units[unit].keywords.push_back(keyword); }

bool committed(const std::vector<GameEvent>& events) {
    return !events_of<event::UnitMoveCommitted>(events).empty() && events_of<event::UnitMoveReverted>(events).empty();
}

// Plays the engaged fixture (Side B first) to Side B's PileIn or Consolidate decision: the units remain stationary,
// end their steps and miss every attack (ScriptedDice of 1s), so the Tyrant stays engaged with the Legionaries.
template <typename Kind>
GameState engaged_fight_move_decision() {
    GameState gs = mf::fixture(true, Side::B);
    start_game(gs);
    ScriptedDice misses(std::vector<int>(4096, 1));  // every hit roll fails; nothing else is rolled
    for (int guard = 0; guard < 200 && gs.pending; ++guard) {
        const DecisionKind& k = gs.pending->kind;
        if (std::holds_alternative<Kind>(k) && gs.pending->side == Side::B) return gs;
        const Side side = decider(gs);
        GameAction a = legal_actions(gs, side).front();
        if (std::holds_alternative<decision::Shoot>(k)) {
            a = action::EndShootStep{};
        } else if (std::holds_alternative<decision::Charge>(k)) {
            a = action::EndChargeStep{};
        } else if (std::holds_alternative<decision::PileIn>(k)) {
            a = action::EndPileIns{};
        } else if (std::holds_alternative<decision::Consolidate>(k)) {
            a = action::EndConsolidations{};
        } else if (const auto* f = std::get_if<decision::DeclareMeleeAttacks>(&k)) {
            a = first_melee_options(gs, f->unit);
        } else if (auto* oc = std::get_if<action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
            oc->priority = wh40k::test::model_ids(*gs.find_unit(oc->target));
        }
        apply_action(gs, side, a, misses);
    }
    ADD_FAILURE() << "did not reach Side B's decision";
    return gs;
}

// From the engaged Tyrant (1" north of the rank) 1.36" south, into the rank, then back to 0.44" from it: 2.3" in all,
// closer to the Legionaries and engaged with them (a pile-in or consolidation could end there), but through two of
// its models.
action::ProposeModelMove dip_into_the_rank(const GameState& gs) {
    return action::ProposeModelMove{tyrant_model(gs), {at(22.0f, 27.6f), at(22.5f, 28.4f)}};
}

// The legal action of type A for the Tyrant in the current decision.
template <typename A>
A tyrant_start(const GameState& gs) {
    for (const GameAction& a : legal_actions(gs, decider(gs))) {
        if (const auto* s = std::get_if<A>(&a); s != nullptr && s->unit == id_of(gs, mf::kTyrant)) return *s;
    }
    ADD_FAILURE() << "no such action for the Tyrant";
    return A{};
}

// ---- 17.03 helpers ----

ShotDeclaration lascannon(const GameState& gs, std::size_t havoc, std::size_t target) {
    return ShotDeclaration{gs.units[mf::kHavocs].models[havoc].id, 0, 0, id_of(gs, target)};
}

std::vector<HitRoll> hit_rolls_of(const std::vector<GameEvent>& events) {
    std::vector<HitRoll> out;
    for (const auto& e : events_of<event::HitRollsMade>(events)) {
        out.insert(out.end(), e.rolls.rolls.begin(), e.rolls.rolls.end());
    }
    return out;
}

// Side A's Shoot step with the Havocs, given the stand-in VEHICLE keyword, moved into Engagement Range of the
// (engaged) Tyrant: 1" east of it on its line.
GameState vehicle_havocs_engaged_with_the_tyrant() {
    GameState gs = mf::fixture(true, Side::A);
    add_keyword(gs, mf::kHavocs, "Vehicle");
    const float tyrant_y = mf::kRankY + mf::tyrant_centre_gap(1.0f);
    const float east_edge = 22.0f + mm_to_inches(30.0f);
    mf::place_rank(gs.units[mf::kHavocs], east_edge + 1.0f + 5.0f * mm_to_inches(20.0f), tyrant_y);
    start_game(gs);
    mf::play_until(gs, [](const GameState& g) { return g.pending && std::holds_alternative<decision::Shoot>(g.pending->kind); });
    return gs;
}

}  // namespace

// ---- 17.01 Moving Monsters and Vehicles ----

TEST(R17_01_MovingMonsters, TheFixtureIsAsDescribed) {
    const GameState gs = mf::fixture();
    EXPECT_TRUE(is_monster_or_vehicle(gs.units[mf::kTyrant], gs.units[mf::kTyrant].models[0]));
    EXPECT_TRUE(is_monster_or_vehicle(gs.units[mf::kScreamer], gs.units[mf::kScreamer].models[0]));
    EXPECT_FALSE(is_monster_or_vehicle(gs.units[mf::kGuard], gs.units[mf::kGuard].models[0]));
    EXPECT_FALSE(is_monster_or_vehicle(gs.units[mf::kLegionaries], gs.units[mf::kLegionaries].models[0]));
    EXPECT_FALSE(is_engaged(gs, gs.units[mf::kTyrant]));
    EXPECT_GT(mm_to_inches(*unit_distance(gs.units[mf::kTyrant], gs.units[mf::kLegionaries])), 2.0f);
    const GameState engaged = mf::fixture(true);
    EXPECT_TRUE(is_engaged(engaged, engaged.units[mf::kTyrant]));
    EXPECT_TRUE(is_engaged(engaged, engaged.units[mf::kGuard]));
    EXPECT_FALSE(is_engaged(engaged, engaged.units[mf::kHavocs]));
    EXPECT_FALSE(is_engaged(engaged, engaged.units[mf::kLord]));
}

TEST(R17_01_MovingMonsters, ANormalMoveThroughEnemyInfantryIsAccepted) {
    GameState gs = moving(mf::kTyrant, MoveType::Normal);
    const auto propose = straight_to(gs, mf::kTyrant, through_the_rank());
    expect_valid(gs, Side::B, propose);
    apply_action(gs, Side::B, propose);
    EXPECT_TRUE(move_end_problems(gs).empty());
    EXPECT_TRUE(committed(apply_action(gs, Side::B, action::CommitUnitMove{})));
    EXPECT_EQ(gs.units[mf::kTyrant].models[0].position, through_the_rank());
}

TEST(R17_01_MovingMonsters, AnAdvanceMoveThroughEnemyInfantryIsAccepted) {
    GameState gs = moving(mf::kTyrant, MoveType::Advance);
    ASSERT_EQ(gs.units[mf::kTyrant].phase_flags.advance_roll, 1);
    // 8.7" (M8 + 1): through the rank, ending 2.74" beyond it.
    const Position end = at(22.0f, mf::kTyrantY - 8.7f);
    const auto propose = straight_to(gs, mf::kTyrant, end);
    expect_valid(gs, Side::B, propose);
    apply_action(gs, Side::B, propose);
    EXPECT_TRUE(committed(apply_action(gs, Side::B, action::CommitUnitMove{})));
    EXPECT_TRUE(gs.units[mf::kTyrant].turn_flags.advanced);
}

TEST(R17_01_MovingMonsters, NotThroughAnEnemyMonsterOrVehicle) {
    for (const char* keyword : {"Monster", "Vehicle"}) {
        GameState gs = mf::at_movement();
        add_keyword(gs, mf::kLegionaries, keyword);  // stand-in: no CSM unit is a MONSTER or VEHICLE
        apply_action(gs, Side::B, action::StartMove{id_of(gs, mf::kTyrant), MoveType::Normal, std::nullopt});
        EXPECT_TRUE(crosses_an_enemy(rejection(gs, Side::B, straight_to(gs, mf::kTyrant, through_the_rank()))))
            << keyword;
    }
}

TEST(R17_01_MovingMonsters, NotInAFallBackMove) {
    GameState gs = moving(mf::kTyrant, MoveType::FallBack, true, FallBackMode::OrderedRetreat);
    const Position beyond = at(22.0f, mf::kRankY - mf::tyrant_centre_gap(2.1f));
    EXPECT_TRUE(crosses_an_enemy(rejection(gs, Side::B, straight_to(gs, mf::kTyrant, beyond))));
    const auto reach = move_reach(gs, tyrant_model(gs));
    ASSERT_TRUE(reach);
    EXPECT_EQ(reach->blocking.size(), gs.units[mf::kLegionaries].models.size() + gs.units[mf::kHavocs].models.size() +
                                          gs.units[mf::kLord].models.size());
}

TEST(R17_01_MovingMonsters, ADesperateEscapeStillPassesEveryEnemyModel) {
    // 09.07 While Moving, unchanged by 17.01.
    GameState gs = mf::at_movement(true);
    gs.units[mf::kTyrant].battle_shocked = true;
    ScriptedDice hazards{6};
    apply_action(gs, Side::B,
                 action::StartMove{id_of(gs, mf::kTyrant), MoveType::FallBack, FallBackMode::DesperateEscape}, hazards);
    const Position beyond = at(22.0f, mf::kRankY - mf::tyrant_centre_gap(2.1f));
    expect_valid(gs, Side::B, straight_to(gs, mf::kTyrant, beyond));
}

TEST(R17_01_MovingMonsters, NotInAChargeMove) {
    GameState gs = mf::at_movement();
    apply_action(gs, Side::B, action::StartMove{id_of(gs, mf::kTyrant), MoveType::RemainStationary, std::nullopt});
    mf::play_until(gs, [](const GameState& g) { return g.pending && std::holds_alternative<decision::Charge>(g.pending->kind); });
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::Charge>(gs.pending->kind));
    ScriptedDice twelve{6, 6};
    apply_action(gs, Side::B, action::DeclareCharge{id_of(gs, mf::kTyrant)}, twelve);
    apply_action(gs, Side::B, action::StartChargeMove{id_of(gs, mf::kTyrant), {id_of(gs, mf::kLegionaries)}});
    // Through the rank to 1" beyond it: closer to and engaged with the target, but through its models.
    const Position beyond = at(22.0f, mf::kRankY - mf::tyrant_centre_gap(1.0f));
    const auto reasons = rejection(gs, Side::B, straight_to(gs, mf::kTyrant, beyond));
    EXPECT_TRUE(crosses_an_enemy(reasons));
    EXPECT_FALSE(any_starts_with(reasons, "11.04: "));
}

TEST(R17_01_MovingMonsters, NotInAPileInMove) {
    GameState gs = engaged_fight_move_decision<decision::PileIn>();
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::PileIn>(gs.pending->kind));
    apply_action(gs, Side::B, tyrant_start<action::StartPileIn>(gs));
    ASSERT_TRUE(gs.flow.move && gs.flow.move->type == MoveType::PileIn);
    const auto reasons = rejection(gs, Side::B, dip_into_the_rank(gs));
    EXPECT_TRUE(crosses_an_enemy(reasons));
    EXPECT_FALSE(any_starts_with(reasons, "12.03: ")) << ::testing::PrintToString(reasons);
}

TEST(R17_01_MovingMonsters, NotInAConsolidationMove) {
    GameState gs = engaged_fight_move_decision<decision::Consolidate>();
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::Consolidate>(gs.pending->kind));
    apply_action(gs, Side::B, tyrant_start<action::StartConsolidation>(gs));
    ASSERT_TRUE(gs.flow.move && gs.flow.move->type == MoveType::Consolidation);
    const auto reasons = rejection(gs, Side::B, dip_into_the_rank(gs));
    EXPECT_TRUE(crosses_an_enemy(reasons));
    EXPECT_FALSE(any_starts_with(reasons, "12.08: ")) << ::testing::PrintToString(reasons);
}

TEST(R17_01_MovingMonsters, NotEndingOnAModel) {
    GameState gs = moving(mf::kTyrant, MoveType::Normal);
    const Model& legionary = gs.units[mf::kLegionaries].models[5];
    const Position on_it{legionary.position.x, legionary.position.y - in(0.5f), 0.0f};
    const auto reasons = rejection(gs, Side::B, straight_to(gs, mf::kTyrant, on_it));
    EXPECT_FALSE(crosses_an_enemy(reasons));
    EXPECT_TRUE(std::any_of(reasons.begin(), reasons.end(), [&](const std::string& r) {
        return r == "03.01: model " + std::to_string(tyrant_model(gs).v) + " cannot end its move on model " +
                        std::to_string(legionary.id.v);
    }));
}

TEST(R17_01_MovingMonsters, NotEndingEngaged) {
    GameState gs = moving(mf::kTyrant, MoveType::Normal);
    // Through the rank, but only 1" beyond it: 09.05 After Moving.
    const auto propose = straight_to(gs, mf::kTyrant, at(22.0f, mf::kRankY - mf::tyrant_centre_gap(1.0f)));
    expect_valid(gs, Side::B, propose);
    apply_action(gs, Side::B, propose);
    EXPECT_TRUE(any_starts_with(move_end_problems(gs), "09.05: "));
    const auto events = apply_action(gs, Side::B, action::CommitUnitMove{});
    ASSERT_EQ(events_of<event::UnitMoveReverted>(events).size(), 1u);
    EXPECT_EQ(gs.units[mf::kTyrant].models[0].position, at(22.0f, mf::kTyrantY));
}

TEST(R17_01_MovingMonsters, ANonMonsterStillCantPass) {
    GameState gs = mf::fixture();
    mf::place_rank(gs.units[mf::kGuard], 22.0f, mf::kTyrantY);  // the Tyrant Guard where the Tyrant was
    mf::place_rank(gs.units[mf::kTyrant], 6.0f, 40.0f);
    start_game(gs);
    mf::play_until(gs, [](const GameState& g) { return g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind); });
    apply_action(gs, Side::B, action::StartMove{id_of(gs, mf::kGuard), MoveType::Normal, std::nullopt});
    const Model& guard = gs.units[mf::kGuard].models[1];  // the middle one, at x 22
    const action::ProposeModelMove propose{guard.id, {at(22.0f, mf::kRankY - 3.0f)}};
    EXPECT_TRUE(crosses_an_enemy(rejection(gs, Side::B, propose)));
    const auto reach = move_reach(gs, guard.id);
    ASSERT_TRUE(reach);
    EXPECT_FALSE(reach->blocking.empty());
}

TEST(R17_01_MovingMonsters, TheReachAreaIncludesTheFarSide) {
    const GameState gs = moving(mf::kTyrant, MoveType::Normal);
    const auto reach = move_reach(gs, tyrant_model(gs));
    ASSERT_TRUE(reach);
    // D-044: no shadow behind the Legionaries, the Lord or the Havocs (none is a MONSTER/VEHICLE).
    EXPECT_TRUE(reach->blocking.empty());
    // Still not on them, nor within their Engagement Range (09.05 After Moving): one circle round every other model.
    std::size_t others = 0;
    for (const Unit& u : gs.units) others += u.models.size();
    EXPECT_EQ(reach->forbidden.size(), others - 1);
    for (const ReachCircle& c : reach->forbidden) {
        const Unit* owner = find_unit_of_model(gs, c.model);
        ASSERT_NE(owner, nullptr);
        EXPECT_EQ(c.engagement, owner->side == Side::A);
    }
    // The far side of the rank is inside the reach (within the 8" and outside every forbidden circle).
    const Position end = through_the_rank();
    EXPECT_LE(centre_distance(reach->from, end), reach->remaining_mm + EPS_MM);
    for (const ReachCircle& c : reach->forbidden) EXPECT_GT(centre_distance(c.centre, end), c.radius_mm);
}

TEST(R17_01_MovingMonsters, AnEnemyMonsterStillBlocksTheReachArea) {
    GameState gs = mf::at_movement();
    add_keyword(gs, mf::kHavocs, "Vehicle");
    apply_action(gs, Side::B, action::StartMove{id_of(gs, mf::kTyrant), MoveType::Normal, std::nullopt});
    const auto reach = move_reach(gs, tyrant_model(gs));
    ASSERT_TRUE(reach);
    ASSERT_EQ(reach->blocking.size(), gs.units[mf::kHavocs].models.size());
    for (std::size_t i = 0; i < reach->blocking.size(); ++i) {
        EXPECT_EQ(reach->blocking[i].model, gs.units[mf::kHavocs].models[i].id);
    }
}

// ---- D-073 readings for 17.01 ----

TEST(D073_Monsters, AMonsterMayPassFriendlyMonsters) {
    // 03.01 lets every model move through friendly models; 17.01 only adds a permission (D-073).
    GameState gs = mf::fixture();
    mf::place_rank(gs.units[mf::kScreamer], 22.0f, 34.0f);  // 4" north of the Tyrant, in its way
    start_game(gs);
    mf::play_until(gs, [](const GameState& g) { return g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind); });
    apply_action(gs, Side::B, action::StartMove{id_of(gs, mf::kTyrant), MoveType::Normal, std::nullopt});
    expect_valid(gs, Side::B, straight_to(gs, mf::kTyrant, at(22.0f, 37.5f)));
}

TEST(D073_Monsters, PassableEnemiesAreCrossedButNotEndedOn) {
    // check_model_path: an enemy model in `passable_enemy` may be crossed; ending on it is still 03.01 Ending a Move.
    const Rect board{0.0f, 0.0f, in(24.0f), in(24.0f)};
    const Base tyrant{BaseShape::Round, 60.0f, 0.0f};
    const std::vector<PlacedModel> infantry{wh40k::test::placed(2, at(12.0f, 12.0f))};
    const std::vector<Position> through{at(12.0f, 8.0f), at(12.0f, 16.0f)};
    PathContext blocking{board, {}, infantry};
    const auto crossed = check_model_path(ModelId{1}, tyrant, through, blocking);
    ASSERT_EQ(crossed.size(), 1u);
    EXPECT_EQ(crossed[0].kind, PathViolationKind::CrossesEnemyModel);
    PathContext passable{board, {}, {}, false, infantry};
    EXPECT_TRUE(check_model_path(ModelId{1}, tyrant, through, passable).empty());
    const std::vector<Position> onto{at(12.0f, 8.0f), at(12.0f, 12.5f)};
    const auto ended = check_model_path(ModelId{1}, tyrant, onto, passable);
    ASSERT_EQ(ended.size(), 1u);
    EXPECT_EQ(ended[0].kind, PathViolationKind::EndsOnModel);
    EXPECT_EQ(ended[0].other, ModelId{2});
}

TEST(D073_Monsters, OnlyNormalAndAdvanceMovesOfAMonsterOrVehicle) {
    const GameState gs = mf::fixture();
    const Unit& tyrant = gs.units[mf::kTyrant];
    const Unit& guard = gs.units[mf::kGuard];
    for (const MoveType t : {MoveType::Normal, MoveType::Advance}) {
        EXPECT_TRUE(moves_through_non_monster_enemies(t, tyrant, tyrant.models[0]));
        EXPECT_FALSE(moves_through_non_monster_enemies(t, guard, guard.models[0]));
    }
    for (const MoveType t : {MoveType::RemainStationary, MoveType::FallBack, MoveType::Charge, MoveType::PileIn,
                             MoveType::Consolidation}) {
        EXPECT_FALSE(moves_through_non_monster_enemies(t, tyrant, tyrant.models[0]));
    }
    Unit vehicle = guard;
    vehicle.keywords.push_back("VEHICLE");  // a unit keyword, matched without case (02.05)
    EXPECT_TRUE(moves_through_non_monster_enemies(MoveType::Normal, vehicle, vehicle.models[0]));
}

TEST(D073_Monsters, NoUnitInTheOldScenariosIsAMonsterOrVehicle) {
    // So 17.01, 17.03 and the 10.06 change can't touch old play (V1, both bot scenarios, Strike).
    for (const char* file : {"data/scenarios/v1_skirmish.json", "data/scenarios/bot_termagants_first.json",
                             "data/scenarios/bot_20_termagants.json", "data/scenarios/strike_44x60.json"}) {
        const GameState gs = load_scenario(repo_root() / file, repo_root());
        for (const Unit& u : gs.units) {
            for (const Model& m : u.models) EXPECT_FALSE(is_monster_or_vehicle(u, m)) << file << ": " << u.name;
        }
    }
}

// ---- 17.03 Shooting at Engaged Monsters and Vehicles ----

TEST(R17_03_ShootingAtEngagedMonsters, AnEngagedEnemyMonsterCanBeTargeted) {
    const GameState gs = mf::at_side_a_shooting();
    ASSERT_TRUE(is_engaged(gs, gs.units[mf::kTyrant]));
    const auto options = shot_options(gs, gs.units[mf::kHavocs], ShootingType::Normal);
    EXPECT_TRUE(std::any_of(options.begin(), options.end(),
                            [&](const ShotDeclaration& d) { return d.target == id_of(gs, mf::kTyrant); }));
    expect_valid(gs, Side::A,
                 action::Shoot{id_of(gs, mf::kHavocs), ShootingType::Normal, {lascannon(gs, 1, mf::kTyrant)}});
    EXPECT_TRUE(has_shot_option(gs, gs.units[mf::kHavocs], ShootingType::Normal));
}

TEST(R17_03_ShootingAtEngagedMonsters, AnEngagedNonMonsterCantBeTargeted) {
    const GameState gs = mf::at_side_a_shooting();
    ASSERT_TRUE(is_engaged(gs, gs.units[mf::kGuard]));
    const auto reasons = rejection(
        gs, Side::A, action::Shoot{id_of(gs, mf::kHavocs), ShootingType::Normal, {lascannon(gs, 1, mf::kGuard)}});
    EXPECT_TRUE(std::find(reasons.begin(), reasons.end(),
                          "04.02: unit " + std::to_string(id_of(gs, mf::kGuard).v) +
                              " is engaged; targets must be unengaged") != reasons.end());
    for (const ShotDeclaration& d : shot_options(gs, gs.units[mf::kHavocs], ShootingType::Normal)) {
        EXPECT_NE(d.target, id_of(gs, mf::kGuard));
    }
}

TEST(R17_03_ShootingAtEngagedMonsters, MinusOneToHit) {
    GameState gs = mf::at_side_a_shooting();
    const std::vector<ShotDeclaration> shots{lascannon(gs, 1, mf::kTyrant), lascannon(gs, 2, mf::kTyrant)};
    const auto selections = make_weapon_selections(gs, gs.units[mf::kHavocs], ShootingType::Normal, shots);
    ASSERT_EQ(selections.size(), 2u);
    for (const WeaponSelection& s : selections) {
        EXPECT_EQ(s.hit_modifier, -1);
        EXPECT_EQ(s.hit_modifier_rules, std::vector<HitModifierRule>{HitModifierRule::EngagedMonsterTarget});
    }
    // BS3+: a 3 becomes a 2 and fails; a 4 becomes a 3 and hits.
    ScriptedDice dice{3, 4, 3, 3, 3, 3, 3, 3};
    const auto events = apply_action(gs, Side::A, action::Shoot{id_of(gs, mf::kHavocs), ShootingType::Normal, shots}, dice);
    const auto rolls = hit_rolls_of(events);
    ASSERT_EQ(rolls.size(), 2u);
    EXPECT_EQ(rolls[0].modified, 2);
    EXPECT_EQ(rolls[0].outcome, HitOutcome::Fail);
    EXPECT_EQ(rolls[1].modified, 3);
    EXPECT_EQ(rolls[1].outcome, HitOutcome::Hit);
}

TEST(R17_03_ShootingAtEngagedMonsters, NoPenaltyAtAnUnengagedMonster) {
    GameState gs = mf::fixture(false, Side::A);
    start_game(gs);
    mf::play_until(gs, [](const GameState& g) { return g.pending && std::holds_alternative<decision::Shoot>(g.pending->kind); });
    ASSERT_FALSE(is_engaged(gs, gs.units[mf::kTyrant]));
    const std::vector<ShotDeclaration> shots{lascannon(gs, 1, mf::kTyrant)};
    const auto selections = make_weapon_selections(gs, gs.units[mf::kHavocs], ShootingType::Normal, shots);
    EXPECT_EQ(selections.at(0).hit_modifier, 0);
    EXPECT_TRUE(selections.at(0).hit_modifier_rules.empty());
}

TEST(R17_03_ShootingAtEngagedMonsters, NoPenaltyForCloseQuartersWeaponsOfAUnitEngagedWithIt) {
    const GameState gs = mf::at_side_a_shooting();
    const Unit& legionaries = gs.units[mf::kLegionaries];
    ASSERT_TRUE(!shooting_type_problem(gs, legionaries, ShootingType::CloseQuarters));
    std::vector<ShotDeclaration> pistols;
    for (const ShotDeclaration& d : shot_options(gs, legionaries, ShootingType::CloseQuarters)) {
        if (d.target == id_of(gs, mf::kTyrant)) pistols.push_back(d);
    }
    ASSERT_FALSE(pistols.empty());
    for (const WeaponSelection& s : make_weapon_selections(gs, legionaries, ShootingType::CloseQuarters, pistols)) {
        EXPECT_EQ(s.hit_modifier, 0);
        EXPECT_TRUE(s.hit_modifier_rules.empty());
    }
    expect_valid(gs, Side::A, action::Shoot{legionaries.id, ShootingType::CloseQuarters, {pistols.front()}});
}

TEST(R17_03_ShootingAtEngagedMonsters, ThePenaltyForCloseQuartersWeaponsOfAUnitNotEngagedWithIt) {
    // The Chaos Lord's plasma pistol ([PISTOL] = [CLOSE-QUARTERS], 24.27) in normal shooting: the Lord isn't engaged
    // with the Tyrant, so the exception doesn't apply.
    const GameState gs = mf::at_side_a_shooting();
    const Unit& lord = gs.units[mf::kLord];
    ASSERT_FALSE(is_engaged(gs, lord));
    const ShotDeclaration pistol{lord.models[0].id, 0, 0, id_of(gs, mf::kTyrant)};
    ASSERT_TRUE(lord.models[0].weapons[0].profiles[0].has<ability::CloseQuarters>());
    expect_valid(gs, Side::A, action::Shoot{lord.id, ShootingType::Normal, {pistol}});
    const auto selections = make_weapon_selections(gs, lord, ShootingType::Normal, std::vector{pistol});
    EXPECT_EQ(selections.at(0).hit_modifier, -1);
    EXPECT_EQ(selections.at(0).hit_modifier_rules, std::vector<HitModifierRule>{HitModifierRule::EngagedMonsterTarget});
}

TEST(R17_03_ShootingAtEngagedMonsters, BlastIsRefused) {
    GameState gs = mf::at_side_a_shooting();
    const Datasheet havocs = load_datasheet(repo_root() / "data/units/havocs.json");
    Model& havoc = gs.units[mf::kHavocs].models[1];
    havoc.weapons[0] = *havocs.find_weapon("Havoc missile launcher");  // frag [BLAST], krak
    ASSERT_TRUE(havoc.weapons[0].profiles[0].has<ability::Blast>());
    const ShotDeclaration frag{havoc.id, 0, 0, id_of(gs, mf::kTyrant)};
    const ShotDeclaration krak{havoc.id, 0, 1, id_of(gs, mf::kTyrant)};
    const auto reasons =
        rejection(gs, Side::A, action::Shoot{id_of(gs, mf::kHavocs), ShootingType::Normal, {frag}});
    ASSERT_EQ(reasons.size(), 1u);
    EXPECT_TRUE(starts_with(reasons[0], "17.03: 'Havoc missile launcher - frag' is a [BLAST] weapon and cannot target "));
    expect_valid(gs, Side::A, action::Shoot{id_of(gs, mf::kHavocs), ShootingType::Normal, {krak}});
    const auto options = shot_options(gs, gs.units[mf::kHavocs], ShootingType::Normal);
    EXPECT_EQ(std::count(options.begin(), options.end(), frag), 0);
    EXPECT_EQ(std::count(options.begin(), options.end(), krak), 1);
    // [BLAST] at an unengaged unit is still fine (none here is: everything else is a Side A unit or engaged), and
    // at the unengaged Tyrant it was always fine.
    GameState apart = mf::fixture(false, Side::A);
    apart.units[mf::kHavocs].models[1].weapons[0] = havoc.weapons[0];
    start_game(apart);
    mf::play_until(apart, [](const GameState& g) { return g.pending && std::holds_alternative<decision::Shoot>(g.pending->kind); });
    expect_valid(apart, Side::A,
                 action::Shoot{id_of(apart, mf::kHavocs), ShootingType::Normal,
                               {ShotDeclaration{apart.units[mf::kHavocs].models[1].id, 0, 0, id_of(apart, mf::kTyrant)}}});
}

TEST(R17_03_ShootingAtEngagedMonsters, TheCapWithCloseQuartersShooting) {
    // 10.06 (a VEHICLE in close-quarters shooting, a non-[CLOSE-QUARTERS] weapon) and 17.03 (an engaged MONSTER) are
    // -1 each: -2 on the selection, which D-011 caps to -1 at the hit roll.
    GameState gs = vehicle_havocs_engaged_with_the_tyrant();
    const Unit& havocs = gs.units[mf::kHavocs];
    ASSERT_TRUE(is_engaged(gs, havocs));
    ASSERT_EQ(eligible_shooting_types(gs, havocs), std::vector<ShootingType>{ShootingType::CloseQuarters});
    const std::vector<ShotDeclaration> shots{lascannon(gs, 1, mf::kTyrant), lascannon(gs, 2, mf::kTyrant)};
    expect_valid(gs, Side::A, action::Shoot{havocs.id, ShootingType::CloseQuarters, shots});
    const auto selections = make_weapon_selections(gs, havocs, ShootingType::CloseQuarters, shots);
    for (const WeaponSelection& s : selections) {
        EXPECT_EQ(s.hit_modifier, -2);
        EXPECT_EQ(s.hit_modifier_rules, (std::vector<HitModifierRule>{HitModifierRule::CloseQuartersMonster,
                                                                       HitModifierRule::EngagedMonsterTarget}));
    }
    // A 4 becomes a 3 (-1, capped), so it hits; a 3 fails.
    ScriptedDice dice{3, 4, 3, 3, 3, 3, 3, 3};
    const auto events = apply_action(gs, Side::A, action::Shoot{havocs.id, ShootingType::CloseQuarters, shots}, dice);
    const auto batches = events_of<event::AttackBatchesGathered>(events);
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].batches.at(0).profile.hit_modifier, -2);  // the cap is applied at the hit roll (D-011)
    const auto rolls = hit_rolls_of(events);
    ASSERT_EQ(rolls.size(), 2u);
    EXPECT_EQ(rolls[0].modified, 2);
    EXPECT_EQ(rolls[0].outcome, HitOutcome::Fail);
    EXPECT_EQ(rolls[1].modified, 3);
    EXPECT_EQ(rolls[1].outcome, HitOutcome::Hit);
}

TEST(R17_03_ShootingAtEngagedMonsters, CloseQuartersWeaponsOfAMonsterEngagedWithItHaveNeither) {
    // Both exceptions at once: a [CLOSE-QUARTERS] weapon at a unit its unit is engaged with (10.06, 17.03).
    GameState gs = vehicle_havocs_engaged_with_the_tyrant();
    const Datasheet havocs = load_datasheet(repo_root() / "data/units/havocs.json");
    gs.units[mf::kHavocs].models[0].weapons[0] = *havocs.find_weapon("Plasma pistol");
    const ShotDeclaration pistol{gs.units[mf::kHavocs].models[0].id, 0, 0, id_of(gs, mf::kTyrant)};
    expect_valid(gs, Side::A, action::Shoot{id_of(gs, mf::kHavocs), ShootingType::CloseQuarters, {pistol}});
    const auto s = make_weapon_selections(gs, gs.units[mf::kHavocs], ShootingType::CloseQuarters, std::vector{pistol});
    EXPECT_EQ(s.at(0).hit_modifier, 0);
    EXPECT_TRUE(s.at(0).hit_modifier_rules.empty());
}

TEST(R10_06_CloseQuartersShooting, AMonstersPenaltyAddsToTheOthers) {
    // 10.06's -1 is added (it used to be set), so it stacks with 17.03's before the D-011 cap. At an unengaged target
    // it is the only one.
    GameState gs = vehicle_havocs_engaged_with_the_tyrant();
    mf::place_rank(gs.units[mf::kScreamer], 36.0f, 40.0f);  // an unengaged MONSTER in range
    const ShotDeclaration at_screamer = lascannon(gs, 3, mf::kScreamer);
    expect_valid(gs, Side::A, action::Shoot{id_of(gs, mf::kHavocs), ShootingType::CloseQuarters, {at_screamer}});
    const auto s =
        make_weapon_selections(gs, gs.units[mf::kHavocs], ShootingType::CloseQuarters, std::vector{at_screamer});
    EXPECT_EQ(s.at(0).hit_modifier, -1);
    EXPECT_EQ(s.at(0).hit_modifier_rules, std::vector<HitModifierRule>{HitModifierRule::CloseQuartersMonster});
}

TEST(D073_Monsters, HitModifierRulesRoundTripAndAreOmittedWhenEmpty) {
    WeaponSelection plain;
    plain.target = UnitId{3};
    EXPECT_FALSE(Json(plain).contains("hit_modifier_rules"));
    EXPECT_EQ(Json(plain).get<WeaponSelection>(), plain);
    WeaponSelection both = plain;
    both.hit_modifier = -2;
    both.hit_modifier_rules = {HitModifierRule::CloseQuartersMonster, HitModifierRule::EngagedMonsterTarget};
    EXPECT_EQ(Json(both)["hit_modifier_rules"], Json::parse(R"(["10.06", "17.03"])"));
    EXPECT_EQ(Json(both).get<WeaponSelection>(), both);
    Json bad = Json(both);
    bad["hit_modifier_rules"] = Json::parse(R"(["99.99"])");
    EXPECT_THROW((void)bad.get<WeaponSelection>(), std::exception);

    // A state waiting in the attack flow with such selections round-trips.
    GameState gs = mf::at_side_a_shooting();
    ScriptedDice dice{4, 4, 1, 1, 1, 1};  // a hit, a wound and a failed save: waiting for the casualty order
    apply_action(gs, Side::A, action::Shoot{id_of(gs, mf::kHavocs), ShootingType::Normal, {lascannon(gs, 1, mf::kTyrant)}},
                 dice);
    ASSERT_TRUE(gs.flow.attack);
    ASSERT_FALSE(gs.flow.attack->selections.empty());
    EXPECT_FALSE(gs.flow.attack->selections[0].hit_modifier_rules.empty());
    EXPECT_EQ(Json(gs).get<GameState>(), gs);
}
