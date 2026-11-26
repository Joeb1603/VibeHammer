// M96 (D-097): the opponent-turn stratagems in the client: the picks and labels of the windows that follow no roll
// (Fire Overwatch, Heroic Intervention, Counteroffensive), the session's declines per stratagem, Undo, the snap shot
// through the shot editor's plan, the cards and log lines, and M95 saves (format 3) loading.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <variant>

#include "action_text.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "decision_view.hpp"
#include "dice_cards.hpp"
#include "event_text.hpp"
#include "saved_game.hpp"
#include "session.hpp"
#include "session_fixtures.hpp"
#include "shot_plan.hpp"
#include "stratagem_picks.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::in;
using wh40k::test::random_session_game;
using wh40k::test::rifle_volley;
using wh40k::test::shoot_step;

namespace {

const std::filesystem::path kLegacyDir = std::filesystem::path(WH40K_REPO_ROOT) / "tests" / "data" / "legacy";

std::size_t declines_in(const std::vector<ReplayStep>& steps) {
    return static_cast<std::size_t>(std::count_if(steps.begin(), steps.end(), [](const ReplayStep& s) {
        return std::holds_alternative<action::DeclineStratagems>(s.action);
    }));
}

// V1 in a session that makes every roll and declines no window: the Intercessors remain stationary, so Side B's Fire
// Overwatch window waits at the end of Side A's Movement phase (round 1: Side B has 1 CP).
GameSession at_fire_overwatch_window() {
    GameSession s(wh40k::test::load_v1_scenario(), {true, true}, {false, false});
    EXPECT_TRUE(s.submit(action::StartMove{s.state().units[0].id, MoveType::RemainStationary, std::nullopt}).ok());
    EXPECT_TRUE(window_pending(s.state()));
    return s;
}

// Side A's Charge step after the Intercessors' charge, with a lone Termagant 5" west of them and Side B with 3 CP, in a
// session that declines no window: ending the step opens Side B's Heroic Intervention window.
GameSession at_heroic_window() {
    GameState gs = wh40k::test::after_successful_charge();
    Position w = gs.units[0].models[0].position;
    for (const Model& m : gs.units[0].models) {
        if (m.position.x < w.x) w = m.position;
    }
    const UnitId lone = wh40k::test::add_unit_copy(gs, 1);
    Unit& u = *gs.find_unit(lone);
    u.models.resize(1);
    u.models[0].position = Position{w.x - 16.0f - 14.25f - in(5.0f), w.y, 0.0f};
    u.turn_flags = UnitTurnFlags{};
    u.phase_flags = UnitPhaseFlags{};
    gs.command_points[1] = 3;
    gs.pending = compute_decision(gs);
    GameSession s(gs, {true, true}, {false, false});
    EXPECT_TRUE(s.submit(action::EndChargeStep{}).ok());
    return s;
}

}  // namespace

// ==== The picks and labels ====

TEST(M96_Picks, FireOverwatchOffersEachUnitAsAButton) {
    GameSession s = at_fire_overwatch_window();
    const StratagemPicks picks(s.state());
    ASSERT_TRUE(picks.active());
    EXPECT_TRUE(picks.reaction_window());
    EXPECT_TRUE(picks.offers(Stratagem::FireOverwatch));
    EXPECT_EQ(picks.headline(), "Fire Overwatch? (15.08)");
    EXPECT_EQ(picks.decline_label(), "No stratagem");
    EXPECT_NE(picks.hint().find("24\""), std::string::npos) << picks.hint();
    EXPECT_NE(picks.hint().find("1 CP"), std::string::npos) << picks.hint();
    const UnitId gants = s.state().units[1].id;
    const std::vector<ReactionChoice> choices = picks.reactions(s.state());
    ASSERT_EQ(choices.size(), 1u);
    EXPECT_EQ(choices[0].label, "Fire Overwatch (1 CP): Termagants");
    EXPECT_EQ(choices[0].unit, gants);
    EXPECT_EQ(choices[0].action, (GameAction{action::UseStratagem{Stratagem::FireOverwatch, gants, std::nullopt}}));
    EXPECT_FALSE(picks.command_reroll().has_value());
    EXPECT_FALSE(picks.insane_bravery().has_value());
    // The engine's legal answers are the decline and these.
    const std::vector<GameAction> legal = s.legal();
    EXPECT_EQ(legal, (std::vector<GameAction>{action::DeclineStratagems{}, choices[0].action}));
    EXPECT_EQ(action_label(s.state(), choices[0].action), "Fire Overwatch (1 CP): Termagants (2) snap shoot");
    EXPECT_EQ(action_label(s.state(), action::DeclineStratagems{}), "No stratagem");
}

TEST(M96_Picks, HeroicInterventionOffersEachModeWithItsCost) {
    GameSession s = at_heroic_window();
    const StratagemPicks picks(s.state());
    ASSERT_TRUE(picks.reaction_window());
    EXPECT_EQ(picks.headline(), "Heroic Intervention? (15.11)");
    EXPECT_NE(picks.hint().find("Into the Fray"), std::string::npos) << picks.hint();
    const std::vector<ReactionChoice> choices = picks.reactions(s.state());
    ASSERT_EQ(choices.size(), 2u);
    EXPECT_EQ(choices[0].label, "Leap to Defend (1 CP): Termagants 2");  // the lone Termagant, the second unit
    EXPECT_EQ(choices[1].label, "Into the Fray (2 CP): Termagants 2");
    EXPECT_EQ(action_label(s.state(), choices[1].action),
              "Heroic Intervention, Into the Fray (2 CP): Termagants 2 (3) charge");
}

TEST(M96_Picks, CounteroffensiveIsOfferedAfterAnEnemyFight) {
    GameState gs = wh40k::test::after_successful_charge();
    gs.command_points[1] = 2;
    gs.pending = compute_decision(gs);
    GameSession s(gs, {true, true}, {false, false});
    ASSERT_TRUE(s.submit(action::EndChargeStep{}).ok());
    while (s.state().flow.stage == Stage::PileIn) ASSERT_TRUE(s.submit(action::EndPileIns{}).ok());
    const UnitId marines = s.state().units[0].id;
    ASSERT_TRUE(s.submit(action::SelectToFight{marines, FightType::Normal}).ok());
    ASSERT_TRUE(s.submit(wh40k::test::first_melee_options(s.state(), marines)).ok());
    // Side A's own windows after its rolls are declined here; Side B's casualties in unit order.
    for (int guard = 0; guard < 40 && s.pending(); ++guard) {
        if (window_pending(s.state()) && s.state().pending->side == Side::B &&
            std::get<decision::UseStratagems>(s.state().pending->kind).timing == WindowTiming::AfterAttacks) {
            break;
        }
        ASSERT_TRUE(s.submit(wh40k::test::neutral_action(s.state())).ok());
    }
    ASSERT_TRUE(window_pending(s.state()));
    const StratagemPicks picks(s.state());
    EXPECT_EQ(picks.headline(), "Counteroffensive? (15.12)");
    const std::vector<ReactionChoice> choices = picks.reactions(s.state());
    ASSERT_EQ(choices.size(), 1u);
    EXPECT_EQ(choices[0].label, "Counteroffensive (2 CP): Termagants");
    EXPECT_EQ(action_label(s.state(), choices[0].action), "Counteroffensive (2 CP): Termagants (2) fight next");
}

// ==== The session: declines per stratagem, Undo ====

TEST(M96_Session, AWindowOfferingOnlyDeclinedStratagemsIsDeclinedByTheSession) {
    GameSession s(wh40k::test::load_v1_scenario(), {true, true}, {false, false});
    s.set_declined_stratagems(Side::B, {Stratagem::FireOverwatch});
    EXPECT_EQ(s.declined_stratagems(Side::B), std::vector<Stratagem>{Stratagem::FireOverwatch});
    ASSERT_TRUE(s.submit(action::StartMove{s.state().units[0].id, MoveType::RemainStationary, std::nullopt}).ok());
    // Side B's Fire Overwatch window was declined at once, a step of its own; Side A shoots next.
    EXPECT_EQ(s.history().back(), (ReplayStep{Side::B, action::DeclineStratagems{}}));
    EXPECT_EQ(s.pending(), (PendingDecision{Side::A, decision::Shoot{}}));
    EXPECT_FALSE(s.declines_window());
    // A window offering a stratagem the side plays waits (Side A's Command Re-roll after its hit rolls).
    s.set_declined_stratagems(Side::A, {Stratagem::FireOverwatch, Stratagem::HeroicIntervention});
    ASSERT_TRUE(s.submit(rifle_volley(s.state())).ok());
    ASSERT_TRUE(window_pending(s.state()));
    EXPECT_EQ(s.state().pending->side, Side::A);
    EXPECT_FALSE(s.declines_window());
    // Undo keeps the lists.
    ASSERT_TRUE(s.undo());
    EXPECT_EQ(s.declined_stratagems(Side::B), std::vector<Stratagem>{Stratagem::FireOverwatch});
}

TEST(M96_Session, UndoTakesBackAFireOverwatchUseToItsWindow) {
    GameSession s = at_fire_overwatch_window();
    const GameState window = s.state();
    ASSERT_TRUE(s.submit(action::UseStratagem{Stratagem::FireOverwatch, s.state().units[1].id, std::nullopt}).ok());
    ASSERT_TRUE(std::holds_alternative<decision::SnapShoot>(s.pending()->kind));
    ASSERT_TRUE(s.undo());
    EXPECT_EQ(s.state(), window);
}

// ==== Snap shooting through the shot editor's plan ====

TEST(M96_SnapShot, TheShotPlanFiresEveryModelAtOneTargetAndTheEngineAcceptsIt) {
    GameSession s = at_fire_overwatch_window();
    const UnitId gants = s.state().units[1].id;
    const UnitId marines = s.state().units[0].id;
    ASSERT_TRUE(s.submit(action::UseStratagem{Stratagem::FireOverwatch, gants, std::nullopt}).ok());
    // The decision's one answer is the snap Shoot template, opened in the shot editor.
    ASSERT_EQ(s.legal(), (std::vector<GameAction>{action::Shoot{gants, ShootingType::Snap, {}}}));
    EXPECT_EQ(editor_kind(s.legal().front()), EditorKind::Shot);
    ShotPlan plan(s.state(), gants, ShootingType::Snap);
    ASSERT_FALSE(plan.choices().empty());
    for (const ShotChoice& c : plan.choices()) EXPECT_EQ(c.target, marines);  // within 24" and eligible only
    plan.set_default(plan.choices().front());
    const action::Shoot shoot = plan.expand();
    EXPECT_EQ(shoot.type, ShootingType::Snap);
    EXPECT_EQ(shoot.shots.size(), s.state().units[1].models.size());
    ASSERT_TRUE(s.submit(shoot).ok());
    EXPECT_TRUE(s.state().flow.attack.has_value());
}

// ==== Cards and log lines ====

TEST(M96_Texts, ASnapHitCardNeedsASixAndItsSixIsNotCritical) {
    GameSession s = at_fire_overwatch_window();
    const UnitId gants = s.state().units[1].id;
    ASSERT_TRUE(s.submit(action::UseStratagem{Stratagem::FireOverwatch, gants, std::nullopt}).ok());
    ShotPlan plan(s.state(), gants, ShootingType::Snap);
    plan.set_default(plan.choices().front());
    const std::size_t from = s.log().size();
    ASSERT_TRUE(s.submit(plan.expand()).ok());
    const std::vector<DiceCard> cards = dice_cards(s.state(), s.log(), from);
    const auto hit = std::find_if(cards.begin(), cards.end(), [](const DiceCard& c) { return c.title == "Hit rolls"; });
    ASSERT_NE(hit, cards.end());
    EXPECT_EQ(hit->need, "Need 6 to hit (snap shooting, 15.09): an unmodified 6, not a critical hit");
    for (const DieFace& d : hit->dice) {
        EXPECT_FALSE(d.critical) << d.value;  // no gold border on a snap 6
        EXPECT_EQ(d.pass, d.value == 6) << d.value;
    }
}

TEST(M96_Texts, TheLogNamesTheModeAndIntoTheFraysCap) {
    EXPECT_EQ(format_event(event::StratagemUsed{Side::B, Stratagem::HeroicIntervention, UnitId{3}, 2, 1,
                                                StratagemMode::IntoTheFray}),
              "[15.01] Side B uses Heroic Intervention (Into the Fray) on unit 3: 2 CP (1 CP left)");
    EXPECT_EQ(format_event(event::StratagemUsed{Side::B, Stratagem::FireOverwatch, UnitId{2}, 1, 0}),
              "[15.01] Side B uses Fire Overwatch on unit 2: 1 CP (0 CP left)");
    const std::string capped = format_event(event::ChargeRolled{UnitId{3}, TwoD6{{4, 5}}, {UnitId{1}}, 6});
    EXPECT_NE(capped.find("= 9\", Into the Fray 6\""), std::string::npos) << capped;
    const std::string plain = format_event(event::ChargeRolled{UnitId{3}, TwoD6{{4, 5}}, {UnitId{1}}});
    EXPECT_EQ(plain.find("Into the Fray"), std::string::npos) << plain;
}

TEST(M96_Texts, TheHeroicChargeCardShowsTheCap) {
    GameSession s = at_heroic_window();
    const UnitId lone = s.state().units.back().id;
    s.set_auto_roll(Side::B, true);
    const std::size_t from = s.log().size();
    ASSERT_TRUE(s.submit(action::UseStratagem{Stratagem::HeroicIntervention, lone, std::nullopt,
                                              StratagemMode::IntoTheFray})
                    .ok());
    const std::vector<DiceCard> cards = dice_cards(s.state(), s.log(), from);
    const auto charge = std::find_if(cards.begin(), cards.end(), [](const DiceCard& c) { return c.title == "Charge roll"; });
    ASSERT_NE(charge, cards.end());
    const auto rolled = std::find_if(s.log().begin() + static_cast<std::ptrdiff_t>(from), s.log().end(), [](const LogEntry& e) {
        return std::any_of(e.events.begin(), e.events.end(),
                           [](const GameEvent& g) { return std::holds_alternative<event::ChargeRolled>(g); });
    });
    ASSERT_NE(rolled, s.log().end());
    const auto& c = std::get<event::ChargeRolled>(*std::find_if(rolled->events.begin(), rolled->events.end(), [](const GameEvent& g) {
        return std::holds_alternative<event::ChargeRolled>(g);
    }));
    if (c.capped) {
        EXPECT_NE(charge->result.find("Into the Fray: a 6\" charge (15.11)"), std::string::npos) << charge->result;
    } else {
        EXPECT_EQ(charge->result.find("Into the Fray"), std::string::npos) << charge->result;
    }
}

// ==== Saves (D-025): format 4; an M95 save (format 3) loads with the new windows declined ====

TEST(M96_Saves, AnM95SaveLoadsWithTheNewWindowsDeclined) {
    // tests/data/legacy/m95_save_format3.json is random_session_game(5, 11, 50) as the M95 engine saved it (format 3).
    const GameSession loaded = GameSession::load(kLegacyDir / "m95_save_format3.json");
    const GameSession again = random_session_game(5, 11, 50);  // the same choices, played now
    EXPECT_EQ(loaded.state(), again.state());
    EXPECT_EQ(loaded.history(), again.history());
    EXPECT_EQ(loaded.to_save_json(), again.to_save_json());
    EXPECT_EQ(loaded.to_save_json().at("format"), kReplayFormatOwnTurnStratagems);  // M97 (D-099): format 5
    EXPECT_GT(loaded.history().size(), 79u);  // its 79 steps and the declines of the new windows
}

TEST(M96_Saves, AnM95SaveWaitingForAWindowStillWaits) {
    // tests/data/legacy/m95_save_waiting_for_a_window_format3.json: Side A's volley, its hit rolls made by the session,
    // its Command Re-roll window waiting (M95_Screens' at_hit_window).
    const GameSession loaded = GameSession::load(kLegacyDir / "m95_save_waiting_for_a_window_format3.json");
    GameSession now(shoot_step(1), {true, true}, {false, false});
    ASSERT_TRUE(now.submit(rifle_volley(now.state())).ok());
    EXPECT_EQ(loaded.state(), now.state());
    EXPECT_EQ(loaded.history(), now.history());
    ASSERT_TRUE(window_pending(loaded.state()));
    EXPECT_EQ(std::get<decision::UseStratagems>(loaded.state().pending->kind).timing, WindowTiming::AfterRoll);
}

TEST(M96_Saves, AnM95SaveAfterAMovementPhaseLoadsWithTheFireOverwatchWindowDeclined) {
    // tests/data/legacy/m95_save_at_shoot_step_format3.json: V1 with the Termagants 1" closer (seed 3), the Intercessors
    // remained stationary, Side A at its Shoot step. Now Side B's Fire Overwatch window comes between, declined by the
    // session as a step of its own.
    const GameSession loaded = GameSession::load(kLegacyDir / "m95_save_at_shoot_step_format3.json");
    GameSession now(loaded.initial());  // the same initial state, played now
    ASSERT_TRUE(now.submit(action::StartMove{now.state().units[0].id, MoveType::RemainStationary, std::nullopt}).ok());
    EXPECT_EQ(loaded.state(), now.state());
    EXPECT_EQ(loaded.history(), now.history());
    ASSERT_EQ(loaded.history().size(), 2u);
    EXPECT_EQ(loaded.history()[1], (ReplayStep{Side::B, action::DeclineStratagems{}}));
    EXPECT_EQ(declines_in(loaded.history()), 1u);
    EXPECT_EQ(loaded.pending(), (PendingDecision{Side::A, decision::Shoot{}}));
}

// ==== Screenshots: saves waiting at each new window ====

// Writes saves for screenshots (docs/dev/client_flags.md). Run on purpose only:
//   WH40K_SCREEN_DIR=<dir> wh40k_client_tests --gtest_also_run_disabled_tests --gtest_filter=M96_Screens.*
TEST(M96_Screens, DISABLED_WriteSavesWaitingForTheNewWindows) {
    const char* dir = std::getenv("WH40K_SCREEN_DIR");
    if (dir == nullptr) GTEST_SKIP() << "set WH40K_SCREEN_DIR";
    const std::filesystem::path out(dir);
    GameSession fo = at_fire_overwatch_window();
    save_game_file(out / "m96_fire_overwatch.json", fo, SavePlayers{});
    // The snap shot's editor, then (after it is declared) its hit roll waiting.
    GameSession snap = fo;
    ASSERT_TRUE(snap.submit(action::UseStratagem{Stratagem::FireOverwatch, snap.state().units[1].id, std::nullopt}).ok());
    save_game_file(out / "m96_snap_shot.json", snap, SavePlayers{});
    GameSession hit(snap.state(), {false, false}, {false, false});
    ShotPlan plan(hit.state(), hit.state().units[1].id, ShootingType::Snap);
    plan.set_default(plan.choices().front());
    ASSERT_TRUE(hit.submit(plan.expand()).ok());
    save_game_file(out / "m96_snap_hit_roll.json", hit, SavePlayers{});
    save_game_file(out / "m96_heroic_intervention.json", at_heroic_window(), SavePlayers{});
    // Counteroffensive: after the Intercessors' fight.
    GameState gs = wh40k::test::after_successful_charge();
    gs.command_points[1] = 2;
    gs.pending = compute_decision(gs);
    GameSession co(gs, {true, true}, {false, false});
    ASSERT_TRUE(co.submit(action::EndChargeStep{}).ok());
    while (co.state().flow.stage == Stage::PileIn) ASSERT_TRUE(co.submit(action::EndPileIns{}).ok());
    const UnitId marines = co.state().units[0].id;
    ASSERT_TRUE(co.submit(action::SelectToFight{marines, FightType::Normal}).ok());
    ASSERT_TRUE(co.submit(wh40k::test::first_melee_options(co.state(), marines)).ok());
    for (int guard = 0; guard < 40 && co.pending(); ++guard) {
        if (window_pending(co.state()) && co.state().pending->side == Side::B &&
            std::get<decision::UseStratagems>(co.state().pending->kind).timing == WindowTiming::AfterAttacks) {
            break;
        }
        ASSERT_TRUE(co.submit(wh40k::test::neutral_action(co.state())).ok());
    }
    save_game_file(out / "m96_counteroffensive.json", co, SavePlayers{});
}
