// M39 (D-054): the client's side of playing against the bot. Auto-allocate leaves the bot's decisions to the
// bot, Undo against a bot returns to the human's last input, and saves record who played each side. The "bot"
// here is a stand-in that submits through the session like the real one (wh40k_client_tests links only
// wh40k_client_logic).

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <variant>
#include <vector>

#include "auto_actions.hpp"
#include "casualty_plan.hpp"
#include "core/serialization/json.hpp"
#include "decision_view.hpp"
#include "melee_plan.hpp"
#include "saved_game.hpp"
#include "session.hpp"
#include "settings.hpp"
#include "shot_plan.hpp"
#include "test_helpers.hpp"
#include "undo_groups.hpp"

using namespace wh40k::core;
using namespace wh40k::client;

namespace {

// A simple player: the first button, or the editors' defaults for a template.
GameAction simple_choice(const GameSession& s) {
    const DecisionView view = make_decision_view(s.legal());
    for (const GameAction& t : view.templates) {
        if (const auto* f = std::get_if<action::Fight>(&t)) return MeleePlan(s.state(), f->unit).expand();
        if (const auto* c = std::get_if<action::OrderCasualties>(&t)) return CasualtyPlan(s.state(), c->target).action();
    }
    return view.buttons.front();
}

// A state where `side` must answer a casualty or allocation decision, along a hotseat game of simple choices.
std::optional<GameSession> reactive_decision() {
    GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario_closer(), 2);
    for (int i = 0; i < 2000 && s.pending(); ++i) {
        const auto& kind = s.pending()->kind;
        if (std::holds_alternative<decision::OrderCasualties>(kind) ||
            std::holds_alternative<decision::DeclareAllocationOrder>(kind)) {
            return s;
        }
        // Shoot when offered, so that damage is allocated.
        const DecisionView view = make_decision_view(s.legal());
        std::optional<GameAction> shot;
        for (const GameAction& t : view.templates) {
            if (const auto* sh = std::get_if<action::Shoot>(&t); sh != nullptr && !shot) {
                const action::Shoot a = ShotPlan(s.state(), sh->unit, sh->type).expand();
                if (!a.shots.empty()) shot = a;
            }
        }
        if (shot) {
            if (!s.submit(*shot).ok()) return std::nullopt;
            continue;
        }
        if (!s.submit(simple_choice(s)).ok()) return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace

TEST(M39_AutoActions, AutoAllocateLeavesTheBotsDecisionsToTheBot) {
    std::optional<GameSession> s = reactive_decision();
    ASSERT_TRUE(s.has_value());
    const Side defender = s->pending()->side;
    AutoSettings human;
    human.skip_empty_steps = false;
    human.auto_allocate = true;
    EXPECT_TRUE(next_auto_action(s->state(), s->legal(), human).has_value());  // a human's: answered
    AutoSettings bot = human;
    bot.bot_controlled[side_index(defender)] = true;
    EXPECT_FALSE(next_auto_action(s->state(), s->legal(), bot).has_value());  // the bot's: left to the bot
    AutoSettings other = human;
    other.bot_controlled[side_index(opponent(defender))] = true;
    EXPECT_TRUE(next_auto_action(s->state(), s->legal(), other).has_value());  // the bot plays the other side
}

TEST(M39_AutoActions, ForcedSkipsStillApplyToTheBot) {
    // A lone skip is the only thing the side can do, bot or not.
    GameSession s(wh40k::test::load_v1_scenario());
    std::vector<GameAction> lone{action::EndShootStep{}};
    AutoSettings settings;
    settings.bot_controlled = {true, true};
    const auto a = next_auto_action(s.state(), lone, settings);
    ASSERT_TRUE(a.has_value());
    EXPECT_TRUE(std::holds_alternative<action::EndShootStep>(*a));
}

TEST(M39_UndoGroups, UndoAgainstABotReturnsToTheHumansLastInput) {
    // Side A is the human, side B the bot. Play until the bot has made two or more inputs after the human's last.
    GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario(), 4);
    UndoGroups groups;
    const BotSides bots{false, true};
    AutoSettings autos;
    autos.bot_controlled = bots;
    std::size_t human_start = 0;
    GameState before_human;
    int bot_groups_after = 0;
    for (int i = 0; i < 3000 && s.pending(); ++i) {
        const bool bot = bots[side_index(s.pending()->side)];
        if (!bot && bot_groups_after >= 2) break;  // the human's decision again, after the bot's turn
        const std::size_t start = s.history().size();
        if (!bot) {
            human_start = start;
            before_human = s.state();
            bot_groups_after = 0;
        } else {
            ++bot_groups_after;
        }
        groups.begin(start, bot ? InputOwner::Bot : InputOwner::Human);
        ASSERT_TRUE(s.submit(simple_choice(s)).ok());
        (void)run_auto_actions(s, autos);
        groups.end(s.history().size());
    }
    ASSERT_GE(bot_groups_after, 2);
    ASSERT_TRUE(s.pending().has_value());
    EXPECT_FALSE(bots[side_index(s.pending()->side)]);  // the human must act now
    EXPECT_TRUE(groups.has_human_group(s));
    EXPECT_FALSE(groups.last_group_is_move_step(s));  // never offered for the bot's input

    ASSERT_TRUE(groups.undo_human(s));
    EXPECT_EQ(s.history().size(), human_start);
    EXPECT_EQ(s.state(), before_human);
    EXPECT_FALSE(bots[side_index(s.pending()->side)]);  // back at the human's decision
}

TEST(M39_UndoGroups, NothingToUndoWhenOnlyTheBotHasPlayed) {
    GameSession s(wh40k::test::load_v1_scenario());
    UndoGroups groups;
    groups.begin(s.history().size(), InputOwner::Bot);
    ASSERT_TRUE(s.submit(simple_choice(s)).ok());
    groups.end(s.history().size());
    EXPECT_FALSE(groups.has_human_group(s));
    const std::size_t size = s.history().size();
    EXPECT_FALSE(groups.undo_human(s));
    EXPECT_EQ(s.history().size(), size);
    EXPECT_TRUE(groups.undo(s));  // the plain Undo (hotseat) still pops the last group
    EXPECT_EQ(s.history().size(), 0u);
}

namespace {

std::filesystem::path temp_save(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("vibehammer_m39_" + name);
}

}  // namespace

TEST(M39_SavePlayers, ASaveWithAPlayersBlockReloadsWithItsPlayers) {
    GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario(), 11);
    for (int i = 0; i < 5; ++i) ASSERT_TRUE(s.submit(simple_choice(s)).ok());
    const SavePlayers players{BotSides{false, true}, BotLevel::Normal};
    const nlohmann::json j = save_json(s, players);
    ASSERT_TRUE(j.contains("players"));
    EXPECT_EQ(j["players"]["A"], "human");
    EXPECT_EQ(j["players"]["B"], "bot");
    EXPECT_EQ(j["players"]["bot_level"], "normal");
    const auto path = temp_save("players.json");
    save_game_file(path, s, players);
    const LoadedGame loaded = load_game_file(path);
    std::filesystem::remove(path);
    EXPECT_EQ(loaded.players, players);
    EXPECT_EQ(loaded.session.state(), s.state());
    EXPECT_EQ(loaded.session.history(), s.history());
}

TEST(M39_SavePlayers, AnOldSaveLoadsAsHotseat) {
    GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario(), 12);
    ASSERT_TRUE(s.submit(simple_choice(s)).ok());
    const auto path = temp_save("old.json");
    s.save(path);  // the M13 format, without "players"
    const LoadedGame loaded = load_game_file(path);
    std::filesystem::remove(path);
    EXPECT_EQ(loaded.players, SavePlayers{});
    EXPECT_FALSE(loaded.players.bots[0] || loaded.players.bots[1]);
    EXPECT_EQ(loaded.session.state(), s.state());
    // A bare GameState too.
    EXPECT_EQ(load_game_json(nlohmann::json(s.state())).players, SavePlayers{});
}

TEST(M39_SavePlayers, AnUnreadablePlayersBlockIsHotseat) {
    GameSession s(wh40k::test::load_v1_scenario());
    nlohmann::json j = save_json(s, SavePlayers{BotSides{true, true}, BotLevel::Hard});
    j["players"]["A"] = 3;
    EXPECT_EQ(players_from_save_json(j), SavePlayers{});
    j["players"] = "bot";
    EXPECT_EQ(players_from_save_json(j), SavePlayers{});
    j["players"] = nlohmann::json{{"A", "bot"}, {"B", "human"}, {"bot_level", "impossible"}};
    const SavePlayers p = players_from_save_json(j);  // an unknown level keeps the default
    EXPECT_EQ(p.bots, (BotSides{true, false}));
    EXPECT_EQ(p.bot_level, BotLevel::Easy);
}

TEST(M39_Settings, BotLevelAndSpeedKeys) {
    for (const BotLevel l : {BotLevel::Easy, BotLevel::Normal, BotLevel::Hard}) {
        EXPECT_EQ(bot_level_from_key(bot_level_key(l)), l);
    }
    for (const BotSpeed sp : {BotSpeed::Slow, BotSpeed::Normal, BotSpeed::Fast, BotSpeed::Instant}) {
        EXPECT_EQ(bot_speed_from_key(bot_speed_key(sp)), sp);
    }
    EXPECT_FALSE(bot_level_from_key("Easy").has_value());
    EXPECT_FALSE(bot_speed_from_key("quick").has_value());
}
