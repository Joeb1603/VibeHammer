// M26: Undo by the player's inputs, and "Undo last step" in a move (D-039).

#include <gtest/gtest.h>

#include <vector>

#include "move_plan.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"
#include "undo_groups.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::in;

namespace {

// Submits `actions` as one player input, as the client does.
void input(GameSession& s, UndoGroups& groups, const std::vector<GameAction>& actions) {
    groups.begin(s.history().size());
    for (const GameAction& a : actions) ASSERT_TRUE(s.submit(a).ok()) << action_name(a);
    groups.end(s.history().size());
}

// The Intercessors' first model, at the start of a Normal move (V1 scenario: the Termagants are 24" away).
struct Moving {
    GameSession session;
    UndoGroups groups;
    UnitId unit;
    ModelId model;
};

Moving start_normal_move() {
    Moving m{GameSession(wh40k::test::load_v1_scenario()), {}, {}, {}};
    m.unit = m.session.state().units[0].id;
    m.model = m.session.state().units[0].models[0].id;
    input(m.session, m.groups, {action::StartMove{m.unit, MoveType::Normal, std::nullopt}});
    return m;
}

Position position_of(const GameSession& s, ModelId id) {
    for (const Unit& u : s.state().units) {
        for (const Model& m : u.models) {
            if (m.id == id) return m.position;
        }
    }
    ADD_FAILURE() << "no model " << id.v;
    return {};
}

// `model` moved by (dx, dy) inches from where it is now.
Position nudged(const GameSession& s, ModelId model, float dx_in, float dy_in) {
    Position p = position_of(s, model);
    p.x += in(dx_in);
    p.y += in(dy_in);
    return p;
}

}  // namespace

TEST(M26_UndoLastStep, IsNotOfferedRightAfterTheUnitIsSelectedToMove) {
    Moving m = start_normal_move();
    EXPECT_FALSE(m.groups.last_group_is_move_step(m.session));
}

TEST(M26_UndoLastStep, UndoesOneModelsDragAtATime) {
    Moving m = start_normal_move();
    // The Termagants are at smaller y (up the board): move up 2", then 1" sideways.
    input(m.session, m.groups, {extend_path(m.session.state(), m.model, nudged(m.session, m.model, 0, -2))});
    const Position after_first = position_of(m.session, m.model);
    const GameState after_first_state = m.session.state();
    input(m.session, m.groups, {extend_path(m.session.state(), m.model, nudged(m.session, m.model, 1, 0))});
    EXPECT_NE(position_of(m.session, m.model), after_first);
    ASSERT_TRUE(m.groups.last_group_is_move_step(m.session));
    ASSERT_TRUE(m.groups.undo(m.session));
    EXPECT_EQ(position_of(m.session, m.model), after_first);
    EXPECT_EQ(m.session.state(), after_first_state);
    ASSERT_TRUE(m.groups.last_group_is_move_step(m.session));  // the first drag can be undone too
    ASSERT_TRUE(m.groups.undo(m.session));
    EXPECT_FALSE(m.groups.last_group_is_move_step(m.session));  // back to just after StartMove
}

TEST(M26_UndoLastStep, AWholeUnitDragUndoesAsAGroupAndASingleModelOnItsOwn) {
    Moving m = start_normal_move();
    const GameState after_start = m.session.state();
    std::vector<GameAction> group;
    for (const auto& a : translate_unit(m.session.state(), m.unit, 0.0f, -in(3))) group.push_back(a);
    ASSERT_EQ(group.size(), m.session.state().units[0].models.size());
    input(m.session, m.groups, group);
    const GameState after_group = m.session.state();
    input(m.session, m.groups, {extend_path(m.session.state(), m.model, nudged(m.session, m.model, 0, -1))});

    ASSERT_TRUE(m.groups.undo(m.session));  // the single model
    EXPECT_EQ(m.session.state(), after_group);
    ASSERT_TRUE(m.groups.last_group_is_move_step(m.session));
    ASSERT_TRUE(m.groups.undo(m.session));  // every model of the unit drag at once
    EXPECT_EQ(m.session.state(), after_start);
}

TEST(M26_UndoGroups, StepsFromBeforeAnyGroupUndoOneAtATime) {
    GameSession s(wh40k::test::load_v1_scenario());
    ASSERT_TRUE(s.submit(action::StartMove{s.state().units[0].id, MoveType::Normal, std::nullopt}).ok());
    UndoGroups groups;  // e.g. after a load: no groups yet
    EXPECT_TRUE(groups.undo(s));
    EXPECT_TRUE(s.history().empty());
    EXPECT_FALSE(groups.undo(s));
}

TEST(M26_UndoGroups, AnInputThatAddedNoStepIsNotAGroup) {
    GameSession s(wh40k::test::load_v1_scenario());
    UndoGroups groups;
    groups.begin(s.history().size());
    EXPECT_FALSE(s.submit(action::EndShootStep{}).ok());  // not this decision
    groups.end(s.history().size());
    EXPECT_TRUE(groups.starts().empty());
}
