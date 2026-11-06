// M35: casualties picked on the board, one model at a time (D-051): 05.04, 06.02.

#include <gtest/gtest.h>

#include <variant>
#include <vector>

#include "auto_actions.hpp"
#include "board_interaction.hpp"
#include "casualty_picks.hpp"
#include "casualty_plan.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;
constexpr std::size_t kFleshborer = 0;

// Side B's Termagants shoot the Intercessors with `n` fleshborers: n failed saves, 1 damage each. Side A
// (the Intercessors' player) must pick the casualties.
GameState casualties_pending(int n, int wounded_model = -1) {
    GameState gs = wh40k::test::shoot_step_state(Side::B);
    if (wounded_model >= 0) gs.units[kMarines].models[static_cast<std::size_t>(wounded_model)].wounds_remaining = 1;
    std::vector<WeaponSelection> sel;
    std::vector<int> rolls;
    for (int i = 0; i < n; ++i) {
        sel.push_back(wh40k::test::select(gs, kGants, static_cast<std::size_t>(i), kFleshborer, 0, gs.units[kMarines].id));
    }
    for (int i = 0; i < 3 * n; ++i) rolls.push_back(i < 2 * n ? 4 : 1);  // hits, wounds, saves of 1
    ScriptedDice dice(rolls);
    begin_attacks(gs, dice, gs.units[kGants].id, AttackContext{}, sel);
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{gs.units[kMarines].id}}));
    return gs;
}

ModelId mid(const GameState& gs, std::size_t i) { return gs.units[kMarines].models[i].id; }

}  // namespace

TEST(M35_CasualtyPicks, TheBoardHandlesCasualtiesAndGreysModelsThatCantBePicked) {
    const GameState gs = casualties_pending(1, 2);  // model 3 (index 2) is wounded: 05.04 forces it
    const BoardInteraction board(gs, legal_actions(gs, Side::A));
    ASSERT_TRUE(board.active());
    EXPECT_TRUE(board.picking_model());
    const UnitId marines = gs.units[kMarines].id;
    EXPECT_TRUE(board.clickable(marines));
    EXPECT_TRUE(board.greyed(gs.units[kGants].id));
    EXPECT_FALSE(board.greyed(marines));
    EXPECT_TRUE(board.model_dimmed(marines, mid(gs, 0)));
    EXPECT_FALSE(board.model_dimmed(marines, mid(gs, 2)));  // still needs its click (D-051)
    EXPECT_FALSE(board.confirm().has_value());
    EXPECT_NE(board.hint(gs).find("Only one model"), std::string::npos);
    // "Use default order" is the unit order, as before.
    EXPECT_EQ(board.step_buttons(), (std::vector<GameAction>{CasualtyPlan(gs, marines).action()}));
}

TEST(M35_CasualtyPicks, ClickingAModelThatCantBePickedDoesNothing) {
    const GameState gs = casualties_pending(1, 2);
    BoardInteraction board(gs, legal_actions(gs, Side::A));
    board.click_model(gs, gs.units[kMarines].id, mid(gs, 0));
    ASSERT_NE(board.casualty_picks(), nullptr);
    EXPECT_TRUE(board.casualty_picks()->picks().empty());
    EXPECT_FALSE(board.can_undo_pick());
    board.click_model(gs, gs.units[kMarines].id, mid(gs, 2));
    EXPECT_EQ(board.casualty_picks()->picks(), (std::vector<ModelId>{mid(gs, 2)}));
    EXPECT_TRUE(board.casualty_picks()->done());
}

TEST(M35_CasualtyPicks, EachClickShowsItsDamageAndConfirmGivesThoseCasualties) {
    // 3 damage into fresh W2 Intercessors: the first click takes 2 (the second is forced), the next 1.
    GameSession session(casualties_pending(3));
    const GameState& gs = session.state();
    const UnitId marines = gs.units[kMarines].id;
    const ModelId first = mid(gs, 1);
    const ModelId second = mid(gs, 3);
    BoardInteraction board(gs, session.legal());

    board.click_model(gs, marines, first);
    EXPECT_EQ(board.casualty_picks()->marks(), (std::vector<CasualtyMark>{{first, 2, true}}));
    EXPECT_FALSE(board.confirm().has_value());  // one more damage to place
    EXPECT_TRUE(board.model_dimmed(marines, first));  // it's already destroyed
    EXPECT_FALSE(board.model_dimmed(marines, second));

    board.click_model(gs, marines, second);
    EXPECT_EQ(board.casualty_picks()->marks(), (std::vector<CasualtyMark>{{first, 2, true}, {second, 1, false}}));
    EXPECT_FALSE(board.waiting_for_click());
    const auto confirm = board.confirm();
    ASSERT_TRUE(confirm.has_value());
    const auto& order = std::get<action::OrderCasualties>(*confirm);
    EXPECT_EQ(order.priority.front(), first);
    EXPECT_EQ(order.priority[1], second);

    ASSERT_TRUE(session.submit(*confirm).ok());
    const Unit& after = *session.state().find_unit(marines);
    EXPECT_EQ(after.models.size(), 4u);
    EXPECT_EQ(find_model(after, first), nullptr);
    ASSERT_NE(find_model(after, second), nullptr);
    EXPECT_EQ(find_model(after, second)->wounds_remaining, 1);
}

TEST(M35_CasualtyPicks, UndoPickTakesBackTheLastPick) {
    const GameState gs = casualties_pending(3);
    BoardInteraction board(gs, legal_actions(gs, Side::A));
    const UnitId marines = gs.units[kMarines].id;
    board.click_model(gs, marines, mid(gs, 1));
    board.click_model(gs, marines, mid(gs, 3));
    ASSERT_TRUE(board.confirm().has_value());
    board.undo_pick(gs);
    EXPECT_EQ(board.casualty_picks()->picks(), (std::vector<ModelId>{mid(gs, 1)}));
    EXPECT_FALSE(board.confirm().has_value());
    EXPECT_TRUE(board.waiting_for_click());
    board.undo_pick(gs);
    EXPECT_FALSE(board.can_undo_pick());
    EXPECT_TRUE(board.casualty_picks()->marks().empty());
}

TEST(M35_CasualtyPicks, TheActionListsThePicksThenTheRestOfTheUnit) {
    const GameState gs = casualties_pending(1);
    CasualtyPicks picks(gs, gs.units[kMarines].id);
    ASSERT_TRUE(picks.pick(gs, mid(gs, 4)));
    EXPECT_FALSE(picks.pick(gs, mid(gs, 0)));  // nothing left to pick
    const auto a = picks.action();
    EXPECT_EQ(a.priority, (std::vector<ModelId>{mid(gs, 4), mid(gs, 0), mid(gs, 1), mid(gs, 2), mid(gs, 3)}));
    EXPECT_TRUE(validate_action(gs, Side::A, a).ok());
}

TEST(M35_CasualtyPicks, AutomaticAllocationStillAnswersAForcedCasualtyAtOnce) {
    GameSession session(casualties_pending(1, 2));
    (void)run_auto_actions(session, AutoSettings{false, true});
    ASSERT_TRUE(session.pending().has_value());
    EXPECT_FALSE(std::holds_alternative<decision::OrderCasualties>(session.pending()->kind));
    EXPECT_EQ(session.state().units[kMarines].models.size(), 4u);  // the wounded model died
}
