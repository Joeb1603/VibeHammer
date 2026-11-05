// M12: the client's logic layer (D-024). GameSession (validate before apply), the decision view, the
// shooting/melee/move/casualty editors' models, a full game played through that layer only, and the
// structural check that the client has no rules logic.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "action_text.hpp"
#include "casualty_plan.hpp"
#include "core/state/queries.hpp"
#include "decision_view.hpp"
#include "event_text.hpp"
#include "melee_plan.hpp"
#include "move_plan.hpp"
#include "session.hpp"
#include "shot_plan.hpp"
#include "core/data/load_error.hpp"
#include "core/engine/replay.hpp"
#include "core/serialization/engine_json.hpp"
#include "core/serialization/json.hpp"
#include "support/engine_helpers.hpp"
#include "timeline.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::events_of;
using wh40k::test::in;

namespace {

const std::vector<GameEvent>& last_events(const GameSession& s) { return s.log().back().events; }

}  // namespace

// ==== M12 acceptance: a unit-wide default plus a single-model override (D-001) ====

TEST(M12_ShotPlan, UnitDefaultPlusOneOverrideGivesMixedBoltRifleProfilesThatTheEngineAccepts) {
    GameSession session(wh40k::test::shoot_step_state());
    const GameState& gs = session.state();
    const UnitId marines = gs.units[0].id;
    const UnitId gants = gs.units[1].id;
    ShotPlan plan(gs, marines, ShootingType::Normal);
    plan.set_default(ShotChoice{"Bolt Rifle", 0, gants});  // Focused Fire for the whole unit
    const ModelId third = gs.units[0].models[2].id;
    const auto options = plan.options_for(third);
    const auto saturation = std::find_if(options.begin(), options.end(), [](const ShotOption& o) {
        return o.weapon == "Bolt Rifle" && o.declaration.profile_index == 1;
    });
    ASSERT_NE(saturation, options.end());
    plan.override_model(third, saturation->declaration);

    const action::Shoot shoot = plan.expand();
    ASSERT_EQ(shoot.shots.size(), 5u);
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(shoot.shots[i].weapon_index, 0u);
        EXPECT_EQ(shoot.shots[i].profile_index, i == 2 ? 1u : 0u);  // mixed profiles
    }
    ASSERT_TRUE(session.check(shoot).ok());
    ASSERT_TRUE(session.submit(shoot).ok());
    const auto gathered = events_of<event::AttackBatchesGathered>(last_events(session));
    ASSERT_EQ(gathered.size(), 1u);
    EXPECT_EQ(gathered[0].batches.size(), 2u);  // Focused Fire and Saturation are not identical (D-006)
}

TEST(M12_ShotPlan, ModelsWithoutTheDefaultOptionDoNotFireUnlessOverridden) {
    GameSession session(wh40k::test::shoot_step_state());
    const GameState& gs = session.state();
    ShotPlan plan(gs, gs.units[0].id, ShootingType::Normal);
    plan.set_default(ShotChoice{"Bolt pistol", 0, gs.units[1].id});  // 12": every model is in range here
    EXPECT_EQ(plan.expand().shots.size(), 5u);
    plan.override_model(gs.units[0].models[0].id, std::nullopt);  // this model doesn't fire
    EXPECT_EQ(plan.expand().shots.size(), 4u);
    plan.clear_override(gs.units[0].models[0].id);
    EXPECT_EQ(plan.expand().shots.size(), 5u);
}

// ==== GameSession: validate before apply ====

TEST(M12_GameSession, AnInvalidActionIsRejectedWithItsReasonsAndChangesNothing) {
    GameSession session(wh40k::test::load_v1_scenario());
    const GameState before = session.state();
    const auto r = session.submit(action::EndShootStep{});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.reasons.at(0).substr(0, 7), "10.02: ");
    EXPECT_EQ(session.state(), before);
    EXPECT_TRUE(session.history().empty());
    EXPECT_EQ(session.log().size(), 1u);  // just the game start
}

TEST(M12_GameSession, ValidActionsAreAppliedAndRecorded) {
    GameSession session(wh40k::test::load_v1_scenario());
    const auto first = session.legal().front();
    ASSERT_TRUE(session.submit(first).ok());
    ASSERT_EQ(session.history().size(), 1u);
    EXPECT_EQ(session.history()[0].action, first);
    EXPECT_EQ(session.log().back().label.rfind("Step 1: Side A ", 0), 0u);
}

// ==== the decision view ====

TEST(M12_DecisionView, MoveModelsGivesCommitResetButtonsAndPathTemplates) {
    GameSession session(wh40k::test::load_v1_scenario());
    ASSERT_TRUE(session.submit(action::StartMove{session.state().units[0].id, MoveType::Normal, std::nullopt}).ok());
    const DecisionView view = make_decision_view(session.legal());
    EXPECT_EQ(view.buttons, (std::vector<GameAction>{action::CommitUnitMove{}, action::ResetUnitMove{}}));
    ASSERT_EQ(view.templates.size(), 5u);
    for (const auto& t : view.templates) EXPECT_EQ(editor_kind(t), EditorKind::Path);
    EXPECT_EQ(action_label(session.state(), view.buttons[0]), "Commit move");
}

TEST(M12_DecisionView, EveryTemplateKindHasAnEditor) {
    EXPECT_EQ(editor_kind(action::Shoot{UnitId{1}, ShootingType::Normal, {}}), EditorKind::Shot);
    EXPECT_EQ(editor_kind(action::Fight{UnitId{1}, {}}), EditorKind::Melee);
    EXPECT_EQ(editor_kind(action::OrderCasualties{UnitId{1}, {}}), EditorKind::Casualties);
    EXPECT_EQ(editor_kind(action::ProposeModelMove{ModelId{1}, {}}), EditorKind::Path);
    EXPECT_FALSE(editor_kind(action::EndShootStep{}).has_value());
}

// ==== moves: drag paths and the "Move unit" handle ====

TEST(M12_MovePlan, TheMoveUnitHandleTranslatesEveryModelAndTheEngineAcceptsIt) {
    GameSession session(wh40k::test::load_v1_scenario());
    const UnitId marines = session.state().units[0].id;
    ASSERT_TRUE(session.submit(action::StartMove{marines, MoveType::Normal, std::nullopt}).ok());
    for (const auto& a : translate_unit(session.state(), marines, 0.0f, in(2))) {
        ASSERT_TRUE(session.submit(a).ok());
    }
    const auto r = ruler(session.state(), session.state().units[0].models[0].id);
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(mm_to_inches(r->moved_mm), 2.0f, 0.001f);
    EXPECT_NEAR(mm_to_inches(r->max_mm), 6.0f, 0.001f);
    EXPECT_FALSE(r->over);
    ASSERT_TRUE(session.submit(action::CommitUnitMove{}).ok());
    EXPECT_TRUE(events_of<event::UnitMoveCommitted>(last_events(session)).size() == 1u);
}

TEST(M12_MovePlan, DragsAccumulateSegmentsAndTheRulerFlagsTooFar) {
    GameSession session(wh40k::test::load_v1_scenario());
    const UnitId marines = session.state().units[0].id;
    ASSERT_TRUE(session.submit(action::StartMove{marines, MoveType::Normal, std::nullopt}).ok());
    const ModelId m = session.state().units[0].models[0].id;
    const Position start = session.state().units[0].models[0].position;
    ASSERT_TRUE(session.submit(extend_path(session.state(), m, Position{start.x, start.y + in(3), 0})).ok());
    const action::ProposeModelMove second = extend_path(session.state(), m, Position{start.x + in(2), start.y + in(3), 0});
    EXPECT_EQ(second.path.size(), 2u);  // the first straight line and a new one
    // 3" proposed, plus a line to the cursor: 5.9" is within the 6" maximum, 6.5" is not.
    EXPECT_FALSE(ruler(session.state(), m, Position{start.x + in(2.9f), start.y + in(3), 0})->over);
    EXPECT_TRUE(ruler(session.state(), m, Position{start.x + in(3.5f), start.y + in(3), 0})->over);
}

// ==== melee and casualties ====

TEST(M12_MeleePlan, TheDefaultDeclarationIsValidAndOverridesAreChecked) {
    GameState gs = wh40k::test::after_successful_charge();
    GameSession session(gs);
    ASSERT_TRUE(session.submit(action::EndChargeStep{}).ok());
    while (session.state().flow.stage == Stage::PileIn) ASSERT_TRUE(session.submit(action::EndPileIns{}).ok());
    const UnitId marines = session.state().units[0].id;
    ASSERT_TRUE(session.submit(action::SelectToFight{marines, FightType::Normal}).ok());
    MeleePlan plan(session.state(), marines);
    const action::Fight fight = plan.expand();
    EXPECT_EQ(fight.declarations.size(), 5u);
    EXPECT_TRUE(session.check(fight).ok());

    MeleeDeclaration wrong = *plan.declaration_for(session.state().units[0].models[0].id);
    wrong.targets.front().attacks = 2;  // Knives and Fists has 3 attacks
    plan.override_model(wrong.model, wrong);
    const auto r = session.check(plan.expand());
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.reasons.at(0), "04.02: model 1's 'Knives and Fists' has 3 attacks but 2 are declared");
}

TEST(M12_CasualtyPlan, ReordersThePriority) {
    const GameState gs = wh40k::test::load_v1_scenario();
    CasualtyPlan plan(gs, gs.units[1].id);
    ASSERT_EQ(plan.priority().size(), 10u);
    const ModelId first = plan.priority()[0];
    const ModelId second = plan.priority()[1];
    plan.move_down(0);
    EXPECT_EQ(plan.priority()[0], second);
    EXPECT_EQ(plan.priority()[1], first);
    plan.move_up(1);
    EXPECT_EQ(plan.priority()[0], first);
    plan.move_up(0);  // no-op at the top
    EXPECT_EQ(plan.action().priority, plan.priority());
}

// ==== M12 acceptance (logic-level proxy): a full game through the UI layer only ====

namespace {

// The direction from `from`'s models to `to`'s, as a unit vector on the battlefield plane.
std::pair<float, float> direction(const Unit& from, const Unit& to) {
    auto centre = [](const Unit& u) {
        float x = 0, y = 0;
        for (const Model& m : u.models) { x += m.position.x; y += m.position.y; }
        return std::pair<float, float>{x / static_cast<float>(u.models.size()), y / static_cast<float>(u.models.size())};
    };
    const auto [ax, ay] = centre(from);
    const auto [bx, by] = centre(to);
    const float len = std::hypot(bx - ax, by - ay);
    return {(bx - ax) / len, (by - ay) / len};
}

const Unit* enemy_of(const GameState& gs, const Unit& u) {
    for (const Unit& o : gs.units) {
        if (o.side != u.side && !o.models.empty()) return &o;
    }
    return nullptr;
}

// What a player clicking through the UI does: buttons from the decision view, editor defaults, and the
// "Move unit" handle. Never builds an action any other way.
GameAction choose(const GameSession& s, int& move_tries) {
    const GameState& gs = s.state();
    const DecisionView view = make_decision_view(s.legal());
    const auto& kind = s.pending()->kind;
    auto button = [&](auto pred) -> std::optional<GameAction> {
        for (const auto& b : view.buttons) {
            if (pred(b)) return b;
        }
        return std::nullopt;
    };
    if (std::holds_alternative<decision::MoveUnits>(kind)) {
        // Normal move towards the enemy while far away; once a move has been reverted, remain stationary.
        if (move_tries == 0) {
            if (auto b = button([](const GameAction& a) {
                    const auto* m = std::get_if<action::StartMove>(&a);
                    return m != nullptr && m->move_type == MoveType::Normal;
                })) {
                const Unit& u = *gs.find_unit(std::get<action::StartMove>(*b).unit);
                const Unit* e = enemy_of(gs, u);
                if (e != nullptr && mm_to_inches(*unit_distance(u, *e)) > 8.0f) return *b;
            }
        }
        return view.buttons.front();  // Remain Stationary (or the only option)
    }
    if (std::holds_alternative<decision::MoveModels>(kind) && gs.flow.move) {
        // Propose every unmoved model once with the "Move unit" handle (2" towards the enemy, or the full
        // charge roll for a charge), then commit.
        const Unit& u = *gs.find_unit(gs.flow.move->unit);
        if (gs.flow.move->paths.size() < u.models.size()) {
            const Unit* e = enemy_of(gs, u);
            float dist = in(2);
            if (gs.flow.move->type == MoveType::Charge) {
                dist = std::max(0.0f, *unit_distance(u, *e) - in(1));
            }
            const auto [dx, dy] = direction(u, *e);
            for (const auto& a : translate_unit(gs, u.id, dx * dist, dy * dist)) {
                if (std::none_of(gs.flow.move->paths.begin(), gs.flow.move->paths.end(),
                                 [&](const ModelPath& p) { return p.model == a.model; })) {
                    return a;
                }
            }
        }
        ++move_tries;
        return action::CommitUnitMove{};
    }
    if (std::holds_alternative<decision::SelectChargeTargets>(kind)) {
        return move_tries > 0 ? view.buttons.back() : view.buttons.front();  // charge once, then skip
    }
    for (const auto& t : view.templates) {
        switch (*editor_kind(t)) {
            case EditorKind::Shot: {
                const auto& sh = std::get<action::Shoot>(t);
                const action::Shoot a = ShotPlan(gs, sh.unit, sh.type).expand();
                if (!a.shots.empty()) return a;
                break;
            }
            case EditorKind::Melee: return MeleePlan(gs, std::get<action::Fight>(t).unit).expand();
            case EditorKind::Casualties: return CasualtyPlan(gs, std::get<action::OrderCasualties>(t).target).action();
            case EditorKind::Path: break;
        }
    }
    // Prefer shooting/charging/fighting over passing when offered.
    if (auto b = button([](const GameAction& a) {
            return std::holds_alternative<action::DeclareCharge>(a) || std::holds_alternative<action::SelectToFight>(a);
        })) {
        return *b;
    }
    return view.buttons.front();
}

}  // namespace

TEST(M12_FullGame, AGameCanBePlayedFromRoundOneToGameOverThroughTheUiLayer) {
    int finished = 0;
    std::map<std::string, int> kinds;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        GameState initial = wh40k::test::load_v1_scenario();
        initial.dice.seed = seed;
        GameSession session(initial);
        int move_tries = 0;
        Stage last_stage = session.state().flow.stage;
        for (int step = 0; step < 3000 && session.pending(); ++step) {
            if (session.state().flow.stage != last_stage &&
                !std::holds_alternative<decision::MoveModels>(session.pending()->kind) &&
                !std::holds_alternative<decision::SelectChargeTargets>(session.pending()->kind)) {
                move_tries = 0;
                last_stage = session.state().flow.stage;
            }
            const GameAction a = choose(session, move_tries);
            const auto r = session.submit(a);
            ASSERT_TRUE(r.ok()) << "seed " << seed << ": " << action_name(a) << ": " << r.reasons.at(0);
        }
        ASSERT_TRUE(session.state().result.has_value()) << "seed " << seed << " did not finish";
        ++finished;
        for (const auto& step : session.history()) ++kinds[std::string(action_name(step.action))];
    }
    EXPECT_EQ(finished, 8);
    // The games used the editors and the board, not just the pass buttons.
    for (const char* k : {"ProposeModelMove", "CommitUnitMove", "Shoot", "DeclareCharge", "StartChargeMove",
                          "SelectToFight", "Fight", "OrderCasualties"}) {
        EXPECT_GT(kinds[k], 0) << k;
    }
}

// ==== M12 acceptance: no rules logic in the client (structural) ====

TEST(M12_NoRulesInTheClient, OnlyTheSessionAndTimelineApplyActions) {
    namespace fs = std::filesystem;
    const fs::path client = fs::path(WH40K_REPO_ROOT) / "src" / "client_2d";
    const std::regex apply(R"(\bapply_action\s*\()");
    const std::regex rules_include(R"(#\s*include\s*"core/rules/)");
    const std::regex allowed_logic_rules(R"(#\s*include\s*"core/rules/(shooting|fight)(_types)?\.hpp"|#\s*include\s*"core/rules/(move_types|attack_types)\.hpp")");
    int files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(client)) {
        const auto ext = entry.path().extension();
        if (ext != ".cpp" && ext != ".hpp") continue;
        ++files;
        std::ifstream in_file(entry.path());
        std::stringstream text;
        text << in_file.rdbuf();
        const std::string src = text.str();
        const std::string name = entry.path().filename().string();
        const bool in_logic = entry.path().parent_path().filename() == "logic";
        if (name != "session.cpp" && name != "timeline.cpp") {
            EXPECT_FALSE(std::regex_search(src, apply)) << name << " calls apply_action; use GameSession::submit";
        }
        std::istringstream lines(src);
        for (std::string line; std::getline(lines, line);) {
            if (!std::regex_search(line, rules_include)) continue;
            // D-024: only the logic library uses core's read-only rule queries (and the plain type headers).
            EXPECT_TRUE(in_logic && std::regex_search(line, allowed_logic_rules)) << name << ": " << line;
        }
    }
    EXPECT_GT(files, 10);
}

// ==== M13: save, load, replay export, undo and the seed ====

namespace {

std::filesystem::path temp_file(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("vibehammer_" + name);
}

// From the V1 scenario with `seed`: Side A moves 6" north, skips shooting and declares a charge. Returns
// nullopt if that roll gives no charge target.
std::optional<GameSession> mid_charge(std::uint64_t seed) {
    GameState initial = wh40k::test::load_v1_scenario();
    GameSession s = GameSession::new_game(initial, seed);
    const UnitId marines = s.state().units[0].id;
    if (!s.submit(action::StartMove{marines, MoveType::Normal, std::nullopt}).ok()) return std::nullopt;
    for (const auto& a : translate_unit(s.state(), marines, 0.0f, in(6))) {
        if (!s.submit(a).ok()) return std::nullopt;
    }
    if (!s.submit(action::CommitUnitMove{}).ok() || !s.submit(action::EndShootStep{}).ok()) return std::nullopt;
    if (!s.submit(action::DeclareCharge{marines}).ok()) return std::nullopt;
    if (!s.pending() || !std::holds_alternative<decision::SelectChargeTargets>(s.pending()->kind)) return std::nullopt;
    return s;
}

}  // namespace

TEST(M13_SaveLoad, SavedMidChargeTheLoadedGameResumesAtTheSamePendingDecision) {
    std::optional<GameSession> s;
    for (std::uint64_t seed = 1; !s; ++seed) {
        ASSERT_LT(seed, 100u);
        s = mid_charge(seed);
    }
    const auto path = temp_file("m13_mid_charge.json");
    s->save(path);
    GameSession loaded = GameSession::load(path);  // "quit, load": a fresh session from the file
    std::filesystem::remove(path);

    ASSERT_TRUE(loaded.pending().has_value());
    EXPECT_EQ(*loaded.pending(), *s->pending());
    EXPECT_TRUE(std::holds_alternative<decision::SelectChargeTargets>(loaded.pending()->kind));
    EXPECT_EQ(loaded.state(), s->state());
    EXPECT_EQ(loaded.history(), s->history());
    EXPECT_EQ(loaded.log().size(), s->log().size());
    EXPECT_EQ(loaded.legal(), s->legal());
    // Both continue identically.
    const GameAction next = loaded.legal().front();
    ASSERT_TRUE(loaded.submit(next).ok());
    ASSERT_TRUE(s->submit(next).ok());
    EXPECT_EQ(loaded.state(), s->state());
}

TEST(M13_SaveLoad, ATamperedSaveIsRejected) {
    GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario(), 3);
    ASSERT_TRUE(s.submit(s.legal().front()).ok());
    nlohmann::json j = s.to_save_json();
    j["state"]["units"][0]["models"][0]["wounds_remaining"] = 1;
    EXPECT_THROW((void)GameSession::from_save_json(j), DataError);
    j = s.to_save_json();
    j["steps"][0]["action"] = Json(GameAction{action::EndShootStep{}});
    EXPECT_THROW((void)GameSession::from_save_json(j), DataError);
}

TEST(M13_SaveLoad, ABareGameStateLoadsWithoutHistory) {
    GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario(), 3);
    const GameSession loaded = GameSession::from_save_json(Json(s.state()));
    EXPECT_EQ(loaded.state(), s.state());
    EXPECT_FALSE(loaded.can_undo());
}

TEST(M13_ReplayExport, AnExportedReplayPlaysBackInTheViewerIdentically) {
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario(), seed);
        int move_tries = 0;
        for (int step = 0; step < 400 && s.pending(); ++step) {
            if (std::holds_alternative<decision::MoveUnits>(s.pending()->kind)) move_tries = 0;
            ASSERT_TRUE(s.submit(choose(s, move_tries)).ok());
        }
        const auto path = temp_file("m13_export.json");
        save_replay(s.to_replay("M13 export test"), path);
        const Replay replay = load_replay(path);
        std::filesystem::remove(path);

        const Timeline timeline = Timeline::from_replay(replay, wh40k::test::repo_root(), "export");
        ASSERT_TRUE(timeline.error().empty()) << timeline.error();
        ASSERT_EQ(timeline.size(), s.log().size());
        for (std::size_t i = 0; i < timeline.size(); ++i) {
            EXPECT_EQ(timeline.frame(i).events, s.log()[i].events) << "frame " << i;
        }
        EXPECT_EQ(timeline.frame(timeline.size() - 1).state, s.state());
        EXPECT_EQ(replay_expectation(run_replay(replay, wh40k::test::repo_root()).final_state), replay.expect);
    }
}

TEST(M13_Undo, UndoRebuildsTheGameWithoutTheLastAction) {
    GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario(), 5);
    EXPECT_FALSE(s.can_undo());
    std::vector<GameState> states{s.state()};
    int move_tries = 0;
    for (int i = 0; i < 30 && s.pending(); ++i) {
        ASSERT_TRUE(s.submit(choose(s, move_tries)).ok());
        states.push_back(s.state());
    }
    for (std::size_t i = states.size() - 1; i > 0; --i) {
        ASSERT_TRUE(s.undo());
        EXPECT_EQ(s.state(), states[i - 1]) << "after undoing to step " << i - 1;  // dice included
        EXPECT_EQ(s.history().size(), i - 1);
    }
    EXPECT_FALSE(s.undo());
}

TEST(M13_Seed, TheSeedCanBeOverriddenAtGameStart) {
    const GameState scenario = wh40k::test::load_v1_scenario();
    const GameSession a = GameSession::new_game(scenario, 42);
    const GameSession b = GameSession::new_game(scenario, 42);
    EXPECT_EQ(a.seed(), 42u);
    EXPECT_EQ(a.state(), b.state());
    EXPECT_EQ(GameSession::new_game(scenario, std::nullopt).seed(), scenario.dice.seed);  // the scenario's own
}

// ==== review fixes ====

TEST(M13_SaveLoad, StructurallyInvalidSavesAreDataErrors) {
    // Missing fields are corrupt data (DataError), not raw JSON library exceptions.
    GameSession s = GameSession::new_game(wh40k::test::load_v1_scenario(), 3);
    for (const char* key : {"steps", "initial", "format", "state"}) {
        nlohmann::json j = s.to_save_json();
        j.erase(key);
        EXPECT_THROW((void)GameSession::from_save_json(j), DataError) << "without " << key;
    }
    EXPECT_THROW((void)GameSession::from_save_json(nlohmann::json::array()), DataError);
}

TEST(M12_EventText, MoveDistancesAreRoundedNotTruncated) {
    // 6" of float millimetres can be 5.9999995"; the log must still read 6.00".
    const event::ModelMoveProposed moved{UnitId{1}, ModelId{2}, {}, 152.39999f};
    EXPECT_EQ(format_event(GameEvent{moved}), "[03.01] model 2 moves 6.00\"");
}
