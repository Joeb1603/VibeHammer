// M61 (D-072): the client and [TWIN-LINKED] (24.38). The re-roll picks the pop-up toggles, the action bar's buttons
// (the first is what --steps clicks), log text, action text and the "Wound re-rolls" dice card, Undo, the automatic
// answers that must not touch the decision, a save made at it, the casualty decision's 06.02 citation (the M60
// carry-over), and the --load flag.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "action_text.hpp"
#include "auto_actions.hpp"
#include "core/engine/engine.hpp"
#include "core/rules/attack_sequence.hpp"
#include "decision_view.hpp"
#include "dice_cards.hpp"
#include "event_text.hpp"
#include "launch_options.hpp"
#include "reroll_picks.hpp"
#include "rules_help.hpp"
#include "saved_game.hpp"
#include "session.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"
#include "undo_groups.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::repo_root;
using wh40k::test::shoot_step_state;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

// The V1 Shoot step with every Bolt Rifle profile [TWIN-LINKED] (a stand-in: no V1 weapon has it).
GameState twin_linked_rifles(std::uint64_t seed) {
    GameState gs = shoot_step_state(Side::A, seed);
    for (Model& m : gs.units[kMarines].models) {
        for (WeaponProfile& p : m.weapons[0].profiles) p.abilities.push_back(ability::TwinLinked{});
    }
    return gs;
}

// Every Intercessor fires its Bolt Rifle on Saturation at the Termagants.
action::Shoot volley(const GameState& gs) {
    action::Shoot s{gs.units[kMarines].id, ShootingType::Normal, {}};
    for (const Model& m : gs.units[kMarines].models) s.shots.push_back(ShotDeclaration{m.id, 0, 1, gs.units[kGants].id});
    return s;
}

// A session (with its seeded dice) at the re-roll decision: the first seed whose volley fails a wound roll.
GameSession session_at_the_decision() {
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        GameSession session(twin_linked_rifles(seed));
        if (!session.submit(volley(session.state())).ok()) continue;
        if (session.pending() && std::holds_alternative<decision::RerollWoundRolls>(session.pending()->kind)) {
            return session;
        }
    }
    ADD_FAILURE() << "no seed reached the re-roll decision";
    return GameSession(twin_linked_rifles(1));
}

// A hand-made state at the decision with known dice: wound rolls 4, 1, 6 (3+), so die 1 failed.
GameState scripted_decision() {
    GameState gs = twin_linked_rifles(0);
    ScriptedDice dice{3, 3, 3, 4, 1, 6};
    begin_attacks(gs, dice, gs.units[kMarines].id, AttackContext{AttackType::Shooting, true},
                  {wh40k::test::select(gs, kMarines, 0, 0, 1, gs.units[kGants].id)});
    EXPECT_TRUE(gs.pending && std::holds_alternative<decision::RerollWoundRolls>(gs.pending->kind));
    return gs;
}

std::filesystem::path temp_file(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("vibehammer_m61_" + name);
}

}  // namespace

// ==== the picks the pop-up toggles ====

TEST(M61_RerollPicks, TheDiceComeFromThePendingDecisionWithTheFailedOnesMarked) {
    const GameState gs = scripted_decision();
    RerollPicks picks(gs);
    ASSERT_TRUE(picks.active());
    EXPECT_EQ(picks.attacker(), gs.units[kMarines].id);
    EXPECT_EQ(picks.required(), 3);
    ASSERT_EQ(picks.dice().size(), 3u);
    EXPECT_EQ(picks.dice()[0], (RerollDie{4, 4, false, false, false}));
    EXPECT_EQ(picks.dice()[1], (RerollDie{1, 1, true, false, false}));   // failed: outlined
    EXPECT_EQ(picks.dice()[2], (RerollDie{6, 6, false, true, false}));   // a critical wound
    EXPECT_EQ(picks.failed_count(), 1u);
    EXPECT_EQ(picks.purpose(), "Intercessor Squad shooting Termagants (Bolt Rifle - Saturation)");
    // Nothing picked yet: "Re-roll selected" re-rolls nothing.
    EXPECT_TRUE(picks.selected().empty());
    EXPECT_FALSE(picks.custom());
    // Another decision: inactive.
    EXPECT_FALSE(RerollPicks(twin_linked_rifles(0)).active());
}

TEST(M61_RerollPicks, ClickingADieTogglesItAndTheActionsFollow) {
    const GameState gs = scripted_decision();
    RerollPicks picks(gs);
    picks.toggle(2);  // a success can be picked too (24.38)
    picks.toggle(1);
    EXPECT_EQ(picks.selected(), (std::vector<std::size_t>{1, 2}));
    EXPECT_TRUE(picks.dice()[2].selected);
    EXPECT_TRUE(picks.custom());
    EXPECT_EQ(picks.action(), (action::RerollWoundRolls{{1, 2}}));
    EXPECT_TRUE(validate_action(gs, Side::A, picks.action()).ok());
    picks.toggle(2);  // a second click drops it again
    EXPECT_EQ(picks.selected(), (std::vector<std::size_t>{1}));
    EXPECT_FALSE(picks.custom());  // exactly the failed dice: "Re-roll all failed" says it
    picks.toggle(7);               // ignored
    EXPECT_EQ(picks.selected().size(), 1u);
    EXPECT_EQ(picks.all_failed(), (action::RerollWoundRolls{{1}}));
    EXPECT_EQ(RerollPicks::keep(), action::RerollWoundRolls{});
    picks.clear();
    EXPECT_TRUE(picks.selected().empty());
    picks.select_failed();
    EXPECT_EQ(picks.selected(), (std::vector<std::size_t>{1}));
}

// ==== the action bar ====

TEST(M61_DecisionPanel, TheFirstButtonIsReRollAllFailedThenKeep) {
    // --steps clicks the first decision button (make_decision_view's first): "Re-roll all failed".
    const GameState gs = scripted_decision();
    const DecisionView view = make_decision_view(legal_actions(gs, Side::A));
    EXPECT_TRUE(view.templates.empty());
    ASSERT_EQ(view.buttons.size(), 2u);
    EXPECT_EQ(view.buttons[0], GameAction{(action::RerollWoundRolls{{1}})});
    EXPECT_EQ(action_label(gs, view.buttons[0]), "Re-roll all failed (1)");
    EXPECT_EQ(action_label(gs, view.buttons[1]), "Keep the wound rolls");
    EXPECT_EQ(action_label(gs, action::RerollWoundRolls{{0, 1}}), "Re-roll 2 wound rolls");
    EXPECT_EQ(action_label(gs, action::RerollWoundRolls{{0}}), "Re-roll 1 wound roll");
    // "Waiting for" (the panel's headline and the sidebar line) cites 24.38.
    const std::string waiting = describe(gs, *gs.pending);
    EXPECT_EQ(waiting, "Side A: choose the wound rolls to re-roll for unit " + std::to_string(gs.units[kMarines].id.v) +
                           " ([TWIN-LINKED]) (24.38)");
}

TEST(M61_DecisionPanel, NeitherAutomaticSettingAnswersTheDecision) {
    // D-036 / D-038: it is the attacker's choice, like the batch order, so skipping and automatic allocation leave it.
    const GameState gs = scripted_decision();
    const AutoSettings all_on{true, true, {false, false}};
    EXPECT_FALSE(next_auto_action(gs, legal_actions(gs, Side::A), all_on).has_value());
}

// ==== text ====

TEST(M61_ClientText, TheReRollIsLoggedAndHasItsDiceCard) {
    GameState gs = scripted_decision();
    std::vector<LogEntry> log;
    ScriptedDice dice{5, 6, 6, 6};
    const std::vector<GameEvent> events = apply_action(gs, Side::A, action::RerollWoundRolls{{1}}, dice);
    log.push_back(LogEntry{"Step 1", events});
    const auto it = std::find_if(events.begin(), events.end(),
                                 [](const GameEvent& e) { return std::holds_alternative<event::WoundRollsRerolled>(e); });
    ASSERT_NE(it, events.end());
    EXPECT_EQ(format_event(*it), "[24.38] Wound re-rolls ([TWIN-LINKED]): 1x -> 5 -> 3 wounds");
    EXPECT_TRUE(is_roll_event(*it));
    // The card follows the attack's context from earlier log entries (the Shoot action's ShootingStarted names the
    // weapon).
    std::vector<LogEntry> full{LogEntry{"Step 0", {}}};
    GameState again = twin_linked_rifles(0);
    ScriptedDice first{3, 3, 3, 4, 1, 6};
    const action::Shoot one{again.units[kMarines].id, ShootingType::Normal,
                            {ShotDeclaration{again.units[kMarines].models[0].id, 0, 1, again.units[kGants].id}}};
    full.push_back(LogEntry{"Shoot", apply_action(again, Side::A, one, first)});
    ASSERT_TRUE(again.pending && std::holds_alternative<decision::RerollWoundRolls>(again.pending->kind));
    ScriptedDice second{5, 6, 6, 6};
    full.push_back(LogEntry{"Re-roll", apply_action(again, Side::A, action::RerollWoundRolls{{1}}, second)});
    const auto cards = dice_cards(again, full, 2);
    ASSERT_FALSE(cards.empty());
    const DiceCard& c = cards.front();
    EXPECT_EQ(c.title, "Wound re-rolls");
    EXPECT_EQ(c.rule, "24.38");
    EXPECT_EQ(c.roller, Side::A);
    EXPECT_EQ(c.purpose, "Intercessor Squad shooting Termagants (Bolt Rifle - Saturation)");
    ASSERT_EQ(c.dice.size(), 1u);  // only the re-rolled die, with its new face
    EXPECT_EQ(c.dice[0], (DieFace{5, true, false, false}));
    // Before the roll it says which die is re-rolled; the result gives old -> new and the new totals.
    ASSERT_EQ(c.notes.size(), 1u);
    EXPECT_EQ(c.notes[0].text, "Re-rolling die 2 (a 1)");
    EXPECT_EQ(c.result, "Die 2: 1 -> 5 (wounds). Now 3 wounds (1 critical) from 3 wound rolls");
    // The wound card says the dice may be re-rolled.
    const auto wound_cards = dice_cards(again, full, 1);
    const auto wound = std::find_if(wound_cards.begin(), wound_cards.end(),
                                    [](const DiceCard& d) { return d.title == "Wound rolls"; });
    ASSERT_NE(wound, wound_cards.end());
    EXPECT_NE(wound->need.find("[TWIN-LINKED]: the attacker may re-roll any of them (24.38)"), std::string::npos)
        << wound->need;
}

TEST(M61_ClientText, KeepIsLoggedButHasNoCard) {
    GameState gs = scripted_decision();
    ScriptedDice dice{6, 6};
    const std::vector<GameEvent> events = apply_action(gs, Side::A, action::RerollWoundRolls{}, dice);
    const auto it = std::find_if(events.begin(), events.end(),
                                 [](const GameEvent& e) { return std::holds_alternative<event::WoundRollsRerolled>(e); });
    ASSERT_NE(it, events.end());
    EXPECT_EQ(format_event(*it), "[24.38] Wound rolls kept, none re-rolled ([TWIN-LINKED]) -> 2 wounds");
    EXPECT_FALSE(is_roll_event(*it));
    const std::vector<LogEntry> log{LogEntry{"Keep", events}};
    for (const DiceCard& c : dice_cards(gs, log, 0)) EXPECT_NE(c.title, "Wound re-rolls");
}

TEST(M61_ClientText, TheHelpAndTheAbilityLineSayItIsInTheGame) {
    const HelpEntry h = weapon_ability_help(ability::TwinLinked{});
    EXPECT_TRUE(h.in_this_game);
    EXPECT_EQ(h.rule, "24.38");
    WeaponProfile p = twin_linked_rifles(0).units[kMarines].models[0].weapons[0].profiles[1];
    bool found = false;
    for (const EffectLine& line : effect_lines(p, {}, AttackContext{AttackType::Shooting, false})) {
        if (line.text == "Twin-linked: the wound rolls can be re-rolled") {
            found = true;
            EXPECT_EQ(line.kind, EffectKind::Active);
        }
    }
    EXPECT_TRUE(found);
}

TEST(M61_ClientText, AHazardCasualtyDecisionIsWaitingFor0602) {
    // The M60 carry-over: the casualty decision for [HAZARDOUS] mortal wounds (24.15) answers 06.02, and the
    // "Waiting for" line (describe with the state) says so; a casualty decision for failed saves still says 05.04.
    GameSession session(load_scenario(repo_root() / wh40k::test::kHavocsVsTyrantFixture, repo_root()));
    ASSERT_TRUE(session.submit(action::StartMove{session.state().units[0].id, MoveType::RemainStationary, std::nullopt}).ok());
    GameState gs = session.state();
    const std::size_t sc = gs.units[0].models[0].weapons[0].profiles[1].has<ability::Hazardous>() ? 1 : 0;
    ScriptedDice dice{1, 1, 2};  // both shots miss; the hazard roll fails
    apply_action(gs, Side::A,
                 action::Shoot{gs.units[0].id, ShootingType::Normal, {ShotDeclaration{gs.units[0].models[0].id, 0, sc, gs.units[1].id}}},
                 dice);
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::OrderCasualties>(gs.pending->kind));
    const std::string waiting = describe(gs, *gs.pending);
    EXPECT_EQ(waiting.substr(waiting.size() - 7), "(06.02)") << waiting;
    // Failed saves: 05.04.
    GameState saves = twin_linked_rifles(0);
    for (Model& m : saves.units[kMarines].models) m.weapons[0].profiles[1].abilities.clear();
    ScriptedDice d{3, 3, 3, 3, 3, 3, 1, 1, 1};
    begin_attacks(saves, d, saves.units[kMarines].id, AttackContext{AttackType::Shooting, true},
                  {wh40k::test::select(saves, kMarines, 0, 0, 1, saves.units[kGants].id)});
    ASSERT_TRUE(saves.pending && std::holds_alternative<decision::OrderCasualties>(saves.pending->kind));
    const std::string casualties = describe(saves, *saves.pending);
    EXPECT_EQ(casualties.substr(casualties.size() - 7), "(05.04)") << casualties;
}

// ==== Undo, saves ====

TEST(M61_Undo, TheAnswerIsItsOwnGroupAndUndoingItReRollsTheSameDice) {
    GameSession session = session_at_the_decision();
    ASSERT_TRUE(session.pending() && std::holds_alternative<decision::RerollWoundRolls>(session.pending()->kind));
    ASSERT_EQ(session.history().size(), 1u);  // the volley
    // D-039: the volley was one input, the re-roll (with what the client then does by itself) is another.
    UndoGroups groups;
    groups.begin(0);
    groups.end(1);
    const GameState at_decision = session.state();
    const RerollPicks picks(session.state());
    groups.begin(session.history().size());
    ASSERT_TRUE(session.submit(picks.all_failed()).ok());
    run_auto_actions(session, AutoSettings{true, true, {false, false}});
    groups.end(session.history().size());
    const GameState after = session.state();
    const std::size_t steps = session.history().size();
    ASSERT_GT(steps, 1u);
    // Undo takes back the re-roll and what followed automatically: back at the decision, the dice position too.
    ASSERT_TRUE(groups.undo(session));
    EXPECT_EQ(session.history().size(), 1u);
    EXPECT_EQ(session.state(), at_decision);
    // The same answer gives the same dice (D-025).
    groups.begin(session.history().size());
    ASSERT_TRUE(session.submit(picks.all_failed()).ok());
    run_auto_actions(session, AutoSettings{true, true, {false, false}});
    groups.end(session.history().size());
    EXPECT_EQ(session.state(), after);
    EXPECT_EQ(session.history().size(), steps);
    // Two undos: the re-roll, then the volley, back to the Shoot decision.
    ASSERT_TRUE(groups.undo(session));
    ASSERT_TRUE(groups.undo(session));
    EXPECT_TRUE(session.history().empty());
    EXPECT_EQ(*session.pending(), (PendingDecision{Side::A, decision::Shoot{}}));
}

TEST(M61_Saves, ASaveMadeAtTheDecisionLoadsBackToIt) {
    const GameSession session = session_at_the_decision();
    const SavePlayers players{{false, true}};
    const LoadedGame loaded = load_game_json(save_json(session, players));
    EXPECT_EQ(loaded.session.state(), session.state());
    ASSERT_TRUE(loaded.session.pending());
    EXPECT_TRUE(std::holds_alternative<decision::RerollWoundRolls>(loaded.session.pending()->kind));
    EXPECT_EQ(loaded.session.legal(), session.legal());
    EXPECT_EQ(loaded.players, players);
}

// ==== --load ====

TEST(M61_LaunchOptions, LoadParsesAndCombinesWithTheScreenshotFlags) {
    const LaunchOptions o = parse_launch_args({"--load", "saves/game.json", "--screenshot", "out.png", "--steps", "1",
                                               "--bot", "B", "--bot-steps", "3", "--dice", "2",
                                               "--zoom", "1.5", "--select", "2"});
    ASSERT_TRUE(o.load.has_value());
    EXPECT_EQ(*o.load, "saves/game.json");
    EXPECT_EQ(o.steps, 1);
    EXPECT_TRUE(o.bots_given);
    EXPECT_EQ(o.bots, (BotSides{false, true}));
    EXPECT_EQ(o.bot_steps, 3);
    EXPECT_EQ(o.dice, 2);
    EXPECT_EQ(o.select_unit, 2u);
    // The save's players, unless --bot says otherwise.
    const SavePlayers saved{{true, false}};
    EXPECT_EQ(launch_players(saved, o), (SavePlayers{{false, true}}));
    // D-085 (M85): one bot, so --bot-level is gone; like any unknown flag it is ignored.
    const LaunchOptions old = parse_launch_args({"--load", "x.json", "--bot", "B", "--bot-level", "hard"});
    EXPECT_EQ(launch_players(saved, old), (SavePlayers{{false, true}}));
    EXPECT_EQ(launch_players(saved, parse_launch_args({"--load", "x.json"})), saved);
    // The flags it can't be combined with, and an empty value.
    for (const char* other : {"--scenario", "--replay", "--seed"}) {
        EXPECT_THROW((void)parse_launch_args({"--load", "x.json", other, "1"}), std::invalid_argument) << other;
        EXPECT_THROW((void)parse_launch_args({other, "1", "--load", "x.json"}), std::invalid_argument) << other;
    }
    EXPECT_THROW((void)parse_launch_args({"--load", ""}), std::invalid_argument);
    // --pick-dice: the wound dice the first re-roll pop-up picks (for screenshots).
    EXPECT_EQ(parse_launch_args({"--pick-dice", "2,0"}).pick_dice, (std::vector<std::size_t>{2, 0}));
    EXPECT_EQ(parse_launch_args({"--pick-dice", "1"}).pick_dice, (std::vector<std::size_t>{1}));
    for (const char* bad : {"", "1,", "a", "1,,2", "-1"}) {
        EXPECT_THROW((void)parse_launch_args({"--pick-dice", bad}), std::invalid_argument) << bad;
    }
    // The other flags parse as they always did.
    EXPECT_EQ(parse_launch_args({"--seed", "7", "--scenario", "s.json"}).seed, 7u);
    EXPECT_THROW((void)parse_launch_args({"--bot", "C"}), std::invalid_argument);
    EXPECT_FALSE(parse_launch_args({}).load.has_value());
}

TEST(M61_LaunchOptions, AGoodSaveLoadsAndABadOneIsRefused) {
    const GameSession session = session_at_the_decision();
    const std::filesystem::path good = temp_file("good.json");
    save_game_file(good, session, SavePlayers{});
    const LoadedGame loaded = load_launch_save(good.string(), repo_root());
    EXPECT_EQ(loaded.session.state(), session.state());
    EXPECT_TRUE(std::holds_alternative<decision::RerollWoundRolls>(loaded.session.pending()->kind));
    std::filesystem::remove(good);
    // A missing file, one that isn't JSON, and a save whose steps don't rebuild its state.
    const auto refused = [](const std::string& file) {
        try {
            (void)load_launch_save(file, repo_root());
        } catch (const std::invalid_argument& e) {
            return std::string(e.what());
        }
        return std::string();
    };
    const std::string missing = refused(temp_file("missing.json").string());
    EXPECT_EQ(missing.rfind("invalid value for --load: '", 0), 0u) << missing;
    EXPECT_NE(missing.find("no such file"), std::string::npos) << missing;
    const std::filesystem::path junk = temp_file("junk.json");
    std::ofstream(junk) << "not json";
    EXPECT_EQ(refused(junk.string()).rfind("invalid value for --load: ", 0), 0u);
    std::filesystem::remove(junk);
    nlohmann::json corrupt = save_json(session, SavePlayers{});
    corrupt["state"]["battle_rounds"] = 99;
    const std::filesystem::path bad = temp_file("corrupt.json");
    std::ofstream(bad) << corrupt.dump();
    EXPECT_EQ(refused(bad.string()).rfind("invalid value for --load: ", 0), 0u);
    std::filesystem::remove(bad);
}
