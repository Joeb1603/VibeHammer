// M95 (D-096): reaction windows in the client: the session's automatic declines, Undo, the picks, the dice cards
// (each card's roll, the re-roll's card, the pre-roll card before a window), the "Offer stratagems" setting, and saves
// (format 3; format 2 converted on load).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <variant>

#include "action_text.hpp"
#include "core/engine/engine.hpp"
#include "dice_cards.hpp"
#include "launch_options.hpp"
#include "saved_game.hpp"
#include "session.hpp"
#include "session_fixtures.hpp"
#include "settings.hpp"
#include "stratagem_picks.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::random_session_game;
using wh40k::test::rifle_volley;
using wh40k::test::shoot_step;

namespace {

const std::filesystem::path kLegacyDir = std::filesystem::path(WH40K_REPO_ROOT) / "tests" / "data" / "legacy";

std::size_t declines_in(const std::vector<ReplayStep>& steps) {
    return static_cast<std::size_t>(std::count_if(steps.begin(), steps.end(), [](const ReplayStep& s) {
        return std::holds_alternative<action::DeclineStratagems>(s.action);
    }));
}

// Side A's volley in V1 (seed 1) through a session that makes every roll and declines no window: Side A's Command
// Re-roll window after its hit rolls waits (round 1: 1 CP).
GameSession at_hit_window() {
    GameSession s(shoot_step(1), {true, true}, {false, false});
    EXPECT_TRUE(s.submit(rifle_volley(s.state())).ok());
    EXPECT_TRUE(window_pending(s.state()));
    return s;
}

// Side A's Charge step in V1 with the Termagants 9.9" away, the charge declared and rolled by the session: Side A's
// window after its charge roll waits.
GameSession at_charge_window() {
    GameState gs = wh40k::test::load_v1_scenario();
    start_game(gs);
    while (!(gs.flow.stage == Stage::Charge && turn_side(gs) == Side::A)) {
        apply_action(gs, gs.pending->side, legal_actions(gs, gs.pending->side).front());
    }
    const float gap = mm_to_inches(*unit_distance(gs.units[0], gs.units[1]));
    for (Model& m : gs.units[1].models) m.position.y -= wh40k::test::in(gap - 9.9f);
    GameSession s(gs, {true, true}, {false, false});
    EXPECT_TRUE(s.submit(action::DeclareCharge{gs.units[0].id}).ok());
    EXPECT_TRUE(window_pending(s.state()));
    return s;
}

// V1 with 2 of the 5 Intercessors (below half-strength): Side A's first Battle-shock step tests them, and Insane
// Bravery's window waits just before the roll (15.04).
GameSession at_insane_bravery_window() {
    GameState gs = wh40k::test::load_v1_scenario();
    gs.units[0].models.resize(2);
    GameSession s = GameSession::new_game(std::move(gs), 1, {true, true}, {false, false});
    EXPECT_TRUE(window_pending(s.state()));
    return s;
}

}  // namespace

// ==== The "Offer stratagems" setting ====

TEST(M95_Settings, OfferStratagemsIsOnByDefaultAndKept) {
    EXPECT_TRUE(AppSettings{}.offer_stratagems);
    AppSettings off;
    off.offer_stratagems = false;
    EXPECT_EQ(settings_from_json(settings_to_json(off)), off);
    // A file written before M95 has no such key: on.
    nlohmann::json old = settings_to_json(AppSettings{});
    old.erase("offer_stratagems");
    EXPECT_TRUE(settings_from_json(old).offer_stratagems);
}

TEST(M95_LaunchOptions, DicePopupsSetsTheSettingForTheRun) {
    EXPECT_FALSE(parse_launch_args({"--steps", "1"}).dice_popups.has_value());
    EXPECT_EQ(parse_launch_args({"--dice-popups", "0"}).dice_popups, false);
    EXPECT_EQ(parse_launch_args({"--dice-popups", "1"}).dice_popups, true);
    for (const char* bad : {"", "2", "off", "-1"}) {
        EXPECT_THROW((void)parse_launch_args({"--dice-popups", bad}), std::invalid_argument) << bad;
    }
}

// ==== GameSession: automatic declines per side (D-096) ====

TEST(M95_Session, TheSessionDeclinesForItsAutoSidesAndWaitsForTheOthers) {
    // Side A's windows are declined for it; Side B's (after its save rolls, the defender's) waits for Side B.
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        GameSession s(shoot_step(seed), {true, true}, {true, false});
        ASSERT_TRUE(s.submit(rifle_volley(s.state())).ok());
        if (!window_pending(s.state())) continue;  // no save rolled
        SCOPED_TRACE("seed " + std::to_string(seed));
        EXPECT_EQ(s.state().pending->side, Side::B);
        EXPECT_EQ(window_info(s.state())->roll, RollKind::Save);
        EXPECT_GE(declines_in(s.history()), 2u);  // Side A's, after its hit and wound rolls
        for (const ReplayStep& step : s.history()) {
            if (std::holds_alternative<action::DeclineStratagems>(step.action)) EXPECT_EQ(step.side, Side::A);
        }
        EXPECT_TRUE(s.auto_decline(Side::A));
        EXPECT_FALSE(s.auto_decline(Side::B));
        return;
    }
    FAIL() << "no seed wounded the Termagants";
}

TEST(M95_Session, ARandomSessionGameDeclinesEveryWindowAsBefore) {
    // The default session declines every window: random play never sees one, so it plays the M94 game.
    const GameSession s = random_session_game(5, 11, 50);
    EXPECT_GT(declines_in(s.history()), 0u);
    EXPECT_TRUE(s.state().stratagems[0].empty());
    EXPECT_TRUE(s.state().stratagems[1].empty());
}

// ==== Undo (D-039): a stratagem on its own; a decline with the input that led to its roll ====

TEST(M95_Undo, AStratagemIsTakenBackOnItsOwnAndADeclineWithItsRoll) {
    GameSession s = at_hit_window();
    const GameState window = s.state();
    const std::size_t at_window = s.history().size();
    StratagemPicks picks(s.state());
    picks.pick(0);
    ASSERT_TRUE(s.submit(*picks.command_reroll()).ok());
    EXPECT_EQ(s.state().command_points[0], 0);
    ASSERT_TRUE(s.undo());
    EXPECT_EQ(s.history().size(), at_window);  // back at the window, its dice as they were
    EXPECT_EQ(s.state(), window);
    // Declined, then undone: the decline goes back with the volley that led to its roll (as the roll itself does).
    ASSERT_TRUE(s.submit(StratagemPicks::decline()).ok());
    ASSERT_TRUE(s.undo());
    EXPECT_EQ(s.state(), shoot_step(1));
    EXPECT_TRUE(s.history().empty());
}

// ==== StratagemPicks ====

TEST(M95_Picks, ADieIsPickedForCommandRerollAndPickedAgainToDropIt) {
    const GameSession s = at_hit_window();
    StratagemPicks picks(s.state());
    ASSERT_TRUE(picks.active());
    const WindowInfo& info = picks.info();
    EXPECT_EQ(info.roll, RollKind::Hit);
    EXPECT_EQ(info.side, Side::A);
    EXPECT_TRUE(picks.offers(Stratagem::CommandReroll));
    EXPECT_FALSE(picks.offers(Stratagem::InsaneBravery));
    EXPECT_EQ(picks.cost(Stratagem::CommandReroll), 1);
    EXPECT_GT(info.faces.size(), 1u);
    EXPECT_FALSE(picks.picked().has_value());
    EXPECT_FALSE(picks.reroll_ready());
    EXPECT_FALSE(picks.command_reroll().has_value());
    picks.pick(1);
    EXPECT_EQ(picks.picked(), std::size_t{1});
    EXPECT_EQ(picks.command_reroll(), (GameAction{action::UseStratagem{Stratagem::CommandReroll, *info.unit, 1}}));
    picks.pick(1);  // dropped again
    EXPECT_FALSE(picks.picked().has_value());
    picks.pick(info.faces.size());  // out of range: ignored
    EXPECT_FALSE(picks.picked().has_value());
    EXPECT_EQ(StratagemPicks::decline(), (GameAction{action::DeclineStratagems{}}));
    EXPECT_EQ(picks.reroll_label(), "Command Re-roll (1 CP)");
    EXPECT_EQ(picks.decline_label(), "Keep the roll");
    EXPECT_EQ(picks.headline(), "Command Re-roll? (15.02)");
    EXPECT_NE(picks.hint().find("1 CP"), std::string::npos);
    // Every answer the picks give is one core accepts.
    picks.pick(0);
    EXPECT_TRUE(s.check(*picks.command_reroll()).ok());
    EXPECT_TRUE(s.check(StratagemPicks::decline()).ok());
    EXPECT_FALSE(StratagemPicks(shoot_step(1)).active());  // no window
}

TEST(M95_Picks, AChargeRollIsReRolledWholeWithNoDieToPick) {
    const GameSession s = at_charge_window();
    StratagemPicks picks(s.state());
    ASSERT_TRUE(picks.active());
    EXPECT_TRUE(picks.info().whole_roll);
    EXPECT_TRUE(picks.reroll_ready());
    picks.pick(0);  // nothing to pick
    EXPECT_FALSE(picks.picked().has_value());
    EXPECT_FALSE(picks.pickable(0));
    EXPECT_EQ(picks.command_reroll(),
              (GameAction{action::UseStratagem{Stratagem::CommandReroll, s.state().units[0].id, std::nullopt}}));
    EXPECT_TRUE(s.check(*picks.command_reroll()).ok());
}

TEST(M95_Picks, InsaneBraveryIsOfferedBeforeTheBattleShockRoll) {
    const GameSession s = at_insane_bravery_window();
    const StratagemPicks picks(s.state());
    ASSERT_TRUE(picks.active());
    EXPECT_EQ(picks.info().timing, WindowTiming::BeforeRoll);
    EXPECT_FALSE(picks.offers(Stratagem::CommandReroll));
    EXPECT_FALSE(picks.reroll_ready());
    ASSERT_TRUE(picks.insane_bravery().has_value());
    EXPECT_TRUE(s.check(*picks.insane_bravery()).ok());
    EXPECT_EQ(picks.decline_label(), "Roll normally");
    EXPECT_EQ(picks.headline(), "Insane Bravery? (15.04)");
    EXPECT_EQ(picks.insane_bravery_label(), "Insane Bravery (1 CP)");
    EXPECT_NE(picks.hint().find("with no dice rolled"), std::string::npos) << picks.hint();
}

// ==== Dice cards (D-033): each card's roll, the window's card, the re-roll's card ====

TEST(M95_Cards, TheRolledCardIsTheWindowsCard) {
    const GameSession s = at_hit_window();
    const std::vector<DiceCard> cards = dice_cards(s.state(), s.log(), 0);
    ASSERT_FALSE(cards.empty());
    const DiceCard& hit = cards.back();
    EXPECT_EQ(hit.roll, RollKind::Hit);
    EXPECT_EQ(hit.unit, s.state().units[0].id);
    EXPECT_TRUE(is_window_card(s.state(), hit));
    // The window's pop-up card (no card showing) is that card again, from core's judgement of the dice.
    const std::optional<DiceCard> popup = window_card(s.state(), s.log());
    ASSERT_TRUE(popup.has_value());
    EXPECT_EQ(popup->dice, hit.dice);
    EXPECT_EQ(popup->result, hit.result);
    EXPECT_EQ(popup->roll, hit.roll);
    EXPECT_FALSE(is_window_card(shoot_step(1), hit));  // no window
}

TEST(M95_Cards, TheReRollHasACardWithTheReRolledDieMarked) {
    GameSession s = at_hit_window();
    const std::size_t from = s.log().size();
    const std::vector<int> before = window_info(s.state())->faces;
    StratagemPicks picks(s.state());
    picks.pick(2);
    ASSERT_TRUE(s.submit(*picks.command_reroll()).ok());
    const std::vector<DiceCard> cards = dice_cards(s.state(), s.log(), from);
    ASSERT_FALSE(cards.empty());
    const DiceCard& card = cards.front();  // then the wound rolls' card (the session made them)
    EXPECT_EQ(card.title, "Command Re-roll: Hit rolls");
    EXPECT_EQ(card.rule, "15.02");
    ASSERT_EQ(card.dice.size(), before.size());
    for (std::size_t i = 0; i < card.dice.size(); ++i) {
        EXPECT_EQ(card.dice[i].rerolled, i == 2) << i;
        if (i != 2) EXPECT_EQ(card.dice[i].value, before[i]) << i;
    }
    EXPECT_EQ(card.result.rfind("Re-rolled die 3: " + std::to_string(before[2]) + " -> ", 0), 0u) << card.result;
}

TEST(M95_Cards, AChargeReRollMarksBothDice) {
    GameSession s = at_charge_window();
    const std::size_t from = s.log().size();
    ASSERT_TRUE(s.submit(*StratagemPicks(s.state()).command_reroll()).ok());
    const std::vector<DiceCard> cards = dice_cards(s.state(), s.log(), from);
    ASSERT_FALSE(cards.empty());
    EXPECT_EQ(cards.front().title, "Command Re-roll: Charge roll");
    ASSERT_EQ(cards.front().dice.size(), 2u);
    EXPECT_TRUE(cards.front().dice[0].rerolled && cards.front().dice[1].rerolled);
    EXPECT_EQ(cards.front().result.rfind("Re-rolled: ", 0), 0u) << cards.front().result;
}

TEST(M95_Cards, InsaneBraverysWindowHasTheRollsCardFaceDownAndItsPassACardOfItsOwn) {
    GameSession s = at_insane_bravery_window();
    EXPECT_FALSE(roll_prompt(s.state(), s.log()).has_value());  // no RollDice waits yet
    const std::optional<DiceCard> prompt = window_prompt(s.state(), s.log());
    ASSERT_TRUE(prompt.has_value());
    EXPECT_EQ(prompt->title, "Battle-shock roll");
    EXPECT_EQ(prompt->roll, RollKind::BattleShock);
    EXPECT_EQ(prompt->roller, Side::A);
    EXPECT_EQ(prompt->dice, std::vector<DieFace>(2, DieFace{0, false, false, true}));
    EXPECT_NE(prompt->need.find("+ to pass"), std::string::npos) << prompt->need;
    EXPECT_FALSE(window_card(s.state(), s.log()).has_value());  // nothing rolled
    // Declined (with the roll left to Side A, as with the dice pop-ups on): the same card waits as the roll's own
    // pre-roll card.
    GameSession declined = s;
    declined.set_auto_roll(Side::A, false);
    ASSERT_TRUE(declined.submit(StratagemPicks::decline()).ok());
    const std::optional<DiceCard> roll = roll_prompt(declined.state(), declined.log());
    ASSERT_TRUE(roll.has_value());
    EXPECT_EQ(roll->dice, prompt->dice);
    EXPECT_EQ(roll->need, prompt->need);
    EXPECT_EQ(roll->purpose, prompt->purpose);
    // Used: the test passes with no dice, and its card says so.
    const std::size_t from = s.log().size();
    ASSERT_TRUE(s.submit(*StratagemPicks(s.state()).insane_bravery()).ok());
    const std::vector<DiceCard> cards = dice_cards(s.state(), s.log(), from);
    ASSERT_EQ(cards.size(), 1u);
    EXPECT_EQ(cards[0].rule, "15.04");
    EXPECT_TRUE(cards[0].dice.empty());
    EXPECT_FALSE(needs_roll(cards[0]));
    EXPECT_EQ(cards[0].result, "Passed automatically (Insane Bravery)");
}


// ==== Saves (D-025): format 3; a format-2 save loads with its windows declined as steps ====

TEST(M95_Saves, AnM94SaveLoadsWithItsWindowsDeclined) {
    // tests/data/legacy/m94_save_format2.json is random_session_game(5, 11, 50) as the M94 engine saved it (format 2).
    const GameSession loaded = GameSession::load(kLegacyDir / "m94_save_format2.json");
    const GameSession again = random_session_game(5, 11, 50);  // the same choices, played now
    EXPECT_EQ(loaded.state(), again.state());
    EXPECT_EQ(loaded.history(), again.history());
    EXPECT_GT(declines_in(loaded.history()), 0u);
    EXPECT_EQ(loaded.to_save_json(), again.to_save_json());
    EXPECT_EQ(loaded.to_save_json().at("format"), kReplayFormatOwnTurnStratagems);  // M96: format 4; M97: 5
}

TEST(M95_Saves, AnM94SaveWaitingForARollLoadsStillWaitingForIt) {
    // tests/data/legacy/m94_save_waiting_for_a_save_roll_format2.json is M94_Screens' save: Side A's volley, its hit and
    // wound rolls made by the session, Side B's save rolls waiting.
    const GameSession loaded = GameSession::load(kLegacyDir / "m94_save_waiting_for_a_save_roll_format2.json");
    ASSERT_TRUE(roll_pending(loaded.state()));
    EXPECT_EQ(loaded.state().pending->side, Side::B);
    EXPECT_EQ(std::get<decision::RollDice>(loaded.state().pending->kind).roll, RollKind::Save);
    // The same game played now: the session declines Side A's windows after its hit and wound rolls (D-096), and the
    // save rolls still wait.
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        GameSession now(shoot_step(seed), {true, false});
        ASSERT_TRUE(now.submit(rifle_volley(now.state())).ok());
        if (!roll_pending(now.state())) continue;
        EXPECT_EQ(loaded.state(), now.state());
        EXPECT_EQ(loaded.history(), now.history());
        EXPECT_EQ(declines_in(loaded.history()), 2u);
        return;
    }
    FAIL() << "no seed wounded the Termagants";
}

TEST(M95_Saves, AGameSavedWhileAWindowWaitsLoadsStillWaitingForIt) {
    GameSession s(shoot_step(1), {true, true}, {false, false});
    ASSERT_TRUE(s.submit(rifle_volley(s.state())).ok());
    ASSERT_TRUE(window_pending(s.state()));  // Side A's Command Re-roll, just after its hit rolls (15.02)
    EXPECT_EQ(s.state().pending->side, Side::A);
    const GameSession loaded = GameSession::from_save_json(s.to_save_json());
    EXPECT_EQ(loaded.state(), s.state());
    EXPECT_TRUE(window_pending(loaded.state()));  // loading answers nothing
    EXPECT_EQ(loaded.history(), s.history());
}

// Writes hotseat saves for screenshots of the windows (xvfb-run, --load): waiting for Side A's hit rolls (then
// `--steps 1 --dice 2` shows the rolled card with the window's buttons), at Side A's window after them with no card
// (the window's pop-up), waiting for Side B's save rolls (the defender's window), and at Insane Bravery's window. Run
// on purpose only:
//   WH40K_SCREEN_DIR=<dir> wh40k_client_tests --gtest_also_run_disabled_tests --gtest_filter=M95_Screens.*
TEST(M95_Screens, DISABLED_WriteSavesWaitingForAWindow) {
    const char* dir = std::getenv("WH40K_SCREEN_DIR");
    if (dir == nullptr) GTEST_SKIP() << "set WH40K_SCREEN_DIR";
    const std::filesystem::path out(dir);
    // A seed whose hit rolls have a critical (a gold border) and a miss (a die worth re-rolling).
    for (std::uint64_t seed = 0; seed < 40; ++seed) {
        GameSession hit(shoot_step(seed), {false, false}, {false, false});
        ASSERT_TRUE(hit.submit(rifle_volley(hit.state())).ok());
        ASSERT_TRUE(roll_pending(hit.state()));
        GameSession rolled = hit;
        ASSERT_TRUE(rolled.submit(action::RollDice{RollKind::Hit, rolled.state().units[0].id}).ok());
        if (!window_pending(rolled.state())) continue;
        const std::vector<int> faces = window_info(rolled.state())->faces;  // a copy: window_info gives a value
        if (std::count(faces.begin(), faces.end(), 6) == 0 || *std::min_element(faces.begin(), faces.end()) > 2) continue;
        std::printf("seed %llu: hit faces", static_cast<unsigned long long>(seed));
        for (const int f : faces) std::printf(" %d", f);
        std::printf("\n");
        save_game_file(out / "m95_hit_roll.json", hit, SavePlayers{});
        save_game_file(out / "m95_hit_window.json", rolled, SavePlayers{});
        break;
    }
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        GameSession save(shoot_step(seed), {true, false}, {true, false});
        ASSERT_TRUE(save.submit(rifle_volley(save.state())).ok());
        if (!roll_pending(save.state())) continue;
        save_game_file(out / "m95_save_roll.json", save, SavePlayers{});
        break;
    }
    save_game_file(out / "m95_insane_bravery.json", at_insane_bravery_window(), SavePlayers{});
}
