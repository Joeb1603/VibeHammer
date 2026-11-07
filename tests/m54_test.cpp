// M54: speed-ups that must not change a single answer. Each faster query is checked against the slower form it
// replaces (or the public reason-giving path), over states from random play on the V1, 20-Termagant and 60"x44"
// scenarios.

#include <gtest/gtest.h>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/replay.hpp"
#include "core/geometry/closest.hpp"
#include "core/geometry/coherency.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/model/keywords.hpp"
#include "core/rules/shooting.hpp"
#include "core/state/queries.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using wh40k::test::TestRng;

namespace {

GameState started(const char* scenario, std::uint64_t seed) {
    GameState gs = load_scenario(wh40k::test::repo_root() / "data/scenarios" / scenario, wh40k::test::repo_root());
    gs.dice.seed = seed;
    start_game(gs);
    return gs;
}

// Every `every`-th state of a random game (the D-004 random play), up to `max_actions` actions.
std::vector<GameState> random_states(const char* scenario, std::uint64_t seed, int max_actions, int every) {
    std::vector<GameState> out;
    GameState gs = started(scenario, seed);
    TestRng rng(seed * 7919 + 1);
    for (int i = 0; i < max_actions && gs.pending && !gs.result; ++i) {
        if (i % every == 0) out.push_back(gs);
        const GameAction a = wh40k::test::random_action(gs, rng);
        (void)apply_action(gs, gs.pending->side, a);
    }
    out.push_back(gs);
    return out;
}

// The states the tests below share (made once: random play in Debug is not free).
const std::vector<GameState>& sample_states() {
    static const std::vector<GameState> states = [] {
        std::vector<GameState> out;
        for (const auto& [scenario, seed] : std::vector<std::pair<const char*, std::uint64_t>>{
                 {"v1_skirmish.json", 3}, {"bot_20_termagants.json", 5}, {"strike_44x60.json", 7}}) {
            for (GameState& s : random_states(scenario, seed, 400, 5)) out.push_back(std::move(s));
        }
        return out;
    }();
    return states;
}

constexpr ShootingType kTypes[] = {ShootingType::Normal, ShootingType::Assault, ShootingType::CloseQuarters};

}  // namespace

// ---- 03.03: one model moved, the others kept ----

TEST(R03_03_Coherency, OneModelMovedAgreesWithTheFullCheck) {
    // Random units of 2-12 models with mixed bases (the subtraction order of two radii matters in float), each
    // model tried at random spots: CoherencyWithOneMoved::ok_at gives check_coherency's answer every time.
    const Base bases[] = {Base{BaseShape::Round, 32.0f, 0.0f}, Base{BaseShape::Round, 25.0f, 0.0f},
                          Base{BaseShape::Round, 28.5f, 0.0f}, Base{BaseShape::Round, 40.0f, 0.0f}};
    TestRng rng(54);
    const auto uniform = [&](float lo, float hi) {
        return lo + (hi - lo) * static_cast<float>(rng.next() % 1000001) / 1000000.0f;
    };
    int ok = 0;
    int not_ok = 0;
    for (int trial = 0; trial < 300; ++trial) {
        const std::size_t n = 2 + rng.below(11);
        const float spread = uniform(30.0f, 260.0f);
        std::vector<PlacedModel> models;
        for (std::size_t i = 0; i < n; ++i) {
            models.push_back(PlacedModel{ModelId{static_cast<std::uint32_t>(i + 1)},
                                         Position{uniform(0.0f, spread), uniform(0.0f, spread), 0.0f},
                                         bases[rng.below(4)]});
        }
        const std::size_t moved = rng.below(n);
        const CoherencyWithOneMoved fast(models, moved);
        for (int spot = 0; spot < 40; ++spot) {
            const Position p{uniform(-20.0f, spread + 20.0f), uniform(-20.0f, spread + 20.0f), 0.0f};
            std::vector<PlacedModel> trial_models = models;
            trial_models[moved].position = p;
            const bool expected = check_coherency(trial_models).ok();
            ASSERT_EQ(fast.ok_at(p), expected) << "trial " << trial << " spot " << spot;
            (expected ? ok : not_ok) += 1;
        }
        // An index past the end moves nothing.
        EXPECT_EQ(CoherencyWithOneMoved(models, n).ok_at(Position{}), check_coherency(models).ok());
    }
    EXPECT_GT(ok, 100);  // both answers are exercised
    EXPECT_GT(not_ok, 100);
    EXPECT_TRUE(CoherencyWithOneMoved(std::vector<PlacedModel>{}, 0).ok_at(Position{}));  // no models
}

// ---- 04.02 / 06.01 / 10.04-10.06: shooting options ----

TEST(M54_ShotOptions, HasShotOptionAgreesWithShotOptions) {
    // D-004 / D-016: legal_actions offers a Shoot template when has_shot_option says so; it must be exactly
    // "shot_options is not empty", for every unit and shooting type.
    int with = 0;
    int without = 0;
    for (const GameState& gs : sample_states()) {
        for (const Unit& u : gs.units) {
            for (const ShootingType type : kTypes) {
                const bool any = !shot_options(gs, u, type).empty();
                ASSERT_EQ(has_shot_option(gs, u, type), any) << u.name << " " << shooting_type_name(type);
                (any ? with : without) += 1;
            }
        }
    }
    EXPECT_GT(with, 50);
    EXPECT_GT(without, 50);
}

TEST(M54_ShotOptions, EachOptionIsADeclarationWithNoProblemInWalkOrder) {
    // shot_options checks range before visibility and shares its facts; the declarations it lists are exactly
    // the single declarations shot_problems finds nothing wrong with, in model, weapon, profile, target order.
    int options = 0;
    const std::vector<GameState>& states = sample_states();
    for (std::size_t k = 0; k < states.size(); k += 4) {
        const GameState& gs = states[k];
        for (const Unit& u : gs.units) {
            for (const ShootingType type : kTypes) {
                std::vector<ShotDeclaration> expected;
                for (const Model& m : u.models) {
                    for (std::size_t w = 0; w < m.weapons.size(); ++w) {
                        for (std::size_t p = 0; p < m.weapons[w].profiles.size(); ++p) {
                            for (const Unit& t : gs.units) {
                                if (t.side == u.side || is_destroyed(t)) continue;
                                const ShotDeclaration d{m.id, w, p, t.id};
                                if (shot_problems(gs, u, type, std::vector<ShotDeclaration>{d}).empty()) {
                                    expected.push_back(d);
                                }
                            }
                        }
                    }
                }
                ASSERT_EQ(shot_options(gs, u, type), expected) << u.name << " " << shooting_type_name(type);
                options += static_cast<int>(expected.size());
            }
        }
    }
    EXPECT_GT(options, 200);
}

TEST(M54_ShotProblems, SharedFactsGiveEveryDeclarationItsOwnReason) {
    // shot_problems works out engagement and visibility once per call (M54): a volley's reasons are the
    // declarations' own reasons, in order, as each gets alone (one profile and target per weapon, so no
    // duplicate-weapon reason; a 24.07 reason only follows when every declaration is valid).
    TestRng rng(5454);
    int reasons = 0;
    for (const GameState& gs : sample_states()) {
        for (const Unit& u : gs.units) {
            std::vector<const Unit*> targets;  // enemies, destroyed ones too (04.02: not on the battlefield)
            for (const Unit& t : gs.units) {
                if (t.side != u.side) targets.push_back(&t);
            }
            if (targets.empty()) continue;
            for (const ShootingType type : kTypes) {
                std::vector<ShotDeclaration> volley;
                std::vector<std::string> alone;
                for (const Model& m : u.models) {
                    for (std::size_t w = 0; w < m.weapons.size(); ++w) {
                        if (m.weapons[w].profiles.empty()) continue;
                        const ShotDeclaration d{m.id, w, rng.below(m.weapons[w].profiles.size()),
                                                targets[rng.below(targets.size())]->id};
                        volley.push_back(d);
                        for (const std::string& r : shot_problems(gs, u, type, std::vector<ShotDeclaration>{d})) {
                            alone.push_back(r);
                        }
                    }
                }
                if (volley.empty()) continue;
                const std::vector<std::string> together = shot_problems(gs, u, type, volley);
                if (alone.empty()) continue;  // a valid volley may break 24.07, which needs all of them
                ASSERT_EQ(together, alone) << u.name << " " << shooting_type_name(type);
                reasons += static_cast<int>(alone.size());
            }
        }
    }
    EXPECT_GT(reasons, 500);
}

// ---- 01.04 / 03.04: engagement and distances without the PlacedModel lists ----

TEST(M54_Queries, EngagementAndDistancesMatchThePlacedModelForms) {
    for (const GameState& gs : sample_states()) {
        for (const Unit& u : gs.units) {
            std::vector<UnitId> engaged;
            for (const Unit& o : gs.units) {
                if (o.side != u.side && units_engaged(placed_models(u), placed_models(o))) engaged.push_back(o.id);
            }
            ASSERT_EQ(engaged_enemy_units(gs, u), engaged);
            ASSERT_EQ(is_engaged(gs, u), !engaged.empty());
            for (const Unit& o : gs.units) {
                // Bit for bit: the same base_distance values, and the least of them.
                ASSERT_EQ(unit_distance(u, o), closest_distance(placed_models(u), placed_models(o)));
                for (const Model& m : u.models) {
                    const Position shifted{m.position.x + 13.7f, m.position.y - 41.3f, 0.0f};
                    for (const Position& at : {m.position, shifted}) {
                        const PlacedModel from{m.id, at, m.base};
                        ASSERT_EQ(model_distance_to_unit(m, at, o),
                                  closest_distance(std::span<const PlacedModel>(&from, 1), placed_models(o)));
                    }
                }
            }
        }
    }
}

// ---- D-026: apply_action with a reused rollback copy ----

TEST(M54_ApplyAction, TheReusedBackupGivesTheSameGameAsTheOtherOverload) {
    // The same random game applied twice, once with apply_action(state, side, action, dice) and once with the
    // backup overload the bot's simulator and Match use: the same events and the same state at every step.
    for (const char* scenario : {"v1_skirmish.json", "strike_44x60.json"}) {
        GameState plain = started(scenario, 11);
        GameState reused = plain;
        GameState backup;
        DiceState plain_dice{99, 0};
        DiceState reused_dice{99, 0};
        TestRng rng(11);
        for (int i = 0; i < 600 && plain.pending && !plain.result; ++i) {
            const GameAction a = wh40k::test::random_action(plain, rng);
            const Side side = plain.pending->side;
            SeededDice d1(plain_dice);
            SeededDice d2(reused_dice);
            const std::vector<GameEvent> e1 = apply_action(plain, side, a, d1);
            const std::vector<GameEvent> e2 = apply_action(reused, side, a, d2, backup);
            ASSERT_EQ(e1, e2) << scenario << " step " << i;
            ASSERT_EQ(plain, reused) << scenario << " step " << i;
            ASSERT_EQ(plain_dice, reused_dice);
        }
    }
}

TEST(M54_ApplyAction, AnEngineExceptionStillLeavesTheStateUnchangedWithTheReusedBackup) {
    // As EngineApi.AnEngineExceptionLeavesTheStateUnchanged, through the backup overload (with a backup that
    // already holds another state, as it does after earlier steps).
    GameState gs = wh40k::test::shoot_step_state();
    const GameState before = gs;
    GameState backup = wh40k::test::load_v1_scenario();
    action::Shoot volley{gs.units[0].id, ShootingType::Normal, {}};
    for (const Model& m : gs.units[0].models) volley.shots.push_back(ShotDeclaration{m.id, 0, 0, gs.units[1].id});
    ScriptedDice too_few{3, 3};
    EXPECT_THROW((void)apply_action(gs, Side::A, volley, too_few, backup), std::logic_error);
    EXPECT_EQ(gs, before);
    EXPECT_TRUE(validate_action(gs, Side::A, volley).ok());  // still at the same decision
    // An invalid action is still refused before anything changes.
    EXPECT_THROW((void)apply_action(gs, Side::B, action::EndShootStep{}, too_few, backup), std::logic_error);
    EXPECT_EQ(gs, before);
}

TEST(M54_Match, ACopiedMatchPlaysOnTheSameAsTheOriginal) {
    // Match keeps its rollback copy as scratch (M54): a copy made mid-game starts without it and plays on to the
    // same states, events and replay.
    GameState initial = wh40k::test::load_v1_scenario();
    initial.dice.seed = 4;
    Match m(initial);
    TestRng rng(4);
    for (int i = 0; i < 40 && m.state().pending; ++i) {
        ASSERT_TRUE(m.submit(m.state().pending->side, wh40k::test::random_action(m.state(), rng)).ok());
    }
    Match copy = m;
    TestRng rng_a(40);
    TestRng rng_b(40);
    for (int i = 0; i < 200 && m.state().pending && !m.state().result; ++i) {
        const GameAction a = wh40k::test::random_action(m.state(), rng_a);
        const GameAction b = wh40k::test::random_action(copy.state(), rng_b);
        ASSERT_EQ(a, b);
        ASSERT_TRUE(m.submit(m.state().pending->side, a).ok());
        ASSERT_TRUE(copy.submit(copy.state().pending->side, b).ok());
        ASSERT_EQ(m.state(), copy.state());
    }
    EXPECT_EQ(m.events(), copy.events());
    EXPECT_EQ(m.steps(), copy.steps());
}
