// M65 (D-074 part 2): what the client logic does with an oval base before M66 converts it. The replay viewer plays the
// Tyrannofex replay and plans its rings at every frame (the rings use the base's circumscribed radius: exact for the
// movement and charge rings, which a model may reach by turning, and the radius itself for a round base); the auto
// moves (D-050, D-056) refuse cleanly while an oval base is on the battlefield instead of throwing.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "auto_charge_move.hpp"
#include "auto_fight_move.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/pose.hpp"
#include "overlay_plan.hpp"
#include "support/engine_helpers.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"
#include "timeline.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
namespace tf = wh40k::test::tyrannofex;

namespace {

const std::filesystem::path kOvalReplay = std::filesystem::path(WH40K_REPLAY_DIR) / "m65_oval_tyrannofex.json";

template <typename Done>
void play_until(GameState& gs, Done done) {
    wh40k::test::play_neutral_until(gs, done);
}

}  // namespace

TEST(M65_Replay, TheOvalReplayPlaysInTheViewerWithItsRings) {
    const Replay replay = load_replay(kOvalReplay);
    const Timeline timeline = Timeline::from_replay(replay, wh40k::test::repo_root(), "m65");
    ASSERT_TRUE(timeline.error().empty()) << timeline.error();
    ASSERT_EQ(timeline.size(), replay.steps.size() + 1);
    bool turned = false;
    bool ringed = false;
    for (std::size_t i = 0; i < timeline.size(); ++i) {
        const GameState& gs = timeline.frame(i).state;
        const Model& t = gs.units[tf::kTyrannofex].models[0];
        turned = turned || t.facing_deg == 45.0f;
        // Every ring of every model at every frame, as the viewer draws them (the Tyrannofex selected).
        const std::vector<Ring> rings = plan_overlays(gs, t.id, OverlayToggles{});
        const std::vector<Circle> circles = ring_circles(gs, rings);
        ASSERT_EQ(circles.size(), rings.size()) << "frame " << i;
        for (std::size_t k = 0; k < rings.size(); ++k) {
            if (rings[k].model != t.id) continue;
            ringed = true;
            EXPECT_FLOAT_EQ(circles[k].radius_mm, circumscribed_radius(t.base) + rings[k].distance_mm) << "frame " << i;
        }
    }
    EXPECT_TRUE(turned);
    EXPECT_TRUE(ringed);
}

TEST(M65_Replay, ARoundBasesRingsAreAsBefore) {
    // The radius of a round base's ring is its radius plus the distance, as before M65 (circumscribed_radius is the
    // radius for a round base).
    GameState gs = tf::fixture(Side::A);
    start_game(gs);
    play_until(gs, [](const GameState& g) { return g.pending && std::holds_alternative<decision::MoveUnits>(g.pending->kind); });
    apply_action(gs, Side::A, action::StartMove{gs.units[tf::kLegionaries].id, MoveType::Normal, std::nullopt});
    const std::vector<Ring> rings = plan_overlays(gs, std::nullopt, OverlayToggles{});
    const std::vector<Circle> circles = ring_circles(gs, rings);
    ASSERT_FALSE(rings.empty());
    for (std::size_t k = 0; k < rings.size(); ++k) {
        const Model* m = find_model(gs, rings[k].model);
        ASSERT_NE(m, nullptr);
        const float r = m->base.shape == BaseShape::Round ? base_radius(m->base) : circumscribed_radius(m->base);
        EXPECT_EQ(circles[k].radius_mm, r + rings[k].distance_mm);
    }
}

TEST(M65_AutoMove, OvalBasesAreRefusedCleanlyUntilM66) {
    // D-074 part 2: auto_charge_move and auto_fight_move are planned on round-only geometry (M66 converts them). While
    // an oval base is on the battlefield they return no arrangement, with a D-074 reason, and never throw.
    GameState gs = tf::fixture(Side::B);
    tf::the_tyrannofex(gs).position = wh40k::test::at(22.0f, 31.0f);
    tf::the_tyrannofex(gs).facing_deg = 90.0f;
    start_game(gs);
    play_until(gs, [](const GameState& g) { return g.pending && std::holds_alternative<decision::Charge>(g.pending->kind); });
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::Charge>(gs.pending->kind));
    ScriptedDice dice{4, 4};
    apply_action(gs, Side::B, action::DeclareCharge{gs.units[tf::kTyrannofex].id}, dice);
    apply_action(gs, Side::B, action::StartChargeMove{gs.units[tf::kTyrannofex].id, {gs.units[tf::kLegionaries].id}});
    ASSERT_TRUE(auto_charge_move_available(gs));
    const AutoChargeMove charge = auto_charge_move(gs);
    EXPECT_FALSE(charge.ok());
    EXPECT_TRUE(charge.moves.empty());
    ASSERT_FALSE(charge.problems.empty());
    EXPECT_EQ(charge.problems.front(), "D-074: Auto move does not handle oval bases yet");

    // A pile-in, after a charge that ends 1" away.
    const Position s = gs.units[tf::kTyrannofex].models[0].position;
    apply_action(gs, Side::B,
                 action::ProposeModelMove{gs.units[tf::kTyrannofex].models[0].id, {Position{s.x, s.y - 20.0f, 0.0f}}, {}});
    apply_action(gs, Side::B, action::CommitUnitMove{});
    ASSERT_TRUE(gs.units[tf::kTyrannofex].turn_flags.made_charge_move);
    play_until(gs, [](const GameState& g) { return g.pending && std::holds_alternative<decision::PileIn>(g.pending->kind); });
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::PileIn>(gs.pending->kind));
    apply_action(gs, gs.pending->side,
                 action::StartPileIn{gs.units[tf::kTyrannofex].id, {gs.units[tf::kLegionaries].id}});
    ASSERT_TRUE(auto_fight_move_available(gs));
    const AutoFightMove pile_in = auto_fight_move(gs);
    EXPECT_FALSE(pile_in.ok());
    EXPECT_TRUE(pile_in.moves.empty());
    ASSERT_FALSE(pile_in.problems.empty());
    EXPECT_EQ(pile_in.problems.front(), "D-074: Auto move does not handle oval bases yet");
}
