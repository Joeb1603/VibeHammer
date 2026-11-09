// M74 (D-081, D-065 amended): the client logic in the deployment step, kept minimal until M76's board editor: the log
// and action text, the decision view's new editor kind, the phase track, unit numbers and shades that never shift as
// units are set up, and one undo group per unit set up.

#include <gtest/gtest.h>

#include <string>
#include <variant>
#include <vector>

#include "action_text.hpp"
#include "core/data/army_list.hpp"
#include "core/engine/engine.hpp"
#include "core/state/deployment.hpp"
#include "decision_view.hpp"
#include "event_text.hpp"
#include "phase_track.hpp"
#include "session.hpp"
#include "support/army_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"
#include "undo_groups.hpp"
#include "unit_style.hpp"

using namespace wh40k::client;
namespace core = wh40k::core;
using core::GameAction;
using core::GameState;
using core::Side;
using core::UnitId;

namespace {

GameState stock_battle(Side first, core::DeploymentMode mode) {
    return wh40k::test::custom_battle(wh40k::test::stock_army(wh40k::test::csm()),
                                      wh40k::test::stock_army(wh40k::test::tyranids()), first, mode);
}

std::vector<std::uint32_t> all_unit_ids(const GameState& gs) {
    std::vector<std::uint32_t> ids;
    for (const core::Unit& u : gs.units) ids.push_back(u.id.v);
    for (const core::Unit& u : gs.undeployed) ids.push_back(u.id.v);
    std::sort(ids.begin(), ids.end());
    return ids;
}

}  // namespace

TEST(M74_UnitStyle, NumbersAndShadesNeverShiftAsUnitsAreSetUp) {
    // D-065 (amended by D-081): units waiting to be set up count at their id positions, so every unit's style is the
    // auto-placed battle's from the first decision of the deployment to its last.
    GameState placed = stock_battle(Side::B, core::DeploymentMode::Auto);
    core::start_game(placed);
    GameState gs = stock_battle(Side::B, core::DeploymentMode::Hand);
    core::start_game(gs);
    int checked = 0;
    for (int guard = 0; guard < 20; ++guard) {
        for (const std::uint32_t id : all_unit_ids(placed)) {
            const auto style = unit_style(gs, UnitId{id});
            const auto expected = unit_style(placed, UnitId{id});
            ASSERT_TRUE(style.has_value()) << "unit " << id;
            ASSERT_TRUE(expected.has_value());
            EXPECT_EQ(style->index, expected->index) << "unit " << id;
            EXPECT_EQ(style->side_units, expected->side_units);
            EXPECT_EQ(style->label, expected->label);
            EXPECT_EQ(style->display_name, expected->display_name);
            EXPECT_EQ(style->tint, expected->tint);
            ++checked;
        }
        if (gs.flow.stage != core::Stage::Deploy) break;
        core::apply_action(gs, wh40k::test::decider(gs), core::legal_actions(gs, wh40k::test::decider(gs)).front());
    }
    EXPECT_EQ(checked, 15 * 14);  // the start, then after each of the 14 set-ups
    EXPECT_EQ(unit_display_name(stock_battle(Side::A, core::DeploymentMode::Hand), UnitId{2}), "Chaos Lord 2");
    EXPECT_EQ(unit_display_name(stock_battle(Side::A, core::DeploymentMode::Hand), UnitId{99}), "unit 99");
}

TEST(M74_Log, DeploymentEventsAndActionsReadAsTheyShould) {
    EXPECT_EQ(format_event(core::event::DeploymentStarted{Side::B}),
              "[D-081] Deployment starts: Side B sets up first, then the sides alternate");
    EXPECT_EQ(format_event(core::event::UnitSetUp{UnitId{3}, Side::A}), "[03.02] Side A sets up unit 3");
    EXPECT_EQ(format_event(core::event::UnitNotSetUp{UnitId{3}, Side::A}),
              "[D-081] Side A can't set up unit 3: no legal spot is left, so it counts as destroyed");
    EXPECT_FALSE(is_roll_event(core::event::UnitSetUp{UnitId{3}, Side::A}));
    GameState gs = stock_battle(Side::B, core::DeploymentMode::Hand);  // A sets up first
    core::start_game(gs);
    const std::vector<GameAction> legal = core::legal_actions(gs, Side::A);
    EXPECT_EQ(action_label(gs, legal.front()), "Set up Legionaries 1 (1) (auto-place)");
    EXPECT_EQ(action_label(gs, core::action::CannotSetUp{UnitId{2}}), "Can't be set up: Chaos Lord 2 (2) (counts as destroyed)");
    // The decision view: one button per unit (its proposal), one DeployUnit editor per unit.
    const DecisionView view = make_decision_view(legal);
    EXPECT_EQ(view.buttons.size(), 5u);
    ASSERT_EQ(view.templates.size(), 5u);
    for (const GameAction& t : view.templates) EXPECT_EQ(editor_kind(t), EditorKind::Deploy);
    EXPECT_FALSE(editor_kind(view.buttons.front()).has_value());
    // The session's log: the start's line, then each set-up's.
    GameSession s = GameSession::new_game(stock_battle(Side::B, core::DeploymentMode::Hand), 1);
    ASSERT_EQ(s.log().size(), 1u);
    ASSERT_EQ(s.log().front().events.size(), 1u);
    EXPECT_EQ(format_event(s.log().front().events.front()),
              "[D-081] Deployment starts: Side A sets up first, then the sides alternate");
    ASSERT_TRUE(s.submit(s.legal().front()).ok());
    EXPECT_EQ(s.log().back().label, "Step 1: Side A DeployUnit");
    EXPECT_EQ(format_event(s.log().back().events.front()), "[03.02] Side A sets up unit 1");
}

TEST(M74_Log, ThePhaseTrackShowsTheDeploymentBeforeBattleRoundOne) {
    GameState gs = stock_battle(Side::A, core::DeploymentMode::Hand);  // B sets up first
    core::start_game(gs);
    const PhaseTrack t = phase_track(gs);
    EXPECT_TRUE(t.deploying);
    EXPECT_EQ(t.round, 0);
    EXPECT_EQ(t.rounds, 5);
    EXPECT_FALSE(t.turn.has_value());  // nobody's turn (01.03)
    EXPECT_FALSE(t.phase_index.has_value());
    EXPECT_EQ(t.step, "Deploy (03.02)");
    EXPECT_EQ(t.waiting_for, Side::B);
    EXPECT_FALSE(t.result.has_value());
    while (gs.flow.stage == core::Stage::Deploy) {
        core::apply_action(gs, wh40k::test::decider(gs), core::legal_actions(gs, wh40k::test::decider(gs)).front());
    }
    const PhaseTrack after = phase_track(gs);
    EXPECT_FALSE(after.deploying);
    EXPECT_EQ(after.round, 1);
    EXPECT_EQ(after.turn, Side::A);
}

TEST(M74_Undo, EachUnitSetUpIsOneUndoGroup) {
    GameSession s = GameSession::new_game(stock_battle(Side::A, core::DeploymentMode::Hand), 1);
    UndoGroups groups;
    const auto press = [&](const GameAction& a) {
        groups.begin(s.history().size());
        ASSERT_TRUE(s.submit(a).ok());
        groups.end(s.history().size());
    };
    press(s.legal().front());  // B: Termagants (unit 6)
    press(s.legal().front());  // A: Legionaries (unit 1)
    press(s.legal().front());  // B: Genestealers (unit 7)
    ASSERT_EQ(s.state().units.size(), 3u);
    ASSERT_TRUE(groups.undo(s));
    EXPECT_EQ(s.state().units.size(), 2u);
    EXPECT_NE(core::find_undeployed(s.state(), UnitId{7}), nullptr);
    EXPECT_EQ(s.state().pending->side, Side::B);
    ASSERT_TRUE(groups.undo(s));
    EXPECT_EQ(s.state().units.size(), 1u);
    EXPECT_EQ(s.state().pending->side, Side::A);
    // Against a bot (D-054): the human's undo takes back the bot's set-up after it too.
    GameSession vs = GameSession::new_game(stock_battle(Side::A, core::DeploymentMode::Hand), 1);
    UndoGroups g2;
    g2.begin(vs.history().size(), InputOwner::Human);
    ASSERT_TRUE(vs.submit(vs.legal().front()).ok());  // B, the human
    g2.end(vs.history().size());
    g2.begin(vs.history().size(), InputOwner::Bot);
    ASSERT_TRUE(vs.submit(vs.legal().front()).ok());  // A, the bot
    g2.end(vs.history().size());
    ASSERT_TRUE(g2.undo_human(vs));
    EXPECT_TRUE(vs.state().units.empty());
    EXPECT_EQ(vs.state().pending->side, Side::B);
}
