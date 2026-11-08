// M62 (D-069): the client and Random Characteristics. The "Attack dice" and "Damage rolls" cards, the log text of the new
// events, the casualty picks with a random D (marks at the maximum, provisional ones), the help citing D-069, and the
// Havocs' default plan now firing their lascannons.

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "board_interaction.hpp"
#include "casualty_picks.hpp"
#include "core/data/datasheet.hpp"
#include "core/engine/attack_flow.hpp"
#include "core/engine/engine.hpp"
#include "datasheet_view.hpp"
#include "dice_cards.hpp"
#include "event_text.hpp"
#include "launch_options.hpp"
#include "rules_help.hpp"
#include "session.hpp"
#include "shot_plan.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::client;
using wh40k::test::model_ids;
using wh40k::test::repo_root;
using wh40k::test::shoot_step_state;

namespace {

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

template <typename E>
const E* find_event(const std::vector<GameEvent>& events) {
    for (const GameEvent& e : events) {
        if (const auto* x = std::get_if<E>(&e)) return x;
    }
    return nullptr;
}

const DiceCard* find_card(const std::vector<DiceCard>& cards, const std::string& title) {
    for (const DiceCard& c : cards) {
        if (c.title == title) return &c;
    }
    return nullptr;
}

// Side A's Shoot step with the Bolt Rifles on Focused Fire a stand-in A D6+3 [BLAST] weapon (no Rapid Fire).
GameState blast_rifles() {
    GameState gs = shoot_step_state();
    for (Model& m : gs.units[kMarines].models) {
        WeaponProfile& p = m.weapons[0].profiles[0];
        p.attacks = DiceExpr{1, 6, 3};
        p.abilities = {ability::Blast{}};
    }
    return gs;
}

// Side B's ten fleshborers as stand-in D3 damage weapons at the Intercessors: three saves fail, Side A picks.
GameState three_failed_saves_d3(std::vector<GameEvent>* events = nullptr) {
    GameState gs = shoot_step_state(Side::B);
    for (Model& m : gs.units[kGants].models) m.weapons[0].profiles[0].damage = DiceExpr{1, 3, 0};
    std::vector<WeaponSelection> sels;
    for (std::size_t i = 0; i < gs.units[kGants].models.size(); ++i) {
        sels.push_back(wh40k::test::select(gs, kGants, i, 0, 0, gs.units[kMarines].id));
    }
    ScriptedDice dice{6, 6, 6, 1, 1, 1, 1, 1, 1, 1, 6, 6, 6, 1, 1, 1};
    const auto e = begin_attacks(gs, dice, gs.units[kGants].id, AttackContext{}, sels);
    if (events != nullptr) *events = e;
    EXPECT_EQ(*gs.pending, (PendingDecision{Side::A, decision::OrderCasualties{gs.units[kMarines].id}}));
    return gs;
}

}  // namespace

TEST(M62_DiceCards, TheAttackDiceCardShowsTheRollAndTheBlastDiceAddedAfter) {
    GameState gs = blast_rifles();
    const action::Shoot one{gs.units[kMarines].id, ShootingType::Normal,
                            {ShotDeclaration{gs.units[kMarines].models[0].id, 0, 0, gs.units[kGants].id}}};
    ScriptedDice dice{4, 1, 1, 1, 1, 1, 1, 1, 1, 1};  // A: 4 + 3 = 7, +2 [BLAST] = 9 hit rolls, all missing
    const std::vector<LogEntry> log{LogEntry{"Step 0", {}}, LogEntry{"Shoot", apply_action(gs, Side::A, one, dice)}};
    const auto cards = dice_cards(gs, log, 1);
    const DiceCard* card = find_card(cards, "Attack dice");
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->rule, "04.03");
    EXPECT_EQ(card->roller, Side::A);
    EXPECT_EQ(card->purpose, "Intercessor Squad shooting Termagants (Bolt Rifle - Focused Fire)");
    EXPECT_NE(card->need.find("D-069"), std::string::npos) << card->need;
    EXPECT_EQ(card->dice, (std::vector<DieFace>{DieFace{4, true, false, true}}));
    EXPECT_EQ(card->result, "Bolt Rifle - Focused Fire: D6+3 -> 4+3 = 7, +2 [BLAST] = 9");
    // The attack-dice card comes before the hit card, which counts the rolled dice.
    ASSERT_GE(cards.size(), 2u);
    EXPECT_EQ(cards[0].title, "Attack dice");
    EXPECT_EQ(cards[1].title, "Hit rolls");
    ASSERT_FALSE(cards[1].notes.empty());
    EXPECT_EQ(cards[1].notes[0].text, "9 attack dice from 1 model");
}

TEST(M62_DiceCards, SeveralModelsGetOneLineEachAndTheTotal) {
    GameState gs = blast_rifles();
    action::Shoot two{gs.units[kMarines].id, ShootingType::Normal, {}};
    for (std::size_t i = 0; i < 2; ++i) {
        two.shots.push_back(ShotDeclaration{gs.units[kMarines].models[i].id, 0, 0, gs.units[kGants].id});
    }
    std::vector<int> rolls{4, 2};
    rolls.resize(2 + 9 + 7, 1);
    ScriptedDice dice(rolls);
    const std::vector<LogEntry> log{LogEntry{"Shoot", apply_action(gs, Side::A, two, dice)}};
    const auto cards = dice_cards(gs, log, 0);
    const DiceCard* card = find_card(cards, "Attack dice");
    ASSERT_NE(card, nullptr);
    const std::string m0 = std::to_string(gs.units[kMarines].models[0].id.v);
    const std::string m1 = std::to_string(gs.units[kMarines].models[1].id.v);
    EXPECT_EQ(card->result, "Model " + m0 + ": D6+3 -> 4+3 = 7, +2 [BLAST] = 9; Model " + m1 +
                                ": D6+3 -> 2+3 = 5, +2 [BLAST] = 7. 16 attack dice in all");
}

TEST(M62_DiceCards, TheDamageCardShowsEachRollWithItsModel) {
    std::vector<GameEvent> shoot;
    GameState gs = three_failed_saves_d3(&shoot);
    const std::vector<ModelId> ids = model_ids(gs.units[kMarines]);
    const std::vector<ModelId> priority{ids[3], ids[1], ids[2], ids[4], ids[0]};
    ScriptedDice damage{1, 1, 6};  // D3: 1, 1, 3
    const std::vector<LogEntry> log{
        LogEntry{"Shoot", shoot},
        LogEntry{"Casualties", apply_action(gs, Side::A, action::OrderCasualties{gs.units[kMarines].id, priority}, damage)}};
    const auto cards = dice_cards(gs, log, 1);
    const DiceCard* card = find_card(cards, "Damage rolls");
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->rule, "05.04");
    EXPECT_EQ(card->roller, Side::B);  // the attacker's weapon's D
    EXPECT_NE(card->need.find("Random D (D3)"), std::string::npos) << card->need;
    EXPECT_EQ(card->dice, (std::vector<DieFace>{DieFace{1, true, false, true}, DieFace{1, true, false, true},
                                                DieFace{6, true, false, true}}));
    const std::string a = std::to_string(ids[3].v);
    const std::string b = std::to_string(ids[1].v);
    EXPECT_EQ(card->result, "1[1] = 1 -> model " + a + " (-1); 1[1] = 1 -> model " + a + " (destroyed); 3[6] = 3 -> model " +
                                b + " (destroyed, 1 lost)");
    // A fixed D has no damage card.
    GameState fixed = wh40k::test::shoot_step_state(Side::B);
    ScriptedDice dice{6, 6, 1, 1};
    std::vector<LogEntry> plain{LogEntry{
        "Shoot", begin_attacks(fixed, dice, fixed.units[kGants].id, AttackContext{},
                               {wh40k::test::select(fixed, kGants, 0, 0, 0, fixed.units[kMarines].id)})}};
    ScriptedDice none{};
    plain.push_back(LogEntry{"Casualties", apply_action(fixed, Side::A,
                                                        action::OrderCasualties{fixed.units[kMarines].id,
                                                                                model_ids(fixed.units[kMarines])},
                                                        none)});
    const auto fixed_cards = dice_cards(fixed, plain, 1);
    EXPECT_EQ(find_card(fixed_cards, "Damage rolls"), nullptr);
}

TEST(M62_ClientText, TheNewEventsAreLoggedAsRolls) {
    const event::AttackDiceRolled rolled{
        0, {AttackDiceRoll{0, ModelId{61}, DiceExpr{1, 6, 3}, CharacteristicRoll{{4}, 7}, 2}}, 9};
    EXPECT_EQ(format_event(rolled), "[04.03] Attack dice (batch 0, random A): model 61 D6+3 -> 4+3 = 7, +2 = 9 -> 9 attack dice");
    EXPECT_TRUE(is_roll_event(rolled));
    const event::AttackDiceRolled d3{0, {AttackDiceRoll{0, ModelId{6}, DiceExpr{1, 3, 0}, CharacteristicRoll{{5}, 3}, 1}}, 4};
    EXPECT_EQ(format_event(d3), "[04.03] Attack dice (batch 0, random A): model 6 D3 -> 3[5] = 3, +1 = 4 -> 4 attack dice");

    InflictDamageResult r;
    SaveStep step;
    step.group = 0;
    step.check = SaveCheck{1, 1, false, SaveOutcome::InflictsDamage};
    step.damage = DamageResult{ModelId{6}, 5, 5, false};
    step.damage_roll = CharacteristicRoll{{4}, 5};
    r.steps = {step};
    const event::SavesResolved saves{UnitId{2}, r};
    EXPECT_EQ(format_event(saves), "[05.04] 1 failed save(s), 0 destroyed; damage rolls: 5 (rolled 4) on model 6");
    EXPECT_TRUE(is_roll_event(saves));
    r.steps[0].damage_roll.reset();
    EXPECT_FALSE(is_roll_event(event::SavesResolved{UnitId{2}, r}));

    const event::AttacksLost lost{UnitId{2}, 1, 0, {RandomAttackDice{0, ModelId{6}, DiceExpr{1, 6, 3}, 1}}};
    EXPECT_EQ(format_event(lost), "[D-012] 1 + D6+3 attacks against unit 2 are lost (not rolled)");
    EXPECT_EQ(format_event(event::AttacksLost{UnitId{2}, 1, 4, {}}), "[D-012] 4 attacks against unit 2 are lost");
    EXPECT_EQ(attack_count_text(0, {RandomAttackDice{0, ModelId{6}, DiceExpr{1, 6, 3}, 0}}), "D6+3");
}

TEST(M62_CasualtyPicks, RandomDamageMarksAreMaximaAndLaterOnesProvisional) {
    const GameState gs = three_failed_saves_d3();
    const UnitId marines = gs.units[kMarines].id;
    const std::vector<ModelId> ids = model_ids(gs.units[kMarines]);
    BoardInteraction board(gs, legal_actions(gs, Side::A));
    ASSERT_NE(board.casualty_picks(), nullptr);
    const std::string hint = board.hint(gs);
    EXPECT_NE(hint.find("the next D3 damage (up to 3)"), std::string::npos) << hint;
    EXPECT_EQ(hint.find("provisional"), std::string::npos) << hint;  // the first pick takes the first roll
    EXPECT_EQ(board.casualty_picks()->preview()->damage_left, 9);   // "up to" in the action bar's box

    board.click_model(gs, marines, ids[3]);
    EXPECT_EQ(board.casualty_picks()->marks(), (std::vector<CasualtyMark>{{ids[3], 2, true, true, false}}));
    EXPECT_NE(board.hint(gs).find("Thin rings are provisional."), std::string::npos) << board.hint(gs);
    board.click_model(gs, marines, ids[1]);
    board.click_model(gs, marines, ids[2]);
    EXPECT_EQ(board.casualty_picks()->marks(),
              (std::vector<CasualtyMark>{{ids[3], 2, true, true, false}, {ids[1], 2, true, true, true}, {ids[2], 2, true, true, true}}));
    EXPECT_TRUE(board.casualty_picks()->done());
    EXPECT_NE(board.hint(gs).find("The D3 damage is rolled when you confirm"), std::string::npos) << board.hint(gs);
    // The answer is still one order (D-013): the picks first.
    const auto confirm = board.confirm();
    ASSERT_TRUE(confirm.has_value());
    const auto& order = std::get<action::OrderCasualties>(*confirm);
    EXPECT_EQ(std::vector<ModelId>(order.priority.begin(), order.priority.begin() + 3),
              (std::vector<ModelId>{ids[3], ids[1], ids[2]}));
}

TEST(M62_RulesHelp, RandomCharacteristicsHaveHelpCitingD069) {
    const Datasheet scream_sheet = load_datasheet(repo_root() / "data/units/screamer_killer.json");
    const Datasheet havocs = load_datasheet(repo_root() / "data/units/havocs.json");
    const WeaponProfile& scream = scream_sheet.find_weapon("Bio-plasmic scream")->profiles.at(0);
    const WeaponProfile& lascannon = havocs.find_weapon("Havoc lascannon")->profiles.at(0);
    const auto a = random_attacks_help(scream);
    ASSERT_TRUE(a);
    EXPECT_EQ(a->title, "Random A (D6+3)");
    EXPECT_EQ(a->rule, "D-069");
    EXPECT_TRUE(a->in_this_game);
    EXPECT_FALSE(random_damage_help(scream));
    const auto d = random_damage_help(lascannon);
    ASSERT_TRUE(d);
    EXPECT_EQ(d->title, "Random D (D6+1)");
    EXPECT_EQ(d->rule, "D-069");
    EXPECT_FALSE(random_attacks_help(lascannon));
    WeaponProfile melee = havocs.find_weapon("Astartes chainsword")->profiles.at(0);
    melee.attacks = DiceExpr{1, 3, 0};
    EXPECT_FALSE(random_attacks_help(melee)->in_this_game);  // refused for melee weapons (D-069)
    EXPECT_EQ(random_characteristic_lines(scream),
              (std::vector<EffectLine>{{"Attacks D6+3 per model, rolled when the attacks are resolved (D-069)", EffectKind::Info}}));
    EXPECT_EQ(random_characteristic_lines(lascannon),
              (std::vector<EffectLine>{{"Damage D6+1 per attack, rolled once its model is selected (D-069)", EffectKind::Info}}));
    // The datasheet window explains a random A or D on hover.
    const GameState rosters = wh40k::test::load_rosters_fixture();
    const DatasheetView view = datasheet_view(*rosters.find_unit(UnitId{5}));
    bool found = false;
    for (const DatasheetWeaponRow& r : view.ranged) {
        if (r.name != "Havoc lascannon") continue;
        found = true;
        EXPECT_FALSE(r.attacks_help);
        ASSERT_TRUE(r.damage_help);
        EXPECT_EQ(r.damage_help->rule, "D-069");
    }
    EXPECT_TRUE(found);
}

TEST(M62_DefaultPlans, TheHavocsFireTheirLascannonsAndTheChampionItsPlasmaGun) {
    // D-071's mixed-loadout default, now that D-069 lets the lascannons fire (Easy and --steps use it).
    GameState gs = load_scenario(repo_root() / wh40k::test::kHavocsVsTyrantFixture, repo_root());
    start_game(gs);
    apply_action(gs, Side::A, action::StartMove{gs.units[0].id, MoveType::RemainStationary, std::nullopt});
    const action::Shoot shoot = ShotPlan(gs, gs.units[0].id, ShootingType::Normal).expand();
    ASSERT_EQ(shoot.shots.size(), 5u);
    for (const ShotDeclaration& d : shoot.shots) {
        const Model& m = *find_model(gs, d.model);
        const WeaponProfile& p = m.weapons[d.weapon_index].profiles[d.profile_index];
        EXPECT_EQ(p.name, m.id == gs.units[0].models[0].id ? "Plasma gun - standard" : "Havoc lascannon");
        EXPECT_EQ(d.target, gs.units[1].id);
    }
    EXPECT_TRUE(validate_action(gs, Side::A, shoot).ok());
}

TEST(M62_LaunchOptions, PickModelsParsesAListOfModelIds) {
    // --pick-models (for screenshots of the casualty picks, D-069): the models picked on the board, as clicks would.
    EXPECT_EQ(parse_launch_args({"--pick-models", "12,9"}).pick_models, (std::vector<unsigned>{12, 9}));
    EXPECT_TRUE(parse_launch_args({"--steps", "1"}).pick_models.empty());
    for (const char* bad : {"", "a", "1,,2", "-1"}) {
        EXPECT_THROW((void)parse_launch_args({"--pick-models", bad}), std::invalid_argument) << bad;
    }
}
