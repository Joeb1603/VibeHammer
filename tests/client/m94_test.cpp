// M94 (D-095): engine-paced dice rolls in the client: GameSession's automatic rolls per side, the Undo rule, the
// pre-roll card, and format-2 saves (format 1 migrated on load).

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/engine/engine.hpp"
#include "dice_cards.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"
#include "undo_groups.hpp"

using namespace wh40k::core;
using namespace wh40k::client;

namespace {

const std::filesystem::path kLegacySave =
    std::filesystem::path(WH40K_REPO_ROOT) / "tests" / "data" / "legacy" / "m93_save_format1.json";

// A short V1 game (the Termagants 1" closer) played by random legal actions through a session: moves, shots, charges
// and fights, every roll made inside the action that led to it. Seeds chosen so the game shoots and charges.
GameSession random_session_game(std::uint64_t seed, std::uint64_t rng_seed, int actions) {
    GameState scenario = wh40k::test::load_v1_scenario_closer();
    GameSession s = GameSession::new_game(std::move(scenario), seed);
    wh40k::test::TestRng rng(rng_seed);
    for (int i = 0; i < actions && s.pending(); ++i) {
        const GameAction a = wh40k::test::random_action(s.state(), rng);
        const ValidationResult r = s.submit(a);
        EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.reasons.front());
        if (!r.ok()) break;
    }
    return s;
}

}  // namespace

// Writes tests/data/legacy/m93_save_format1.json: a save made by the M93 engine (format 1, every roll inside its action),
// kept as the fixture for D-095's migration of format-1 saves. M94 ran it once, before any roll point moved. Run on
// purpose only (it must be run with an M93 engine to make a format-1 file):
//   wh40k_client_tests --gtest_also_run_disabled_tests --gtest_filter=M94_Legacy.DISABLED_WriteFormat1Save
TEST(M94_Legacy, DISABLED_WriteFormat1Save) {
    const GameSession s = random_session_game(5, 11, 50);
    std::filesystem::create_directories(kLegacySave.parent_path());
    s.save(kLegacySave);
}

namespace {

// Side A's Shoot step in V1 (the Termagants 1" closer) with `seed`, and the Shoot action firing every Bolt Rifle on
// Focused Fire at the Termagants.
action::Shoot rifle_volley(const GameState& gs) {
    action::Shoot a{gs.units[0].id, ShootingType::Normal, {}};
    for (const Model& m : gs.units[0].models) a.shots.push_back(ShotDeclaration{m.id, 0, 0, gs.units[1].id});
    return a;
}

GameState shoot_step(std::uint64_t seed) { return wh40k::test::shoot_step_state(Side::A, seed); }

}  // namespace

// ==== GameSession: automatic rolls per side ====

TEST(M94_Session, TheSessionRollsForItsAutoSidesAndWaitsForTheOthers) {
    // Side A's rolls are made for it; Side B's save rolls (05.03: the defender's) wait for Side B.
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        GameSession s(shoot_step(seed), {true, false});
        const std::size_t before = s.history().size();
        ASSERT_TRUE(s.submit(rifle_volley(s.state())).ok());
        if (!roll_pending(s.state())) continue;  // nothing wounded: no save to wait for
        SCOPED_TRACE("seed " + std::to_string(seed));
        EXPECT_TRUE(std::holds_alternative<action::Shoot>(s.history()[before].action));
        for (std::size_t i = before + 1; i < s.history().size(); ++i) {
            EXPECT_TRUE(std::holds_alternative<action::RollDice>(s.history()[i].action));  // Side A's, made at once
            EXPECT_EQ(s.history()[i].side, Side::A);
        }
        EXPECT_EQ(s.pending()->side, Side::B);
        EXPECT_EQ(std::get<decision::RollDice>(s.pending()->kind).roll, RollKind::Save);
        EXPECT_EQ(s.log().back().label.rfind("Step", 0), 0u);
        // Turning Side B's on makes the waiting roll at once.
        s.set_auto_roll(Side::B, true);
        const std::vector<GameAction> made = s.roll_automatically();
        ASSERT_FALSE(made.empty());
        EXPECT_EQ(made.front(), (GameAction{action::RollDice{RollKind::Save, s.state().units[1].id}}));
        EXPECT_FALSE(roll_pending(s.state()));
        return;
    }
    FAIL() << "no seed wounded the Termagants";
}

TEST(M94_Session, ARollStepIsLoggedWithItsKind) {
    GameSession s(shoot_step(1), {false, false});
    ASSERT_TRUE(s.submit(rifle_volley(s.state())).ok());
    ASSERT_TRUE(roll_pending(s.state()));
    const auto& d = std::get<decision::RollDice>(s.pending()->kind);
    ASSERT_TRUE(s.submit(action::RollDice{d.roll, d.unit}).ok());
    EXPECT_NE(s.log().back().label.find("RollDice (Hit)"), std::string::npos) << s.log().back().label;
    EXPECT_EQ(wh40k::test::events_of<event::HitRollsMade>(s.log().back().events).size(), 1u);
}

// ==== The pre-roll card ====

TEST(M94_RollPrompt, EachPreRollCardIsTheCardOfItsRollFaceDown) {
    // Random play with every roll made by hand: before each roll its card (who rolls, what for, what it needs, the dice
    // face down); after it, the card of its events says the same with the faces showing (D-033).
    std::map<RollKind, int> seen;
    const std::vector<GameState> scenarios{wh40k::test::load_v1_scenario_closer(), wh40k::test::load_rosters_scenario()};
    for (std::size_t n = 0; n < scenarios.size(); ++n) {
        for (std::uint64_t seed = 1; seed <= 3; ++seed) {
            GameSession s = GameSession::new_game(scenarios[n], seed, {false, false});
            wh40k::test::TestRng rng(seed + 100 * n);
            for (int i = 0; i < 400 && s.pending(); ++i) {
                if (!roll_pending(s.state())) {
                    ASSERT_TRUE(s.submit(wh40k::test::random_action(s.state(), rng)).ok());
                    continue;
                }
                const std::optional<DiceCard> prompt = roll_prompt(s.state(), s.log());
                ASSERT_TRUE(prompt.has_value());
                const auto& d = std::get<decision::RollDice>(s.pending()->kind);
                SCOPED_TRACE(std::string(roll_kind_name(d.roll)) + " at step " + std::to_string(s.history().size()));
                ++seen[d.roll];
                EXPECT_EQ(prompt->roller, s.pending()->side);
                EXPECT_FALSE(prompt->dice.empty());
                for (const DieFace& f : prompt->dice) EXPECT_EQ(f.value, 0);  // face down
                const std::size_t from = s.log().size();
                ASSERT_TRUE(s.submit(action::RollDice{d.roll, d.unit}).ok());
                const std::vector<DiceCard> cards = dice_cards(s.state(), s.log(), from);
                ASSERT_FALSE(cards.empty());
                EXPECT_EQ(cards.front().title, prompt->title);
                EXPECT_EQ(cards.front().roller, prompt->roller);
                EXPECT_EQ(cards.front().purpose, prompt->purpose);
                EXPECT_EQ(cards.front().need, prompt->need);
                // A batch's damage rolls: the preview's count (each D at its maximum) can be fewer than the rolls.
                if (d.roll != RollKind::Damage) EXPECT_EQ(cards.front().dice.size(), prompt->dice.size());
            }
        }
    }
    for (const RollKind k : {RollKind::Advance, RollKind::Charge, RollKind::Hit, RollKind::Wound, RollKind::Save}) {
        EXPECT_GT(seen[k], 0) << roll_kind_name(k);
    }
}

TEST(M94_RollPrompt, NoCardWithoutARollWaiting) {
    const GameSession s(shoot_step(1), {false, false});
    EXPECT_FALSE(roll_prompt(s.state(), s.log()).has_value());
}

// ==== Undo (D-039): a roll is never taken back on its own ====

TEST(M94_Undo, UndoTakesARollBackWithTheInputThatLedToIt) {
    GameSession s(shoot_step(1), {false, false});
    const GameState before = s.state();
    const std::size_t start = s.history().size();
    UndoGroups groups;
    groups.begin(start);
    ASSERT_TRUE(s.submit(rifle_volley(s.state())).ok());
    groups.end(s.history().size());
    ASSERT_TRUE(roll_pending(s.state()));
    // The Roll button: a step, but no Undo group of its own (the client's submit_roll).
    const auto& d = std::get<decision::RollDice>(s.pending()->kind);
    ASSERT_TRUE(s.submit(action::RollDice{d.roll, d.unit}).ok());
    EXPECT_EQ(s.history().size(), start + 2);
    ASSERT_TRUE(groups.undo(s));
    EXPECT_EQ(s.history().size(), start);
    EXPECT_EQ(s.state(), before);
    EXPECT_FALSE(s.auto_roll(Side::A));  // kept by the rebuild
    EXPECT_FALSE(s.auto_roll(Side::B));
}

TEST(M94_Undo, TruncateNeverStopsBetweenAnActionAndItsRolls) {
    GameSession s(shoot_step(1), {false, false});
    const GameState before = s.state();
    const std::size_t start = s.history().size();
    ASSERT_TRUE(s.submit(rifle_volley(s.state())).ok());
    const auto& d = std::get<decision::RollDice>(s.pending()->kind);
    ASSERT_TRUE(s.submit(action::RollDice{d.roll, d.unit}).ok());
    // Keeping the volley but not its roll would leave the roll to make again: the volley goes too.
    s.truncate(start + 1);
    EXPECT_EQ(s.history().size(), start);
    EXPECT_EQ(s.state(), before);
    // A plain undo of the volley alone, with the session making Side A's rolls itself.
    GameSession a(shoot_step(1), {true, true});
    ASSERT_TRUE(a.submit(rifle_volley(a.state())).ok());
    EXPECT_GT(a.history().size(), start + 1);  // the volley and its rolls
    ASSERT_TRUE(a.undo());
    EXPECT_EQ(a.history().size(), start);
    EXPECT_EQ(a.state(), before);
}

// ==== Saves (D-025): format 2; a format-1 save loads with its rolls added as steps ====

TEST(M94_Saves, AnM93SaveLoadsWithItsRollsAsSteps) {
    const GameSession loaded = GameSession::load(kLegacySave);
    const GameSession again = random_session_game(5, 11, 50);  // the same choices, played now
    EXPECT_EQ(loaded.state(), again.state());
    EXPECT_EQ(loaded.history(), again.history());
    EXPECT_GT(loaded.history().size(), 50u);  // its 50 steps and their rolls
    EXPECT_EQ(loaded.to_save_json().at("format"), 2);
    EXPECT_EQ(GameSession::from_save_json(loaded.to_save_json()).state(), loaded.state());
}

TEST(M94_Saves, AGameSavedWhileARollWaitsLoadsStillWaitingForIt) {
    GameSession s(shoot_step(1), {false, false});
    ASSERT_TRUE(s.submit(rifle_volley(s.state())).ok());
    ASSERT_TRUE(roll_pending(s.state()));
    const GameSession loaded = GameSession::from_save_json(s.to_save_json());
    EXPECT_EQ(loaded.state(), s.state());
    EXPECT_TRUE(roll_pending(loaded.state()));  // no roll is made by loading
    EXPECT_EQ(loaded.history(), s.history());
}
