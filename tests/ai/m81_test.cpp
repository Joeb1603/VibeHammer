// M81 (D-083 part 3): focus fire and joint charges (Normal). Shooting: a finishing bonus (a target's reply x the share of
// its models destroyed), a small bonus for the army plan's group targets, a split only when it beats concentrating, and a
// unit shooting at a focus target first. Charges: a partner already engaged with the target is credited (its fight, the
// target's fight back split), a plan partner that may still charge it at its charge odds, and the best-odds charger of a
// target goes first. Each checked against Generation::M77 (the pre-batch choice). Hand-built roster positions.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "bot.hpp"
#include "core/engine/actions.hpp"
#include "core/engine/engine.hpp"
#include "core/geometry/distance.hpp"
#include "core/state/queries.hpp"
#include "generation.hpp"
#include "normal/army_plan.hpp"
#include "normal/charge.hpp"
#include "normal/shooting.hpp"
#include "normal/value.hpp"
#include "support/monster_fixtures.hpp"
#include "test_helpers.hpp"

using namespace wh40k::core;
using namespace wh40k::ai;
using namespace wh40k::ai::normal;
using wh40k::ai::analysis::OddsCache;
using wh40k::test::in;
namespace mf = wh40k::test::monsters;

namespace {

constexpr UnitId kChainswords{1};
constexpr UnitId kLord{2};
constexpr UnitId kTerminators{3};
constexpr UnitId kHavocs{4};
constexpr UnitId kBoltguns{5};
constexpr UnitId kTermagants{6};
constexpr UnitId kGenestealers{7};
constexpr UnitId kScreamer{8};
constexpr UnitId kGuard{9};
constexpr UnitId kTyrant{10};
constexpr UnitId kTyrannofex{11};
constexpr UnitId kScreamer2{12};

GameState rosters_with(std::initializer_list<UnitId> ids) {
    const GameState all = wh40k::test::load_rosters_scenario();
    GameState gs = all;
    gs.units.clear();
    for (const Unit& u : all.units) {
        if (std::find(ids.begin(), ids.end(), u.id) != ids.end()) gs.units.push_back(u);
    }
    return gs;
}

Unit& unit(GameState& gs, UnitId id) {
    Unit* u = gs.find_unit(id);
    if (u == nullptr) throw std::logic_error("no such unit");
    return *u;
}

BotConfig normal_at(Generation g) {
    BotConfig c;
    c.level = BotLevel::Normal;
    c.generation = g;
    return c;
}

template <typename Kind>
void advance_to(GameState& gs, Side side) {
    for (int guard = 0; guard < 600 && gs.pending; ++guard) {
        if (gs.pending->side == side && std::holds_alternative<Kind>(gs.pending->kind)) return;
        const Side d = gs.pending->side;
        const DecisionKind& k = gs.pending->kind;
        const std::vector<GameAction> legal = legal_actions(gs, d);
        GameAction a = legal.front();
        if (std::holds_alternative<decision::MoveUnits>(k)) {
            for (const GameAction& l : legal) {
                const auto* m = std::get_if<action::StartMove>(&l);
                if (m != nullptr && m->move_type == MoveType::RemainStationary) {
                    a = l;
                    break;
                }
            }
        } else if (std::holds_alternative<decision::Shoot>(k)) {
            a = action::EndShootStep{};
        } else if (std::holds_alternative<decision::Charge>(k)) {
            a = action::EndChargeStep{};
        }
        if (auto* oc = std::get_if<action::OrderCasualties>(&a); oc != nullptr && oc->priority.empty()) {
            oc->priority = wh40k::test::model_ids(*gs.find_unit(oc->target));
        }
        apply_action(gs, d, a);
    }
    FAIL() << "the decision was not reached";
}

std::vector<UnitId> targets_of(const std::vector<ShotDeclaration>& shots) {
    std::vector<UnitId> out;
    for (const ShotDeclaration& d : shots) {
        if (std::find(out.begin(), out.end(), d.target) == out.end()) out.push_back(d.target);
    }
    return out;
}

}  // namespace

namespace {

float radius_in(const GameState& gs, UnitId id) { return mm_to_inches(base_radius(gs.find_unit(id)->models.front().base)); }

// The Screamer-killer at (22, 34); the chainsword Legionaries' rank engaged with it (1" south of it) when `engaged`,
// else `chain_gap` south; the boltgun Legionaries `bolt_gap` south-east (edge to edge, roughly).
GameState charge_fixture(bool engaged, float chain_gap, float bolt_gap, bool terminators = false, float term_gap = 0.0f) {
    GameState gs = terminators ? rosters_with({kChainswords, kTerminators, kBoltguns, kScreamer, kGuard})
                               : rosters_with({kChainswords, kBoltguns, kScreamer, kGuard});
    mf::place_rank(unit(gs, kScreamer), 22.0f, 34.0f);
    const float rs = radius_in(gs, kScreamer);
    const float rl = radius_in(gs, kChainswords);
    mf::place_rank(unit(gs, kChainswords), 22.0f, 34.0f - rs - rl - (engaged ? 1.0f : chain_gap));
    if (bolt_gap < 0.0f) {  // north of it instead, -bolt_gap from it
        mf::place_rank(unit(gs, kBoltguns), 22.0f, 34.0f + rs + rl - bolt_gap);
    } else {
        mf::place_rank(unit(gs, kBoltguns), 22.0f + 14.0f, 34.0f - rs - rl - bolt_gap);
    }
    if (terminators) {
        mf::place_rank(unit(gs, kTerminators), 22.0f, 34.0f + rs + radius_in(gs, kTerminators) + term_gap);
    }
    mf::place_rank(unit(gs, kGuard), 40.0f, 10.0f);
    gs.first_turn = Side::A;
    start_game(gs);
    advance_to<decision::Charge>(gs, Side::A);
    return gs;
}


// A started game with every unit standing, at Side A's first Shoot decision: the Havocs 16" south of a Screamer-killer
// with `screamer_wounds` wounds left and of a fresh Hive Tyrant, both able to reach our units in their next turn; both
// Legionaries units far back.
GameState finish_fixture(int screamer_wounds) {
    GameState gs = rosters_with({kChainswords, kHavocs, kBoltguns, kScreamer, kTyrant, kGuard});
    mf::place_rank(unit(gs, kHavocs), 22.0f, 10.0f);
    mf::place_rank(unit(gs, kChainswords), 6.0f, 4.0f);
    mf::place_rank(unit(gs, kBoltguns), 38.0f, 4.0f);
    mf::place_rank(unit(gs, kScreamer), 14.0f, 26.0f);
    mf::place_rank(unit(gs, kTyrant), 30.0f, 26.0f);
    mf::place_rank(unit(gs, kGuard), 22.0f, 55.0f);
    unit(gs, kScreamer).models.front().wounds_remaining = screamer_wounds;
    gs.first_turn = Side::A;
    start_game(gs);
    advance_to<decision::Shoot>(gs, Side::A);
    return gs;
}

// Side A's first Shoot decision: the chainsword Legionaries engaged with a Screamer-killer at the north edge and the boltgun
// Legionaries 9.4" from it (a group in the army plan); the Havocs at the south edge, 48" from the Screamer-killer (out of
// their lascannons' range) and 22" from the Tyrant Guard.
GameState focus_fixture() {
    GameState gs = rosters_with({kChainswords, kHavocs, kBoltguns, kScreamer, kGuard});
    const float rs = radius_in(gs, kScreamer);
    const float rl = radius_in(gs, kChainswords);
    mf::place_rank(unit(gs, kScreamer), 22.0f, 52.0f);
    mf::place_rank(unit(gs, kChainswords), 22.0f, 52.0f - rs - rl - 1.0f);
    mf::place_rank(unit(gs, kBoltguns), 36.0f, 52.0f - rs - rl - 6.0f);
    mf::place_rank(unit(gs, kHavocs), 22.0f, 1.5f);
    mf::place_rank(unit(gs, kGuard), 10.0f, 24.0f);
    gs.first_turn = Side::A;
    start_game(gs);
    advance_to<decision::Shoot>(gs, Side::A);
    return gs;
}

bool is_lascannon(const GameState& gs, const ShotDeclaration& d) {
    const Model* m = find_model(gs, d.model);
    return m != nullptr && m->weapons[d.weapon_index].profiles[d.profile_index].name.find("lascannon") != std::string::npos;
}

std::vector<UnitId> lascannon_targets(const GameState& gs, const std::vector<ShotDeclaration>& shots) {
    std::vector<UnitId> out;
    for (const ShotDeclaration& d : shots) {
        if (is_lascannon(gs, d) && std::find(out.begin(), out.end(), d.target) == out.end()) out.push_back(d.target);
    }
    return out;
}

const analysis::TargetOutcome* outcome_at(const ShootingChoice& c, UnitId target) {
    for (const analysis::TargetOutcome& o : c.outcomes) {
        if (o.target == target) return &o;
    }
    return nullptr;
}

}  // namespace

// ---- Focus fire ----

TEST(M81_Shooting, TheHavocsPutEveryLascannonOnOneMonster) {
    // The Havocs 14" from three monsters in a row: a Screamer-killer, the Hive Tyrant with 8 of its 12 wounds left and
    // another Screamer-killer. The pre-batch Normal splits the four lascannons over all three (as PLAN.md's traced game
    // did, seed 3, battle round 2), wounding three monsters and likely destroying none. Now a split must beat the
    // concentrated declaration by kSplitMargin, and each monster's reply removed by its destruction counts (the finishing
    // bonus): every lascannon, and the plasma gun, at the wounded Tyrant.
    GameState gs = rosters_with({kChainswords, kHavocs, kBoltguns, kScreamer, kTyrant, kScreamer2, kGuard});
    mf::place_rank(unit(gs, kHavocs), 22.0f, 6.0f);
    mf::place_rank(unit(gs, kChainswords), 6.0f, 3.0f);
    mf::place_rank(unit(gs, kBoltguns), 38.0f, 3.0f);
    mf::place_rank(unit(gs, kScreamer), 10.0f, 20.0f);
    mf::place_rank(unit(gs, kTyrant), 22.0f, 20.0f);
    mf::place_rank(unit(gs, kScreamer2), 34.0f, 20.0f);
    mf::place_rank(unit(gs, kGuard), 22.0f, 55.0f);
    unit(gs, kTyrant).models.front().wounds_remaining = 8;
    gs.first_turn = Side::A;
    start_game(gs);
    advance_to<decision::Shoot>(gs, Side::A);
    OddsCache cache;
    const ShootingAim off(cache, gs, Side::A, Generation::M77);
    const std::optional<ShootingChoice> before = plan_shooting(cache, gs, kHavocs, ShootingType::Normal, &off);
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(lascannon_targets(gs, before->shots).size(), 3u) << describe_shooting(gs, *before);
    const ShootingAim aim(cache, gs, Side::A, Generation::Current);
    const std::optional<ShootingChoice> now = plan_shooting(cache, gs, kHavocs, ShootingType::Normal, &aim);
    ASSERT_TRUE(now.has_value());
    EXPECT_EQ(lascannon_targets(gs, now->shots), std::vector<UnitId>{kTyrant}) << describe_shooting(gs, *now);
    EXPECT_EQ(std::count_if(now->shots.begin(), now->shots.end(), [&](const ShotDeclaration& d) { return is_lascannon(gs, d); }),
              4);
    EXPECT_EQ(targets_of(now->shots), std::vector<UnitId>{kTyrant});  // the plasma gun too
    EXPECT_GT(outcome_at(*now, kTyrant)->outcome.p_wipe(), outcome_at(*before, kTyrant)->outcome.p_wipe());
}

TEST(M81_Shooting, AUnitFinishesAWoundedTarget) {
    // The Havocs between a Screamer-killer with 1 wound left and a fresh Hive Tyrant, both 16" away. Its wounds alone, the
    // Screamer-killer is worth little (1 of its 10 wounds), so the pre-batch Normal puts everything on the Tyrant. Destroying
    // it takes its reply away (about half a unit of ours in each of the enemy's turns left, kFinishTurns of them at most),
    // so now the Havocs finish it: more likely than not destroyed. With all 10 wounds it isn't finished: the lascannons all
    // go on the Tyrant.
    OddsCache cache;
    {
        const GameState gs = finish_fixture(1);
        const ShootingAim before(cache, gs, Side::A, Generation::M77);
        EXPECT_FALSE(before.active());
        const std::optional<ShootingChoice> m77 = plan_shooting(cache, gs, kHavocs, ShootingType::Normal, &before);
        ASSERT_TRUE(m77.has_value());
        EXPECT_EQ(targets_of(m77->shots), std::vector<UnitId>{kTyrant}) << describe_shooting(gs, *m77);
        const std::optional<ShootingChoice> plain = plan_shooting(cache, gs, kHavocs, ShootingType::Normal);
        ASSERT_TRUE(plain.has_value());
        EXPECT_EQ(m77->shots, plain->shots) << "M77: as without an aim";
        EXPECT_DOUBLE_EQ(m77->value, plain->value);

        const ShootingAim aim(cache, gs, Side::A, Generation::Current);
        ASSERT_TRUE(aim.active());
        EXPECT_GT(aim.reply(kScreamer), 0.3);  // its scream and its charge, on the unit of ours it hurts most
        EXPECT_GT(aim.reply(kTyrant), 0.3);
        const std::optional<ShootingChoice> now = plan_shooting(cache, gs, kHavocs, ShootingType::Normal, &aim);
        ASSERT_TRUE(now.has_value());
        const analysis::TargetOutcome* sk = outcome_at(*now, kScreamer);
        ASSERT_NE(sk, nullptr) << describe_shooting(gs, *now);
        EXPECT_GT(sk->outcome.p_wipe(), 0.5) << describe_shooting(gs, *now);
        EXPECT_GT(now->value, m77->value);
        // choose_shooting's reason names it (the Havocs are the first to shoot).
        const std::optional<ShootingChoice> chosen = choose_shooting(cache, gs, legal_actions(gs, Side::A));
        ASSERT_TRUE(chosen.has_value());
        EXPECT_EQ(chosen->unit, kHavocs);
        EXPECT_NE(describe_shooting(gs, *chosen).find(", to finish Screamer-killer 1 ("), std::string::npos)
            << describe_shooting(gs, *chosen);
    }
    {
        const GameState gs = finish_fixture(10);
        const ShootingAim aim(cache, gs, Side::A, Generation::Current);
        const std::optional<ShootingChoice> now = plan_shooting(cache, gs, kHavocs, ShootingType::Normal, &aim);
        ASSERT_TRUE(now.has_value());
        EXPECT_EQ(lascannon_targets(gs, now->shots), std::vector<UnitId>{kTyrant}) << describe_shooting(gs, *now);
    }
}

TEST(M81_Shooting, ShootingAtTheFocusGoesFirst) {
    // Both Legionaries units are a group on the Screamer-killer (the chainsword unit engaged with it as the seed). The
    // Havocs' lascannons at the Tyrant Guard are the most valuable declaration, so the pre-batch Normal shoots them first;
    // now a unit shooting at the focus goes first (the boltgun Legionaries, worth a little more than the chainswords'
    // pistols), so the shots that follow see the focus's losses.
    const GameState gs = focus_fixture();
    const std::vector<GameAction> legal = legal_actions(gs, Side::A);
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::A);
    ASSERT_EQ(plan.groups.size(), 1u) << describe(gs, plan);
    EXPECT_EQ(plan.groups.front().target, kScreamer);
    const std::optional<ShootingChoice> before = choose_shooting(cache, gs, legal, Generation::M77);
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(before->unit, kHavocs);
    EXPECT_EQ(targets_of(before->shots), std::vector<UnitId>{kGuard});
    const ShootingAim aim(cache, gs, Side::A, Generation::Current);
    EXPECT_TRUE(aim.focus(kScreamer));
    EXPECT_FALSE(aim.focus(kGuard));
    const std::optional<ShootingChoice> havocs = plan_shooting(cache, gs, kHavocs, ShootingType::Normal, &aim);
    const std::optional<ShootingChoice> now = choose_shooting(cache, gs, legal, Generation::Current);
    ASSERT_TRUE(now.has_value());
    ASSERT_TRUE(havocs.has_value());
    EXPECT_EQ(now->unit, kBoltguns) << describe_shooting(gs, *now);
    EXPECT_EQ(targets_of(now->shots), std::vector<UnitId>{kScreamer});
    EXPECT_LT(now->value, havocs->value);  // not the most valuable: the focus first
    EXPECT_NE(describe_shooting(gs, *now).find(", on the focus Screamer-killer 1"), std::string::npos)
        << describe_shooting(gs, *now);
}

TEST(M81_Shooting, OneUnitASideShootsAsBefore) {
    // V1 (one unit a side): focus fire is off, so every value and choice is the pre-batch one.
    GameState gs = wh40k::test::load_v1_scenario_closer();
    start_game(gs);
    advance_to<decision::Shoot>(gs, gs.first_turn);
    OddsCache cache;
    const ShootingAim aim(cache, gs, gs.first_turn, Generation::Current);
    EXPECT_FALSE(aim.active());
    const std::vector<GameAction> legal = legal_actions(gs, gs.first_turn);
    const std::optional<ShootingChoice> now = choose_shooting(cache, gs, legal, Generation::Current);
    const std::optional<ShootingChoice> m77 = choose_shooting(cache, gs, legal, Generation::M77);
    ASSERT_TRUE(now.has_value());
    ASSERT_TRUE(m77.has_value());
    EXPECT_EQ(now->unit, m77->unit);
    EXPECT_EQ(now->shots, m77->shots);
    EXPECT_DOUBLE_EQ(now->value, m77->value);
    EXPECT_TRUE(now->notes.empty());
}

// ---- Joint charges ----

TEST(M81_Charge, ASecondUnitJoinsAFightAPartnerIsIn) {
    // The chainsword Legionaries are engaged with a Screamer-killer; the boltgun Legionaries are 7.5" from it. Alone the
    // boltguns lose the fight (a charge move -1.06 against holding +0.01): the pre-batch Normal ends the Charge step. Now
    // the chainswords' fight counts too, and the Screamer-killer's fight back is split between them: the boltguns declare.
    const GameState gs = charge_fixture(true, 0.0f, 3.0f);
    ASSERT_TRUE(is_engaged(gs, *gs.find_unit(kChainswords)));
    const std::vector<GameAction> legal = legal_actions(gs, Side::A);
    OddsCache cache;
    const std::optional<PolicyResult> before = decide_charge(cache, gs, Side::A, legal, decision_seed(gs), Generation::M77);
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(before->action, (GameAction{action::EndChargeStep{}})) << before->reason;
    const std::optional<ChargeProspect> alone =
        charge_prospect(cache, gs, kBoltguns, decision_seed(gs), 0.0, std::nullopt, Generation::M77);
    ASSERT_TRUE(alone.has_value());
    EXPECT_LT(alone->success_mean, alone->hold);

    const std::optional<PolicyResult> now = decide_charge(cache, gs, Side::A, legal, decision_seed(gs), Generation::Current);
    ASSERT_TRUE(now.has_value());
    EXPECT_EQ(now->action, (GameAction{action::DeclareCharge{kBoltguns}})) << now->reason;
    EXPECT_NE(now->reason.find("; joins Legionaries 1 against Screamer-killer 1"), std::string::npos) << now->reason;
    const std::optional<ChargeProspect> joint = charge_prospect(cache, gs, kBoltguns, decision_seed(gs));
    ASSERT_TRUE(joint.has_value());
    EXPECT_GT(joint->success_mean, joint->hold);
    ASSERT_EQ(joint->partners.size(), 1u);
    EXPECT_EQ(joint->partners.front(), (ChargePartner{kChainswords, 1.0, true, true}));
    const std::optional<ChargeCredit> credit = charge_credit(gs, kBoltguns, kScreamer, nullptr, Generation::Current);
    ASSERT_TRUE(credit.has_value());
    EXPECT_FALSE(charge_credit(gs, kBoltguns, kScreamer, nullptr, Generation::M77).has_value());

    // Alone (the chainswords 20" back, not engaged) the boltguns still don't charge.
    const GameState far = charge_fixture(false, 20.0f, 3.0f);
    const std::optional<ChargeProspect> lone =
        charge_prospect(cache, far, kBoltguns, decision_seed(far), 0.0, std::nullopt, Generation::Current);
    ASSERT_TRUE(lone.has_value());
    EXPECT_DOUBLE_EQ(lone->gain, 0.0);
    EXPECT_TRUE(lone->partners.empty());
    const std::optional<PolicyResult> lone_decision =
        decide_charge(cache, far, Side::A, legal_actions(far, Side::A), decision_seed(far), Generation::Current);
    ASSERT_TRUE(lone_decision.has_value());
    EXPECT_NE(lone_decision->action, (GameAction{action::DeclareCharge{kBoltguns}})) << lone_decision->reason;
}

TEST(M81_Charge, TheBestOddsChargerGoesFirst) {
    // Both Legionaries units within charge range of a Screamer-killer, a group in the army plan: the chainswords 2" south
    // of it (97% to make it in), the boltguns 3" north (92%). Each is credited with the other following at its odds, and
    // the boltguns (worse alone) gain the more; but the chainswords have the better odds, so they declare first, and the
    // boltguns can follow them in (or hold if they fail). The pre-batch Normal declares the chainswords too, alone: the
    // boltguns would lose alone.
    const GameState gs = charge_fixture(false, 2.0f, -3.0f);
    const std::vector<GameAction> legal = legal_actions(gs, Side::A);
    OddsCache cache;
    const ArmyPlan plan = army_plan(cache, gs, Side::A);
    ASSERT_NE(plan.group_of(kChainswords), nullptr) << describe(gs, plan);
    EXPECT_EQ(plan.group_of(kChainswords), plan.group_of(kBoltguns));
    const std::optional<ChargeProspect> chains =
        charge_prospect(cache, gs, kChainswords, decision_seed(gs), 0.0, std::nullopt, Generation::Current, &plan);
    const std::optional<ChargeProspect> bolts =
        charge_prospect(cache, gs, kBoltguns, decision_seed(gs), 0.0, std::nullopt, Generation::Current, &plan);
    ASSERT_TRUE(chains.has_value());
    ASSERT_TRUE(bolts.has_value());
    EXPECT_GT(chains->p_success, bolts->p_success);
    EXPECT_GT(bolts->gain, chains->gain);
    EXPECT_GT(chains->gain, 0.0);
    ASSERT_EQ(chains->partners.size(), 1u);
    EXPECT_EQ(chains->partners.front().unit, kBoltguns);
    EXPECT_FALSE(chains->partners.front().engaged);
    EXPECT_NEAR(chains->partners.front().weight, 0.92, 0.01);
    const std::optional<PolicyResult> now = decide_charge(cache, gs, Side::A, legal, decision_seed(gs), Generation::Current);
    ASSERT_TRUE(now.has_value());
    EXPECT_EQ(now->action, (GameAction{action::DeclareCharge{kChainswords}})) << now->reason;
    EXPECT_NE(now->reason.find("Legionaries 2 may follow (92% charge odds); the best odds of the units charging "
                               "Screamer-killer 1, ahead of Legionaries 2"),
              std::string::npos)
        << now->reason;
    const std::optional<PolicyResult> before = decide_charge(cache, gs, Side::A, legal, decision_seed(gs), Generation::M77);
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(before->action, (GameAction{action::DeclareCharge{kChainswords}})) << before->reason;
    EXPECT_EQ(before->reason.find("may follow"), std::string::npos) << before->reason;
    const std::optional<ChargeProspect> bolts_before =
        charge_prospect(cache, gs, kBoltguns, decision_seed(gs), 0.0, std::nullopt, Generation::M77);
    ASSERT_TRUE(bolts_before.has_value());
    EXPECT_DOUBLE_EQ(bolts_before->gain, 0.0);
    EXPECT_TRUE(bolts_before->partners.empty());
}

