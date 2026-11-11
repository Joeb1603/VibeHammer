// M41: level Normal's shooting, defensive and fight decisions (D-054): profiles from the analysis, overkill
// avoidance, batch order, casualty order (coherency, front models), coherency removal, pile-ins and
// consolidations, and whole games (dice independence, clean arena games).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "auto_actions.hpp"
#include "bot.hpp"
#include "casualty_plan.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/rules/fight.hpp"
#include "core/rules/shooting.hpp"
#include "core/state/queries.hpp"
#include "fight_move_skip.hpp"
#include "normal/defense.hpp"
#include "normal/fight.hpp"
#include "normal/shooting.hpp"
#include "normal/value.hpp"
#include "session.hpp"
#include "state_hash.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::at;
using wh40k::test::in;

namespace {

BotConfig normal_level() { return BotConfig{}; }

constexpr std::size_t kMarines = 0;
constexpr std::size_t kGants = 1;

const WeaponProfile& profile_of(const GameState& gs, const ShotDeclaration& d) {
    const Model* m = find_model(gs, d.model);
    return m->weapons[d.weapon_index].profiles[d.profile_index];
}

// Translates `mover` along y until the units are `gap_mm` apart edge to edge (as in the M40 tests).
void set_gap(GameState& gs, UnitId mover, UnitId other, float gap_mm) {
    Unit& m = *gs.find_unit(mover);
    const Unit& o = *gs.find_unit(other);
    const float sign = m.models.front().position.y > o.models.front().position.y ? 1.0f : -1.0f;
    for (int i = 0; i < 50; ++i) {
        const float d = *unit_distance(m, o);
        if (std::fabs(d - gap_mm) < 1e-4f) return;
        for (Model& model : m.models) model.position.y += sign * (gap_mm - d);
    }
}

// Side A's Shoot step with the Termagants `gap_in` away; the Intercessors remained stationary unless `moved`
// (then they moved more than 3", 24.16).
GameState marine_shoot_state(float gap_in, bool moved) {
    GameState gs = wh40k::test::shoot_step_state(Side::A);
    set_gap(gs, gs.units[kGants].id, gs.units[kMarines].id, in(gap_in));
    gs.units[kMarines].turn_flags.moved_over_3in = moved;
    return gs;
}

// A Fleshborer-free fixture helper: the Termagants' casualty decision after one Intercessor's Focused Fire, with
// the rolls scripted so that exactly `hits` attacks inflict damage (each destroys a Termagant).
GameState after_marine_volley(GameState gs, std::size_t shooters, int damaging) {
    std::vector<ShotDeclaration> shots;
    for (std::size_t i = 0; i < shooters; ++i) {
        shots.push_back(ShotDeclaration{gs.units[kMarines].models[i].id, 0, 0, gs.units[kGants].id});  // Focused Fire
    }
    const int dice_count = static_cast<int>(shooters) * 2;  // [RAPID FIRE 1] within half range
    std::vector<int> rolls;
    for (int i = 0; i < dice_count; ++i) rolls.push_back(i < damaging ? 6 : 1);  // hits
    for (int i = 0; i < damaging; ++i) rolls.push_back(6);                        // wounds
    for (int i = 0; i < damaging; ++i) rolls.push_back(1);                        // saves fail
    ScriptedDice dice(rolls);
    (void)apply_action(gs, Side::A, action::Shoot{gs.units[kMarines].id, ShootingType::Normal, shots}, dice);
    EXPECT_TRUE(gs.pending && std::holds_alternative<decision::OrderCasualties>(gs.pending->kind));
    return gs;
}

std::set<ModelId> ids_of(const Unit& u) {
    std::set<ModelId> out;
    for (const Model& m : u.models) out.insert(m.id);
    return out;
}

std::set<ModelId> removed_by(GameState gs, const GameAction& order) {
    const std::set<ModelId> before = ids_of(gs.units[kGants]);
    (void)apply_action(gs, gs.pending->side, order);
    std::set<ModelId> out;
    const std::set<ModelId> after = ids_of(gs.units[kGants]);
    for (const ModelId id : before) {
        if (after.count(id) == 0) out.insert(id);
    }
    return out;
}

// Plays a seeded bot game through GameSession (as the arena does) and calls `visit` with the state and the
// deciding side before every bot decision, and the choice made.
template <typename Visit>
client::GameSession play(std::uint64_t seed, BotConfig a, BotConfig b, Visit visit) {
    client::GameSession s = client::GameSession::new_game(wh40k::test::load_v1_scenario(), seed);
    std::array<Bot, 2> bots{Bot(a), Bot(b)};
    client::AutoSettings autos;
    autos.bot_controlled = {true, true};
    while (s.pending() && s.history().size() < 5000) {
        (void)client::run_auto_actions(s, autos);
        if (!s.pending()) break;
        const Side side = s.pending()->side;
        const GameState before = s.state();
        const BotChoice c = bots[side_index(side)].decide(before, side);
        visit(before, side, c);
        if (c.stopped || !s.submit(c.action).ok()) break;
    }
    return s;
}

int engaged_models(const GameState& gs, const Unit& u) {
    int n = 0;
    for (const Model& m : u.models) n += enemy_units_engaged_with_model(gs, u, m, m.position).empty() ? 0 : 1;
    return n;
}

// Answers decisions with `bot` (applying each with the engine) until the pending decision isn't `side`'s
// MoveModels any more: a planned pile-in or consolidation, from its start to its commit.
void run_move(GameState& gs, Bot& bot, Side side) {
    for (int guard = 0; guard < 64 && gs.pending && gs.pending->side == side &&
                        std::holds_alternative<decision::MoveModels>(gs.pending->kind);
         ++guard) {
        const BotChoice c = bot.decide(gs, side);
        ASSERT_TRUE(validate_action(gs, side, c.action).ok()) << action_name(c.action);
        (void)apply_action(gs, side, c.action);
    }
}

}  // namespace

// ==== Shooting: the profile follows range and movement (24.16 [HEAVY], 24.30 [RAPID FIRE]) ====

TEST(M41_Shooting, ProfileChoiceFollowsRangeAndMovement) {
    struct Case {
        float gap_in;
        bool moved;
        const char* expected;
        double per_model;  // M40's numbers: model equivalents per Intercessor
    };
    const Case cases[] = {
        {14.0f, false, "Bolt Rifle - Saturation", 0.741},   // beyond half range: 2 dice at 3+ beat 1 at 2+
        {10.0f, false, "Bolt Rifle - Focused Fire", 1.157},  // stationary within 12": 2 dice at 2+, D2
        {10.0f, true, "Bolt Rifle - Saturation", 1.111},     // moved over 3": no [HEAVY], 3 dice at 3+ win
        {14.0f, true, "Bolt Rifle - Saturation", 0.741},
    };
    for (const Case& c : cases) {
        const GameState gs = marine_shoot_state(c.gap_in, c.moved);
        const BotChoice choice = Bot(normal_level()).decide(gs, Side::A);
        const auto* shoot = std::get_if<action::Shoot>(&choice.action);
        ASSERT_NE(shoot, nullptr) << c.gap_in << " " << choice.reason;
        EXPECT_EQ(shoot->shots.size(), 5u);
        for (const ShotDeclaration& d : shoot->shots) {
            EXPECT_EQ(profile_of(gs, d).name, c.expected) << c.gap_in << "\" moved " << c.moved;
        }
        EXPECT_NE(choice.reason.find(c.expected), std::string::npos) << choice.reason;
        // The value the choice was made on is the M40 analysis's.
        analysis::OddsCache cache;
        const auto outcomes = analysis::evaluate_shots(cache, gs, shoot->unit, shoot->type, shoot->shots);
        ASSERT_EQ(outcomes.size(), 1u);
        EXPECT_NEAR(outcomes[0].outcome.model_equivalents / 5.0, c.per_model, 0.01) << c.gap_in;
    }
}

TEST(M41_Shooting, OverkillMovesShotsToASecondTarget) {
    // Two Termagant units of two models each, within half range: all five Focused Fire volleys at one would
    // waste most of them (excess damage and attacks after a wipe are lost, 05.04), so the plan splits.
    GameState gs = marine_shoot_state(10.0f, false);
    gs.units[kGants].models.resize(2);
    gs.units[kGants].starting_strength = 2;
    const UnitId second = wh40k::test::add_unit_copy(gs, kGants);
    for (Model& m : gs.find_unit(second)->models) m.position.x += in(6.0f);
    analysis::OddsCache cache;
    const auto plan = normal::plan_shooting(cache, gs, gs.units[kMarines].id, ShootingType::Normal);
    ASSERT_TRUE(plan.has_value());
    EXPECT_TRUE(shot_problems(gs, gs.units[kMarines], plan->type, plan->shots).empty());
    int at_first = 0;
    int at_second = 0;
    for (const ShotDeclaration& d : plan->shots) (d.target == second ? at_second : at_first) += 1;
    EXPECT_GT(at_first, 0);
    EXPECT_GT(at_second, 0);
    EXPECT_LE(std::max(at_first, at_second), 3);
    // Every model valued alone picks the same unit; together that is worth less than the split.
    std::vector<ShotDeclaration> all_first;
    for (const Model& m : gs.units[kMarines].models) all_first.push_back(ShotDeclaration{m.id, 0, 0, gs.units[kGants].id});
    const auto lumped = analysis::evaluate_shots(cache, gs, gs.units[kMarines].id, ShootingType::Normal, all_first);
    const double lumped_value = normal::share_value(gs, lumped);
    EXPECT_GT(plan->value, lumped_value + 0.2);
    // The bot's answer at the Shoot decision is that plan.
    const BotChoice c = Bot(normal_level()).decide(gs, Side::A);
    ASSERT_TRUE(std::holds_alternative<action::Shoot>(c.action));
    EXPECT_EQ(std::get<action::Shoot>(c.action).shots, plan->shots);
}

// ==== 04.03: the batch order comes from the exact outcome (D2 before D1 against W2 models) ====

TEST(M41_AttackBatch, DamageTwoBeforeDamageOneAgainstTwoWoundModels) {
    const GameState gs = wh40k::test::load_v1_scenario();
    AttackContext moved{AttackType::Shooting, false};
    const Model& marine = gs.units[kMarines].models[0];
    const AttackProfile focused = make_attack_profile(marine.weapons[0].profiles[0], moved);
    const AttackProfile saturation = make_attack_profile(marine.weapons[0].profiles[1], moved);
    const analysis::Volley volley{{saturation, 9}, {focused, 6}};
    analysis::OddsCache cache;
    // Against Intercessors (W2): the D2 batch first, so the D1 batch finishes wounded models instead of
    // wounding models the D2 attacks would then overkill.
    const auto marines_order = normal::best_batch_order(cache, volley, analysis::snapshot_of(gs.units[kMarines]));
    EXPECT_EQ(marines_order, (std::vector<std::size_t>{1, 0}));
    const std::size_t d1_first[] = {0, 1};
    const std::size_t d2_first[] = {1, 0};
    const auto snap = analysis::snapshot_of(gs.units[kMarines]);
    EXPECT_GT(normal::order_value(cache, volley, d2_first, snap), normal::order_value(cache, volley, d1_first, snap));
    // Against Termagants (W1) the order makes no difference: the given order is kept.
    EXPECT_EQ(normal::best_batch_order(cache, volley, analysis::snapshot_of(gs.units[kGants])),
              (std::vector<std::size_t>{0, 1}));
}

TEST(M41_AttackBatch, TheBotSelectsTheFocusedFireBatchFirstAgainstIntercessors) {
    // Intercessors against Intercessors (side B a copy, facing them 12" away): two Bolt Rifles fire Focused Fire
    // (D2), three fire Saturation (D1). At SelectAttackBatch the bot picks the D2 batch.
    GameState gs = wh40k::test::shoot_step_state(Side::A);
    const UnitId foes = wh40k::test::add_unit_copy(gs, kMarines);
    Unit& foe = *gs.find_unit(foes);
    foe.side = Side::B;
    for (Model& m : foe.models) m.position.y = in(24.0f) - m.position.y;
    gs.units.erase(gs.units.begin() + static_cast<std::ptrdiff_t>(kGants));
    const Unit& marines = gs.units[kMarines];
    std::vector<ShotDeclaration> shots;
    for (std::size_t i = 0; i < marines.models.size(); ++i) {
        shots.push_back(ShotDeclaration{marines.models[i].id, 0, i < 2 ? 0u : 1u, foes});
    }
    ASSERT_TRUE(shot_problems(gs, marines, ShootingType::Normal, shots).empty());
    (void)apply_action(gs, Side::A, action::Shoot{marines.id, ShootingType::Normal, shots});
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::SelectAttackBatch>(gs.pending->kind));
    const BotChoice c = Bot(normal_level()).decide(gs, Side::A);
    const auto* pick = std::get_if<action::SelectAttackBatch>(&c.action);
    ASSERT_NE(pick, nullptr);
    ASSERT_LT(pick->batch, gs.flow.attack->batches.size());
    EXPECT_EQ(gs.flow.attack->batches[pick->batch].profile.damage.fixed_value(), 2) << c.reason;
}

// ==== Defense: casualty order keeps coherency, and the front goes first when a charge threatens ====

TEST(M41_Casualties, KeepsCoherencyWhereUnitOrderWouldLoseAModel) {
    // The closest Termagant is the only link of the one beside it to the rest of the unit (03.03: every model
    // within 2" of another). One casualty: front-first would isolate that model, and the End of Turn step would
    // remove it too. The bot removes the side model instead; unit order (the default before M47) removes the link.
    GameState gs = wh40k::test::shoot_step_state(Side::A);
    float front_y = 0.0f;
    for (const Model& m : gs.units[kMarines].models) front_y = std::max(front_y, m.position.y);
    const float y0 = core::mm_to_inches(front_y) + 10.0f;
    const std::vector<Position> layout{
        at(12.0f, y0),        // the link (closest to the Marines), model 0 in unit order
        at(14.0f, y0 + 0.3f),  // beside it: within 2" of the link only
        at(7.5f, y0 + 1.5f), at(9.0f, y0 + 1.5f), at(10.5f, y0 + 1.5f),
        at(7.5f, y0 + 3.0f), at(9.0f, y0 + 3.0f), at(10.5f, y0 + 3.0f),
        at(7.5f, y0 + 4.5f), at(9.0f, y0 + 4.5f),
    };
    Unit& gants = gs.units[kGants];
    ASSERT_EQ(gants.models.size(), layout.size());
    for (std::size_t i = 0; i < layout.size(); ++i) gants.models[i].position = layout[i];
    ASSERT_TRUE(unit_coherency(gants).ok());
    const ModelId link = gants.models[0].id;
    const ModelId side_model = gants.models[1].id;

    const GameState decision = after_marine_volley(gs, 1, 1);
    const BotChoice c = Bot(normal_level()).decide(decision, Side::B);
    ASSERT_TRUE(std::holds_alternative<action::OrderCasualties>(c.action)) << c.reason;
    EXPECT_NE(c.reason.find("keeping coherency"), std::string::npos) << c.reason;
    const std::set<ModelId> by_bot = removed_by(decision, c.action);
    EXPECT_EQ(by_bot, (std::set<ModelId>{side_model}));
    GameState after = decision;
    (void)apply_action(after, Side::B, c.action);
    EXPECT_TRUE(unit_coherency(after.units[kGants]).ok());
    // Unit order removes the link and leaves the side model isolated.
    const GameAction unit_order =
        action::OrderCasualties{decision.units[kGants].id, wh40k::test::model_ids(decision.units[kGants])};
    EXPECT_EQ(removed_by(decision, unit_order), (std::set<ModelId>{link}));
    GameState by_unit_order = decision;
    (void)apply_action(by_unit_order, Side::B, unit_order);
    EXPECT_FALSE(unit_coherency(by_unit_order.units[kGants]).ok());
    EXPECT_EQ(normal::coherency_losses(placed_models(by_unit_order.units[kGants])), 1);
    // M47 (D-058): the default order (CasualtyPlan's) keeps coherency here too.
    const GameAction by_plan = client::CasualtyPlan(decision, decision.units[kGants].id).action();
    EXPECT_EQ(removed_by(decision, by_plan), (std::set<ModelId>{side_model}));  // the straggler, at risk
    GameState by_default = decision;
    (void)apply_action(by_default, Side::B, by_plan);
    EXPECT_TRUE(unit_coherency(by_default.units[kGants]).ok());
    EXPECT_EQ(normal::coherency_losses(placed_models(by_default.units[kGants])), 0);
}

TEST(M41_Casualties, TheFrontModelsGoFirstWhenAnEnemyChargeThreatens) {
    // Two ranks of Termagants 10" from the Intercessors, the rear rank first in unit order (so the default would
    // remove it). Three casualties: the bot removes the three models closest to the Intercessors, which may
    // charge next and gain more from a combat than the Termagants (so their charge gets longer).
    GameState gs = wh40k::test::shoot_step_state(Side::A);
    std::stable_sort(gs.units[kGants].models.begin(), gs.units[kGants].models.end(),
                     [](const Model& a, const Model& b) { return a.position.y > b.position.y; });
    const GameState decision = after_marine_volley(gs, 2, 3);
    const Unit& gants = decision.units[kGants];
    const Unit& marines = decision.units[kMarines];
    const BotChoice c = Bot(normal_level()).decide(decision, Side::B);
    ASSERT_TRUE(std::holds_alternative<action::OrderCasualties>(c.action)) << c.reason;
    EXPECT_NE(c.reason.find("front models first"), std::string::npos) << c.reason;
    const std::set<ModelId> gone = removed_by(decision, c.action);
    ASSERT_EQ(gone.size(), 3u);
    float worst_removed = 0.0f;
    float best_kept = 1e9f;
    for (const Model& m : gants.models) {
        const float d = *model_distance_to_unit(m, m.position, marines);
        if (gone.count(m.id) > 0) {
            worst_removed = std::max(worst_removed, d);
        } else {
            best_kept = std::min(best_kept, d);
        }
    }
    EXPECT_LE(worst_removed, best_kept + EPS_MM);
    // Unit order (the default before M47) would have removed rear models instead, and so does the default order
    // (D-058, M47: furthest from the enemy first).
    GameState after = decision;
    (void)apply_action(after, Side::B, c.action);
    EXPECT_NE(removed_by(decision, action::OrderCasualties{gants.id, wh40k::test::model_ids(gants)}), gone);
    EXPECT_NE(removed_by(decision, client::CasualtyPlan(decision, gants.id).action()), gone);
    EXPECT_TRUE(unit_coherency(after.units[kGants]).ok());
}

TEST(M41_Casualties, AUnitThatWantsTheCombatKeepsItsFrontModels) {
    // The Intercessors gain more from a combat with the Termagants than they lose: their stance keeps the front.
    const GameState gs = wh40k::test::shoot_step_state(Side::A);
    analysis::OddsCache cache;
    EXPECT_EQ(normal::casualty_stance(cache, gs, gs.units[kMarines]).stance, normal::CasualtyStance::SeekContact);
    EXPECT_EQ(normal::casualty_stance(cache, gs, gs.units[kGants]).stance, normal::CasualtyStance::AvoidContact);
}

TEST(M41_Casualties, DesperateEscapeHazardsTakeTheEngagedModelFirst) {
    // 09.07 / 06.03: a battle-shocked Intercessor squad, its first model engaged with a Termagant, falls back with
    // Desperate Escape; two hazard rolls fail (2 mortal wounds, 06.02). Its owner picks the casualties: the engaged
    // model is the one that has to get away, so it takes both (W2: destroyed) and the unit is no longer engaged.
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
    const BotChoice c = Bot(normal_level()).decide(gs, Side::A);
    ASSERT_TRUE(std::holds_alternative<action::OrderCasualties>(c.action)) << c.reason;
    EXPECT_NE(c.reason.find("engaged models first (falling back)"), std::string::npos) << c.reason;
    const ModelId engaged = gs.units[kMarines].models[0].id;
    (void)apply_action(gs, Side::A, c.action);
    EXPECT_EQ(find_model(gs.units[kMarines], engaged), nullptr);
    EXPECT_FALSE(is_engaged(gs, gs.units[kMarines]));
}

TEST(M41_Coherency, RemovesTheModelWhoseRemovalRestoresCoherency) {
    GameState gs = wh40k::test::load_v1_scenario();
    Unit& gants = gs.units[kGants];
    // Model 3 wanders 3.5" (centre to centre) east of the unit: isolated (03.03). Removing it restores coherency.
    float east = 0.0f;
    for (const Model& m : gants.models) east = std::max(east, m.position.x);
    gants.models[3].position.x = east + in(3.5f);
    ASSERT_FALSE(unit_coherency(gants).ok());
    EXPECT_EQ(normal::coherency_losses(placed_models(gants)), 1);
    analysis::OddsCache cache;
    std::string why;
    const auto pick = normal::coherency_removal(cache, gs, gants.id, &why);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(*pick, gants.models[3].id) << why;
    // A pair at the east edge, more than 9" from most of the unit: removing one of the pair leaves the other
    // isolated, so it costs two; the bot still starts with the pair (removing the others never helps sooner).
    GameState pair = wh40k::test::load_v1_scenario();
    Unit& g2 = pair.units[kGants];
    const float y = core::mm_to_inches(g2.models[0].position.y);
    g2.models[8].position = at(22.0f, y);
    g2.models[9].position = at(23.2f, y);
    const auto p2 = normal::coherency_removal(cache, pair, g2.id, &why);
    ASSERT_TRUE(p2.has_value());
    EXPECT_TRUE(*p2 == g2.models[8].id || *p2 == g2.models[9].id) << why;
    EXPECT_NE(why.find("1 more to go"), std::string::npos) << why;
}

// ==== Fight: pile-ins, consolidations and melee declarations ====

TEST(M41_Fight, PilesInWhenItBringsMoreOfItsModelsIntoTheFight) {
    // After a successful charge three Intercessors are pulled 2" back (unengaged; the unit is still engaged and in
    // coherency). Piling in engages them, so the bot piles in; the planned move commits without a revert.
    GameState gs = wh40k::test::after_successful_charge();
    (void)apply_action(gs, Side::A, action::EndChargeStep{});
    ASSERT_TRUE(gs.pending && std::holds_alternative<decision::PileIn>(gs.pending->kind));
    for (std::size_t i = 2; i < gs.units[kMarines].models.size(); ++i) gs.units[kMarines].models[i].position.y -= in(2.0f);
    ASSERT_TRUE(unit_coherency(gs.units[kMarines]).ok());
    const int engaged_before = engaged_models(gs, gs.units[kMarines]);
    ASSERT_GT(engaged_before, 0);
    ASSERT_LT(engaged_before, 5);
    Bot bot(normal_level());
    const BotChoice c = bot.decide(gs, Side::A);
    ASSERT_TRUE(std::holds_alternative<action::StartPileIn>(c.action)) << c.reason;
    (void)apply_action(gs, Side::A, c.action);
    run_move(gs, bot, Side::A);
    EXPECT_GT(engaged_models(gs, gs.units[kMarines]), engaged_before);
    EXPECT_TRUE(gs.units[kMarines].phase_flags.piled_in);  // committed, not reverted
    EXPECT_EQ(bot.fallbacks(), 0u);
}

TEST(M41_Fight, APointlessConsolidationIsSkipped) {
    // After the fight every Intercessor is still engaged (as in D-047's fixture): an Ongoing Consolidation is
    // legal and some model could still move, but no more of either unit's models would fight, so the position
    // doesn't improve and the bot ends its consolidations.
    const GameState gs = wh40k::test::consolidate_step_state();
    ASSERT_EQ(gs.pending->side, Side::A);
    const std::vector<GameAction> legal = legal_actions(gs, Side::A);
    const auto start = std::find_if(legal.begin(), legal.end(), [](const GameAction& a) {
        return std::holds_alternative<action::StartConsolidation>(a);
    });
    ASSERT_NE(start, legal.end());
    analysis::OddsCache cache;
    const auto plan = normal::simulate_fight_move(gs, Side::A, *start, decision_seed(gs), "test");
    if (plan) {  // the move itself is possible: it is skipped on value
        EXPECT_LE(normal::consolidation_score(cache, plan->after, Side::A) - normal::consolidation_score(cache, gs, Side::A),
                  1e-9);
    }
    const BotChoice c = Bot(normal_level()).decide(gs, Side::A);
    EXPECT_TRUE(std::holds_alternative<action::EndConsolidations>(c.action)) << c.reason;
}

TEST(M41_Fight, ConsolidatesWhenTheMoveImprovesThePosition) {
    // The same Consolidate step with three Intercessors 2" back from the combat: consolidating brings them
    // back in (more attacks next fight, close-quarters shooting), so the bot consolidates and the move commits.
    GameState gs = wh40k::test::consolidate_step_state();
    for (std::size_t i = 2; i < gs.units[kMarines].models.size(); ++i) gs.units[kMarines].models[i].position.y -= in(2.0f);
    ASSERT_TRUE(unit_coherency(gs.units[kMarines]).ok());
    const int engaged_before = engaged_models(gs, gs.units[kMarines]);
    Bot bot(normal_level());
    const BotChoice c = bot.decide(gs, Side::A);
    ASSERT_TRUE(std::holds_alternative<action::StartConsolidation>(c.action)) << c.reason;
    (void)apply_action(gs, Side::A, c.action);
    run_move(gs, bot, Side::A);
    EXPECT_GT(engaged_models(gs, gs.units[kMarines]), engaged_before);
    EXPECT_TRUE(gs.units[kMarines].phase_flags.consolidated);
}

TEST(M41_Fight, MeleeDeclarationsCoverEveryEngagedModelAndValidate) {
    GameState gs = wh40k::test::after_successful_charge();
    (void)apply_action(gs, Side::A, action::EndChargeStep{});
    while (gs.flow.stage == Stage::PileIn) (void)apply_action(gs, gs.pending->side, action::EndPileIns{});
    const BotChoice sel = Bot(normal_level()).decide(gs, gs.pending->side);
    ASSERT_TRUE(std::holds_alternative<action::SelectToFight>(sel.action)) << sel.reason;
    EXPECT_EQ(std::get<action::SelectToFight>(sel.action).unit, gs.units[kMarines].id);  // Fights First
    (void)apply_action(gs, gs.pending->side, sel.action);
    ASSERT_TRUE(std::holds_alternative<decision::DeclareMeleeAttacks>(gs.pending->kind));
    const BotChoice fight = Bot(normal_level()).decide(gs, Side::A);
    const auto* f = std::get_if<action::Fight>(&fight.action);
    ASSERT_NE(f, nullptr);
    EXPECT_TRUE(melee_problems(gs, gs.units[kMarines], f->declarations).empty());
    std::set<ModelId> can_fight;
    for (const MeleeDeclaration& o : melee_options(gs, gs.units[kMarines])) can_fight.insert(o.model);
    std::set<ModelId> declared;
    for (const MeleeDeclaration& d : f->declarations) declared.insert(d.model);
    EXPECT_EQ(declared, can_fight);  // D-021: every model that can fight declares
}

// ==== Whole games: dice independence, zero rejections / reverts / fallbacks, and the arena ====

TEST(M41_NeverPeeksAtDice, NormalGivesTheSameAnswerWhateverTheDiceAndFromAnyStateOfItsPlan) {
    std::size_t checked = 0;
    std::set<std::size_t> kinds;
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        (void)play(seed, normal_level(), normal_level(), [&](const GameState& gs, Side side, const BotChoice& played) {
            GameState other = gs;
            other.dice.seed ^= 0x9E3779B97F4A7C15ull;
            other.dice.position += 37;
            const BotChoice a = Bot(normal_level()).decide(gs, side);
            const BotChoice b = Bot(normal_level()).decide(other, side);
            EXPECT_EQ(a.action, b.action) << "seed " << seed << ": " << action_name(a.action);
            EXPECT_EQ(a.action, played.action) << "seed " << seed << ": the memo differs from a fresh plan ("
                                               << played.reason << " / " << a.reason << ")";
            kinds.insert(gs.pending->kind.index());
            ++checked;
        });
    }
    EXPECT_GT(checked, 60u);
    EXPECT_GE(kinds.size(), 6u);
}

TEST(M41_Arena, NormalPlaysCleanGames) {
    // 20 seeds (the Release arena's 200 per seat are recorded in PLAN.md, M41): every game with no rejected action, no
    // reverted move and no fallback. D-085 (M85): the comparison with level Easy went with Easy.
    const GameState scenario = wh40k::test::load_v1_scenario();
    const ArenaResult both = run_arena(scenario, 20, 1, normal_level(), normal_level());
    for (const ArenaResult* r : {&both}) {
        EXPECT_TRUE(r->clean()) << format_arena(*r, scenario);
        EXPECT_EQ(r->fallbacks, 0u) << format_arena(*r, scenario);
        for (const GameRecord& g : r->games) {
            EXPECT_TRUE(g.finished) << "seed " << g.seed;
            EXPECT_EQ(g.rejected, 0u) << "seed " << g.seed;
            EXPECT_EQ(g.reverted, 0u) << "seed " << g.seed;
            EXPECT_EQ(g.fallbacks, 0u) << "seed " << g.seed;
        }
    }
}
