// M90 (D-091): 12.08 Objective Consolidation and 24.18 [IGNORES COVER] in the client logic: the board's offer, the
// Auto move (D-050), the "too small to matter" skip (D-047, D-049), the action, event and board text, and the army
// builder offering the flamers.

#include <gtest/gtest.h>

#include <string>
#include <variant>
#include <vector>

#include "action_text.hpp"
#include "army_builder.hpp"
#include "auto_fight_move.hpp"
#include "board_interaction.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/state/objectives.hpp"
#include "event_text.hpp"
#include "fight_move_skip.hpp"
#include "phase_track.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::at;
using wh40k::test::objective_area;
using wh40k::test::objective_consolidation_state;

namespace {

constexpr std::size_t kMarines = 0;

UnitId marines(const GameState& gs) { return gs.units[kMarines].id; }

GameState started(float gap_in) {
    GameState gs = objective_consolidation_state(gap_in);
    apply_action(gs, Side::A, action::StartConsolidation{marines(gs), {}, 0});
    return gs;
}

int within_range(const GameState& gs) {
    int n = 0;
    for (const Model& m : gs.units[kMarines].models) n += model_within_objective(m, gs.mission->objectives[0]) ? 1 : 0;
    return n;
}

// Applies an auto move on a copy, each proposal validated, and the move able to end.
GameState applied(const GameState& gs, const AutoFightMove& plan) {
    EXPECT_TRUE(plan.ok()) << (plan.problems.empty() ? "" : plan.problems.front());
    GameState work = gs;
    for (const GameAction& a : plan.moves) {
        const ValidationResult v = validate_action(work, Side::A, a);
        EXPECT_TRUE(v.ok()) << (v.reasons.empty() ? "" : v.reasons.front());
        apply_action(work, Side::A, a);
    }
    EXPECT_TRUE(move_end_problems(work).empty());
    return work;
}

}  // namespace

TEST(M90_Board, ClickingTheUnitStartsItsObjectiveConsolidation) {
    const GameState gs = objective_consolidation_state();
    BoardInteraction board(gs, legal_actions(gs, Side::A));
    EXPECT_TRUE(board.clickable(marines(gs)));
    const ClickResult r = board.click(gs, marines(gs));
    ASSERT_TRUE(r.submit.has_value());
    EXPECT_EQ(*r.submit, GameAction{(action::StartConsolidation{marines(gs), {}, 0})});
}

TEST(M90_Board, SeveralObjectivesInRangeGiveAMenuNamingThem) {
    GameState gs = objective_consolidation_state();
    gs.mission->objectives.push_back(objective_area("West", at(2.0f, 4.0f), 2.0f));  // 2.4" west of the row
    gs.pending = compute_decision(gs);
    BoardInteraction board(gs, legal_actions(gs, Side::A));
    const ClickResult r = board.click(gs, marines(gs));
    EXPECT_FALSE(r.submit.has_value());
    ASSERT_EQ(r.menu.size(), 3u);  // the two objectives, then Cancel
    EXPECT_NE(r.menu[0].label.find("onto Centre"), std::string::npos) << r.menu[0].label;
    EXPECT_NE(r.menu[1].label.find("onto West"), std::string::npos) << r.menu[1].label;
    EXPECT_EQ(r.menu[1].action, GameAction{(action::StartConsolidation{marines(gs), {}, 1})});
}

TEST(M90_AutoMove, AutoMoveGetsModelsWithinRangeOfTheObjective) {
    for (const float gap : {0.5f, 2.0f, 2.9f}) {
        SCOPED_TRACE(gap);
        const GameState gs = started(gap);
        ASSERT_TRUE(auto_fight_move_available(gs));
        const GameState after = applied(gs, auto_fight_move(gs));
        EXPECT_GT(within_range(after), within_range(gs));
        // Every moved model ended within range or closer (the engine's While Moving check, as the skip measures it).
        for (const Model& m : after.units[kMarines].models) {
            const Model& before = *find_model(gs, m.id);
            if (m.position == before.position) continue;
            EXPECT_LT(*gap_to_closest_target(after, m, m.position) - 0.01f,
                      *gap_to_closest_target(gs, before, before.position));
        }
    }
}

TEST(M90_Skip, TheGapIsToTheObjectiveAndAUnitAlreadyInRangeSkipsTheStep) {
    const GameState moving = started(2.0f);
    const Model& middle = moving.units[kMarines].models[2];
    EXPECT_NEAR(*gap_to_closest_target(moving, middle, middle.position), wh40k::test::in(2.0f), 0.01f);

    // Every Intercessor within the area: nothing worth moving for, so the step is skipped (D-047).
    GameState inside = objective_consolidation_state(-6.0f, 14.0f);
    for (const Model& m : inside.units[kMarines].models) ASSERT_TRUE(model_within_objective(m, inside.mission->objectives[0]));
    const std::vector<GameAction> legal = legal_actions(inside, Side::A);
    EXPECT_FALSE(fight_move_worthwhile(inside, Side::A, legal.at(1)));
    EXPECT_EQ(pointless_fight_step_skip(inside, legal), std::optional<GameAction>{action::EndConsolidations{}});

    // 2" away it is worth making.
    const GameState near = objective_consolidation_state(2.0f);
    EXPECT_EQ(pointless_fight_step_skip(near, legal_actions(near, Side::A)), std::nullopt);
}

TEST(M90_Text, TheActionEventAndBoardNameTheObjective) {
    const GameState gs = objective_consolidation_state();
    const std::string label = action_label(gs, action::StartConsolidation{marines(gs), {}, 0});
    EXPECT_EQ(label.rfind("Consolidate: ", 0), 0u) << label;
    EXPECT_NE(label.find(" onto Centre"), std::string::npos) << label;
    EXPECT_EQ(format_event(event::ConsolidationStarted{UnitId{1}, ConsolidationMode::Objective, {}, 0, "Centre"}),
              "[12.08] unit 1 consolidates (Objective, Centre)");
    EXPECT_EQ(format_event(event::ConsolidationStarted{UnitId{1}, ConsolidationMode::Engaging, {UnitId{2}}}),
              "[12.08] unit 1 consolidates (Engaging)");  // as before
    // The board outlines the objective being consolidated onto, only during that move.
    EXPECT_FALSE(objective_marks(gs).at(0).consolidating);
    EXPECT_TRUE(objective_marks(started(2.0f)).at(0).consolidating);
}

TEST(M90_Builder, TheFlamersCanBePicked) {
    ArmyBuilder b(wh40k::test::repo_root());
    ASSERT_FALSE(b.add_unit(Side::A, "data/units/havocs.json"));
    const std::size_t havocs = b.list(Side::A).units.size() - 1;
    bool offered = false;
    for (const GroupView& g : b.groups(Side::A, havocs)) {
        for (const SlotView& slot : g.slots) {
            for (const OptionView& o : slot.options) {
                if (o.name != "Flamer") continue;
                offered = true;
                EXPECT_FALSE(o.refusal.has_value()) << *o.refusal;
            }
        }
    }
    EXPECT_TRUE(offered);
    // The Terminators' Heavy weapon type (a Heavy flamer) can be added.
    ASSERT_FALSE(b.add_unit(Side::A, "data/units/chaos_terminator_squad.json"));
    const std::size_t terminators = b.list(Side::A).units.size() - 1;
    EXPECT_FALSE(b.add_model_type(Side::A, terminators, "Heavy weapon").has_value());
}
