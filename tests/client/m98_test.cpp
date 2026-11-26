// M98 (D-100): 15.05 Explosives' and 15.06 Crushing Impact's selection (SelectModelAndEnemy) picked on the board: a click
// on an enemy unit submits its answer (the chosen model's, else the first legal model's, as the bar's button), a click on
// a model of the stratagem's unit chooses it first, a model in no answer is dimmed, and a unit that can't be picked is
// grey with the engine's reason. The bar keeps its buttons.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "board_interaction.hpp"
#include "core/engine/engine.hpp"
#include "core/rules/stratagem_effects.hpp"
#include "core/state/queries.hpp"
#include "saved_game.hpp"
#include "session.hpp"
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

UnitId unit_at(const GameState& gs, std::size_t i) { return gs.units[i].id; }

// The monsters fixture (Side A first) at Side A's Shoot step, with 1 CP, two enemy units placed for Explosives: the Hive
// Tyrant north-east of the Legionaries' rank (within 8" of its east end only) and the Tyrant Guard north-west of it
// (within 8" of its west end only), so the middle of the rank reaches neither.
GameState explosives_state() {
    GameState gs = mf::at_movement(false, Side::A);
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::Shoot>(g.pending->kind) && g.pending->side == Side::A;
    });
    mf::place_rank(gs.units[mf::kTyrant], 30.0f, 33.0f);
    mf::place_rank(gs.units[mf::kGuard], 12.0f, 31.0f);
    gs.command_points[0] = 1;
    gs.pending = compute_decision(gs);
    return gs;
}

// That Shoot step in a session that makes no roll, Explosives used on the Legionaries: the selection waits.
GameSession explosives_selection() {
    GameSession s(explosives_state(), {false, false}, {false, false});
    EXPECT_TRUE(s.submit(action::UseStratagem{Stratagem::Explosives, unit_at(s.state(), mf::kLegionaries), std::nullopt})
                    .ok());
    EXPECT_TRUE(s.pending() && std::holds_alternative<decision::SelectModelAndEnemy>(s.pending()->kind));
    return s;
}

// The models of the Legionaries by what Explosives can select with them.
struct Throwers {
    std::optional<ModelId> east;    // a pair with the Tyrant only
    std::optional<ModelId> west;    // a pair with the Tyrant Guard only
    std::optional<ModelId> middle;  // no pair
};

Throwers throwers(const GameState& gs) {
    const Unit& legionaries = gs.units[mf::kLegionaries];
    const std::vector<EffectPair> pairs = explosives_pairs(gs, legionaries);
    const auto has = [&](ModelId m, UnitId e) {
        return std::find(pairs.begin(), pairs.end(), EffectPair{m, e}) != pairs.end();
    };
    Throwers out;
    for (const Model& m : legionaries.models) {
        const bool tyrant = has(m.id, unit_at(gs, mf::kTyrant));
        const bool guard = has(m.id, unit_at(gs, mf::kGuard));
        if (tyrant && !guard && !out.east) out.east = m.id;
        if (guard && !tyrant && !out.west) out.west = m.id;
        if (!tyrant && !guard && !out.middle) out.middle = m.id;
    }
    return out;
}

GameAction pair(ModelId m, UnitId e) { return action::SelectModelAndEnemy{m, e}; }

// The monsters fixture (Side B first): the Hive Tyrant's charge into the Legionaries committed and Crushing Impact used,
// in a session that makes no roll: the selection waits.
GameSession crushing_selection() {
    GameState gs = mf::at_movement();
    mf::play_until(gs, [](const GameState& g) {
        return g.pending && std::holds_alternative<decision::Charge>(g.pending->kind) && g.pending->side == Side::B;
    });
    gs.command_points[1] = 1;
    gs.pending = compute_decision(gs);
    const UnitId tyrant = unit_at(gs, mf::kTyrant);
    ScriptedDice six{3, 3};
    apply_action(gs, Side::B, action::DeclareCharge{tyrant}, six);
    apply_action(gs, Side::B, action::StartChargeMove{tyrant, {unit_at(gs, mf::kLegionaries)}});
    const Model& t = gs.units[mf::kTyrant].models[0];
    apply_action(gs, Side::B,
                 action::ProposeModelMove{t.id, {Position{t.position.x, in(mf::kRankY + mf::tyrant_centre_gap(0.5f)), 0.0f}}});
    ScriptedDice none{std::vector<int>{}};
    apply_action_paced(gs, Side::B, action::CommitUnitMove{}, none);
    EXPECT_TRUE(window_pending(gs));
    GameSession s(gs, {false, false}, {false, false});
    EXPECT_TRUE(s.submit(action::UseStratagem{Stratagem::CrushingImpact, tyrant, std::nullopt}).ok());
    EXPECT_TRUE(s.pending() && std::holds_alternative<decision::SelectModelAndEnemy>(s.pending()->kind));
    return s;
}

}  // namespace

// ==== Explosives: the selection on the board ====

TEST(M98_Board, TheSelectionIsPickedOnTheBoard) {
    const GameSession s = explosives_selection();
    const GameState& gs = s.state();
    const Throwers t = throwers(gs);
    ASSERT_TRUE(t.east && t.west && t.middle) << "the fixture's layout";
    const UnitId legionaries = unit_at(gs, mf::kLegionaries);
    const UnitId tyrant = unit_at(gs, mf::kTyrant);
    const UnitId guard = unit_at(gs, mf::kGuard);
    BoardInteraction board(gs, s.legal_offered());
    EXPECT_TRUE(board.active());
    EXPECT_TRUE(board.picking_effect());
    EXPECT_TRUE(board.picking_model());
    EXPECT_TRUE(board.picking_target());
    EXPECT_TRUE(board.waiting_for_click());
    EXPECT_EQ(board.actor(), legionaries);
    EXPECT_EQ(board.effect_model(), std::nullopt);
    // Clickable: the stratagem's unit (to choose a model) and the enemy units of the answers; the rest is grey.
    EXPECT_TRUE(board.clickable(legionaries));
    EXPECT_TRUE(board.clickable(tyrant));
    EXPECT_TRUE(board.clickable(guard));
    for (const std::size_t i : {mf::kLord, mf::kHavocs, mf::kScreamer}) {
        EXPECT_FALSE(board.clickable(unit_at(gs, i))) << i;
        EXPECT_TRUE(board.greyed(unit_at(gs, i))) << i;
    }
    EXPECT_FALSE(board.greyed(tyrant));
    // A model in no answer is dimmed; the others, and other units' models, are not.
    EXPECT_TRUE(board.model_dimmed(legionaries, *t.middle));
    EXPECT_FALSE(board.model_dimmed(legionaries, *t.east));
    EXPECT_FALSE(board.model_dimmed(legionaries, *t.west));
    EXPECT_FALSE(board.model_dimmed(tyrant, gs.units[mf::kTyrant].models[0].id));
    // The headline, the hint, and the bar: M97's buttons (one per enemy unit), no step buttons beside them.
    EXPECT_EQ(board.prompt(gs), "Explosives: pick the enemy unit (15.05)");
    EXPECT_EQ(board.hint(gs).rfind("Click the enemy unit to throw at (the first model of the " +
                                       unit_display_name(gs, legionaries) +
                                       " that can throw at it throws), or first click the model that throws. Six D6",
                                   0),
              0u)
        << board.hint(gs);
    EXPECT_TRUE(board.step_buttons().empty());
    EXPECT_EQ(board.effect_buttons(gs), effect_choices(gs));
    ASSERT_EQ(board.effect_buttons(gs).size(), 2u);
    // A click on an enemy unit submits its answer: the first legal model's, as the bar's button.
    for (const EffectChoice& c : effect_choices(gs)) {
        BoardInteraction b(gs, s.legal_offered());
        const ClickResult r = b.click(gs, c.enemy);
        ASSERT_TRUE(r.submit.has_value());
        EXPECT_EQ(*r.submit, c.action);
        EXPECT_TRUE(r.menu.empty());
        EXPECT_TRUE(validate_action(gs, Side::A, *r.submit).ok());
    }
    // A click on the stratagem's unit as a whole (not on a model) does nothing; nor on a grey unit.
    EXPECT_FALSE(board.click(gs, legionaries).submit.has_value());
    EXPECT_FALSE(board.click(gs, unit_at(gs, mf::kScreamer)).submit.has_value());
    // A click on an enemy model is a click on its unit.
    BoardInteraction by_model(gs, s.legal_offered());
    EXPECT_EQ(by_model.click_model(gs, tyrant, gs.units[mf::kTyrant].models[0].id).submit,
              std::optional<GameAction>(effect_choices(gs).front().action));
}

TEST(M98_Board, AModelChosenFirstNarrowsTheEnemyUnits) {
    const GameSession s = explosives_selection();
    const GameState& gs = s.state();
    const Throwers t = throwers(gs);
    ASSERT_TRUE(t.east && t.west && t.middle);
    const UnitId legionaries = unit_at(gs, mf::kLegionaries);
    const UnitId tyrant = unit_at(gs, mf::kTyrant);
    const UnitId guard = unit_at(gs, mf::kGuard);
    BoardInteraction board(gs, s.legal_offered());
    // The east model: chosen (no submit); only the Tyrant can be clicked now.
    EXPECT_FALSE(board.click_model(gs, legionaries, *t.east).submit.has_value());
    EXPECT_EQ(board.effect_model(), t.east);
    EXPECT_EQ(board.chosen_model(), std::nullopt);  // D-042's removal ring is not drawn
    EXPECT_TRUE(board.clickable(tyrant));
    EXPECT_FALSE(board.clickable(guard));
    EXPECT_TRUE(board.greyed(guard));
    EXPECT_EQ(board.hint(gs).rfind("Model " + std::to_string(t.east->v) + " of the " +
                                       unit_display_name(gs, legionaries) + " throws: click the enemy unit to throw at.",
                                   0),
              0u)
        << board.hint(gs);
    // The bar: that model's answers only.
    const std::vector<EffectChoice> buttons = board.effect_buttons(gs);
    ASSERT_EQ(buttons.size(), 1u);
    EXPECT_EQ(buttons[0].action, pair(*t.east, tyrant));
    EXPECT_NE(buttons[0].label.find("model " + std::to_string(t.east->v)), std::string::npos) << buttons[0].label;
    EXPECT_EQ(buttons, effect_choices(gs, t.east));
    // Why the Guard can't be picked: the engine's reason for the chosen model.
    EXPECT_EQ(board.effect_refusal(gs, guard),
              "15.05: unit " + std::to_string(guard.v) + " is not within 8\" of model " + std::to_string(t.east->v));
    EXPECT_EQ(board.effect_refusal(gs, tyrant), std::nullopt);  // it can be picked
    // A click on the Guard does nothing; on the Tyrant it submits the chosen model's answer.
    EXPECT_FALSE(board.click(gs, guard).submit.has_value());
    const ClickResult r = board.click(gs, tyrant);
    ASSERT_TRUE(r.submit.has_value());
    EXPECT_EQ(*r.submit, pair(*t.east, tyrant));
    EXPECT_TRUE(validate_action(gs, Side::A, *r.submit).ok());
    // The west model instead (a click on another model changes it): the Guard, not the Tyrant.
    board.click_model(gs, legionaries, *t.west);
    EXPECT_EQ(board.effect_model(), t.west);
    EXPECT_TRUE(board.clickable(guard));
    EXPECT_FALSE(board.clickable(tyrant));
    EXPECT_EQ(board.click(gs, guard).submit, std::optional<GameAction>(pair(*t.west, guard)));
    // A second click on the chosen model drops it; a model in no answer can't be chosen; Esc drops it too.
    board.click_model(gs, legionaries, *t.west);
    EXPECT_EQ(board.effect_model(), std::nullopt);
    board.click_model(gs, legionaries, *t.middle);
    EXPECT_EQ(board.effect_model(), std::nullopt);
    board.click_model(gs, legionaries, *t.east);
    ASSERT_EQ(board.effect_model(), t.east);
    board.cancel();
    EXPECT_EQ(board.effect_model(), std::nullopt);
    EXPECT_TRUE(board.picking_effect());  // still picking (the actor is the decision's)
    EXPECT_TRUE(board.clickable(guard));
}

TEST(M98_Board, WithoutAModelTheRefusalIsTheClosestModels) {
    const GameSession s = explosives_selection();
    const GameState& gs = s.state();
    const Unit& legionaries = gs.units[mf::kLegionaries];
    const Unit& screamer = gs.units[mf::kScreamer];
    const BoardInteraction board(gs, s.legal_offered());
    // The Screamer-killer is beyond 8" of every Legionary: the reason names the closest of them.
    std::optional<ModelId> closest;
    std::optional<float> best;
    for (const Model& m : legionaries.models) {
        const std::optional<float> d = model_distance_to_unit(pose_of(m), screamer);
        if (d && (!best || *d < *best)) {
            best = d;
            closest = m.id;
        }
    }
    ASSERT_TRUE(closest);
    EXPECT_EQ(board.effect_refusal(gs, screamer.id),
              "15.05: unit " + std::to_string(screamer.id.v) + " is not within 8\" of model " + std::to_string(closest->v));
    EXPECT_EQ(effect_refusal_text(gs, screamer.id), board.effect_refusal(gs, screamer.id));
    // No reason for a friendly unit, the stratagem's unit, or a unit that can be picked.
    EXPECT_EQ(board.effect_refusal(gs, unit_at(gs, mf::kLord)), std::nullopt);
    EXPECT_EQ(board.effect_refusal(gs, legionaries.id), std::nullopt);
    EXPECT_EQ(board.effect_refusal(gs, unit_at(gs, mf::kTyrant)), std::nullopt);
    // Nothing outside the selection.
    const GameState shoot = explosives_state();
    EXPECT_EQ(effect_refusal_text(shoot, screamer.id), std::nullopt);
    EXPECT_FALSE(BoardInteraction(shoot, legal_actions(shoot, Side::A)).picking_effect());
}

TEST(M98_Session, TheBoardsAnswerGoesOnToTheRoll) {
    GameSession s = explosives_selection();
    const Throwers t = throwers(s.state());
    ASSERT_TRUE(t.west);
    BoardInteraction board(s.state(), s.legal_offered());
    board.click_model(s.state(), s.state().units[mf::kLegionaries].id, *t.west);
    const ClickResult r = board.click(s.state(), unit_at(s.state(), mf::kGuard));
    ASSERT_TRUE(r.submit.has_value());
    ASSERT_TRUE(s.submit(*r.submit).ok());
    // The six D6 wait for Side A's roll, for that model and the Guard.
    ASSERT_TRUE(roll_pending(s.state()));
    EXPECT_EQ(std::get<decision::RollDice>(s.pending()->kind).roll, RollKind::Explosives);
    ASSERT_TRUE(s.state().flow.effect.has_value());
    EXPECT_EQ(s.state().flow.effect->model, t.west);
    EXPECT_EQ(s.state().flow.effect->enemy, unit_at(s.state(), mf::kGuard));
    // A new board at the roll is not picking any more.
    EXPECT_FALSE(BoardInteraction(s.state(), s.legal_offered()).picking_effect());
}

// ==== Crushing Impact's selection ====

TEST(M98_Board, CrushingImpactsSelectionIsTheEngagedEnemyUnit) {
    GameSession s = crushing_selection();
    const GameState& gs = s.state();
    const UnitId tyrant = unit_at(gs, mf::kTyrant);
    const UnitId legionaries = unit_at(gs, mf::kLegionaries);
    const ModelId model = gs.units[mf::kTyrant].models[0].id;
    BoardInteraction board(gs, s.legal_offered());
    ASSERT_TRUE(board.picking_effect());
    EXPECT_EQ(board.actor(), tyrant);
    EXPECT_TRUE(board.clickable(tyrant));
    EXPECT_TRUE(board.clickable(legionaries));
    for (const std::size_t i : {mf::kLord, mf::kHavocs, mf::kGuard, mf::kScreamer}) {
        EXPECT_FALSE(board.clickable(unit_at(gs, i))) << i;
    }
    EXPECT_FALSE(board.model_dimmed(tyrant, model));
    EXPECT_EQ(board.prompt(gs), "Crushing Impact: pick the enemy unit (15.06)");
    EXPECT_EQ(board.hint(gs).rfind("Click the engaged enemy unit (the first model of the " +
                                       unit_display_name(gs, tyrant) + " engaged with it rolls)",
                                   0),
              0u)
        << board.hint(gs);
    const std::vector<EffectChoice> buttons = board.effect_buttons(gs);
    ASSERT_EQ(buttons.size(), 1u);
    EXPECT_NE(buttons[0].label.find("T10: 10 dice"), std::string::npos) << buttons[0].label;
    // The Havocs aren't engaged with the Tyrant: the engine's reason.
    EXPECT_EQ(board.effect_refusal(gs, unit_at(gs, mf::kHavocs)),
              "15.06: unit " + std::to_string(unit_at(gs, mf::kHavocs).v) + " is not engaged with unit " +
                  std::to_string(tyrant.v));
    // The Tyrant's model chosen, then the Legionaries clicked: its answer, and the session goes on to the T D6.
    board.click_model(gs, tyrant, model);
    EXPECT_EQ(board.effect_model(), model);
    EXPECT_EQ(board.hint(gs).rfind("Model " + std::to_string(model.v) + " of the " + unit_display_name(gs, tyrant) +
                                       " rolls: click the engaged enemy unit.",
                                   0),
              0u)
        << board.hint(gs);
    const ClickResult r = board.click(gs, legionaries);
    ASSERT_TRUE(r.submit.has_value());
    EXPECT_EQ(*r.submit, pair(model, legionaries));
    ASSERT_TRUE(s.submit(*r.submit).ok());
    ASSERT_TRUE(roll_pending(s.state()));
    EXPECT_EQ(std::get<decision::RollDice>(s.pending()->kind).roll, RollKind::CrushingImpact);
}

// ==== Screenshots ====

// Writes saves for screenshots (docs/dev/client_flags.md; --load, with --pick-models choosing a model). Run on purpose:
//   WH40K_SCREEN_DIR=<dir> wh40k_client_tests --gtest_also_run_disabled_tests --gtest_filter=M98_Screens.*
TEST(M98_Screens, DISABLED_WriteSavesWaitingForTheSelection) {
    const char* dir = std::getenv("WH40K_SCREEN_DIR");
    if (dir == nullptr) GTEST_SKIP() << "set WH40K_SCREEN_DIR";
    const std::filesystem::path out(dir);
    const GameSession explosives = explosives_selection();
    save_game_file(out / "m98_explosives_selection.json", explosives, SavePlayers{});
    const Throwers t = throwers(explosives.state());
    if (t.east) std::printf("Explosives: the east model is %u\n", t.east->v);
    const GameSession crushing = crushing_selection();
    save_game_file(out / "m98_crushing_selection.json", crushing, SavePlayers{});
}
