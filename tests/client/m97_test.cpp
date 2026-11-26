// M97 (D-099): 15.05 Explosives and 15.06 Crushing Impact in the client: the Explosives answers at the Shoot step and
// the session's offered list (legal_offered), the selection's buttons (effect_choices), Crushing Impact's window, the
// cards and log lines, Undo, and M96 saves (format 4) loading.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <variant>

#include "action_text.hpp"
#include "auto_actions.hpp"
#include "board_interaction.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/queries.hpp"
#include "decision_view.hpp"
#include "dice_cards.hpp"
#include "event_text.hpp"
#include "saved_game.hpp"
#include "session.hpp"
#include "session_fixtures.hpp"
#include "stratagem_picks.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"
#include "unit_style.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::in;
namespace mf = wh40k::test::monsters;

namespace {

const std::filesystem::path kLegacyDir = std::filesystem::path(WH40K_REPO_ROOT) / "tests" / "data" / "legacy";

GameAction explosives(UnitId unit) { return action::UseStratagem{Stratagem::Explosives, unit, std::nullopt}; }
GameAction crushing(UnitId unit) { return action::UseStratagem{Stratagem::CrushingImpact, unit, std::nullopt}; }

bool has(const std::vector<GameAction>& legal, const GameAction& a) {
    return std::find(legal.begin(), legal.end(), a) != legal.end();
}

// V1 at Side A's Shoot step (round 1, Side A with `cp_a` CP), the Termagants `gap_in` from the Intercessors.
GameState explosives_state(float gap_in = 6.0f, int cp_a = 1) {
    GameState gs = wh40k::test::shoot_step_state(Side::A);
    const float gap = mm_to_inches(*unit_distance(gs.units[0], gs.units[1]));
    for (Model& m : gs.units[1].models) m.position.y -= in(gap - gap_in);
    gs.command_points[0] = cp_a;
    gs.pending = compute_decision(gs);
    return gs;
}

// A session at that Shoot step that offers stratagems to both sides and makes every roll.
GameSession explosives_session(float gap_in = 6.0f, int cp_a = 1) {
    return GameSession(explosives_state(gap_in, cp_a), {true, true}, {false, false});
}

// The monsters fixture at Side B's Charge decision (round 1, Side B with `cp_b` CP), in a session that offers
// stratagems and makes every roll; then the Hive Tyrant's charge: Crushing Impact's window waits.
GameSession crushing_session(int cp_b = 1) {
    GameState gs = mf::at_movement();
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::Charge>(g.pending->kind) && g.pending->side == Side::B;
    });
    gs.command_points[1] = cp_b;
    gs.pending = compute_decision(gs);
    GameSession s(gs, {true, true}, {false, false});
    const UnitId tyrant = s.state().units[mf::kTyrant].id;
    EXPECT_TRUE(s.submit(action::DeclareCharge{tyrant}).ok());
    // The charge roll is the session's; a Command Re-roll window may follow it (declined here).
    while (window_pending(s.state())) EXPECT_TRUE(s.submit(action::DeclineStratagems{}).ok());
    if (!(s.pending() && std::holds_alternative<decision::SelectChargeTargets>(s.pending()->kind))) {
        ADD_FAILURE() << "the charge reached nothing (seed)";
        return s;
    }
    EXPECT_TRUE(s.submit(action::StartChargeMove{tyrant, {s.state().units[mf::kLegionaries].id}}).ok());
    const Model& t = s.state().units[mf::kTyrant].models[0];
    EXPECT_TRUE(s.submit(action::ProposeModelMove{t.id, {Position{t.position.x, in(mf::kRankY + mf::tyrant_centre_gap(0.5f)),
                                                                   0.0f}}})
                    .ok());
    EXPECT_TRUE(s.submit(action::CommitUnitMove{}).ok());
    EXPECT_TRUE(window_pending(s.state()));
    return s;
}

}  // namespace

// ==== The Shoot step: the Explosives answers and the session's offered list ====

TEST(M97_Session, ExplosivesIsOfferedOnlyToASideThatPlaysIt) {
    GameSession s = explosives_session();
    const UnitId marines = s.state().units[0].id;
    ASSERT_TRUE(has(s.legal(), explosives(marines)));
    EXPECT_TRUE(has(s.legal_offered(), explosives(marines)));
    EXPECT_FALSE(s.declines(Side::A, Stratagem::Explosives));
    // "Offer stratagems" off: every stratagem answer is hidden.
    s.set_auto_decline(Side::A, true);
    EXPECT_TRUE(s.declines(Side::A, Stratagem::Explosives));
    EXPECT_FALSE(has(s.legal_offered(), explosives(marines)));
    EXPECT_EQ(s.legal_offered().size(), s.legal().size() - 1);
    // A bot whose Explosives key is 0: hidden, the rest as it was.
    s.set_auto_decline(Side::A, false);
    s.set_declined_stratagems(Side::A, {Stratagem::Explosives});
    EXPECT_FALSE(has(s.legal_offered(), explosives(marines)));
    s.set_declined_stratagems(Side::A, {Stratagem::CrushingImpact});
    EXPECT_TRUE(has(s.legal_offered(), explosives(marines)));
    // The decision view puts it among the buttons, the Shoot templates among the editors.
    const DecisionView view = make_decision_view(s.legal_offered());
    EXPECT_TRUE(has(view.buttons, explosives(marines)));
    // On the board it is a bar button (no unit to click for it), labelled with its cost.
    const BoardInteraction board(s.state(), s.legal_offered());
    EXPECT_TRUE(has(board.step_buttons(), explosives(marines)));
    EXPECT_EQ(action_label(s.state(), explosives(marines)), "Explosives (1 CP): Intercessor Squad (1)");
}

TEST(M97_Session, TheEmptyStepSkipHoldsForASideThatDeclinesExplosives) {
    // The Intercessors' rifles reach no one (every range 1"): the Shoot step's answers are the end and Explosives.
    GameState gs = explosives_state();
    for (Model& m : gs.units[0].models) {
        for (Weapon& w : m.weapons) {
            for (WeaponProfile& p : w.profiles) {
                if (p.range_mm) p.range_mm = in(1.0f);
            }
        }
    }
    gs.pending = compute_decision(gs);
    GameSession s(gs, {true, true}, {false, false});
    ASSERT_EQ(s.legal(), (std::vector<GameAction>{action::EndShootStep{}, explosives(s.state().units[0].id)}));
    AutoSettings settings;
    settings.skip_empty_steps = true;
    EXPECT_FALSE(next_auto_action(s.state(), s.legal_offered(), settings).has_value());  // it can still use it
    s.set_auto_decline(Side::A, true);
    EXPECT_EQ(next_auto_action(s.state(), s.legal_offered(), settings), (GameAction{action::EndShootStep{}}));
    const std::vector<GameAction> done = run_auto_actions(s, settings, nullptr, 1);
    EXPECT_EQ(done, (std::vector<GameAction>{action::EndShootStep{}}));
}

// ==== The selection: one button per enemy unit ====

TEST(M97_Picks, TheSelectionOffersEachEnemyUnitWithItsFirstLegalModel) {
    GameSession s = explosives_session();
    const UnitId marines = s.state().units[0].id;
    const UnitId gants = s.state().units[1].id;
    EXPECT_TRUE(effect_choices(s.state()).empty());  // not before the stratagem
    ASSERT_TRUE(s.submit(explosives(marines)).ok());
    ASSERT_EQ(s.pending(), (PendingDecision{Side::A, decision::SelectModelAndEnemy{Stratagem::Explosives, marines}}));
    const std::vector<EffectChoice> choices = effect_choices(s.state());
    ASSERT_EQ(choices.size(), 1u);
    EXPECT_EQ(choices[0].enemy, gants);
    EXPECT_EQ(choices[0].action, s.legal().front());  // the first legal pair for that unit
    EXPECT_EQ(choices[0].model, std::get<action::SelectModelAndEnemy>(s.legal().front()).model);
    EXPECT_EQ(choices[0].label, "Termagants (model " + std::to_string(choices[0].model.v) + ")");
    EXPECT_EQ(effect_headline(s.state()), "Explosives: pick the enemy unit (15.05)");
    EXPECT_NE(effect_hint(s.state()).find("six D6, each 4+"), std::string::npos);
    EXPECT_EQ(action_label(s.state(), choices[0].action),
              "Model " + std::to_string(choices[0].model.v) + " at Termagants (2)");
    // M98 (D-100, amending D-099 part 8): the board picks it too, with the same buttons in its bar (sidebar mode in M97).
    const BoardInteraction board(s.state(), s.legal_offered());
    EXPECT_TRUE(board.active());
    EXPECT_TRUE(board.picking_effect());
    EXPECT_EQ(board.effect_buttons(s.state()), choices);
}

TEST(M97_Picks, CrushingImpactsWindowAndSelectionNameTheModelsT) {
    GameSession s = crushing_session(2);
    const UnitId tyrant = s.state().units[mf::kTyrant].id;
    const StratagemPicks picks(s.state());
    ASSERT_TRUE(picks.active());
    EXPECT_TRUE(picks.reaction_window());
    EXPECT_TRUE(picks.offers(Stratagem::CrushingImpact));
    EXPECT_EQ(picks.headline(), "Crushing Impact? (15.06)");
    EXPECT_EQ(picks.decline_label(), "No stratagem");
    EXPECT_NE(picks.hint().find("each 5+"), std::string::npos) << picks.hint();
    EXPECT_NE(picks.hint().find("2 CP"), std::string::npos) << picks.hint();
    const std::vector<ReactionChoice> reactions = picks.reactions(s.state());
    ASSERT_EQ(reactions.size(), 1u);
    const std::string tyrant_name = unit_display_name(s.state(), tyrant);  // "Hive Tyrant 1": the roster has two
    const std::string legion_name = unit_display_name(s.state(), s.state().units[mf::kLegionaries].id);
    EXPECT_EQ(reactions[0].label, "Crushing Impact (1 CP): " + tyrant_name);
    EXPECT_EQ(reactions[0].unit, tyrant);
    EXPECT_EQ(reactions[0].action, crushing(tyrant));
    EXPECT_EQ(action_label(s.state(), reactions[0].action),
              "Crushing Impact (1 CP): " + tyrant_name + " (" + std::to_string(tyrant.v) + ")");
    EXPECT_EQ(action_label(s.state(), action::DeclineStratagems{}), "No stratagem");
    ASSERT_TRUE(s.submit(reactions[0].action).ok());
    const std::vector<EffectChoice> choices = effect_choices(s.state());
    ASSERT_EQ(choices.size(), 1u);
    const ModelId t = s.state().units[mf::kTyrant].models[0].id;
    EXPECT_EQ(choices[0].label, legion_name + " (model " + std::to_string(t.v) + ", T10: 10 dice)");
    EXPECT_EQ(effect_headline(s.state()), "Crushing Impact: pick the enemy unit (15.06)");
    EXPECT_EQ(action_label(s.state(), choices[0].action),
              "Model " + std::to_string(t.v) + " (T10) at " + legion_name + " (" +
                  std::to_string(s.state().units[mf::kLegionaries].id.v) + ")");
}

TEST(M97_Session, ASideThatDeclinesCrushingImpactHasItsWindowDeclinedByTheSession) {
    GameState gs = mf::at_movement();
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::Charge>(g.pending->kind) && g.pending->side == Side::B;
    });
    gs.command_points[1] = 1;
    gs.pending = compute_decision(gs);
    GameSession s(gs, {true, true}, {false, false});
    s.set_declined_stratagems(Side::B, {Stratagem::CommandReroll, Stratagem::CrushingImpact});
    const UnitId tyrant = s.state().units[mf::kTyrant].id;
    ASSERT_TRUE(s.submit(action::DeclareCharge{tyrant}).ok());
    ASSERT_TRUE(s.pending() && std::holds_alternative<decision::SelectChargeTargets>(s.pending()->kind));
    ASSERT_TRUE(s.submit(action::StartChargeMove{tyrant, {s.state().units[mf::kLegionaries].id}}).ok());
    const Model& t = s.state().units[mf::kTyrant].models[0];
    ASSERT_TRUE(s.submit(action::ProposeModelMove{t.id, {Position{t.position.x, in(mf::kRankY + mf::tyrant_centre_gap(0.5f)), 0.0f}}})
                    .ok());
    ASSERT_TRUE(s.submit(action::CommitUnitMove{}).ok());
    // Declined at once, a step of its own: the Charge decision again.
    EXPECT_EQ(s.history().back(), (ReplayStep{Side::B, action::DeclineStratagems{}}));
    EXPECT_EQ(s.pending(), (PendingDecision{Side::B, decision::Charge{}}));
}

// ==== Cards and log lines ====

TEST(M97_Texts, TheExplosivesCardsNameTheDiceAndTheirNeed) {
    GameSession s(explosives_state(), {false, false}, {false, false});  // the rolls wait for their Roll button
    const UnitId marines = s.state().units[0].id;
    ASSERT_TRUE(s.submit(explosives(marines)).ok());
    ASSERT_TRUE(s.submit(s.legal().front()).ok());
    ASSERT_TRUE(roll_pending(s.state()));
    const std::optional<DiceCard> prompt = roll_prompt(s.state(), s.log());
    ASSERT_TRUE(prompt.has_value());
    EXPECT_EQ(prompt->title, "Explosives dice");
    EXPECT_EQ(prompt->rule, "15.05");
    EXPECT_EQ(prompt->dice.size(), 6u);
    EXPECT_EQ(prompt->purpose, "Intercessor Squad's Explosives at Termagants");
    EXPECT_EQ(prompt->need, "6 dice: each 4+ is 1 mortal wound on Termagants");
    EXPECT_EQ(action_label(s.state(), s.legal().front()), "Roll: Explosives dice for Intercessor Squad (1)");
    const std::size_t from = s.log().size();
    ASSERT_TRUE(s.submit(s.legal().front()).ok());
    const std::vector<DiceCard> cards = dice_cards(s.state(), s.log(), from);
    ASSERT_EQ(cards.size(), 1u);
    EXPECT_EQ(cards[0].title, "Explosives dice");
    EXPECT_EQ(cards[0].roll, RollKind::Explosives);
    ASSERT_EQ(cards[0].dice.size(), 6u);
    int wounds = 0;
    for (const DieFace& d : cards[0].dice) {
        EXPECT_EQ(d.pass, d.value >= 4) << d.value;
        wounds += d.pass ? 1 : 0;
    }
    EXPECT_EQ(cards[0].result, std::to_string(wounds) + (wounds == 1 ? " mortal wound" : " mortal wounds") +
                                   " on Termagants");
    // The log line, with the stratagem's rule.
    const auto rolled = std::find_if(s.log().back().events.begin(), s.log().back().events.end(),
                                     [](const GameEvent& e) { return std::holds_alternative<event::StratagemDiceRolled>(e); });
    ASSERT_NE(rolled, s.log().back().events.end());
    const std::string line = format_event(*rolled);
    EXPECT_EQ(line.rfind("[15.05] Explosives dice: ", 0), 0u) << line;
    EXPECT_NE(line.find("(each 4+)"), std::string::npos) << line;
}

TEST(M97_Texts, TheCrushingImpactCardShowsItsOnesAndFives) {
    GameSession s = crushing_session();
    const UnitId tyrant = s.state().units[mf::kTyrant].id;
    s.set_auto_roll(Side::B, false);
    ASSERT_TRUE(s.submit(crushing(tyrant)).ok());
    ASSERT_TRUE(s.submit(s.legal().front()).ok());
    const std::optional<DiceCard> prompt = roll_prompt(s.state(), s.log());
    ASSERT_TRUE(prompt.has_value());
    const std::string tyrant_name = unit_display_name(s.state(), tyrant);
    const std::string legion_name = unit_display_name(s.state(), s.state().units[mf::kLegionaries].id);
    EXPECT_EQ(prompt->title, "Crushing Impact dice");
    EXPECT_EQ(prompt->rule, "15.06");
    EXPECT_EQ(prompt->dice.size(), 10u);  // the Hive Tyrant's T
    EXPECT_EQ(prompt->need, "10 dice (T): each 5+ is 1 mortal wound on " + legion_name + ", each 1 is 1 on " +
                                tyrant_name + " (at most 6 each)");
    const std::size_t from = s.log().size();
    ASSERT_TRUE(s.submit(s.legal().front()).ok());
    const std::vector<DiceCard> cards = dice_cards(s.state(), s.log(), from);
    ASSERT_EQ(cards.size(), 1u);
    EXPECT_EQ(cards[0].roll, RollKind::CrushingImpact);
    ASSERT_EQ(cards[0].dice.size(), 10u);
    for (const DieFace& d : cards[0].dice) {
        EXPECT_EQ(d.pass, d.value >= 5) << d.value;
        EXPECT_EQ(d.neutral, d.value >= 2 && d.value <= 4) << d.value;  // a 1 is a failure (it wounds the Tyrant)
    }
    EXPECT_NE(cards[0].result.find("on " + legion_name + ", "), std::string::npos) << cards[0].result;
    EXPECT_NE(cards[0].result.find("on " + tyrant_name), std::string::npos) << cards[0].result;
    EXPECT_EQ(format_event(event::ModelAndEnemySelected{Stratagem::CrushingImpact, UnitId{4}, ModelId{37}, UnitId{2}}),
              "[15.06] Crushing Impact: model 37 of unit 4 selects unit 2");
    EXPECT_EQ(format_event(event::StratagemDiceRolled{Stratagem::CrushingImpact, UnitId{4}, ModelId{37}, UnitId{2},
                                                      {1, 5, 6, 2}, 2, 1}),
              "[15.06] Crushing Impact dice: 1 5 6 2 -> 2 mortal wounds on unit 2 (each 5+), 1 mortal wound on unit 4 "
              "(each 1; at most 6 per unit)");
    EXPECT_EQ(roll_label(RollKind::CrushingImpact), "Crushing Impact dice");
}

// ==== Undo and saves ====

TEST(M97_Session, UndoTakesBackTheSelectionWithItsRollThenTheStratagem) {
    GameSession s = explosives_session();
    const GameState shoot = s.state();
    const UnitId marines = s.state().units[0].id;
    ASSERT_TRUE(s.submit(explosives(marines)).ok());
    const GameState selection = s.state();
    ASSERT_TRUE(s.submit(s.legal().front()).ok());  // the roll is the session's (a step of its own)
    ASSERT_TRUE(std::holds_alternative<action::RollDice>(s.history().back().action));
    ASSERT_TRUE(s.undo());  // the roll goes with the selection that led to it
    EXPECT_EQ(s.state(), selection);
    ASSERT_TRUE(s.undo());  // the stratagem is an input of its own
    EXPECT_EQ(s.state(), shoot);
}

TEST(M97_Saves, AGameSavedDuringTheEffectLoadsAsItWas) {
    GameSession s = explosives_session();
    ASSERT_TRUE(s.submit(explosives(s.state().units[0].id)).ok());
    const nlohmann::json j = s.to_save_json();
    EXPECT_EQ(j.at("format"), kReplayFormatOwnTurnStratagems);
    const GameSession loaded = GameSession::from_save_json(j);
    EXPECT_EQ(loaded.state(), s.state());
    EXPECT_EQ(loaded.history(), s.history());
    EXPECT_TRUE(loaded.state().flow.effect.has_value());
}

TEST(M97_Saves, AnM96SaveWithAMonsterChargeLoadsWithTheNewWindowDeclined) {
    // tests/data/legacy/m96_save_monster_charge_format4.json: the monsters fixture (seed 1), Side B's units stationary,
    // the Hive Tyrant's charge at the Legionaries committed, Side B with 1 CP, as the M96 session saved it. Now
    // Crushing Impact's window comes just after the charge move, declined as a step of its own.
    const GameSession loaded = GameSession::load(kLegacyDir / "m96_save_monster_charge_format4.json");
    const nlohmann::json old = [] {
        std::ifstream in(kLegacyDir / "m96_save_monster_charge_format4.json");
        nlohmann::json j;
        in >> j;
        return j;
    }();
    EXPECT_EQ(old.at("format"), kReplayFormatReactionsRecorded);
    const std::vector<ReplayStep> old_steps = old.at("steps").get<std::vector<ReplayStep>>();
    ASSERT_EQ(loaded.history().size(), old_steps.size() + 1);
    EXPECT_EQ(std::vector<ReplayStep>(loaded.history().begin(), loaded.history().end() - 1), old_steps);
    EXPECT_EQ(loaded.history().back(), (ReplayStep{Side::B, action::DeclineStratagems{}}));
    EXPECT_EQ(loaded.pending(), (PendingDecision{Side::B, decision::Charge{}}));
    EXPECT_TRUE(loaded.state().units[mf::kTyrant].turn_flags.made_charge_move);
    EXPECT_EQ(loaded.to_save_json().at("format"), kReplayFormatOwnTurnStratagems);
    // The same game played now (nothing made by the session), its window declined by hand.
    GameSession now(old.at("initial").get<GameState>(), {false, false}, {false, false});
    for (const ReplayStep& step : old_steps) ASSERT_TRUE(now.submit(step.action).ok()) << action_name(step.action);
    ASSERT_TRUE(window_pending(now.state()));
    ASSERT_TRUE(now.submit(action::DeclineStratagems{}).ok());
    EXPECT_EQ(now.state(), loaded.state());
}

// ==== Screenshots: saves waiting at each new step ====

// Writes saves for screenshots (docs/dev/client_flags.md). Run on purpose only:
//   WH40K_SCREEN_DIR=<dir> wh40k_client_tests --gtest_also_run_disabled_tests --gtest_filter=M97_Screens.*
TEST(M97_Screens, DISABLED_WriteSavesWaitingForTheNewSteps) {
    const char* dir = std::getenv("WH40K_SCREEN_DIR");
    if (dir == nullptr) GTEST_SKIP() << "set WH40K_SCREEN_DIR";
    const std::filesystem::path out(dir);
    // The Shoot step with the Explosives button; its selection; its roll waiting (face down).
    GameSession shoot(explosives_state(), {false, false}, {false, false});
    save_game_file(out / "m97_explosives_button.json", shoot, SavePlayers{});
    ASSERT_TRUE(shoot.submit(explosives(shoot.state().units[0].id)).ok());
    save_game_file(out / "m97_explosives_selection.json", shoot, SavePlayers{});
    ASSERT_TRUE(shoot.submit(shoot.legal().front()).ok());
    ASSERT_TRUE(roll_pending(shoot.state()));
    save_game_file(out / "m97_explosives_roll.json", shoot, SavePlayers{});
    // Crushing Impact's window, its selection and its roll waiting.
    GameSession crush = crushing_session();
    save_game_file(out / "m97_crushing_window.json", crush, SavePlayers{});
    crush.set_auto_roll(Side::B, false);
    ASSERT_TRUE(crush.submit(crushing(crush.state().units[mf::kTyrant].id)).ok());
    save_game_file(out / "m97_crushing_selection.json", crush, SavePlayers{});
    ASSERT_TRUE(crush.submit(crush.legal().front()).ok());
    ASSERT_TRUE(roll_pending(crush.state()));
    save_game_file(out / "m97_crushing_roll.json", crush, SavePlayers{});
}
