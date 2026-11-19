// M90 (D-091): the bot and 12.08 Objective Consolidation and 24.18 [IGNORES COVER]. The bot (Normal, and its defaults)
// answers the new consolidation option legally: an Auto move onto the area (D-050), taken when it adds models within
// range of it (M91 weighs it with the objective terms). Its odds ignore cover for an [IGNORES COVER] profile, and a
// random-army game with a flamer plays clean.

#include <gtest/gtest.h>

#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "bot.hpp"
#include "core/engine/engine.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/state/objectives.hpp"
#include "default_choice.hpp"
#include "normal/fight.hpp"
#include "session.hpp"
#include "state_hash.hpp"
#include "support/army_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::objective_consolidation_state;

namespace {

constexpr std::size_t kMarines = 0;

int within_range(const GameState& gs) {
    int n = 0;
    for (const Model& m : gs.units[kMarines].models) n += model_within_objective(m, gs.mission->objectives[0]) ? 1 : 0;
    return n;
}

// Answers `side`'s decisions with `decide` until its Consolidate step is over (or 64 answers), each one validated.
template <typename Decide>
void consolidate(GameState& gs, Side side, Decide decide) {
    for (int guard = 0; guard < 64 && gs.pending && gs.pending->side == side &&
                        (std::holds_alternative<decision::Consolidate>(gs.pending->kind) ||
                         std::holds_alternative<decision::MoveModels>(gs.pending->kind));
         ++guard) {
        const BotChoice c = decide(gs);
        const ValidationResult v = validate_action(gs, side, c.action);
        ASSERT_TRUE(v.ok()) << action_name(c.action) << ": " << (v.reasons.empty() ? "" : v.reasons.front());
        (void)apply_action(gs, side, c.action);
    }
}

bool has_flamer(const GameState& gs) {
    for (const Unit& u : gs.units) {
        for (const Model& m : u.models) {
            for (const Weapon& w : m.weapons) {
                if (w.name == "Flamer" || w.name == "Heavy flamer") return true;
            }
        }
    }
    return false;
}

}  // namespace

TEST(M90_Bot, NormalMakesAnObjectiveConsolidationThatGetsModelsWithinRange) {
    GameState gs = objective_consolidation_state(2.0f);
    ASSERT_EQ(within_range(gs), 0);
    Bot bot(BotConfig{});
    const BotChoice first = bot.decide(gs, Side::A);
    ASSERT_TRUE(std::holds_alternative<action::StartConsolidation>(first.action)) << action_name(first.action);
    EXPECT_EQ(std::get<action::StartConsolidation>(first.action).objective, std::optional<std::size_t>{0});
    EXPECT_NE(first.reason.find("Objective Consolidation"), std::string::npos) << first.reason;
    consolidate(gs, Side::A, [&](const GameState& now) { return bot.decide(now, Side::A); });
    EXPECT_TRUE(gs.units[kMarines].phase_flags.consolidated);
    EXPECT_GT(within_range(gs), 0);
}

TEST(M90_Bot, NormalEndsTheStepWhenEveryModelIsAlreadyWithinRange) {
    GameState gs = objective_consolidation_state(-6.0f, 14.0f);
    ASSERT_EQ(within_range(gs), 5);
    const BotChoice c = Bot(BotConfig{}).decide(gs, Side::A);
    EXPECT_TRUE(std::holds_alternative<action::EndConsolidations>(c.action)) << action_name(c.action);
}

TEST(M90_Defaults, TheDefaultsMakeAnObjectiveConsolidationThatGetsModelsWithinRange) {
    GameState gs = objective_consolidation_state(2.9f);
    const BotChoice first = wh40k::test::default_choice(gs, Side::A);
    ASSERT_TRUE(std::holds_alternative<action::StartConsolidation>(first.action)) << action_name(first.action);
    consolidate(gs, Side::A, [](const GameState& now) { return wh40k::test::default_choice(now, Side::A); });
    EXPECT_TRUE(gs.units[kMarines].phase_flags.consolidated);
    EXPECT_GT(within_range(gs), 0);

    GameState inside = objective_consolidation_state(-6.0f, 14.0f);
    EXPECT_TRUE(std::holds_alternative<action::EndConsolidations>(wh40k::test::default_choice(inside, Side::A).action));
}

TEST(M90_StateHash, OnlyAnObjectiveConsolidationHashesItsObjective) {
    GameState gs = objective_consolidation_state(2.0f);
    (void)apply_action(gs, Side::A, action::StartConsolidation{gs.units[kMarines].id, {}, 0});
    ASSERT_TRUE(gs.flow.move.has_value());
    const std::uint64_t with = state_hash(gs);
    gs.flow.move->objective.reset();
    EXPECT_NE(state_hash(gs), with);  // the field is hashed when set (behind its own tag), and not otherwise
}

TEST(M90_Odds, AnIgnoresCoverProfileIsNotWorsenedByTheTargetsCover) {
    GameState gs = wh40k::test::shoot_step_state();
    Unit attacker = gs.units[0];
    analysis::OddsCache cache;
    analysis::ShotContext open;
    open.gap_mm = wh40k::test::in(10.0f);
    analysis::ShotContext covered = open;
    covered.target_cover = true;
    const analysis::Volley bare = analysis::unit_ranged_volley(cache, attacker, gs.units[1], open);
    const analysis::Volley in_cover = analysis::unit_ranged_volley(cache, attacker, gs.units[1], covered);
    ASSERT_FALSE(bare.empty());
    ASSERT_EQ(in_cover.size(), bare.size());
    // 13.08: the Bolt Rifles' BS is worsened by 1 (D-089, D-090).
    EXPECT_EQ(*in_cover[0].profile.skill, *bare[0].profile.skill + 1);
    for (Model& m : attacker.models) {
        for (Weapon& w : m.weapons) {
            for (WeaponProfile& p : w.profiles) p.abilities.push_back(ability::IgnoresCover{});
        }
    }
    analysis::OddsCache fresh;
    EXPECT_EQ(analysis::unit_ranged_volley(fresh, attacker, gs.units[1], covered),
              analysis::unit_ranged_volley(fresh, attacker, gs.units[1], open));  // 24.18
}

TEST(M90_Arena, ARandomArmyGameWithAFlamerPlaysClean) {
    // The first random-army seed whose armies carry a Flamer or a Heavy flamer (24.18: selectable since M90).
    std::uint64_t seed = 0;
    for (std::uint64_t s = 1; s <= 200 && seed == 0; ++s) {
        if (has_flamer(wh40k::test::random_armies_battle(s))) seed = s;
    }
    ASSERT_NE(seed, 0u);
    GameState gs = wh40k::test::random_armies_battle(seed);
    gs.dice.seed = seed;
    client::GameSession session(gs);
    const GameRecord rec = play_bot_session(session, BotConfig{}, BotConfig{});
    EXPECT_FALSE(session.pending().has_value()) << "the game did not finish";
    EXPECT_EQ(rec.rejected, 0u);
    EXPECT_EQ(rec.reverted, 0u);
    EXPECT_EQ(rec.fallbacks, 0u);
    EXPECT_TRUE(rec.problems.empty()) << rec.problems.front();
}
