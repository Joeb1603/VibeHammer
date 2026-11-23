// M92 (D-093): the client's text for 04.03.03 RetargetAttacks: the engine's answers as buttons, and the log line of
// AttacksRetargeted. The dice cards follow the new batches.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "action_text.hpp"
#include "core/engine/engine.hpp"
#include "event_text.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;

TEST(M92_ClientText, RetargetAnswersAndTheEventReadPlainly) {
    const GameState gs = wh40k::test::shoot_step_state();
    const UnitId gants = gs.units[1].id;
    const std::string none = action_label(gs, action::RetargetAttacks{});
    EXPECT_EQ(none, "Select no new targets (the attacks are lost)");
    const std::string two = action_label(gs, action::RetargetAttacks{{AttackRetarget{1, gants}, AttackRetarget{3, gants}}});
    EXPECT_EQ(two.rfind("Retarget 2 weapons at ", 0), 0u) << two;
    EXPECT_NE(two.find("(" + std::to_string(gants.v) + ")"), std::string::npos) << two;

    AttackBatch batch;
    batch.target = UnitId{3};
    batch.attack_dice = 4;
    const event::AttacksRetargeted e{UnitId{1}, {AttackRetarget{1, UnitId{3}}}, {}, 2, {batch}};
    EXPECT_EQ(format_event(e), "[D-093] unit 1 selects new targets for 1 weapon: unit 3 (4 attack dice)");
}
