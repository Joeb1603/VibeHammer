// M92 (D-093): the bot answers 04.03.03 RetargetAttacks: the new target with the most expected value, through
// Bot::decide, the defaults and the always-legal fallback.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "bot.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/model/keywords.hpp"
#include "core/state/queries.hpp"
#include "default_choice.hpp"
#include "fallback.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::add_unit_copy;
using wh40k::test::model_ids;
using wh40k::test::select;

namespace {

// As tests/m92_test.cpp: the first Termagant unit (one model) is destroyed by batch 0, batch 1 (Focused Fire) waits
// for a new target, and the second unit is eligible.
GameState at_the_decision(UnitId* second_out) {
    GameState gs = wh40k::test::shoot_step_state();
    const UnitId gants = gs.units[1].id;
    const UnitId second = add_unit_copy(gs, 1);
    gs.units[1].models.resize(1);
    const AttackContext shooting{AttackType::Shooting, true};
    const std::vector<WeaponSelection> sel{select(gs, 0, 0, 0, 1, gants), select(gs, 0, 1, 0, 0, gants),
                                           select(gs, 0, 2, 0, 1, second)};
    ScriptedDice dice{3, 3, 3, 3, 3, 3, 1, 1, 1};
    begin_attacks(gs, dice, gs.units[0].id, shooting, sel);
    apply_action(gs, Side::A, action::SelectAttackBatch{0}, dice);
    apply_action(gs, Side::B, action::OrderCasualties{gants, model_ids(gs.units[1])}, dice);
    *second_out = second;
    return gs;
}

}  // namespace

TEST(M92_Bots, TheBotRetargetsAtTheEligibleUnit) {
    UnitId second;
    const GameState gs = at_the_decision(&second);
    ASSERT_TRUE(std::holds_alternative<decision::RetargetAttacks>(gs.pending->kind));
    const GameAction expected = action::RetargetAttacks{{AttackRetarget{1, second}}};
    for (const bool defaults : {false, true}) {
        Bot bot(BotConfig{});
        const BotChoice c = defaults ? wh40k::test::default_choice(gs, Side::A) : bot.decide(gs, Side::A);
        ASSERT_FALSE(c.stopped) << defaults;
        EXPECT_EQ(c.action, expected) << defaults;
        EXPECT_TRUE(validate_action(gs, Side::A, c.action).ok());
        EXPECT_EQ(bot.fallbacks(), 0u) << defaults;
    }
    const FallbackResult fb = fallback_action(gs, Side::A);
    ASSERT_TRUE(fb.action.has_value());
    EXPECT_EQ(*fb.action, expected);
}

// ==== M93 (D-094): 13.11.01 Gone to Ground in the move score, behind gone_to_ground ====

#include "analysis/terrain_sight.hpp"
#include "arena.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/rules/terrain.hpp"
#include "normal/move_score.hpp"

namespace {

std::string per_game_field(const std::string& line, const std::string& key) {
    const std::size_t at = line.find(key + " ");
    if (at == std::string::npos) return "";
    const std::size_t start = at + key.size() + 1;
    return line.substr(start, line.find_first_of(" \n", start) - start);
}

}  // namespace

TEST(M93_GoneToGround, TheKeyIsOffByDefaultAndAGameWithItOnPlaysCleanly) {
    EXPECT_EQ(normal::MoveWeights{}.gone_to_ground, 0.0);  // D-094: measured +8 in 400, so off (D-092's keep rule)
    const GameState scenario =
        load_scenario(wh40k::test::repo_root() / "data/scenarios/incursion_terrain_44x60.json", wh40k::test::repo_root());
    BotConfig on;
    on.move.gone_to_ground = 1.0;
    const PerGameReport with_it = run_per_game(scenario, 1, 1, on, on);
    EXPECT_TRUE(with_it.clean);
    const PerGameReport without = run_per_game(scenario, 1, 1, BotConfig{}, BotConfig{});
    EXPECT_TRUE(without.clean);
    EXPECT_FALSE(per_game_field(with_it.text, "actions").empty());
}

TEST(M93_GoneToGround, TheSightSeesADenseFeatureBetweenAModelAndAPoint) {
    const GameState gs =
        load_scenario(wh40k::test::repo_root() / "data/scenarios/incursion_terrain_44x60.json", wh40k::test::repo_root());
    analysis::TerrainSight sight(gs);
    const Base base{BaseShape::Round, 32.0f, 0.0f};
    // The Centre ruin's south wall runs along y 27"-27.5" from x 17" to 23": a model at (20", 29") looking south at
    // (20", 20") has it between; looking north at (20", 40") it does not (the north wall spans x 21"-27").
    const BasePose at{wh40k::test::at(20.0f, 29.0f), base, 0.0f};
    EXPECT_TRUE(sight.dense_between(at, wh40k::test::at(20.0f, 20.0f)));
    EXPECT_FALSE(sight.dense_between(at, wh40k::test::at(20.0f, 40.0f)));
}
