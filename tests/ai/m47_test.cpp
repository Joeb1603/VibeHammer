// M47: the bots and the default casualty order (D-058): the bot's defaults (once level Easy) for casualties and the
// 03.03 pick, and the fallback's. D-085 (M85): Hard's fourth casualty candidate, Easy's coherency in the arena and the
// Easy-vs-Easy replay tests/replays/m47_default_casualties.json went with those levels.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "casualty_order.hpp"
#include "casualty_plan.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "core/state/queries.hpp"
#include "fallback.hpp"
#include "normal/defaults.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::in;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

// Side A's Intercessors fire `shooters` Bolt Rifles (Focused Fire) at the Termagants, `damaging` of the attacks
// inflicting damage (each destroys a Termagant): side B's casualty decision (as M41's after_marine_volley).
GameState after_marine_volley(std::size_t shooters, int damaging) {
    GameState gs = wh40k::test::shoot_step_state(Side::A);
    std::vector<ShotDeclaration> shots;
    for (std::size_t i = 0; i < shooters; ++i) {
        shots.push_back(ShotDeclaration{gs.units[kMarines].models[i].id, 0, 0, gs.units[kGants].id});
    }
    const int dice_count = static_cast<int>(shooters) * 2;  // [RAPID FIRE 1] within half range
    std::vector<int> rolls;
    for (int i = 0; i < dice_count; ++i) rolls.push_back(i < damaging ? 6 : 1);  // hits
    for (int i = 0; i < damaging; ++i) rolls.push_back(6);                        // wounds
    for (int i = 0; i < damaging; ++i) rolls.push_back(1);                        // saves fail
    ScriptedDice dice(rolls);
    (void)apply_action(gs, Side::A, action::Shoot{gs.units[kMarines].id, ShootingType::Normal, shots}, dice);
    EXPECT_TRUE(gs.pending && std::holds_alternative<decision::OrderCasualties>(gs.pending->kind));
    return gs;
}

// M28's End-of-Turn setup with one wanderer: Intercessor `wanderer` steps 5" east of the line in the Movement
// phase, so side A must remove a model in the End of Turn step (03.03).
GameState end_of_turn_out_of_coherency(std::size_t wanderer) {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (gs.flow.stage != Stage::Charge) {
        (void)apply_action(gs, gs.pending->side, legal_actions(gs, gs.pending->side).front());
    }
    gs.units[kMarines].models[wanderer].position.x += in(5.0f) + in(1.26f) * static_cast<float>(4 - wanderer);
    (void)apply_action(gs, Side::A, action::EndChargeStep{});
    EXPECT_TRUE(gs.pending && std::holds_alternative<decision::RestoreCoherency>(gs.pending->kind));
    return gs;
}

}  // namespace

// ---- The defaults (once level Easy) ----

TEST(M47_Defaults, CasualtiesUseTheDefaultOrder) {
    // Two Termagants die to a volley: the defaults (the Termagants) answer with the default order, the rear rank first.
    const GameState gs = after_marine_volley(2, 2);
    const UnitId gants = gs.units[kGants].id;
    const PolicyResult c = normal::default_answer(gs, Side::B, legal_actions(gs, Side::B), decision_seed(gs));
    ASSERT_TRUE(std::holds_alternative<action::OrderCasualties>(c.action)) << c.reason;
    EXPECT_TRUE(validate_action(gs, Side::B, c.action).ok());
    EXPECT_EQ(std::get<action::OrderCasualties>(c.action), client::CasualtyPlan(gs, gants).action());
    EXPECT_EQ(std::get<action::OrderCasualties>(c.action).priority, client::default_casualty_order(gs, gants));
    GameState after = gs;
    (void)apply_action(after, Side::B, c.action);
    EXPECT_EQ(after.units[kGants].models.size(), 8u);
    EXPECT_TRUE(unit_coherency(after.units[kGants]).ok());
    // The two lost are rear-rank models (further from the Intercessors than any front-rank model).
    float front_y = 1e9f;
    for (const Model& m : gs.units[kGants].models) front_y = std::min(front_y, m.position.y);
    int front_left = 0;
    for (const Model& m : after.units[kGants].models) front_left += m.position.y <= front_y + EPS_MM ? 1 : 0;
    EXPECT_EQ(front_left, 5);
}

TEST(M47_Defaults, RemoveTheModelThatRestoresCoherency) {
    // An Intercessor wandered off (03.03, End of Turn): before M47 level Easy removed the first model offered (the
    // Sergeant) and then still had to remove the wanderer; now the defaults remove the wanderer, and that is enough.
    GameState gs = end_of_turn_out_of_coherency(3);
    const UnitId marines = gs.units[kMarines].id;
    const ModelId wanderer = gs.units[kMarines].models[3].id;
    const std::vector<GameAction> legal = legal_actions(gs, Side::A);
    ASSERT_FALSE(legal.empty());
    EXPECT_EQ(legal.front(), (GameAction{action::RemoveModelForCoherency{marines, gs.units[kMarines].models[0].id}}));
    const PolicyResult c = normal::default_answer(gs, Side::A, legal, decision_seed(gs));
    EXPECT_EQ(c.action, (GameAction{action::RemoveModelForCoherency{marines, wanderer}})) << c.reason;
    EXPECT_EQ(client::default_coherency_removal(gs, marines), wanderer);
    (void)apply_action(gs, Side::A, c.action);
    EXPECT_TRUE(unit_coherency(gs.units[kMarines]).ok());
    EXPECT_EQ(gs.units[kMarines].models.size(), 4u);
    ASSERT_TRUE(gs.pending.has_value());
    EXPECT_FALSE(std::holds_alternative<decision::RestoreCoherency>(gs.pending->kind));
}

// ---- the fallback ----

TEST(M47_Fallback, UsesTheDefaultOrderAndTheDefaultCoherencyPick) {
    const GameState casualties = after_marine_volley(1, 1);
    const FallbackResult f = fallback_action(casualties, Side::B);
    ASSERT_TRUE(f.action.has_value());
    EXPECT_EQ(*f.action, GameAction{client::CasualtyPlan(casualties, casualties.units[kGants].id).action()});

    const GameState coherency = end_of_turn_out_of_coherency(3);
    const FallbackResult g = fallback_action(coherency, Side::A);
    ASSERT_TRUE(g.action.has_value());
    const UnitId marines = coherency.units[kMarines].id;
    EXPECT_EQ(*g.action, (GameAction{action::RemoveModelForCoherency{
                             marines, *client::default_coherency_removal(coherency, marines)}}));
    EXPECT_EQ(std::get<action::RemoveModelForCoherency>(*g.action).model, coherency.units[kMarines].models[3].id);
}
