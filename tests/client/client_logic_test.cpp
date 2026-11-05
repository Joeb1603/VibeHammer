// M12: the client's logic layer (D-024). GameSession (validate before apply), the decision view, the
// shooting/melee/move/casualty editors' models, a full game played through that layer only, and the
// structural check that the client has no rules logic.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "action_text.hpp"
#include "board_interaction.hpp"
#include "casualty_plan.hpp"
#include "core/engine/movement.hpp"
#include "core/state/queries.hpp"
#include "datasheet_view.hpp"
#include "decision_view.hpp"
#include "dice_cards.hpp"
#include "event_text.hpp"
#include "measure.hpp"
#include "melee_plan.hpp"
#include "move_plan.hpp"
#include "overlay_plan.hpp"
#include "phase_track.hpp"
#include "ring_outline.hpp"
#include "session.hpp"
#include "shot_plan.hpp"
#include "core/data/load_error.hpp"
#include "core/engine/replay.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "support/engine_helpers.hpp"
#include "timeline.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::events_of;
using wh40k::test::in;

namespace {

const std::vector<GameEvent>& last_events(const GameSession& s) { return s.log().back().events; }

}  // namespace

// ==== M12 acceptance: a unit-wide default plus a single-model override (D-001) ====

TEST(M12_ShotPlan, UnitDefaultPlusOneOverrideGivesMixedBoltRifleProfilesThatTheEngineAccepts) {
    GameSession session(wh40k::test::shoot_step_state());
    const GameState& gs = session.state();
    const UnitId marines = gs.units[0].id;
    const UnitId gants = gs.units[1].id;
    ShotPlan plan(gs, marines, ShootingType::Normal);
    plan.set_default(ShotChoice{"Bolt Rifle", 0, gants});  // Focused Fire for the whole unit
    const ModelId third = gs.units[0].models[2].id;
    const auto options = plan.options_for(third);
    const auto saturation = std::find_if(options.begin(), options.end(), [](const ShotOption& o) {
        return o.weapon == "Bolt Rifle" && o.declaration.profile_index == 1;
    });
    ASSERT_NE(saturation, options.end());
    plan.override_model(third, saturation->declaration);

    const action::Shoot shoot = plan.expand();
    ASSERT_EQ(shoot.shots.size(), 5u);
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(shoot.shots[i].weapon_index, 0u);
        EXPECT_EQ(shoot.shots[i].profile_index, i == 2 ? 1u : 0u);  // mixed profiles
    }
    ASSERT_TRUE(session.check(shoot).ok());
    ASSERT_TRUE(session.submit(shoot).ok());
    const auto gathered = events_of<event::AttackBatchesGathered>(last_events(session));
    ASSERT_EQ(gathered.size(), 1u);
    EXPECT_EQ(gathered[0].batches.size(), 2u);  // Focused Fire and Saturation are not identical (D-006)
}

TEST(M12_ShotPlan, ModelsWithoutTheDefaultOptionDoNotFireUnlessOverridden) {
    GameSession session(wh40k::test::shoot_step_state());
    const GameState& gs = session.state();
    ShotPlan plan(gs, gs.units[0].id, ShootingType::Normal);
    plan.set_default(ShotChoice{"Bolt pistol", 0, gs.units[1].id});  // 12": every model is in range here
    EXPECT_EQ(plan.expand().shots.size(), 5u);
    plan.override_model(gs.units[0].models[0].id, std::nullopt);  // this model doesn't fire
    EXPECT_EQ(plan.expand().shots.size(), 4u);
    plan.clear_override(gs.units[0].models[0].id);
    EXPECT_EQ(plan.expand().shots.size(), 5u);
}

// ==== GameSession: validate before apply ====

TEST(M12_GameSession, AnInvalidActionIsRejectedWithItsReasonsAndChangesNothing) {
    GameSession session(wh40k::test::load_v1_scenario());
    const GameState before = session.state();
    const auto r = session.submit(action::EndShootStep{});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.reasons.at(0).substr(0, 7), "10.02: ");
    EXPECT_EQ(session.state(), before);
    EXPECT_TRUE(session.history().empty());
    EXPECT_EQ(session.log().size(), 1u);  // just the game start
}

TEST(M12_GameSession, ValidActionsAreAppliedAndRecorded) {
    GameSession session(wh40k::test::load_v1_scenario());
    const auto first = session.legal().front();
    ASSERT_TRUE(session.submit(first).ok());
    ASSERT_EQ(session.history().size(), 1u);
    EXPECT_EQ(session.history()[0].action, first);
    EXPECT_EQ(session.log().back().label.rfind("Step 1: Side A ", 0), 0u);
}

// ==== the decision view ====

TEST(M12_DecisionView, MoveModelsGivesCommitResetButtonsAndPathTemplates) {
    GameSession session(wh40k::test::load_v1_scenario());
    ASSERT_TRUE(session.submit(action::StartMove{session.state().units[0].id, MoveType::Normal, std::nullopt}).ok());
    const DecisionView view = make_decision_view(session.legal());
    EXPECT_EQ(view.buttons, (std::vector<GameAction>{action::CommitUnitMove{}, action::ResetUnitMove{}}));
    ASSERT_EQ(view.templates.size(), 5u);
    for (const auto& t : view.templates) EXPECT_EQ(editor_kind(t), EditorKind::Path);
    EXPECT_EQ(action_label(session.state(), view.buttons[0]), "Commit move");
}

TEST(M12_DecisionView, EveryTemplateKindHasAnEditor) {
    EXPECT_EQ(editor_kind(action::Shoot{UnitId{1}, ShootingType::Normal, {}}), EditorKind::Shot);
    EXPECT_EQ(editor_kind(action::Fight{UnitId{1}, {}}), EditorKind::Melee);
    EXPECT_EQ(editor_kind(action::OrderCasualties{UnitId{1}, {}}), EditorKind::Casualties);
    EXPECT_EQ(editor_kind(action::ProposeModelMove{ModelId{1}, {}}), EditorKind::Path);
    EXPECT_FALSE(editor_kind(action::EndShootStep{}).has_value());
}

// ==== moves: drag paths and the "Move unit" handle ====

TEST(M12_MovePlan, TheMoveUnitHandleTranslatesEveryModelAndTheEngineAcceptsIt) {
    GameSession session(wh40k::test::load_v1_scenario());
    const UnitId marines = session.state().units[0].id;
    ASSERT_TRUE(session.submit(action::StartMove{marines, MoveType::Normal, std::nullopt}).ok());
    for (const auto& a : translate_unit(session.state(), marines, 0.0f, in(2))) {
        ASSERT_TRUE(session.submit(a).ok());
    }
    const auto r = ruler(session.state(), session.state().units[0].models[0].id);
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(mm_to_inches(r->moved_mm), 2.0f, 0.001f);
    EXPECT_NEAR(mm_to_inches(r->max_mm), 6.0f, 0.001f);
    EXPECT_FALSE(r->over);
    ASSERT_TRUE(session.submit(action::CommitUnitMove{}).ok());
    EXPECT_TRUE(events_of<event::UnitMoveCommitted>(last_events(session)).size() == 1u);
}

TEST(M14_TranslateUnit, ATouchingUnitMovesInEveryDirectionWithoutARejectedProposal) {
    // The bases touch along and between ranks (D-008, M14). Each proposal may not end on a model at its
    // current position (D-027), so the handle must order the models furthest along the move first.
    GameSession session(wh40k::test::load_v1_scenario());
    // Side B's first Move Units step: Side A passes its turn.
    while (!(session.pending() && std::holds_alternative<decision::MoveUnits>(session.pending()->kind) &&
             session.pending()->side == Side::B)) {
        ASSERT_TRUE(session.submit(make_decision_view(session.legal()).buttons.front()).ok());
    }
    const UnitId gants = session.state().units[1].id;  // two ranks of five
    for (const auto& [dx, dy] : {std::pair{in(1.5f), 0.0f}, std::pair{-in(1.5f), 0.0f}, std::pair{0.0f, in(1.5f)},
                                 std::pair{0.0f, -in(1.5f)}, std::pair{in(1), in(1)}}) {
        ASSERT_TRUE(session.submit(action::StartMove{gants, MoveType::Normal, std::nullopt}).ok());
        for (const auto& a : translate_unit(session.state(), gants, dx, dy)) {
            const auto r = session.submit(a);
            ASSERT_TRUE(r.ok()) << "(" << dx << ", " << dy << "): " << r.reasons.at(0);
        }
        EXPECT_TRUE(move_end_problems(session.state()).empty());
        ASSERT_TRUE(session.submit(action::ResetUnitMove{}).ok());  // back to the formation for the next one
    }
}

TEST(M14_TranslateUnit, ModelsFurthestAlongTheMoveComeFirst) {
    GameSession session(wh40k::test::load_v1_scenario());
    const Unit& marines = session.state().units[0];  // one rank, model 1 furthest west
    ASSERT_TRUE(session.submit(action::StartMove{marines.id, MoveType::Normal, std::nullopt}).ok());
    const auto east = translate_unit(session.state(), marines.id, in(1), 0.0f);
    const auto west = translate_unit(session.state(), marines.id, -in(1), 0.0f);
    const auto north = translate_unit(session.state(), marines.id, 0.0f, in(1));
    ASSERT_EQ(east.size(), 5u);
    EXPECT_EQ(east.front().model, marines.models[4].id);
    EXPECT_EQ(west.front().model, marines.models[0].id);
    for (std::size_t i = 0; i < 5; ++i) EXPECT_EQ(north[i].model, marines.models[i].id);  // a tie: unit order
}

TEST(M12_MovePlan, DragsAccumulateSegmentsAndTheRulerFlagsTooFar) {
    GameSession session(wh40k::test::load_v1_scenario());
    const UnitId marines = session.state().units[0].id;
    ASSERT_TRUE(session.submit(action::StartMove{marines, MoveType::Normal, std::nullopt}).ok());
    const ModelId m = session.state().units[0].models[0].id;
    const Position start = session.state().units[0].models[0].position;
    ASSERT_TRUE(session.submit(extend_path(session.state(), m, Position{start.x, start.y + in(3), 0})).ok());
    const action::ProposeModelMove second = extend_path(session.state(), m, Position{start.x + in(2), start.y + in(3), 0});
    EXPECT_EQ(second.path.size(), 2u);  // the first straight line and a new one
    // 3" proposed, plus a line to the cursor: 5.9" is within the 6" maximum, 6.5" is not.
    EXPECT_FALSE(ruler(session.state(), m, Position{start.x + in(2.9f), start.y + in(3), 0})->over);
    EXPECT_TRUE(ruler(session.state(), m, Position{start.x + in(3.5f), start.y + in(3), 0})->over);
}

// ==== melee and casualties ====

TEST(M12_MeleePlan, TheDefaultDeclarationIsValidAndOverridesAreChecked) {
    GameState gs = wh40k::test::after_successful_charge();
    GameSession session(gs);
    ASSERT_TRUE(session.submit(action::EndChargeStep{}).ok());
    while (session.state().flow.stage == Stage::PileIn) ASSERT_TRUE(session.submit(action::EndPileIns{}).ok());
    const UnitId marines = session.state().units[0].id;
    ASSERT_TRUE(session.submit(action::SelectToFight{marines, FightType::Normal}).ok());
    MeleePlan plan(session.state(), marines);
    const action::Fight fight = plan.expand();
    EXPECT_EQ(fight.declarations.size(), 5u);
    EXPECT_TRUE(session.check(fight).ok());

    MeleeDeclaration wrong = *plan.declaration_for(session.state().units[0].models[0].id);
    wrong.targets.front().attacks = 2;  // Knives and Fists has 3 attacks
    plan.override_model(wrong.model, wrong);
    const auto r = session.check(plan.expand());
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.reasons.at(0), "04.02: model 1's 'Knives and Fists' has 3 attacks but 2 are declared");
}

TEST(M12_CasualtyPlan, ReordersThePriority) {
    const GameState gs = wh40k::test::load_v1_scenario();
    CasualtyPlan plan(gs, gs.units[1].id);
    ASSERT_EQ(plan.priority().size(), 10u);
    const ModelId first = plan.priority()[0];
    const ModelId second = plan.priority()[1];
    plan.move_down(0);
    EXPECT_EQ(plan.priority()[0], second);
    EXPECT_EQ(plan.priority()[1], first);
    plan.move_up(1);
    EXPECT_EQ(plan.priority()[0], first);
    plan.move_up(0);  // no-op at the top
    EXPECT_EQ(plan.action().priority, plan.priority());
}

// ==== M12 acceptance (logic-level proxy): a full game through the UI layer only ====

namespace {

// The direction from `from`'s models to `to`'s, as a unit vector on the battlefield plane.
std::pair<float, float> direction(const Unit& from, const Unit& to) {
    auto centre = [](const Unit& u) {
        float x = 0, y = 0;
        for (const Model& m : u.models) { x += m.position.x; y += m.position.y; }
        return std::pair<float, float>{x / static_cast<float>(u.models.size()), y / static_cast<float>(u.models.size())};
    };
    const auto [ax, ay] = centre(from);
    const auto [bx, by] = centre(to);
    const float len = std::hypot(bx - ax, by - ay);
    return {(bx - ax) / len, (by - ay) / len};
}

const Unit* enemy_of(const GameState& gs, const Unit& u) {
    for (const Unit& o : gs.units) {
        if (o.side != u.side && !o.models.empty()) return &o;
    }
    return nullptr;
}

// What a player clicking through the UI does: buttons from the decision view, editor defaults, and the
// "Move unit" handle. Never builds an action any other way.
GameAction choose(const GameSession& s, int& move_tries) {
    const GameState& gs = s.state();
    const DecisionView view = make_decision_view(s.legal());
    const auto& kind = s.pending()->kind;
    auto button = [&](auto pred) -> std::optional<GameAction> {
        for (const auto& b : view.buttons) {
            if (pred(b)) return b;
        }
        return std::nullopt;
    };
    if (std::holds_alternative<decision::MoveUnits>(kind)) {
        // Normal move towards the enemy while far away; once a move has been reverted, remain stationary.
        if (move_tries == 0) {
            if (auto b = button([](const GameAction& a) {
                    const auto* m = std::get_if<action::StartMove>(&a);
                    return m != nullptr && m->move_type == MoveType::Normal;
                })) {
                const Unit& u = *gs.find_unit(std::get<action::StartMove>(*b).unit);
                const Unit* e = enemy_of(gs, u);
                if (e != nullptr && mm_to_inches(*unit_distance(u, *e)) > 8.0f) return *b;
            }
        }
        return view.buttons.front();  // Remain Stationary (or the only option)
    }
    if (std::holds_alternative<decision::MoveModels>(kind) && gs.flow.move) {
        // Propose every unmoved model once with the "Move unit" handle (2" towards the enemy, or the full
        // charge roll for a charge), then commit.
        const Unit& u = *gs.find_unit(gs.flow.move->unit);
        if (gs.flow.move->paths.size() < u.models.size()) {
            const Unit* e = enemy_of(gs, u);
            float dist = in(2);
            if (gs.flow.move->type == MoveType::Charge) {
                dist = std::max(0.0f, *unit_distance(u, *e) - in(1));
            }
            const auto [dx, dy] = direction(u, *e);
            for (const auto& a : translate_unit(gs, u.id, dx * dist, dy * dist)) {
                if (std::none_of(gs.flow.move->paths.begin(), gs.flow.move->paths.end(),
                                 [&](const ModelPath& p) { return p.model == a.model; })) {
                    return a;
                }
            }
        }
        ++move_tries;
        return action::CommitUnitMove{};
    }
    if (std::holds_alternative<decision::SelectChargeTargets>(kind)) {
        return move_tries > 0 ? view.buttons.back() : view.buttons.front();  // charge once, then skip
    }
    for (const auto& t : view.templates) {
        switch (*editor_kind(t)) {
            case EditorKind::Shot: {
                const auto& sh = std::get<action::Shoot>(t);
                const action::Shoot a = ShotPlan(gs, sh.unit, sh.type).expand();
                if (!a.shots.empty()) return a;
                break;
            }
            case EditorKind::Melee: return MeleePlan(gs, std::get<action::Fight>(t).unit).expand();
            case EditorKind::Casualties: return CasualtyPlan(gs, std::get<action::OrderCasualties>(t).target).action();
            case EditorKind::Path: break;
        }
    }
    // Prefer shooting/charging/fighting over passing when offered.
    if (auto b = button([](const GameAction& a) {
            return std::holds_alternative<action::DeclareCharge>(a) || std::holds_alternative<action::SelectToFight>(a);
        })) {
        return *b;
    }
    return view.buttons.front();
}

}  // namespace

TEST(M12_FullGame, AGameCanBePlayedFromRoundOneToGameOverThroughTheUiLayer) {
    int finished = 0;
    std::map<std::string, int> kinds;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        GameState initial = wh40k::test::load_v1_scenario();
        initial.dice.seed = seed;
        GameSession session(initial);
        int move_tries = 0;
        Stage last_stage = session.state().flow.stage;
        for (int step = 0; step < 3000 && session.pending(); ++step) {
            if (session.state().flow.stage != last_stage &&
                !std::holds_alternative<decision::MoveModels>(session.pending()->kind) &&
                !std::holds_alternative<decision::SelectChargeTargets>(session.pending()->kind)) {
                move_tries = 0;
                last_stage = session.state().flow.stage;
            }
            const GameAction a = choose(session, move_tries);
            const auto r = session.submit(a);
            ASSERT_TRUE(r.ok()) << "seed " << seed << ": " << action_name(a) << ": " << r.reasons.at(0);
        }
        ASSERT_TRUE(session.state().result.has_value()) << "seed " << seed << " did not finish";
        ++finished;
        for (const auto& step : session.history()) ++kinds[std::string(action_name(step.action))];
    }
    EXPECT_EQ(finished, 8);
    // The games used the editors and the board, not just the pass buttons.
    for (const char* k : {"ProposeModelMove", "CommitUnitMove", "Shoot", "DeclareCharge", "StartChargeMove",
                          "SelectToFight", "Fight", "OrderCasualties"}) {
        EXPECT_GT(kinds[k], 0) << k;
    }
}

// ==== M15: rings only when relevant, and the phase bar (D-028) ====

namespace {

std::size_t count_rings(const std::vector<Ring>& rings, RingKind kind) {
    return static_cast<std::size_t>(
        std::count_if(rings.begin(), rings.end(), [&](const Ring& r) { return r.kind == kind; }));
}

}  // namespace

TEST(M15_OverlayPlan, NoRingsWhileSelectingAUnitToMove) {
    const GameSession session(wh40k::test::load_v1_scenario());
    ASSERT_TRUE(std::holds_alternative<decision::MoveUnits>(session.pending()->kind));
    EXPECT_TRUE(plan_overlays(session.state(), session.state().units[0].models[0].id, OverlayToggles{}).empty());
}

TEST(M15_OverlayPlan, MovingAUnitShowsEngagementAroundEnemiesAndCoherencyAroundTheMover) {
    GameSession session(wh40k::test::load_v1_scenario());
    const Unit& marines = session.state().units[0];
    ASSERT_TRUE(session.submit(action::StartMove{marines.id, MoveType::Normal, std::nullopt}).ok());
    const auto all = plan_overlays(session.state(), std::nullopt, OverlayToggles{});
    EXPECT_EQ(count_rings(all, RingKind::MoveRange), 5u);    // every Intercessor: M 6"
    EXPECT_EQ(count_rings(all, RingKind::Engagement), 10u);  // every Termagant
    EXPECT_EQ(count_rings(all, RingKind::Coherency), 5u);    // every Intercessor
    EXPECT_EQ(all.size(), 20u);
    for (const Ring& r : all) {
        if (r.kind != RingKind::MoveRange) continue;
        EXPECT_FLOAT_EQ(r.distance_mm, in(6));
        EXPECT_TRUE(r.centre.has_value());  // the start position
    }
    OverlayToggles no_move;
    no_move.move_range = false;
    const auto rings = plan_overlays(session.state(), std::nullopt, no_move);
    EXPECT_EQ(rings.size(), 15u);
    for (const Ring& r : rings) {
        const bool gant = r.model.v >= session.state().units[1].models.front().id.v;
        EXPECT_EQ(gant, r.kind == RingKind::Engagement);
        EXPECT_FLOAT_EQ(r.distance_mm, in(2));
    }
    // Each ring can be switched off.
    OverlayToggles off = no_move;
    off.engagement = false;
    EXPECT_EQ(plan_overlays(session.state(), std::nullopt, off).size(), 5u);
    off.coherency = false;
    EXPECT_TRUE(plan_overlays(session.state(), std::nullopt, off).empty());
}

TEST(M15_OverlayPlan, WeaponRangesOnlyForTheDecidingSidesSelectedModelWhileChoosingShots) {
    const GameState gs = wh40k::test::shoot_step_state();
    ASSERT_TRUE(std::holds_alternative<decision::Shoot>(gs.pending->kind));
    const auto rings = plan_overlays(gs, gs.units[0].models[0].id, OverlayToggles{});
    ASSERT_EQ(rings.size(), 2u);  // Bolt Rifle 24" (both profiles) and Bolt pistol 12"
    EXPECT_EQ(count_rings(rings, RingKind::WeaponRange), 2u);
    EXPECT_FLOAT_EQ(rings[0].distance_mm, in(24));
    EXPECT_FLOAT_EQ(rings[1].distance_mm, in(12));
    EXPECT_TRUE(plan_overlays(gs, gs.units[1].models[0].id, OverlayToggles{}).empty());  // an enemy model
    EXPECT_TRUE(plan_overlays(gs, std::nullopt, OverlayToggles{}).empty());               // nothing selected
}

namespace {

// Side A's charge with a roll of 12, at the Select Charge Targets decision (Termagants 3" closer).
GameState charging_state() {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (!(gs.flow.stage == Stage::Charge && turn_side(gs) == Side::A)) {
        apply_action(gs, wh40k::test::decider(gs), legal_actions(gs, wh40k::test::decider(gs)).front());
    }
    for (auto& m : gs.units[1].models) m.position.y -= in(3);
    ScriptedDice dice{6, 6};
    apply_action(gs, Side::A, action::DeclareCharge{gs.units[0].id}, dice);
    return gs;
}

}  // namespace

TEST(M15_OverlayPlan, TheChargeRollIsShownOnlyWhileTheChargeIsMade) {
    GameState gs = charging_state();
    ASSERT_TRUE(std::holds_alternative<decision::SelectChargeTargets>(gs.pending->kind));
    const auto rings = plan_overlays(gs, std::nullopt, OverlayToggles{});
    EXPECT_EQ(count_rings(rings, RingKind::ChargeDistance), 5u);
    EXPECT_FLOAT_EQ(rings.front().distance_mm, in(12));  // the charge roll (6 + 6)
    OverlayToggles off;
    off.charge = false;
    EXPECT_TRUE(plan_overlays(gs, std::nullopt, off).empty());
    // Once a charge has been resolved the roll no longer matters: no ring.
    const GameState done = wh40k::test::after_successful_charge();
    ASSERT_EQ(done.flow.stage, Stage::Charge);
    EXPECT_EQ(count_rings(plan_overlays(done, std::nullopt, OverlayToggles{}), RingKind::ChargeDistance), 0u);
}

TEST(M15_OverlayPlan, TheChargeDistanceStaysAtTheStartWhileAModelIsDragged) {
    GameState gs = charging_state();
    apply_action(gs, Side::A, action::StartChargeMove{gs.units[0].id, {gs.units[1].id}});
    const Model& first = gs.units[0].models[0];
    const Position start = first.position;
    apply_action(gs, Side::A, action::ProposeModelMove{first.id, {Position{start.x, start.y + in(5), 0}}});
    OverlayToggles only_charge;
    only_charge.engagement = false;
    only_charge.coherency = false;
    const auto rings = plan_overlays(gs, std::nullopt, only_charge);
    ASSERT_EQ(rings.size(), 5u);
    const auto circles = ring_circles(gs, rings, {{first.id, Position{0, 0, 0}}});  // a drag preview
    EXPECT_EQ(circles[0].centre, start);  // where the charge move started, not where the model is now
    EXPECT_FLOAT_EQ(circles[0].radius_mm, first.base.diameter_mm * 0.5f + in(12));
}

TEST(M15_PhaseTrack, TheStartOfTheGameIsSideAsMovementPhase) {
    const GameSession session(wh40k::test::load_v1_scenario());
    const PhaseTrack t = phase_track(session.state());
    EXPECT_EQ(t.round, 1);
    EXPECT_EQ(t.rounds, 5);
    EXPECT_EQ(t.turn, Side::A);
    ASSERT_TRUE(t.phase_index.has_value());
    EXPECT_EQ(kTurnPhases[*t.phase_index], Phase::Movement);
    EXPECT_EQ(t.step, "Move Units (09.02)");
    EXPECT_FALSE(t.waiting_for.has_value());
    EXPECT_FALSE(t.result.has_value());
}

TEST(M15_PhaseTrack, ADecisionForTheOpponentNamesWhoTheGameIsWaitingFor) {
    GameSession session(wh40k::test::after_successful_charge());
    ASSERT_TRUE(session.submit(action::EndChargeStep{}).ok());
    ASSERT_TRUE(session.submit(action::EndPileIns{}).ok());  // Side A's pile-ins are over: Side B piles in
    const PhaseTrack t = phase_track(session.state());
    EXPECT_EQ(t.turn, Side::A);
    EXPECT_EQ(kTurnPhases[*t.phase_index], Phase::Fight);
    EXPECT_EQ(t.step, "Pile In (12.02)");
    EXPECT_EQ(t.waiting_for, Side::B);
}

TEST(M15_PhaseTrack, AFinishedGameShowsItsResult) {
    GameState gs = wh40k::test::load_v1_scenario();
    gs.result = GameResult{Side::A, "D-007: Side B has no models on the battlefield; Side A wins"};
    const PhaseTrack t = phase_track(gs);
    ASSERT_TRUE(t.result.has_value());
    EXPECT_EQ(*t.result, "D-007: Side B has no models on the battlefield; Side A wins");
    EXPECT_FALSE(t.turn.has_value());
}

TEST(M15_PhaseTrack, StageNamesAreSpacedForDisplay) {
    EXPECT_EQ(spaced("MoveUnits"), "Move Units");
    EXPECT_EQ(spaced("StartOfBattleRound"), "Start Of Battle Round");
    EXPECT_EQ(spaced("Shoot"), "Shoot");
}

// ==== M15: merged ring outlines and rings at drag previews (D-028) ====

namespace {

constexpr float kPi = 3.14159265f;

Circle circle_at(float x, float y, float r, RingKind kind = RingKind::Engagement) {
    return Circle{Position{x, y, 0.0f}, r, kind};
}

float sweep(const Arc& a) { return a.end_rad - a.start_rad; }

float total_sweep(const std::vector<Arc>& arcs, float centre_x) {
    float total = 0.0f;
    for (const Arc& a : arcs) {
        if (std::abs(a.centre.x - centre_x) < 1e-3f) total += sweep(a);
    }
    return total;
}

}  // namespace

TEST(M15_RingOutline, CirclesApartAreDrawnWhole) {
    const auto arcs = outline_arcs({circle_at(0, 0, 10), circle_at(25, 0, 10)});
    ASSERT_EQ(arcs.size(), 2u);
    for (const Arc& a : arcs) EXPECT_NEAR(sweep(a), 2 * kPi, 1e-4f);
}

TEST(M15_RingOutline, OverlappingCirclesEachLoseTheArcFacingTheOther) {
    // Equal circles 10 apart, radius 10: each loses 60 degrees either side of the line between them.
    const auto arcs = outline_arcs({circle_at(0, 0, 10), circle_at(10, 0, 10)});
    ASSERT_EQ(arcs.size(), 2u);
    EXPECT_NEAR(sweep(arcs[0]), 2 * kPi - 2 * kPi / 3, 1e-4f);
    EXPECT_NEAR(arcs[0].start_rad, kPi / 3, 1e-4f);      // the left circle's rim starts 60 degrees up
    EXPECT_NEAR(arcs[0].end_rad, 2 * kPi - kPi / 3, 1e-4f);
    EXPECT_NEAR(sweep(arcs[1]), 2 * kPi - 2 * kPi / 3, 1e-4f);
    // The right circle's covered arc faces 180 degrees, so its outline runs from -120 to +120 degrees.
    EXPECT_NEAR(arcs[1].start_rad, 2 * kPi - 2 * kPi / 3, 1e-4f);
    EXPECT_NEAR(arcs[1].end_rad, 2 * kPi + 2 * kPi / 3, 1e-4f);
}

TEST(M15_RingOutline, ACircleInsideAnotherIsNotDrawn) {
    const auto arcs = outline_arcs({circle_at(0, 0, 3), circle_at(1, 0, 10)});
    ASSERT_EQ(arcs.size(), 1u);
    EXPECT_NEAR(arcs[0].radius_mm, 10.0f, 1e-4f);
}

TEST(M15_RingOutline, TheMiddleOfThreeCirclesKeepsOnlyItsTopAndBottom) {
    const auto arcs = outline_arcs({circle_at(0, 0, 10), circle_at(10, 0, 10), circle_at(20, 0, 10)});
    EXPECT_NEAR(total_sweep(arcs, 10), 2 * kPi - 4 * kPi / 3, 1e-4f);  // two 60-degree arcs
    std::size_t middle = 0;
    for (const Arc& a : arcs) middle += std::abs(a.centre.x - 10) < 1e-3f ? 1 : 0;
    EXPECT_EQ(middle, 2u);
}

TEST(M15_RingOutline, DifferentKindsAreNotMergedAndDuplicatesCountOnce) {
    const auto kinds = outline_arcs({circle_at(0, 0, 10), circle_at(10, 0, 10, RingKind::Coherency)});
    ASSERT_EQ(kinds.size(), 2u);
    for (const Arc& a : kinds) EXPECT_NEAR(sweep(a), 2 * kPi, 1e-4f);
    const auto dupes = outline_arcs({circle_at(0, 0, 10), circle_at(0, 0, 10)});
    ASSERT_EQ(dupes.size(), 1u);
    EXPECT_NEAR(sweep(dupes[0]), 2 * kPi, 1e-4f);
}

TEST(M15_RingOutline, TheTermagantsEngagementRangeIsOneBorder) {
    // Two touching ranks of five (D-008): no arc of any model's ring lies inside another's.
    GameSession session(wh40k::test::load_v1_scenario());
    ASSERT_TRUE(session.submit(action::StartMove{session.state().units[0].id, MoveType::Normal, std::nullopt}).ok());
    const auto circles = ring_circles(session.state(), plan_overlays(session.state(), std::nullopt, OverlayToggles{}));
    const auto arcs = outline_arcs(circles);
    for (const Arc& a : arcs) {
        const float mid = (a.start_rad + a.end_rad) * 0.5f;
        const Position p{a.centre.x + a.radius_mm * std::cos(mid), a.centre.y + a.radius_mm * std::sin(mid), 0};
        for (const Circle& c : circles) {
            if (c.kind != a.kind) continue;
            EXPECT_GE(std::hypot(p.x - c.centre.x, p.y - c.centre.y), c.radius_mm - 1e-2f);
        }
    }
    // The centre Termagant of the front rank is hemmed in left, right and behind: most of its ring is hidden.
    const Position gant = session.state().units[1].models[2].position;
    float shown = 0.0f;
    for (const Arc& a : arcs) {
        if (a.kind == RingKind::Engagement && std::abs(a.centre.x - gant.x) < 1e-3f &&
            std::abs(a.centre.y - gant.y) < 1e-3f) {
            shown += sweep(a);
        }
    }
    EXPECT_GT(shown, 0.0f);
    EXPECT_LT(shown, kPi);
}

TEST(M15_RingCircles, AnOverrideMovesOnlyThatModelsCircle) {
    GameSession session(wh40k::test::load_v1_scenario());
    const Unit& marines = session.state().units[0];
    ASSERT_TRUE(session.submit(action::StartMove{marines.id, MoveType::Normal, std::nullopt}).ok());
    OverlayToggles two_inch;
    two_inch.move_range = false;
    const auto rings = plan_overlays(session.state(), std::nullopt, two_inch);
    const ModelId dragged = marines.models[0].id;
    const Position preview{100.0f, 100.0f, 0.0f};
    const auto circles = ring_circles(session.state(), rings, {{dragged, preview}});
    ASSERT_EQ(circles.size(), rings.size());
    for (std::size_t i = 0; i < rings.size(); ++i) {
        const Model* m = nullptr;
        for (const Unit& u : session.state().units) {
            for (const Model& x : u.models) {
                if (x.id == rings[i].model) m = &x;
            }
        }
        ASSERT_NE(m, nullptr);
        EXPECT_FLOAT_EQ(circles[i].radius_mm, m->base.diameter_mm * 0.5f + in(2));  // edge to edge (01.04)
        EXPECT_EQ(circles[i].centre, rings[i].model == dragged ? preview : m->position);
    }
}

TEST(M15_OverlayPlan, TheMovementRangeStaysAtTheStartWhileTheModelIsDragged) {
    GameSession session(wh40k::test::load_v1_scenario());
    const Unit& marines = session.state().units[0];
    ASSERT_TRUE(session.submit(action::StartMove{marines.id, MoveType::Normal, std::nullopt}).ok());
    const Model& first = session.state().units[0].models[0];
    const Position start = first.position;
    ASSERT_TRUE(session.submit(action::ProposeModelMove{first.id, {Position{start.x, start.y + in(3), 0}}}).ok());
    OverlayToggles only_move;
    only_move.engagement = false;
    only_move.coherency = false;
    const auto rings = plan_overlays(session.state(), std::nullopt, only_move);
    ASSERT_EQ(rings.size(), 5u);
    // Its bubble is still round where it started, M 6" out from the edge of its base, even with a preview.
    const auto circles = ring_circles(session.state(), rings, {{first.id, Position{0, 0, 0}}});
    EXPECT_EQ(circles[0].centre, start);
    EXPECT_FLOAT_EQ(circles[0].radius_mm, first.base.diameter_mm * 0.5f + in(6));
}

TEST(M15_OverlayPlan, AChargeMoveShowsTheChargeDistanceNotAMovementRange) {
    GameState gs = charging_state();
    apply_action(gs, Side::A, action::StartChargeMove{gs.units[0].id, {gs.units[1].id}});
    ASSERT_TRUE(std::holds_alternative<decision::MoveModels>(gs.pending->kind));
    const auto rings = plan_overlays(gs, std::nullopt, OverlayToggles{});
    EXPECT_EQ(count_rings(rings, RingKind::MoveRange), 0u);
    EXPECT_EQ(count_rings(rings, RingKind::ChargeDistance), 5u);
}

TEST(M15_RingText, EveryRingHasANameAndAnExplanation) {
    for (const RingKind k : {RingKind::MoveRange, RingKind::Engagement, RingKind::Coherency, RingKind::WeaponRange,
                             RingKind::ChargeDistance}) {
        EXPECT_FALSE(ring_name(k).empty());
        EXPECT_FALSE(ring_meaning(k).empty());
    }
    EXPECT_NE(ring_meaning(RingKind::Engagement).find("03.04"), std::string_view::npos);
    EXPECT_NE(ring_meaning(RingKind::Coherency).find("03.03"), std::string_view::npos);
}

// ==== M16: click-on-the-board interaction (D-029) ====

namespace {

template <typename T>
bool has_action(const std::vector<GameAction>& actions) {
    return std::any_of(actions.begin(), actions.end(), [](const GameAction& a) { return std::holds_alternative<T>(a); });
}

}  // namespace

TEST(M16_BoardInteraction, AtTheStartOnlyTheIntercessorsAreClickableAndOfferTheirMoveTypes) {
    GameSession session(wh40k::test::load_v1_scenario());
    BoardInteraction board(session.state(), session.legal());
    const UnitId marines = session.state().units[0].id;
    const UnitId gants = session.state().units[1].id;
    ASSERT_TRUE(board.active());
    EXPECT_TRUE(board.clickable(marines));
    EXPECT_FALSE(board.clickable(gants));
    EXPECT_TRUE(board.greyed(gants));
    EXPECT_FALSE(board.greyed(marines));
    EXPECT_EQ(board.hint(session.state()), "Click a unit to move.");
    EXPECT_TRUE(board.waiting_for_click());
    EXPECT_TRUE(board.step_buttons().empty());
    // Remain stationary is the end-phase button, not a menu entry.
    const auto end = board.end_phase();
    ASSERT_TRUE(end.has_value());
    EXPECT_EQ(end->label, "End Movement phase (the rest remain stationary)");
    EXPECT_EQ(end->actions,
              (std::vector<GameAction>{action::StartMove{marines, MoveType::RemainStationary, std::nullopt}}));
    EXPECT_FALSE(board.click(session.state(), gants).submit.has_value());

    const ClickResult r = board.click(session.state(), marines);
    EXPECT_FALSE(r.submit.has_value());
    ASSERT_EQ(r.menu.size(), 2u);  // Normal and Advance, in legal order
    EXPECT_EQ(r.menu[0].label, "Normal: Intercessor Squad (1)");
    EXPECT_EQ(r.menu[1].label, "Advance: Intercessor Squad (1)");
    const ClickResult chosen = board.choose(session.state(), r.menu[0].action);
    ASSERT_TRUE(chosen.submit.has_value());
    ASSERT_TRUE(session.submit(*chosen.submit).ok());
    EXPECT_TRUE(std::holds_alternative<decision::MoveModels>(session.pending()->kind));

    const BoardInteraction moving(session.state(), session.legal());
    EXPECT_TRUE(moving.clickable(marines));
    EXPECT_FALSE(moving.clickable(gants));
    EXPECT_FALSE(moving.waiting_for_click());  // dragging, not clicking: nothing is grey
    EXPECT_FALSE(moving.greyed(gants));
    EXPECT_FALSE(moving.greyed(marines));
    EXPECT_FALSE(moving.end_phase().has_value());
    const auto steps = moving.step_buttons();
    EXPECT_EQ(steps, (std::vector<GameAction>{action::CommitUnitMove{}, action::ResetUnitMove{}}));
}

TEST(M16_BoardInteraction, ShootingIsAClickOnTheShooterThenTheTargetThenAWeapon) {
    GameSession session(wh40k::test::shoot_step_state());
    const GameState& gs = session.state();
    const UnitId marines = gs.units[0].id;
    const UnitId gants = gs.units[1].id;
    BoardInteraction board(gs, session.legal());
    EXPECT_FALSE(board.clickable(gants));  // no shooter yet
    EXPECT_TRUE(board.step_buttons().empty());
    EXPECT_EQ(board.end_phase()->label, "End Shooting phase");
    EXPECT_EQ(board.end_phase()->actions, (std::vector<GameAction>{action::EndShootStep{}}));
    EXPECT_TRUE(board.prompt(gs).empty());
    EXPECT_FALSE(board.picking_target());

    EXPECT_TRUE(board.click(gs, marines).menu.empty());  // one shooting type: the shooter is picked at once
    EXPECT_EQ(board.actor(), marines);
    EXPECT_EQ(board.prompt(gs), "Pick a target for the Intercessor Squad");
    EXPECT_TRUE(board.picking_target());
    EXPECT_TRUE(board.waiting_for_click());
    ASSERT_NE(board.shot_plan(), nullptr);
    EXPECT_TRUE(board.clickable(gants));

    const ClickResult r = board.click(gs, gants);
    ASSERT_EQ(r.menu.size(), 4u);  // the weapon choices, then "Custom..." (D-031)
    EXPECT_EQ(r.menu[0].label, "Bolt Rifle - Focused Fire: 5 models");
    EXPECT_EQ(r.menu[1].label, "Bolt Rifle - Saturation: 5 models");
    EXPECT_EQ(r.menu[2].label, "Bolt pistol: 5 models");
    EXPECT_EQ(r.menu[3].label, "Custom...");
    EXPECT_TRUE(r.menu[3].custom);
    EXPECT_FALSE(r.menu[0].custom);
    const ClickResult fire = board.choose(gs, r.menu[1].action);
    ASSERT_TRUE(fire.submit.has_value());
    EXPECT_TRUE(session.check(*fire.submit).ok());
    ASSERT_TRUE(session.submit(*fire.submit).ok());
    EXPECT_FALSE(events_of<event::AttackBatchesGathered>(last_events(session)).empty());
}

// D-031: "Custom..." on the target menu opens the per-model editor, starting from the first weapon there.
TEST(M18_CustomShot, CustomEntryOpensTheEditorWithTheFirstWeaponAtTheTarget) {
    const GameState gs = wh40k::test::shoot_step_state();
    const UnitId marines = gs.units[0].id;
    const UnitId gants = gs.units[1].id;
    BoardInteraction board(gs, legal_actions(gs, Side::A));
    (void)board.click(gs, marines);
    const ClickResult r = board.click(gs, gants);
    ASSERT_FALSE(r.menu.empty());
    ASSERT_TRUE(r.menu.back().custom);

    const ClickResult opened = board.choose(gs, r.menu.back());
    EXPECT_FALSE(opened.submit.has_value());
    EXPECT_TRUE(opened.menu.empty());
    EXPECT_TRUE(board.custom_shot());
    EXPECT_FALSE(board.picking_target());  // no target line
    EXPECT_FALSE(board.waiting_for_click());  // nothing grey
    EXPECT_FALSE(board.greyed(gants));
    EXPECT_FALSE(board.end_phase().has_value());  // only Back and Shoot
    EXPECT_TRUE(board.step_buttons().empty());
    EXPECT_EQ(board.prompt(gs), "Custom shot: Intercessor Squad at Termagants");
    EXPECT_FALSE(board.click(gs, gants).submit.has_value());
    EXPECT_TRUE(board.click(gs, gants).menu.empty());

    ShotPlan* plan = board.shot_plan();
    ASSERT_NE(plan, nullptr);
    // Every model starts on the first weapon entry (Focused Fire) at the clicked target.
    EXPECT_EQ(plan->expand(), std::get<action::Shoot>(r.menu.front().action));
    for (const auto& d : plan->expand().shots) EXPECT_EQ(d.target, gants);
}

// The M12 acceptance, now through the custom editor: a mixed declaration is accepted by the engine.
TEST(M18_CustomShot, MixedProfilesAreAcceptedByTheEngine) {
    GameSession session(wh40k::test::shoot_step_state());
    const GameState& gs = session.state();
    const UnitId marines = gs.units[0].id;
    const UnitId gants = gs.units[1].id;
    BoardInteraction board(gs, session.legal());
    (void)board.click(gs, marines);
    const ClickResult r = board.click(gs, gants);
    (void)board.choose(gs, r.menu.back());
    ShotPlan* plan = board.shot_plan();
    ASSERT_NE(plan, nullptr);

    const ModelId first = gs.units[0].models[0].id;
    const ModelId second = gs.units[0].models[1].id;
    const auto first_d = plan->declaration_for(first);
    ASSERT_TRUE(first_d.has_value());
    std::optional<ShotDeclaration> other;
    for (const ShotOption& o : plan->options_for(first)) {
        if (o.declaration.weapon_index == first_d->weapon_index &&
            o.declaration.profile_index != first_d->profile_index) {
            other = o.declaration;  // the other Bolt Rifle profile
        }
    }
    ASSERT_TRUE(other.has_value());
    plan->override_model(first, *other);
    plan->override_model(second, std::nullopt);  // doesn't fire

    const action::Shoot shoot = plan->expand();
    EXPECT_EQ(shoot.shots.size(), gs.units[0].models.size() - 1);
    ASSERT_TRUE(session.submit(shoot).ok());
    EXPECT_FALSE(events_of<event::AttackBatchesGathered>(last_events(session)).empty());
}

TEST(M18_CustomShot, CancelReturnsToPickingATarget) {
    const GameState gs = wh40k::test::shoot_step_state();
    const UnitId marines = gs.units[0].id;
    const UnitId gants = gs.units[1].id;
    BoardInteraction board(gs, legal_actions(gs, Side::A));
    (void)board.click(gs, marines);
    const ClickResult r = board.click(gs, gants);
    (void)board.choose(gs, r.menu.back());
    board.shot_plan()->override_model(gs.units[0].models[0].id, std::nullopt);

    board.cancel();  // Esc or Back
    EXPECT_FALSE(board.custom_shot());
    EXPECT_TRUE(board.picking_target());
    EXPECT_EQ(board.actor(), marines);
    // Nothing customised is kept: the target menu fires every model again.
    const ClickResult again = board.click(gs, gants);
    ASSERT_EQ(again.menu.size(), r.menu.size());
    EXPECT_EQ(again.menu.front().action, r.menu.front().action);
}

namespace {
// A session waiting for the Intercessors' melee declarations (DeclareMeleeAttacks, 04.01).
GameSession declare_melee_session() {
    GameSession session(wh40k::test::after_successful_charge());
    EXPECT_TRUE(session.submit(action::EndChargeStep{}).ok());
    while (session.state().flow.stage == Stage::PileIn) EXPECT_TRUE(session.submit(action::EndPileIns{}).ok());
    EXPECT_TRUE(session.submit(action::SelectToFight{session.state().units[0].id, FightType::Normal}).ok());
    return session;
}
}  // namespace

// D-032: a fight's weapons are picked like a shot's: click the target, then a weapon or Custom...
TEST(M19_CustomMelee, ATargetClickOffersTheMeleeWeaponsThenCustom) {
    GameSession session = declare_melee_session();
    ASSERT_TRUE(std::holds_alternative<decision::DeclareMeleeAttacks>(session.pending()->kind));
    const GameState& gs = session.state();
    const UnitId marines = gs.units[0].id;
    const UnitId gants = gs.units[1].id;
    BoardInteraction board(gs, session.legal());
    EXPECT_TRUE(board.active());
    EXPECT_EQ(board.actor(), marines);
    EXPECT_TRUE(board.picking_target());
    EXPECT_TRUE(board.waiting_for_click());
    EXPECT_TRUE(board.clickable(gants));
    EXPECT_FALSE(board.clickable(marines));
    EXPECT_EQ(board.prompt(gs), "Pick a target for the Intercessor Squad's attacks");
    EXPECT_FALSE(board.end_phase().has_value());
    EXPECT_TRUE(board.step_buttons().empty());

    const ClickResult r = board.click(gs, gants);
    EXPECT_FALSE(r.submit.has_value());
    ASSERT_GE(r.menu.size(), 2u);
    EXPECT_EQ(r.menu.back().label, "Custom...");
    EXPECT_TRUE(r.menu.back().custom);
    EXPECT_FALSE(r.menu.front().custom);
    EXPECT_NE(r.menu.front().label.find(": 5 models"), std::string::npos) << r.menu.front().label;
    const ClickResult fight = board.choose(gs, r.menu.front());
    ASSERT_TRUE(fight.submit.has_value());
    EXPECT_EQ(std::get<action::Fight>(*fight.submit).declarations.size(), 5u);
    EXPECT_TRUE(session.submit(*fight.submit).ok());
}

TEST(M19_CustomMelee, CustomOpensTheEditorAndItsFightIsAccepted) {
    GameSession session = declare_melee_session();
    const GameState& gs = session.state();
    const UnitId gants = gs.units[1].id;
    BoardInteraction board(gs, session.legal());
    const ClickResult r = board.click(gs, gants);
    ASSERT_FALSE(r.menu.empty());
    const ClickResult opened = board.choose(gs, r.menu.back());
    EXPECT_FALSE(opened.submit.has_value());
    EXPECT_TRUE(board.custom_editor());
    EXPECT_TRUE(board.custom_melee());
    EXPECT_FALSE(board.custom_shot());
    EXPECT_FALSE(board.picking_target());
    EXPECT_FALSE(board.waiting_for_click());
    EXPECT_FALSE(board.greyed(gants));
    EXPECT_FALSE(board.end_phase().has_value());
    EXPECT_EQ(board.prompt(gs), "Custom fight: Intercessor Squad at Termagants");

    MeleePlan* plan = board.melee_plan();
    ASSERT_NE(plan, nullptr);
    // Starts on the first menu entry's weapon.
    EXPECT_EQ(plan->expand(), std::get<action::Fight>(r.menu.front().action));
    // One model fights with another of its options, if it has one; the engine accepts the declaration.
    const ModelId m = gs.units[0].models[0].id;
    const auto now = plan->declaration_for(m);
    ASSERT_TRUE(now.has_value());
    for (const MeleeOption& o : plan->options_for(m)) {
        if (!(o.declaration == *now)) {
            plan->override_model(m, o.declaration);
            break;
        }
    }
    const action::Fight fight = plan->expand();
    EXPECT_EQ(fight.declarations.size(), 5u);  // every model that can fight does (D-021)
    ASSERT_TRUE(session.submit(fight).ok());
}

TEST(M19_CustomMelee, CancelReturnsToPickingATarget) {
    GameSession session = declare_melee_session();
    const GameState& gs = session.state();
    const UnitId marines = gs.units[0].id;
    const UnitId gants = gs.units[1].id;
    BoardInteraction board(gs, session.legal());
    const ClickResult r = board.click(gs, gants);
    (void)board.choose(gs, r.menu.back());
    ASSERT_TRUE(board.custom_melee());

    board.cancel();  // Esc or Back
    EXPECT_FALSE(board.custom_editor());
    EXPECT_TRUE(board.picking_target());
    EXPECT_EQ(board.actor(), marines);  // the fighter is fixed by the decision
    const ClickResult again = board.click(gs, gants);
    EXPECT_EQ(again.menu.size(), r.menu.size());
    board.cancel();  // picking a target: the fighter stays
    EXPECT_EQ(board.actor(), marines);
    EXPECT_TRUE(board.picking_target());
}

// ==== M20: dice roll pop-ups (D-033) ====

TEST(M20_DiceCards, AShootingAttackGivesHitThenWoundThenSaveCards) {
    GameSession session(wh40k::test::shoot_step_state());
    const UnitId marines = session.state().units[0].id;
    const UnitId gants = session.state().units[1].id;
    const std::size_t from = session.log().size();
    BoardInteraction board(session.state(), session.legal());
    (void)board.click(session.state(), marines);
    const ClickResult r = board.click(session.state(), gants);
    ASSERT_TRUE(session.submit(r.menu.front().action).ok());  // Focused Fire, every model
    // Resolve the attack: the sidebar's batch, allocation and casualty decisions, first choice each.
    for (int i = 0; i < 50 && session.pending() && !std::holds_alternative<decision::Shoot>(session.pending()->kind);
         ++i) {
        const DecisionView view = make_decision_view(session.legal());
        GameAction a = view.buttons.empty() ? GameAction{} : view.buttons.front();
        for (const auto& t : view.templates) {
            if (const auto* c = std::get_if<action::OrderCasualties>(&t)) a = CasualtyPlan(session.state(), c->target).action();
        }
        ASSERT_TRUE(session.submit(a).ok());
    }

    const std::vector<DiceCard> cards = dice_cards(session.state(), session.log(), from);
    ASSERT_GE(cards.size(), 2u);
    EXPECT_EQ(cards[0].title, "Hit rolls");
    EXPECT_EQ(cards[0].rule, "05.01");
    EXPECT_EQ(cards[0].roller, Side::A);
    EXPECT_EQ(cards[0].purpose, "Intercessor Squad shooting Termagants (Bolt Rifle - Focused Fire)");
    EXPECT_EQ(cards[0].need.rfind("Need 3+ to hit", 0), 0u) << cards[0].need;
    EXPECT_EQ(cards[1].title, "Wound rolls");
    EXPECT_EQ(cards[1].roller, Side::A);

    // The dice match the engine's events exactly.
    std::vector<GameEvent> events;
    for (std::size_t i = from; i < session.log().size(); ++i) {
        events.insert(events.end(), session.log()[i].events.begin(), session.log()[i].events.end());
    }
    const auto hits = events_of<event::HitRollsMade>(events);
    const auto selected = events_of<event::AttackBatchSelected>(events);
    ASSERT_FALSE(hits.empty());
    ASSERT_FALSE(selected.empty());
    ASSERT_EQ(cards[0].dice.size(), hits[0].rolls.rolls.size());
    EXPECT_EQ(static_cast<int>(cards[0].dice.size()), selected[0].attack_dice);
    for (std::size_t i = 0; i < cards[0].dice.size(); ++i) {
        const HitRoll& h = hits[0].rolls.rolls[i];
        EXPECT_EQ(cards[0].dice[i].value, h.unmodified.value_or(0));
        EXPECT_EQ(cards[0].dice[i].pass, h.outcome != HitOutcome::Fail);
        EXPECT_EQ(cards[0].dice[i].critical, h.outcome == HitOutcome::CriticalHit);
    }
    const auto wounds = events_of<event::WoundRollsMade>(events);
    ASSERT_FALSE(wounds.empty());
    EXPECT_EQ(cards[1].need.rfind("Need " + std::to_string(wounds[0].rolls.required) + "+ to wound", 0), 0u);
    const auto saves = events_of<event::SaveRollsMade>(events);
    if (!saves.empty()) {
        ASSERT_GE(cards.size(), 3u);
        EXPECT_EQ(cards[2].title, "Save rolls");
        EXPECT_EQ(cards[2].roller, Side::B);  // the defender rolls saves
        EXPECT_EQ(cards[2].dice.size(), saves[0].rolls.size());
        for (const DieFace& d : cards[2].dice) {
            if (d.value == 1) EXPECT_FALSE(d.pass);  // an unmodified 1 always fails (05.04)
        }
    }
    // Nothing new, nothing shown.
    EXPECT_TRUE(dice_cards(session.state(), session.log(), session.log().size()).empty());
}

TEST(M20_DiceCards, OtherRollsShowTheirDiceAndWhatTheyNeed) {
    const GameState gs = wh40k::test::load_v1_scenario();
    const UnitId marines = gs.units[0].id;
    const UnitId gants = gs.units[1].id;
    BattleShockRoll shock;
    shock.leadership = LeadershipRoll{TwoD6{{3, 2}}, 7, false};
    shock.becomes_battle_shocked = true;
    HazardRolls hazard;
    hazard.rolls = {1, 4, 2};
    hazard.failures = 2;
    hazard.mortal_wounds = 2;
    const std::vector<LogEntry> log{
        LogEntry{"no rolls", {event::CommandPointGained{}}},
        LogEntry{"rolls",
                 {event::BattleShockRolled{gants, shock}, event::ChargeRolled{marines, TwoD6{{4, 5}}, {gants}},
                  event::HazardRollsMade{gants, hazard}, event::AdvanceRolled{marines, 3, false}}}};
    EXPECT_TRUE(dice_cards(gs, log, 0).size() == 4u);
    EXPECT_TRUE(dice_cards(gs, {log[0]}, 0).empty());  // no roll events, no cards

    const std::vector<DiceCard> cards = dice_cards(gs, log, 1);
    ASSERT_EQ(cards.size(), 4u);
    EXPECT_EQ(cards[0].title, "Battle-shock roll");
    EXPECT_EQ(cards[0].roller, Side::B);
    EXPECT_EQ(cards[0].need, "2D6: need 7+ to pass");
    EXPECT_EQ(cards[0].dice, (std::vector<DieFace>{{3, false, false, false}, {2, false, false, false}}));
    EXPECT_EQ(cards[0].result, "5: battle-shocked");

    EXPECT_EQ(cards[1].title, "Charge roll");
    EXPECT_EQ(cards[1].roller, Side::A);
    EXPECT_EQ(cards[1].dice.size(), 2u);
    EXPECT_TRUE(cards[1].dice[0].neutral);
    EXPECT_EQ(cards[1].result, "9\" charge");

    EXPECT_EQ(cards[2].title, "Hazard rolls");
    EXPECT_EQ(cards[2].dice, (std::vector<DieFace>{{1, false, false, false}, {4, true, false, false},
                                                   {2, false, false, false}}));
    EXPECT_EQ(cards[2].result, "2 failures: 2 mortal wounds");

    EXPECT_EQ(cards[3].title, "Advance roll");
    EXPECT_EQ(cards[3].result, "+3\" to Move");
}

TEST(M16_BoardInteraction, ClickingTheShooterAgainPicksAnotherUnit) {
    const GameState gs = wh40k::test::shoot_step_state();
    const auto legal = legal_actions(gs, Side::A);
    BoardInteraction board(gs, legal);
    (void)board.click(gs, gs.units[0].id);
    ASSERT_TRUE(board.actor().has_value());
    (void)board.click(gs, gs.units[0].id);
    EXPECT_FALSE(board.actor().has_value());
    EXPECT_FALSE(board.clickable(gs.units[1].id));
    (void)board.click(gs, gs.units[0].id);
    board.cancel();  // Esc or a click on empty board
    EXPECT_FALSE(board.actor().has_value());
}

TEST(M16_BoardInteraction, AChargeIsAClickOnTheUnitThenOnItsTarget) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (!(gs.flow.stage == Stage::Charge && turn_side(gs) == Side::A)) {
        apply_action(gs, wh40k::test::decider(gs), legal_actions(gs, wh40k::test::decider(gs)).front());
    }
    for (auto& m : gs.units[1].models) m.position.y -= in(3);
    const UnitId marines = gs.units[0].id;
    const UnitId gants = gs.units[1].id;
    BoardInteraction declare(gs, legal_actions(gs, Side::A));
    EXPECT_EQ(declare.hint(gs), "Click a unit to declare a charge with.");
    const ClickResult d = declare.click(gs, marines);
    ASSERT_TRUE(d.submit.has_value());
    EXPECT_EQ(*d.submit, GameAction{action::DeclareCharge{marines}});
    ScriptedDice dice{6, 6};
    apply_action(gs, Side::A, *d.submit, dice);

    BoardInteraction targets(gs, legal_actions(gs, Side::A));
    EXPECT_EQ(targets.actor(), marines);  // given by the decision
    EXPECT_TRUE(targets.picking_target());
    EXPECT_EQ(targets.prompt(gs), "Pick the enemy units to charge");
    EXPECT_FALSE(targets.clickable(marines));
    EXPECT_TRUE(targets.clickable(gants));
    EXPECT_EQ(targets.step_buttons(), (std::vector<GameAction>{action::SkipChargeMove{marines}}));
    const ClickResult c = targets.click(gs, gants);
    ASSERT_TRUE(c.submit.has_value());  // the only option: submitted at once
    EXPECT_EQ(*c.submit, (GameAction{action::StartChargeMove{marines, {gants}}}));
}

TEST(M16_BoardInteraction, AnEngagedUnitPilesInAndFightsWithOneClick) {
    GameSession session(wh40k::test::after_successful_charge());
    ASSERT_TRUE(session.submit(action::EndChargeStep{}).ok());
    const UnitId marines = session.state().units[0].id;
    BoardInteraction pile(session.state(), session.legal());
    EXPECT_EQ(pile.hint(session.state()), "Click a unit to pile in with.");
    EXPECT_TRUE(pile.step_buttons().empty());
    EXPECT_EQ(pile.end_phase()->actions, (std::vector<GameAction>{action::EndPileIns{}}));
    const ClickResult p = pile.click(session.state(), marines);
    ASSERT_TRUE(p.submit.has_value());  // engaged: its pile-in targets are fixed (12.03)
    EXPECT_TRUE(std::holds_alternative<action::StartPileIn>(*p.submit));
    EXPECT_TRUE(session.check(*p.submit).ok());

    while (session.state().flow.stage == Stage::PileIn) ASSERT_TRUE(session.submit(action::EndPileIns{}).ok());
    BoardInteraction fight(session.state(), session.legal());
    EXPECT_EQ(fight.hint(session.state()), "Click a unit to fight with.");
    const ClickResult f = fight.click(session.state(), marines);
    ASSERT_TRUE(f.submit.has_value());  // only a Normal fight is possible: no menu
    EXPECT_EQ(*f.submit, (GameAction{action::SelectToFight{marines, FightType::Normal}}));
}

TEST(M16_BoardInteraction, WithNoUnitLeftToActOnlyTheEndPhaseButtonIsOffered) {
    GameState gs = wh40k::test::shoot_step_state();
    for (auto& m : gs.units[1].models) m.position.y += in(30);  // far out of range (off the board is fine here)
    const auto legal = legal_actions(gs, Side::A);
    ASSERT_EQ(legal, (std::vector<GameAction>{action::EndShootStep{}}));  // no unit can shoot
    const BoardInteraction board(gs, legal);
    EXPECT_TRUE(board.active());
    EXPECT_FALSE(board.waiting_for_click());
    EXPECT_FALSE(board.clickable(gs.units[0].id));
    EXPECT_TRUE(board.greyed(gs.units[0].id));  // no unit is left to act: everything is grey
    EXPECT_TRUE(board.greyed(gs.units[1].id));
    EXPECT_EQ(board.hint(gs), "No units left to shoot with.");
    EXPECT_TRUE(board.step_buttons().empty());
    ASSERT_TRUE(board.end_phase().has_value());
    EXPECT_EQ(board.end_phase()->actions, legal);
}

TEST(M16_BoardInteraction, OtherDecisionsStayInTheSidebarAndNothingIsGrey) {
    GameSession session(wh40k::test::after_successful_charge());
    ASSERT_TRUE(session.submit(action::EndChargeStep{}).ok());
    while (session.state().flow.stage == Stage::PileIn) ASSERT_TRUE(session.submit(action::EndPileIns{}).ok());
    const UnitId marines = session.state().units[0].id;
    ASSERT_TRUE(session.submit(action::SelectToFight{marines, FightType::Normal}).ok());
    ASSERT_TRUE(std::holds_alternative<decision::DeclareMeleeAttacks>(session.pending()->kind));
    // Since D-032 the melee declaration is picked on the board; the attacks that follow (attack batch,
    // allocation order, casualties) stay in the sidebar.
    ASSERT_TRUE(session.submit(MeleePlan(session.state(), marines).expand()).ok());
    ASSERT_TRUE(session.pending().has_value());
    const auto& kind = session.pending()->kind;
    ASSERT_TRUE(std::holds_alternative<decision::SelectAttackBatch>(kind) ||
                std::holds_alternative<decision::DeclareAllocationOrder>(kind) ||
                std::holds_alternative<decision::OrderCasualties>(kind))
        << decision_name(kind);
    const BoardInteraction board(session.state(), session.legal());
    EXPECT_FALSE(board.active());
    EXPECT_FALSE(board.clickable(marines));
    EXPECT_FALSE(board.greyed(marines));  // the sidebar's decisions grey nothing
    EXPECT_FALSE(BoardInteraction().active());  // replay mode
    EXPECT_FALSE(BoardInteraction().greyed(marines));
}

// M16 acceptance (logic-level proxy): full games played with board clicks, menus, step buttons, drags and
// the melee/casualty editors only; no unit button from the sidebar.
namespace {

GameAction choose_on_board(const GameSession& s, int& move_tries) {
    const GameState& gs = s.state();
    BoardInteraction board(gs, s.legal());
    const auto& kind = s.pending()->kind;
    auto step = [&](auto pred) -> std::optional<GameAction> {
        for (const GameAction& a : board.step_buttons()) {
            if (pred(a)) return a;
        }
        return std::nullopt;
    };
    auto friendly_clickable = [&]() -> std::optional<UnitId> {
        for (const Unit& u : gs.units) {
            if (u.side == s.pending()->side && board.clickable(u.id)) return u.id;
        }
        return std::nullopt;
    };
    auto enemy_clickable = [&]() -> std::optional<UnitId> {
        for (const Unit& u : gs.units) {
            if (u.side != s.pending()->side && board.clickable(u.id)) return u.id;
        }
        return std::nullopt;
    };
    if (std::holds_alternative<decision::MoveModels>(kind)) {
        const Unit& u = *gs.find_unit(gs.flow.move->unit);
        if (gs.flow.move->paths.size() < u.models.size()) {
            const Unit* e = enemy_of(gs, u);
            float dist = in(2);
            if (gs.flow.move->type == MoveType::Charge) dist = std::max(0.0f, *unit_distance(u, *e) - in(1));
            const auto [dx, dy] = direction(u, *e);
            for (const auto& a : translate_unit(gs, u.id, dx * dist, dy * dist)) {
                if (std::none_of(gs.flow.move->paths.begin(), gs.flow.move->paths.end(),
                                 [&](const ModelPath& p) { return p.model == a.model; })) {
                    return a;
                }
            }
        }
        ++move_tries;
        return *step([](const GameAction& a) { return std::holds_alternative<action::CommitUnitMove>(a); });
    }
    if (board.active()) {
        if (std::holds_alternative<decision::MoveUnits>(kind)) {
            const UnitId unit = *friendly_clickable();
            const ClickResult r = board.click(gs, unit);
            const Unit& u = *gs.find_unit(unit);
            const Unit* e = enemy_of(gs, u);
            const bool far = e != nullptr && mm_to_inches(*unit_distance(u, *e)) > 8.0f;
            for (const MenuEntry& m : r.menu) {
                const auto* sm = std::get_if<action::StartMove>(&m.action);
                if (move_tries == 0 && far && sm != nullptr && sm->move_type == MoveType::Normal) return m.action;
            }
            return board.end_phase()->actions.front();  // Remain Stationary
        }
        if (std::holds_alternative<decision::Shoot>(kind)) {
            if (const auto shooter = friendly_clickable()) {
                (void)board.click(gs, *shooter);
                if (const auto target = enemy_clickable()) {
                    const ClickResult r = board.click(gs, *target);
                    if (!r.menu.empty()) return *board.choose(gs, r.menu.front().action).submit;
                }
            }
            return board.end_phase()->actions.front();  // End Shooting phase
        }
        if (std::holds_alternative<decision::SelectChargeTargets>(kind) && move_tries > 0) {
            return *step([](const GameAction& a) { return std::holds_alternative<action::SkipChargeMove>(a); });
        }
        if (std::holds_alternative<decision::PileIn>(kind) || std::holds_alternative<decision::Consolidate>(kind)) {
            return board.end_phase()->actions.front();  // End pile-ins / consolidations
        }
        if (std::holds_alternative<decision::DeclareMeleeAttacks>(kind)) {
            // D-032: click the target, then the first melee weapon on the menu.
            const ClickResult r = board.click(gs, *enemy_clickable());
            return r.menu.front().action;
        }
        // Charge, charge targets, fights, overrun pile-ins: click the unit, then a target if needed.
        if (const auto unit = friendly_clickable()) {
            const ClickResult r = board.click(gs, *unit);
            if (r.submit) return *r.submit;
            if (!r.menu.empty()) return r.menu.front().action;
        }
        if (const auto target = enemy_clickable()) {
            const ClickResult r = board.click(gs, *target);
            if (r.submit) return *r.submit;
            if (const auto c = board.confirm()) return *c;
        }
        if (const auto end = board.end_phase()) return end->actions.front();
        return board.step_buttons().front();
    }
    // The sidebar's decisions: the editors and the remaining buttons.
    const DecisionView view = make_decision_view(s.legal());
    for (const auto& t : view.templates) {
        if (const auto* f = std::get_if<action::Fight>(&t)) return MeleePlan(gs, f->unit).expand();
        if (const auto* c = std::get_if<action::OrderCasualties>(&t)) return CasualtyPlan(gs, c->target).action();
    }
    return view.buttons.front();
}

}  // namespace

TEST(M16_FullGame, AGameCanBePlayedToGameOverByClickingOnTheBoard) {
    std::map<std::string, int> kinds;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        GameState initial = wh40k::test::load_v1_scenario();
        initial.dice.seed = seed;
        GameSession session(initial);
        int move_tries = 0;
        Stage last_stage = session.state().flow.stage;
        for (int step = 0; step < 3000 && session.pending(); ++step) {
            if (session.state().flow.stage != last_stage &&
                !std::holds_alternative<decision::MoveModels>(session.pending()->kind) &&
                !std::holds_alternative<decision::SelectChargeTargets>(session.pending()->kind)) {
                move_tries = 0;
                last_stage = session.state().flow.stage;
            }
            const GameAction a = choose_on_board(session, move_tries);
            const auto r = session.submit(a);
            ASSERT_TRUE(r.ok()) << "seed " << seed << ": " << action_name(a) << ": " << r.reasons.at(0);
        }
        ASSERT_TRUE(session.state().result.has_value()) << "seed " << seed << " did not finish";
        for (const auto& step : session.history()) ++kinds[std::string(action_name(step.action))];
    }
    for (const char* k : {"StartMove", "ProposeModelMove", "CommitUnitMove", "Shoot", "DeclareCharge",
                          "StartChargeMove", "SelectToFight", "Fight", "OrderCasualties"}) {
        EXPECT_GT(kinds[k], 0) << k;
    }
}

// ==== M12 acceptance: no rules logic in the client (structural) ====

TEST(M12_NoRulesInTheClient, OnlyTheSessionAndTimelineApplyActions) {
    namespace fs = std::filesystem;
    const fs::path client = fs::path(WH40K_REPO_ROOT) / "src" / "client_2d";
    const std::regex apply(R"(\bapply_action\s*\()");
    const std::regex rules_include(R"(#\s*include\s*"core/rules/)");
    // attack_sequence: the dice pop-ups (D-033) call its read-only 05.04 check_save_roll to colour save dice.
    const std::regex allowed_logic_rules(R"(#\s*include\s*"core/rules/(shooting|fight)(_types)?\.hpp"|#\s*include\s*"core/rules/(move_types|attack_types|attack_sequence)\.hpp")");
    int files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(client)) {
        const auto ext = entry.path().extension();
        if (ext != ".cpp" && ext != ".hpp") continue;
        ++files;
        std::ifstream in_file(entry.path());
        std::stringstream text;
        text << in_file.rdbuf();
        const std::string src = text.str();
        const std::string name = entry.path().filename().string();
        const bool in_logic = entry.path().parent_path().filename() == "logic";
        if (name != "session.cpp" && name != "timeline.cpp") {
            EXPECT_FALSE(std::regex_search(src, apply)) << name << " calls apply_action; use GameSession::submit";
        }
        std::istringstream lines(src);
        for (std::string line; std::getline(lines, line);) {
            if (!std::regex_search(line, rules_include)) continue;
            // D-024: only the logic library uses core's read-only rule queries (and the plain type headers).
            EXPECT_TRUE(in_logic && std::regex_search(line, allowed_logic_rules)) << name << ": " << line;
        }
    }
    EXPECT_GT(files, 10);
}

// ==== M13: save, load, replay export, undo and the seed ====

namespace {

std::filesystem::path temp_file(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("vibehammer_" + name);
}

// From the V1 scenario with `seed`: Side A moves 6" north, skips shooting and declares a charge. Returns
// nullopt if that roll gives no charge target.
std::optional<GameSession> mid_charge(std::uint64_t seed) {
    GameState initial = wh40k::test::load_v1_scenario();
    GameSession s = GameSession::new_game(initial, seed);
    const UnitId marines = s.state().units[0].id;
    if (!s.submit(action::StartMove{marines, MoveType::Normal, std::nullopt}).ok()) return std::nullopt;
    for (const auto& a : translate_unit(s.state(), marines, 0.0f, in(6))) {
        if (!s.submit(a).ok()) return std::nullopt;
    }
    if (!s.submit(action::CommitUnitMove{}).ok() || !s.submit(action::EndShootStep{}).ok()) return std::nullopt;
    if (!s.submit(action::DeclareCharge{marines}).ok()) return std::nullopt;
    if (!s.pending() || !std::holds_alternative<decision::SelectChargeTargets>(s.pending()->kind)) return std::nullopt;
    return s;
}

}  // namespace

TEST(M13_SaveLoad, SavedMidChargeTheLoadedGameResumesAtTheSamePendingDecision) {
    std::optional<GameSession> s;
    for (std::uint64_t seed = 1; !s; ++seed) {
        ASSERT_LT(seed, 100u);
        s = mid_charge(seed);
    }
    const auto path = temp_file("m13_mid_charge.json");
    s->save(path);
    GameSession loaded = GameSession::load(path);  // "quit, load": a fresh session from the file
    std::filesystem::remove(path);

    ASSERT_TRUE(loaded.pending().has_value());
    EXPECT_EQ(*loaded.pending(), *s->pending());
    EXPECT_TRUE(std::holds_alternative<decision::SelectChargeTargets>(loaded.pending()->kind));
    EXPECT_EQ(loaded.state(), s->state());
    EXPECT_EQ(loaded.history(), s->history());
    EXPECT_EQ(loaded.log().size(), s->log().size());
    EXPECT_EQ(loaded.legal(), s->legal());
    // Both continue identically.
    const GameAction next = loaded.legal().front();
    ASSERT_TRUE(loaded.submit(next).ok());
    ASSERT_TRUE(s->submit(next).ok());
    EXPECT_EQ(loaded.state(), s->state());
}

TEST(M13_SaveLoad, ATamperedSaveIsRejected) {
    GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario(), 3);
    ASSERT_TRUE(s.submit(s.legal().front()).ok());
    nlohmann::json j = s.to_save_json();
    j["state"]["units"][0]["models"][0]["wounds_remaining"] = 1;
    EXPECT_THROW((void)GameSession::from_save_json(j), DataError);
    j = s.to_save_json();
    j["steps"][0]["action"] = Json(GameAction{action::EndShootStep{}});
    EXPECT_THROW((void)GameSession::from_save_json(j), DataError);
}

TEST(M13_SaveLoad, ABareGameStateLoadsWithoutHistory) {
    GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario(), 3);
    const GameSession loaded = GameSession::from_save_json(Json(s.state()));
    EXPECT_EQ(loaded.state(), s.state());
    EXPECT_FALSE(loaded.can_undo());
}

TEST(M13_ReplayExport, AnExportedReplayPlaysBackInTheViewerIdentically) {
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario(), seed);
        int move_tries = 0;
        for (int step = 0; step < 400 && s.pending(); ++step) {
            if (std::holds_alternative<decision::MoveUnits>(s.pending()->kind)) move_tries = 0;
            ASSERT_TRUE(s.submit(choose(s, move_tries)).ok());
        }
        const auto path = temp_file("m13_export.json");
        save_replay(s.to_replay("M13 export test"), path);
        const Replay replay = load_replay(path);
        std::filesystem::remove(path);

        const Timeline timeline = Timeline::from_replay(replay, wh40k::test::repo_root(), "export");
        ASSERT_TRUE(timeline.error().empty()) << timeline.error();
        ASSERT_EQ(timeline.size(), s.log().size());
        for (std::size_t i = 0; i < timeline.size(); ++i) {
            EXPECT_EQ(timeline.frame(i).events, s.log()[i].events) << "frame " << i;
        }
        EXPECT_EQ(timeline.frame(timeline.size() - 1).state, s.state());
        EXPECT_EQ(replay_expectation(run_replay(replay, wh40k::test::repo_root()).final_state), replay.expect);
    }
}

TEST(M13_Undo, UndoRebuildsTheGameWithoutTheLastAction) {
    GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario(), 5);
    EXPECT_FALSE(s.can_undo());
    std::vector<GameState> states{s.state()};
    int move_tries = 0;
    for (int i = 0; i < 30 && s.pending(); ++i) {
        ASSERT_TRUE(s.submit(choose(s, move_tries)).ok());
        states.push_back(s.state());
    }
    for (std::size_t i = states.size() - 1; i > 0; --i) {
        ASSERT_TRUE(s.undo());
        EXPECT_EQ(s.state(), states[i - 1]) << "after undoing to step " << i - 1;  // dice included
        EXPECT_EQ(s.history().size(), i - 1);
    }
    EXPECT_FALSE(s.undo());
}

TEST(M13_Seed, TheSeedCanBeOverriddenAtGameStart) {
    const GameState scenario = wh40k::test::load_v1_scenario();
    const GameSession a = GameSession::new_game(scenario, 42);
    const GameSession b = GameSession::new_game(scenario, 42);
    EXPECT_EQ(a.seed(), 42u);
    EXPECT_EQ(a.state(), b.state());
    EXPECT_EQ(GameSession::new_game(scenario, std::nullopt).seed(), scenario.dice.seed);  // the scenario's own
}

// ==== review fixes ====

TEST(M13_SaveLoad, StructurallyInvalidSavesAreDataErrors) {
    // Missing fields are corrupt data (DataError), not raw JSON library exceptions.
    GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario(), 3);
    for (const char* key : {"steps", "initial", "format", "state"}) {
        nlohmann::json j = s.to_save_json();
        j.erase(key);
        EXPECT_THROW((void)GameSession::from_save_json(j), DataError) << "without " << key;
    }
    EXPECT_THROW((void)GameSession::from_save_json(nlohmann::json::array()), DataError);
}

TEST(M12_EventText, MoveDistancesAreRoundedNotTruncated) {
    // 6" of float millimetres can be 5.9999995"; the log must still read 6.00".
    const event::ModelMoveProposed moved{UnitId{1}, ModelId{2}, {}, 152.39999f};
    EXPECT_EQ(format_event(GameEvent{moved}), "[03.01] model 2 moves 6.00\"");
}

// ==== M21: datasheet windows (D-034) ====

namespace {

const DatasheetWeaponRow* find_row(const std::vector<DatasheetWeaponRow>& rows, const std::string& name) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](const DatasheetWeaponRow& r) { return r.name == name; });
    return it == rows.end() ? nullptr : &*it;
}

}  // namespace

TEST(M21_Datasheet, IntercessorStatsAndBoltRifleProfiles) {
    const GameState gs = wh40k::test::load_v1_scenario();
    const DatasheetView v = datasheet_view(gs.units[0]);
    EXPECT_EQ(v.name, "Intercessor Squad");
    EXPECT_EQ(v.side, "Side A");
    EXPECT_EQ(v.models, "5/5 models");
    ASSERT_EQ(v.stats.size(), 2u);  // Intercessor Sergeant x1, Intercessor x4
    EXPECT_EQ(v.stats[0].model_type, "Intercessor Sergeant");
    EXPECT_EQ(v.stats[0].count, 1);
    EXPECT_EQ(v.stats[1].count, 4);
    const DatasheetStatRow& r = v.stats[1];
    EXPECT_EQ(r.move, "6\"");
    EXPECT_EQ(r.toughness, "5");
    EXPECT_EQ(r.save, "3+");
    EXPECT_EQ(r.invuln_save, "-");
    EXPECT_EQ(r.wounds, "2");
    EXPECT_EQ(r.leadership, "6+");
    EXPECT_EQ(r.objective_control, "2");
    EXPECT_EQ(stat_line(gs.units[0].models[1].characteristics), "M 6\"  T 5  Sv 3+  W 2  Ld 6+  OC 2");

    const DatasheetWeaponRow* focused = find_row(v.ranged, "Bolt Rifle - Focused Fire");
    const DatasheetWeaponRow* saturation = find_row(v.ranged, "Bolt Rifle - Saturation");
    ASSERT_NE(focused, nullptr);
    ASSERT_NE(saturation, nullptr);
    EXPECT_EQ(focused->range, "24\"");
    EXPECT_EQ(focused->skill, "3+");
    EXPECT_EQ(focused->armour_penetration, "-1");
    EXPECT_EQ(focused->damage, "2");
    EXPECT_EQ(focused->abilities, "[HEAVY] [RAPID FIRE 1]");
    EXPECT_EQ(saturation->abilities, "[ASSAULT] [RAPID FIRE 1]");
    EXPECT_EQ(focused->carried_by, 5);
    const DatasheetWeaponRow* knives = find_row(v.melee, "Knives and Fists");
    ASSERT_NE(knives, nullptr);
    EXPECT_EQ(knives->range, "Melee");
    EXPECT_EQ(knives->carried_by, 5);
    EXPECT_EQ(v.ranged.size(), 3u);  // two Bolt Rifle profiles and the Bolt pistol: only what is carried
}

TEST(M21_Datasheet, TermagantFleshborerAndKeywords) {
    const GameState gs = wh40k::test::load_v1_scenario();
    const DatasheetView v = datasheet_view(gs.units[1]);
    EXPECT_EQ(v.models, "10/10 models");
    const DatasheetWeaponRow* fleshborer = find_row(v.ranged, "Fleshborer");
    ASSERT_NE(fleshborer, nullptr);
    EXPECT_EQ(fleshborer->abilities, "[ASSAULT]");
    EXPECT_EQ(fleshborer->skill, "4+");
    EXPECT_EQ(fleshborer->carried_by, 10);
    EXPECT_NE(v.keywords.find("Endless Multitude"), std::string::npos);
    EXPECT_EQ(v.faction_keywords, "Tyranids");
}

TEST(M21_Datasheet, AbilityTextHasNoGlyphsTheFontLacksOrMarkup) {
    const GameState gs = wh40k::test::load_v1_scenario();
    for (const Unit& u : gs.units) {
        const DatasheetView v = datasheet_view(u);
        ASSERT_FALSE(v.abilities.empty());
        for (const DatasheetAbility& a : v.abilities) {
            for (const std::string* s : {&a.name, &a.text}) {
                EXPECT_EQ(s->find("\xC2\xA0"), std::string::npos) << *s;
                EXPECT_EQ(s->find("\xE2\x96\xAA"), std::string::npos) << *s;
                EXPECT_EQ(s->find("\xE2\x96\xA0"), std::string::npos) << *s;
                EXPECT_EQ(s->find("**"), std::string::npos) << *s;
                EXPECT_EQ(s->find("^^"), std::string::npos) << *s;
            }
        }
    }
    EXPECT_EQ(clean_ability_text("If **^^Tyranids^^**,\xC2\xA0\n\xE2\x96\xAA Each"), "If Tyranids,\n- Each");
}

TEST(M21_Datasheet, ModelCountFollowsCasualties) {
    GameState gs = wh40k::test::load_v1_scenario();
    gs.units[1].models.pop_back();
    gs.units[1].models.pop_back();
    const DatasheetView v = datasheet_view(gs.units[1]);
    EXPECT_EQ(v.models, "8/10 models");
    EXPECT_EQ(v.stats[0].count, 8);
    EXPECT_EQ(find_row(v.ranged, "Fleshborer")->carried_by, 8);
}

TEST(M21_Datasheet, WeaponAbilityLabels) {
    EXPECT_EQ(weapon_ability_label(ability::Blast{1}), "[BLAST 1]");
    EXPECT_EQ(weapon_ability_label(ability::CloseQuarters{}), "[CLOSE-QUARTERS]");
    EXPECT_EQ(weapon_ability_label(ability::TwinLinked{}), "[TWIN-LINKED]");
    EXPECT_EQ(weapon_ability_label(ability::Unknown{"Lethal Hits"}), "[LETHAL HITS]");
}

// ---- M22: the measuring ruler (D-035) ----

TEST(M22_Measure, SamePointIsZero) {
    EXPECT_EQ(measure_label(wh40k::test::at(5, 5), wh40k::test::at(5, 5)), "0.00\"");
}

TEST(M22_Measure, StraightLineDistanceInInches) {
    EXPECT_NEAR(measure_mm(wh40k::test::at(1, 1), wh40k::test::at(4, 5)), in(5.0f), 0.001f);
    EXPECT_EQ(measure_label(wh40k::test::at(1, 1), wh40k::test::at(4, 5)), "5.00\"");
    EXPECT_EQ(measure_label(wh40k::test::at(0, 0), wh40k::test::at(0, 12.346f)), "12.35\"");
}
