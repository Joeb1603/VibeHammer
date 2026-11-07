// M47: the default casualty order, furthest from the action first, keeping coherency (D-058): 03.03, 05.04, 06.02.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "auto_actions.hpp"
#include "board_interaction.hpp"
#include "casualty_order.hpp"
#include "casualty_picks.hpp"
#include "casualty_plan.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/state/queries.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::at;
using wh40k::test::in;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;
constexpr std::size_t kFleshborer = 0;

// The rows of the V1 formation (D-008), read from the loaded scenario (inches).
float y_in(const Model& m) { return mm_to_inches(m.position.y); }
float x_in(const Model& m) { return mm_to_inches(m.position.x); }

// The positions in `order` (model ids) as indices in unit order, so the expected orders read like the formation.
std::vector<std::size_t> indices(const Unit& unit, const std::vector<ModelId>& order) {
    std::vector<std::size_t> out;
    for (const ModelId id : order) {
        for (std::size_t i = 0; i < unit.models.size(); ++i) {
            if (unit.models[i].id == id) out.push_back(i);
        }
    }
    return out;
}

std::vector<std::size_t> order_of(const GameState& gs, std::size_t unit) {
    return indices(gs.units[unit], default_casualty_order(gs, gs.units[unit].id));
}

// Whether the unit is in coherency (03.03) after losing each prefix of `order` (indices), up to one model left.
bool every_prefix_coherent(const GameState& gs, std::size_t unit, const std::vector<std::size_t>& order) {
    const std::vector<PlacedModel> all = placed_models(gs.units[unit]);
    std::set<std::size_t> gone;
    for (const std::size_t i : order) {
        gone.insert(i);
        std::vector<PlacedModel> rest;
        for (std::size_t j = 0; j < all.size(); ++j) {
            if (gone.count(j) == 0) rest.push_back(all[j]);
        }
        if (!check_coherency(rest).ok()) return false;
    }
    return true;
}

// The pre-M47 Normal bot's coherency_losses (M41), copied verbatim from src/ai/normal/defense.cpp, as the
// reference the moved function must match.
int old_coherency_problems(const std::vector<PlacedModel>& models) {
    const CoherencyResult r = check_coherency(models);
    return static_cast<int>(r.isolated.size() + r.too_far.size());
}

int old_coherency_losses(std::vector<PlacedModel> models) {
    int removed = 0;
    while (models.size() > 1 && old_coherency_problems(models) > 0) {
        std::size_t best = 0;
        int best_problems = -1;
        for (std::size_t i = 0; i < models.size(); ++i) {
            std::vector<PlacedModel> rest = models;
            rest.erase(rest.begin() + static_cast<std::ptrdiff_t>(i));
            const int p = old_coherency_problems(rest);
            if (best_problems < 0 || p < best_problems) {
                best = i;
                best_problems = p;
            }
        }
        models.erase(models.begin() + static_cast<std::ptrdiff_t>(best));
        ++removed;
    }
    return removed;
}

// Every subset of `models` (at most 12): the new coherency_losses equals the old one.
void expect_same_losses_on_every_subset(const std::vector<PlacedModel>& models, const std::string& label) {
    ASSERT_LE(models.size(), 12u);
    const std::size_t n = models.size();
    int out_of_coherency = 0;
    for (std::size_t mask = 0; mask < (std::size_t{1} << n); ++mask) {
        std::vector<PlacedModel> subset;
        for (std::size_t i = 0; i < n; ++i) {
            if ((mask >> i) & 1u) subset.push_back(models[i]);
        }
        const int expected = old_coherency_losses(subset);
        if (expected > 0) ++out_of_coherency;
        ASSERT_EQ(coherency_losses(subset), expected) << label << ", subset " << mask;
    }
    EXPECT_GT(out_of_coherency, 0) << label;  // the layouts do exercise the greedy
}

// M35's fixture: side B's Termagants shoot the Intercessors with `n` fleshborers, n failed saves of 1 damage
// each; side A must pick the casualties.
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

// The Termagants as five models at the given positions (inches).
void five_termagants_at(GameState& gs, const std::vector<Position>& at_in) {
    std::vector<Model>& ms = gs.units[kGants].models;
    ms.resize(at_in.size());
    for (std::size_t i = 0; i < at_in.size(); ++i) ms[i].position = at_in[i];
}

}  // namespace

// ---- coherency_losses, moved from the Normal bot ----

TEST(M47_CoherencyLosses, MatchesTheOldGreedy) {
    const GameState v1 = wh40k::test::load_v1_scenario();
    expect_same_losses_on_every_subset(placed_models(v1.units[kGants]), "V1 Termagants");

    // M41's layouts: a link and a side model; a wanderer 3.5" east; a pair more than 9" from the rest.
    std::vector<PlacedModel> link;
    const float y0 = 15.37f;
    const std::vector<Position> link_at{at(12.0f, y0),        at(14.0f, y0 + 0.3f), at(7.5f, y0 + 1.5f),
                                        at(9.0f, y0 + 1.5f),  at(10.5f, y0 + 1.5f), at(7.5f, y0 + 3.0f),
                                        at(9.0f, y0 + 3.0f),  at(10.5f, y0 + 3.0f), at(7.5f, y0 + 4.5f),
                                        at(9.0f, y0 + 4.5f)};
    for (std::size_t i = 0; i < link_at.size(); ++i) {
        link.push_back(wh40k::test::placed(static_cast<std::uint32_t>(i + 1), link_at[i], wh40k::test::kTermagantBase));
    }
    expect_same_losses_on_every_subset(link, "M41 link layout");

    std::vector<PlacedModel> wanderer = placed_models(v1.units[kGants]);
    float east = 0.0f;
    for (const PlacedModel& p : wanderer) east = std::max(east, p.position.x);
    wanderer[3].position.x = east + in(3.5f);
    expect_same_losses_on_every_subset(wanderer, "M41 wanderer");

    std::vector<PlacedModel> pair = placed_models(v1.units[kGants]);
    pair[8].position = at(22.0f, y_in(v1.units[kGants].models[0]));
    pair[9].position = at(23.2f, y_in(v1.units[kGants].models[0]));
    expect_same_losses_on_every_subset(pair, "M41 pair");

    // Scattered layouts (a fixed test RNG): bases of both sizes in a 12" square.
    wh40k::test::TestRng rng(47);
    for (int layout = 0; layout < 12; ++layout) {
        std::vector<PlacedModel> scattered;
        for (std::uint32_t i = 0; i < 9; ++i) {
            const float x = 2.0f + static_cast<float>(rng.below(1200)) / 100.0f;
            const float y = 2.0f + static_cast<float>(rng.below(1200)) / 100.0f;
            scattered.push_back(wh40k::test::placed(i + 1, at(x, y),
                                                    rng.below(2) == 0 ? wh40k::test::kTermagantBase
                                                                      : wh40k::test::kIntercessorBase));
        }
        expect_same_losses_on_every_subset(scattered, "scattered layout " + std::to_string(layout));
    }
}

// ---- the order (D-058) ----

TEST(M47_CasualtyOrder, TheBackRankGoesFirstAndEveryPrefixIsCoherent) {
    // V1 (D-008): Termagants 0-4 are the front rank, 5-9 the rear rank, each left to right; the Intercessors are
    // to the south. The rear corners are furthest from them (by 0.07 mm over the centre: beyond EPS_MM), the later
    // of each tie first; then the front rank the same way.
    const GameState gs = wh40k::test::load_v1_scenario();
    const Unit& gants = gs.units[kGants];
    ASSERT_GT(y_in(gants.models[5]), y_in(gants.models[0]));
    const std::vector<std::size_t> order = order_of(gs, kGants);
    EXPECT_EQ(order, (std::vector<std::size_t>{9, 5, 8, 6, 7, 4, 0, 3, 1, 2}));
    EXPECT_TRUE(every_prefix_coherent(gs, kGants, order));
    EXPECT_EQ(CasualtyPlan(gs, gants.id).priority(), default_casualty_order(gs, gants.id));
}

TEST(M47_CasualtyOrder, ASideStragglerGoesFirst) {
    // Termagant 4 (the front rank's east end) stands out east of the rear rank: within 2" of model 9 only (03.03),
    // so it is at risk of being cut off. It goes first, though it is nearer the Intercessors than the rear rank.
    GameState gs = wh40k::test::load_v1_scenario();
    Unit& gants = gs.units[kGants];
    gants.models[4].position = at(16.8f, y_in(gants.models[0]));
    ASSERT_TRUE(unit_coherency(gants).ok());
    const std::vector<PlacedModel> placed = placed_models(gants);
    int near_4 = 0;
    for (std::size_t j = 0; j < placed.size(); ++j) {
        if (j == 4) continue;
        const float d = base_distance(placed[4].position, placed[4].base, placed[j].position, placed[j].base);
        if (within(d, kCoherencyNearMm)) {
            ++near_4;
            EXPECT_EQ(j, 9u);
        }
    }
    ASSERT_EQ(near_4, 1);
    const std::vector<std::size_t> order = order_of(gs, kGants);
    ASSERT_GE(order.size(), 2u);
    EXPECT_EQ(order[0], 4u);
    EXPECT_EQ(order[1], 9u);  // then the rear rank as before
    EXPECT_TRUE(every_prefix_coherent(gs, kGants, order));
}

TEST(M47_CasualtyOrder, ALineKeepsCoherency) {
    // Five Termagants in a bent line, the tip (C) furthest from the Intercessors: A(8,14) B(10,15.5) C(12,17)
    // D(14,15.5) E(16.2,14). By distance alone C, then B, would go first, and A would be left isolated (03.03).
    // The order eats the line from an end instead: E, D, C, B, A, in coherency after every casualty.
    GameState gs = wh40k::test::load_v1_scenario();
    five_termagants_at(gs, {at(8.0f, 14.0f), at(10.0f, 15.5f), at(12.0f, 17.0f), at(14.0f, 15.5f), at(16.2f, 14.0f)});
    ASSERT_TRUE(unit_coherency(gs.units[kGants]).ok());
    const std::vector<std::size_t> order = order_of(gs, kGants);
    EXPECT_EQ(order, (std::vector<std::size_t>{4, 3, 2, 1, 0}));
    EXPECT_TRUE(every_prefix_coherent(gs, kGants, order));
    // By distance alone (furthest first): C, B, D, ... and losing C and B leaves A isolated.
    std::vector<std::pair<float, std::size_t>> by_gap;
    for (std::size_t i = 0; i < 5; ++i) {
        const Model& m = gs.units[kGants].models[i];
        by_gap.emplace_back(*model_distance_to_unit(m, m.position, gs.units[kMarines]), i);
    }
    std::sort(by_gap.begin(), by_gap.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    EXPECT_EQ(by_gap[0].second, 2u);
    EXPECT_EQ(by_gap[1].second, 1u);
    EXPECT_FALSE(every_prefix_coherent(gs, kGants, {by_gap[0].second, by_gap[1].second}));
}

TEST(M47_CasualtyOrder, AnIsolatedModelGoesFirstEvenAtTheFront) {
    // The centre of the front rank has stepped 3" towards the Intercessors: out of coherency (03.03), so losing
    // it costs nothing more, though it is the model closest to the enemy.
    GameState gs = wh40k::test::load_v1_scenario();
    gs.units[kGants].models[2].position.y -= in(3.0f);
    ASSERT_FALSE(unit_coherency(gs.units[kGants]).ok());
    const std::vector<std::size_t> order = order_of(gs, kGants);
    ASSERT_FALSE(order.empty());
    EXPECT_EQ(order[0], 2u);
    EXPECT_TRUE(every_prefix_coherent(gs, kGants, order));
}

TEST(M47_CasualtyOrder, TheSergeantIsLast) {
    // The Intercessor Sergeant (model 0) is the unit's only model of its type: kept until last (D-058).
    GameState gs = wh40k::test::load_v1_scenario();
    ASSERT_EQ(gs.units[kMarines].models[0].type_name, "Intercessor Sergeant");
    std::vector<std::size_t> order = order_of(gs, kMarines);
    ASSERT_EQ(order.size(), 5u);
    EXPECT_EQ(order.back(), 0u);
    EXPECT_TRUE(every_prefix_coherent(gs, kMarines, order));

    // Also when he stands behind the line, the furthest of all from the Termagants.
    GameState behind = wh40k::test::load_v1_scenario();
    Unit& marines = behind.units[kMarines];
    marines.models[0].position = at(x_in(marines.models[1]), y_in(marines.models[1]) - 1.26f);
    ASSERT_TRUE(unit_coherency(marines).ok());
    order = order_of(behind, kMarines);
    ASSERT_EQ(order.size(), 5u);
    EXPECT_EQ(order.back(), 0u);
    EXPECT_TRUE(every_prefix_coherent(behind, kMarines, order));

    // With two models left: the Intercessor, then the Sergeant.
    GameState two = wh40k::test::load_v1_scenario();
    two.units[kMarines].models.resize(2);
    EXPECT_EQ(order_of(two, kMarines), (std::vector<std::size_t>{1, 0}));

    // Unless keeping him costs coherency: a Sergeant out of coherency goes first (key 1 before key 2).
    GameState isolated = wh40k::test::load_v1_scenario();
    isolated.units[kMarines].models[0].position.x -= in(4.0f);
    ASSERT_FALSE(unit_coherency(isolated.units[kMarines]).ok());
    order = order_of(isolated, kMarines);
    ASSERT_FALSE(order.empty());
    EXPECT_EQ(order.front(), 0u);
}

TEST(M47_CasualtyOrder, AWoundedModelTakesTheDamage) {
    // 05.04: a model that has lost wounds must take the damage. The order follows the engine's preview, so the
    // wounded Intercessor (index 2) comes first, and the action destroys it.
    GameSession session(casualties_pending(1, 2));
    const GameState gs = session.state();  // a copy: the session moves on
    const std::vector<std::size_t> order = order_of(gs, kMarines);
    ASSERT_EQ(order.size(), 5u);
    EXPECT_EQ(order.front(), 2u);
    EXPECT_EQ(order.back(), 0u);  // the Sergeant still last
    const ModelId wounded = mid(gs, 2);
    ASSERT_TRUE(session.submit(CasualtyPlan(gs, gs.units[kMarines].id).action()).ok());
    EXPECT_EQ(find_model(session.state().units[kMarines], wounded), nullptr);
}

TEST(M47_CasualtyOrder, APickedW2ModelKeepsTakingTheDamage) {
    // Three 1-damage attacks into fresh W2 Intercessors: the first pick (the model furthest from the Termagants,
    // index 4: the Sergeant at the other end ties but is kept) takes two and dies, the next (index 3) one.
    GameSession session(casualties_pending(3));
    const GameState gs = session.state();  // a copy: the session moves on
    const std::vector<std::size_t> order = order_of(gs, kMarines);
    EXPECT_EQ(order, (std::vector<std::size_t>{4, 3, 1, 2, 0}));
    ASSERT_TRUE(session.submit(CasualtyPlan(gs, gs.units[kMarines].id).action()).ok());
    const Unit& after = session.state().units[kMarines];
    EXPECT_EQ(after.models.size(), 4u);
    EXPECT_EQ(find_model(after, mid(gs, 4)), nullptr);
    ASSERT_NE(find_model(after, mid(gs, 3)), nullptr);
    EXPECT_EQ(find_model(after, mid(gs, 3))->wounds_remaining, 1);
}

TEST(M47_CasualtyOrder, InMeleeTheEngagedModelsStayAndTheSergeantIsLast) {
    // Intercessors at y 10; two Termagants at (14.52, 12.691) and (15.78, 12.691) engage Intercessors 3 and 4
    // (03.04, 2"). The engaged models are the action: the unengaged furthest goes first (1), then the engaged ones
    // (3, 4: removing 2 would leave the Sergeant isolated), then 2, the Sergeant last.
    GameState gs = wh40k::test::load_v1_scenario();
    for (Model& m : gs.units[kMarines].models) m.position.y = in(10.0f);
    gs.units[kGants].models.resize(2);
    gs.units[kGants].models[0].position = at(14.52f, 12.691f);
    gs.units[kGants].models[1].position = at(15.78f, 12.691f);
    const std::vector<ModelId> engaged =
        engaged_models(placed_models(gs.units[kMarines]), placed_models(gs.units[kGants]));
    ASSERT_EQ(engaged, (std::vector<ModelId>{mid(gs, 3), mid(gs, 4)}));
    const std::vector<std::size_t> order = order_of(gs, kMarines);
    EXPECT_EQ(order, (std::vector<std::size_t>{1, 3, 4, 2, 0}));
    EXPECT_TRUE(every_prefix_coherent(gs, kMarines, order));
}

TEST(M47_CasualtyOrder, DesperateEscapeHazardsFollowTheOrder) {
    // 06.02 / 09.07 (M41's setup): a battle-shocked Intercessor squad, its Sergeant engaged with a Termagant to
    // the west, falls back with Desperate Escape and two hazard rolls fail (2 mortal wounds). The default order
    // gives them to the model furthest from the enemy (index 4), which takes both (W2: destroyed).
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    const Position first = gs.units[kMarines].models[0].position;
    gs.units[kGants].models[0].position = Position{first.x - 16.0f - 14.25f - in(1), first.y, 0.0f};
    gs.units[kMarines].battle_shocked = true;
    gs.pending = compute_decision(gs);
    ScriptedDice dice{1, 1, 6, 6, 6};
    (void)apply_action(gs, Side::A,
                       action::StartMove{gs.units[kMarines].id, MoveType::FallBack, FallBackMode::DesperateEscape}, dice);
    ASSERT_EQ(*gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{gs.units[kMarines].id}}));
    const std::vector<std::size_t> order = order_of(gs, kMarines);
    ASSERT_EQ(order.size(), 5u);
    EXPECT_EQ(order.front(), 4u);
    EXPECT_EQ(order.back(), 0u);
    const ModelId furthest = mid(gs, 4);
    GameSession session(gs);
    ASSERT_TRUE(session.submit(CasualtyPlan(gs, gs.units[kMarines].id).action()).ok());
    const Unit& after = session.state().units[kMarines];
    EXPECT_EQ(after.models.size(), 4u);
    EXPECT_EQ(find_model(after, furthest), nullptr);
}

TEST(M47_CasualtyOrder, PicksComeFirstUnchangedAndEveryModelOnce) {
    const GameState gs = wh40k::test::load_v1_scenario();
    const Unit& gants = gs.units[kGants];
    const std::vector<ModelId> picks{gants.models[2].id, gants.models[7].id, gants.models[2].id, ModelId{999}};
    const std::vector<ModelId> order = default_casualty_order(gs, gants.id, picks);
    ASSERT_EQ(order.size(), gants.models.size());
    EXPECT_EQ(order[0], gants.models[2].id);
    EXPECT_EQ(order[1], gants.models[7].id);
    const std::set<ModelId> once(order.begin(), order.end());
    EXPECT_EQ(once.size(), order.size());
    EXPECT_TRUE(default_casualty_order(gs, UnitId{999}).empty());
}

// ---- where it is used ----

TEST(M47_CasualtyPicks, FillsTheRestWithTheDefaultOrder) {
    // Three 1-damage attacks into fresh W2 Intercessors. Picking model 1 gives it the first two; the rest of the
    // answer is the default order from there (05.04 through the preview: model 4 takes the third).
    GameSession session(casualties_pending(3));
    const GameState gs = session.state();  // a copy: the session moves on
    const UnitId marines = gs.units[kMarines].id;
    BoardInteraction board(gs, session.legal());
    ASSERT_NE(board.casualty_picks(), nullptr);
    EXPECT_EQ(board.default_order_label(), "Use default order");
    EXPECT_EQ(board.step_buttons(), (std::vector<GameAction>{CasualtyPlan(gs, marines).action()}));
    EXPECT_NE(board.hint(gs).find("or use the default order."), std::string::npos) << board.hint(gs);

    (void)board.click_model(gs, marines, mid(gs, 1));
    const CasualtyPicks& picks = *board.casualty_picks();
    ASSERT_EQ(picks.picks(), (std::vector<ModelId>{mid(gs, 1)}));
    ASSERT_FALSE(picks.done());
    const std::vector<ModelId> expected = default_casualty_order(gs, marines, picks.picks());
    EXPECT_EQ(indices(gs.units[kMarines], expected), (std::vector<std::size_t>{1, 4, 3, 2, 0}));
    EXPECT_EQ(picks.action().priority, expected);
    // "Default order for the rest" finishes from the pick.
    EXPECT_EQ(board.default_order_label(), "Default order for the rest");
    EXPECT_NE(board.hint(gs).find("or use the default order for the rest."), std::string::npos) << board.hint(gs);
    const std::vector<GameAction> steps = board.step_buttons();
    ASSERT_EQ(steps.size(), 1u);
    EXPECT_EQ(steps[0], GameAction{picks.action()});
    ASSERT_TRUE(session.submit(steps[0]).ok());
    const Unit& after = session.state().units[kMarines];
    EXPECT_EQ(find_model(after, mid(gs, 1)), nullptr);
    ASSERT_NE(find_model(after, mid(gs, 4)), nullptr);
    EXPECT_EQ(find_model(after, mid(gs, 4))->wounds_remaining, 1);

    // Undoing the pick goes back to the default order; once every casualty is picked the button goes (Confirm).
    BoardInteraction again(gs, legal_actions(gs, Side::A));
    (void)again.click_model(gs, marines, mid(gs, 1));
    again.undo_pick(gs);
    EXPECT_EQ(again.casualty_picks()->action().priority, default_casualty_order(gs, marines));
    EXPECT_EQ(again.default_order_label(), "Use default order");
    (void)again.click_model(gs, marines, mid(gs, 1));
    (void)again.click_model(gs, marines, mid(gs, 2));
    ASSERT_TRUE(again.casualty_picks()->done());
    EXPECT_TRUE(again.step_buttons().empty());
    ASSERT_TRUE(again.confirm().has_value());
    EXPECT_EQ(std::get<action::OrderCasualties>(*again.confirm()).priority,
              default_casualty_order(gs, marines, again.casualty_picks()->picks()));
}

TEST(M47_AutoActions, AutomaticAllocationUsesTheDefaultOrder) {
    // "Allocate damage automatically" (D-038) answers with CasualtyPlan, now the default order: one damage into
    // fresh W2 Intercessors goes to the model furthest from the Termagants (index 4), not the Sergeant (index 0,
    // as unit order did).
    GameSession session(casualties_pending(1));
    const GameState before = session.state();
    AutoSettings autos;
    autos.skip_empty_steps = false;
    autos.auto_allocate = true;
    const std::vector<GameAction> done = run_auto_actions(session, autos);
    ASSERT_FALSE(done.empty());
    const auto* order = std::get_if<action::OrderCasualties>(&done.front());
    ASSERT_NE(order, nullptr);
    EXPECT_EQ(order->priority, default_casualty_order(before, before.units[kMarines].id));
    EXPECT_EQ(indices(before.units[kMarines], order->priority).front(), 4u);
    const Unit& after = session.state().units[kMarines];
    ASSERT_EQ(after.models.size(), 5u);
    EXPECT_EQ(find_model(after, mid(before, 4))->wounds_remaining, 1);
    EXPECT_EQ(find_model(after, mid(before, 0))->wounds_remaining, 2);
}

// ---- the 03.03 pick ----

TEST(M47_CoherencyRemoval, RemovesTheModelThatRestoresCoherency) {
    // M41's setup: Termagant 3 wanders 3.5" east of the unit. Removing it restores coherency.
    GameState gs = wh40k::test::load_v1_scenario();
    Unit& gants = gs.units[kGants];
    float east = 0.0f;
    for (const Model& m : gants.models) east = std::max(east, m.position.x);
    gants.models[3].position.x = east + in(3.5f);
    ASSERT_FALSE(unit_coherency(gants).ok());
    const std::optional<ModelId> pick = default_coherency_removal(gs, gants.id);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(*pick, gants.models[3].id);
    EXPECT_EQ(default_casualty_order(gs, gants.id).front(), *pick);  // the default order's first model
    EXPECT_FALSE(default_coherency_removal(gs, UnitId{999}).has_value());
}

TEST(M47_CoherencyRemoval, TheFurtherOfTwoIsolatedModels) {
    // Termagants 0 and 4 have each stepped 3.5" out sideways, 0 also 2" back: both isolated (03.03), so removing
    // either leaves one more to remove. The one further from the Intercessors goes.
    GameState gs = wh40k::test::load_v1_scenario();
    Unit& gants = gs.units[kGants];
    const float front = y_in(gants.models[0]);
    gants.models[0].position = at(x_in(gants.models[0]) - 3.5f, front + 2.0f);
    gants.models[4].position = at(x_in(gants.models[4]) + 3.5f, front);
    const CoherencyResult c = unit_coherency(gants);
    ASSERT_EQ(c.isolated, (std::vector<ModelId>{gants.models[0].id, gants.models[4].id}));
    EXPECT_EQ(coherency_losses(placed_models(gants)), 2);
    const std::optional<ModelId> pick = default_coherency_removal(gs, gants.id);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(*pick, gants.models[0].id);
}

TEST(M47_CoherencyRemoval, TheIntercessorOverTheSergeant) {
    // A chain of Intercessors 1.9" apart: the ends are more than 9" apart (03.03), and removing either end
    // restores coherency. The end that is not the Sergeant goes (D-058: the odd one out is kept).
    GameState gs = wh40k::test::load_v1_scenario();
    Unit& marines = gs.units[kMarines];
    for (std::size_t i = 0; i < marines.models.size(); ++i) {
        marines.models[i].position = at(2.0f + 3.16f * static_cast<float>(i), 5.37f);
    }
    const CoherencyResult c = unit_coherency(marines);
    ASSERT_TRUE(c.isolated.empty());
    ASSERT_EQ(c.too_far, (std::vector<ModelId>{marines.models[0].id, marines.models[4].id}));
    const std::optional<ModelId> pick = default_coherency_removal(gs, marines.id);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(*pick, marines.models[4].id);
    // Removing it restores coherency, as removing the Sergeant would.
    GameState after = gs;
    after.units[kMarines].models.pop_back();
    EXPECT_TRUE(unit_coherency(after.units[kMarines]).ok());
}
