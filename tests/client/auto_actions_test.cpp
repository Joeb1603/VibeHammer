// M23: "Skip steps with nothing to do" (D-036). Client only: lone skips are submitted through the session.

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "auto_actions.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::in;

namespace {

// Side A's shooting step with the Termagants far out of range: nothing can shoot or charge.
GameState nothing_to_shoot() {
    GameState gs = wh40k::test::shoot_step_state();
    for (auto& m : gs.units[1].models) m.position.y += in(30);
    return gs;
}

}  // namespace

TEST(M23_AutoSkip, ALoneSkipIsFound) {
    for (const GameAction& a : {GameAction{action::EndShootStep{}}, GameAction{action::EndChargeStep{}},
                                GameAction{action::EndPileIns{}}, GameAction{action::EndConsolidations{}},
                                GameAction{action::PassFight{}}, GameAction{action::SkipPileIn{UnitId{1}}}}) {
        const auto skip = lone_skip({a});
        ASSERT_TRUE(skip.has_value()) << action_name(a);
        EXPECT_EQ(*skip, a);
    }
}

TEST(M23_AutoSkip, NotWhenThereIsAChoiceOrTheOnlyActionIsNotASkip) {
    EXPECT_FALSE(lone_skip({}).has_value());
    EXPECT_FALSE(lone_skip({action::EndShootStep{}, action::Shoot{UnitId{1}, ShootingType::Normal, {}}}).has_value());
    EXPECT_FALSE(lone_skip({action::RemoveModelForCoherency{UnitId{1}, ModelId{2}}}).has_value());
    EXPECT_FALSE(lone_skip({action::SelectToFight{UnitId{1}, FightType::Normal}}).has_value());
    EXPECT_FALSE(lone_skip({action::DeclareAllocationOrder{UnitId{1}, {0}}}).has_value());
}

TEST(M23_AutoSkip, ChainsThroughStepsWithNothingToDoAndStopsAtARealChoice) {
    GameSession session(nothing_to_shoot());
    const std::vector<GameAction> skipped = run_auto_actions(session, AutoSettings{true, false});
    ASSERT_GE(skipped.size(), 2u);
    EXPECT_EQ(skipped[0], GameAction{action::EndShootStep{}});
    EXPECT_EQ(skipped[1], GameAction{action::EndChargeStep{}});
    for (const GameAction& a : skipped) EXPECT_TRUE(is_skip(a));
    ASSERT_TRUE(session.pending().has_value());
    EXPECT_FALSE(lone_skip(session.legal()).has_value());  // a real choice is left
    // The skips are ordinary steps: the history replays to the same state.
    EXPECT_EQ(session.history().size(), skipped.size());
    const GameSession rebuilt = GameSession::from_steps(session.initial(), session.history());
    EXPECT_EQ(rebuilt.state(), session.state());
}

TEST(M23_AutoSkip, NothingIsSkippedWhenAUnitCanShoot) {
    GameSession session(wh40k::test::shoot_step_state());
    EXPECT_TRUE(run_auto_actions(session, AutoSettings{true, false}).empty());
    EXPECT_TRUE(session.history().empty());
}

TEST(M23_AutoSkip, UndoRemovesThePlayersActionAndTheSkipsAfterIt) {
    // The player ends shooting by hand; the steps after it with nothing to do are skipped.
    GameSession s(nothing_to_shoot());
    const GameState start = s.state();
    ASSERT_TRUE(s.submit(action::EndShootStep{}).ok());
    std::vector<std::size_t> auto_steps;
    run_auto_actions(s, AutoSettings{true, false}, [&](const GameAction& a) {
        const std::size_t index = s.history().size();
        if (!s.submit(a).ok()) return false;
        auto_steps.push_back(index);
        return true;
    });
    ASSERT_FALSE(auto_steps.empty());
    EXPECT_EQ(auto_steps.front(), 1u);
    EXPECT_TRUE(undo_with_auto_skips(s, auto_steps));
    EXPECT_EQ(s.state(), start);
    EXPECT_TRUE(auto_steps.empty());
    EXPECT_FALSE(undo_with_auto_skips(s, auto_steps));
}
