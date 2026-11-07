// M56: Easy and Normal across several units (D-066). 01.04 (closest unit, D-010), 09.02, 10.02, 11.02, 11.04,
// 12.02-12.08. Easy aims at the closest enemy unit and walks its full Move while it is far away; Normal moves its
// units closest first, weighs every enemy unit's shooting and charge threat in its moves, values a charge with
// several targets from one shared strength, and measures casualty worth against the attacker. Seeded games on the
// 44"x60" scenario are clean, and the M55 regression switch plays exactly as M55 did.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <variant>
#include <vector>

#include "analysis/odds.hpp"
#include "arena.hpp"
#include "auto_actions.hpp"
#include "bot.hpp"
#include "core/data/scenario_loader.hpp"
#include "core/dice/dice.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/movement.hpp"
#include "core/geometry/distance.hpp"
#include "core/geometry/engagement.hpp"
#include "core/state/queries.hpp"
#include "normal/charge.hpp"
#include "normal/defense.hpp"
#include "normal/move_score.hpp"
#include "normal/movement.hpp"
#include "normal/value.hpp"
#include "session.hpp"
#include "support/charge_fixtures.hpp"
#include "support/engine_helpers.hpp"
#include "test_helpers.hpp"

using namespace wh40k;
using namespace wh40k::core;
using namespace wh40k::ai;
using wh40k::test::at;
using wh40k::test::in;

namespace {

// Units in the strike scenario's order (D-064): Intercessor Squads 1-3, then Termagants 1-4.
constexpr std::size_t kSquad1 = 0;
constexpr std::size_t kSquad2 = 1;
constexpr std::size_t kSquad3 = 2;
constexpr std::size_t kGants1 = 3;
constexpr std::size_t kGants2 = 4;
constexpr std::size_t kGants3 = 5;
constexpr std::size_t kGants4 = 6;

BotConfig level(BotLevel l, Generation g = Generation::Current) {
    BotConfig c;
    c.level = l;
    c.generation = g;
    return c;
}

GameState strike_scenario() {
    const auto root = wh40k::test::repo_root();
    return load_scenario(root / "data/scenarios/strike_44x60.json", root);
}

// The strike scenario played with the first legal action each time until `done` holds.
template <typename Pred>
GameState strike_until(Pred done) {
    GameState gs = strike_scenario();
    start_game(gs);
    for (int guard = 0; guard < 400 && !done(gs); ++guard) {
        const Side side = gs.pending.value().side;
        apply_action(gs, side, legal_actions(gs, side).front());
    }
    EXPECT_TRUE(done(gs)) << "fixture not reached";
    return gs;
}

GameState side_a_move_units() {
    return strike_until([](const GameState& gs) {
        return gs.pending && gs.pending->side == Side::A && std::holds_alternative<decision::MoveUnits>(gs.pending->kind);
    });
}

void shift(Unit& u, float dx_in, float dy_in) {
    for (Model& m : u.models) {
        m.position.x += in(dx_in);
        m.position.y += in(dy_in);
    }
}

Position centroid(const Unit& u) {
    Position c;
    for (const Model& m : u.models) {
        c.x += m.position.x;
        c.y += m.position.y;
    }
    const float n = static_cast<float>(u.models.size());
    return Position{c.x / n, c.y / n, 0.0f};
}

// The unit's centroid's displacement as a unit vector's dot product with the direction from `from` to `to`.
float dot_towards(const Position& from, const Position& moved_to, const Position& to) {
    const float mx = moved_to.x - from.x;
    const float my = moved_to.y - from.y;
    const float ml = std::hypot(mx, my);
    const float tx = to.x - from.x;
    const float ty = to.y - from.y;
    const float tl = std::hypot(tx, ty);
    return ml > 0.0f && tl > 0.0f ? (mx * tx + my * ty) / (ml * tl) : 0.0f;
}

// Answers `side`'s decisions with `bot` (the engine applies them, the state's own dice) while `going` holds.
template <typename Pred>
std::vector<std::string> play_while(GameState& gs, Bot& bot, Side side, Pred going) {
    std::vector<std::string> reasons;
    for (int guard = 0; guard < 120 && gs.pending && gs.pending->side == side && going(gs); ++guard) {
        const BotChoice c = bot.decide(gs, side);
        EXPECT_FALSE(c.stopped) << c.reason;
        reasons.push_back(c.reason);
        EXPECT_TRUE(validate_action(gs, side, c.action).ok()) << c.reason;
        for (const GameEvent& e : apply_action(gs, side, c.action)) {
            EXPECT_FALSE(std::holds_alternative<event::UnitMoveReverted>(e)) << c.reason;
        }
    }
    return reasons;
}

// Side A's first Normal move with `bot` for the unit the bot picks: the unit's centroid before and after.
struct MoveResult {
    UnitId unit;
    Position before;
    Position after;
    std::string reason;
};

MoveResult first_move(GameState gs, BotConfig config) {
    Bot bot(config);
    const BotChoice c = bot.decide(gs, Side::A);
    const auto* start = std::get_if<action::StartMove>(&c.action);
    EXPECT_NE(start, nullptr) << c.reason;
    MoveResult out;
    if (start == nullptr) return out;
    out.unit = start->unit;
    out.before = centroid(*gs.find_unit(start->unit));
    out.reason = c.reason;
    (void)apply_action(gs, Side::A, c.action);
    (void)play_while(gs, bot, Side::A, [](const GameState& s) {
        return std::holds_alternative<decision::MoveModels>(s.pending->kind);
    });
    out.after = centroid(*gs.find_unit(start->unit));
    return out;
}

// Side A's Charge decision in the strike scenario with only Intercessor Squad 1 and Termagants 1 and 2 left on the
// battlefield: the Squad's line centred at (30", 24"); Termagants 2 straight north `near_in` away; Termagants 1
// further west, about 10.5" away (both within 12", 11.02).
GameState two_target_charge_step(float near_in) {
    GameState gs = strike_until([](const GameState& s) {
        return s.pending && s.pending->side == Side::A && std::holds_alternative<decision::Charge>(s.pending->kind);
    });
    for (const std::size_t i : {kSquad2, kSquad3, kGants3, kGants4}) gs.units[i].models.clear();
    Unit& us = gs.units[kSquad1];
    const Position c = centroid(us);
    shift(us, 30.0f - core::mm_to_inches(c.x), 24.0f - core::mm_to_inches(c.y));
    Unit& near = gs.units[kGants2];
    Unit& far = gs.units[kGants1];
    // Termagants 2: front rank (its southern models) `near_in` north of the Squad's line, centred on x = 30".
    {
        float min_y = 1e9f;
        Position cc = centroid(near);
        for (const Model& m : near.models) min_y = std::min(min_y, m.position.y);
        const float front_gap = (min_y - near.models.front().base.diameter_mm * 0.5f) -
                                (in(24.0f) + us.models.front().base.diameter_mm * 0.5f);
        shift(near, 30.0f - core::mm_to_inches(cc.x), near_in - core::mm_to_inches(front_gap));
    }
    {
        const Position cn = centroid(near);
        const Position cf = centroid(far);
        shift(far, 15.0f - core::mm_to_inches(cf.x), core::mm_to_inches(cn.y - cf.y));
    }
    for (Unit& u : gs.units) {
        u.turn_flags = {};
        u.phase_flags = {};
    }
    gs.pending = compute_decision(gs);
    return gs;
}

}  // namespace

// ---- Easy ----

TEST(M56_Easy, AimsAtTheClosestEnemyUnitNotTheFirst) {
    // D-010 / D-066: Termagants 2 is brought within 15" of Intercessor Squad 1 while Termagants 1 (the first enemy
    // unit by id) stays 20" away. Easy's 2" step goes at Termagants 2; the M55 Easy walked at Termagants 1.
    GameState gs = side_a_move_units();
    shift(gs.units[kGants2], 0.0f, -6.0f);
    const float to_first = *unit_distance(gs.units[kSquad1], gs.units[kGants1]);
    const float to_closest = *unit_distance(gs.units[kSquad1], gs.units[kGants2]);
    ASSERT_LT(to_closest, to_first);
    ASSERT_LT(to_closest, in(18.0f));  // within 12" + M: the old 2" step
    ASSERT_GT(to_closest, in(8.0f));
    const MoveResult now = first_move(gs, level(BotLevel::Easy));
    EXPECT_EQ(now.unit, gs.units[kSquad1].id);
    EXPECT_NE(now.reason.find("Termagants 2"), std::string::npos) << now.reason;
    EXPECT_NEAR(centre_distance(now.before, now.after), in(2.0f), 0.05f);
    EXPECT_GT(dot_towards(now.before, now.after, centroid(gs.units[kGants2])), 0.999f);
    const MoveResult m55 = first_move(gs, level(BotLevel::Easy, Generation::M55));
    EXPECT_NE(m55.reason.find("Termagants 1"), std::string::npos) << m55.reason;
    EXPECT_GT(dot_towards(m55.before, m55.after, centroid(gs.units[kGants1])), 0.999f);
}

TEST(M56_Easy, WalksItsFullMoveWhileTheClosestEnemyIsFarAway) {
    // D-066: at the start of the strike scenario the closest Termagants are 20" away (more than 12" + its 6" Move):
    // Easy walks the full 6" at them; M55 Easy stepped 2".
    const GameState gs = side_a_move_units();
    ASSERT_GT(*unit_distance(gs.units[kSquad1], gs.units[kGants1]), in(18.0f));
    const MoveResult now = first_move(gs, level(BotLevel::Easy));
    EXPECT_NEAR(centre_distance(now.before, now.after), in(6.0f), 0.05f) << now.reason;
    EXPECT_NE(now.reason.find("Normal move 6\""), std::string::npos) << now.reason;
    EXPECT_GT(dot_towards(now.before, now.after, centroid(gs.units[kGants1])), 0.999f);
    const MoveResult m55 = first_move(gs, level(BotLevel::Easy, Generation::M55));
    EXPECT_NEAR(centre_distance(m55.before, m55.after), in(2.0f), 0.05f) << m55.reason;
}

TEST(M56_Easy, AFreshBotReplansTheFullMoveTheSame) {
    // D-054: from every state along Easy's full Move, a fresh bot (no memo) gives the played answer.
    GameState gs = side_a_move_units();
    Bot bot(level(BotLevel::Easy));
    for (int i = 0; i < 12 && gs.pending && gs.pending->side == Side::A; ++i) {
        const BotChoice c = bot.decide(gs, Side::A);
        const BotChoice fresh = Bot(level(BotLevel::Easy)).decide(gs, Side::A);
        EXPECT_EQ(c.action, fresh.action) << c.reason << " / " << fresh.reason;
        (void)apply_action(gs, Side::A, c.action);
        if (std::holds_alternative<action::CommitUnitMove>(c.action)) break;
    }
}

TEST(M56_Easy, TheChargeMoveGoesAtItsTarget) {
    // 11.04 / D-066: a roll of 6 makes only Termagants 2 (5" north) a charge target; Termagants 1 (the first enemy
    // unit by id, about 10" west) is not one. Easy's charge move goes at Termagants 2 and ends engaged with it only.
    // The M55 Easy translated the unit towards Termagants 1 and found no move that stands (SkipChargeMove).
    GameState gs = two_target_charge_step(5.0f);
    const UnitId us = gs.units[kSquad1].id;
    const UnitId target = gs.units[kGants2].id;
    const UnitId other = gs.units[kGants1].id;
    ASSERT_GT(*unit_distance(gs.units[kSquad1], gs.units[kGants1]), in(6.0f));
    ASSERT_LT(*unit_distance(gs.units[kSquad1], gs.units[kGants1]), in(12.0f));
    ScriptedDice dice{3, 3};
    (void)apply_action(gs, Side::A, action::DeclareCharge{us}, dice);
    ASSERT_TRUE(std::holds_alternative<decision::SelectChargeTargets>(gs.pending->kind));
    {
        const BotChoice m55 = Bot(level(BotLevel::Easy, Generation::M55)).decide(gs, Side::A);
        EXPECT_TRUE(std::holds_alternative<action::SkipChargeMove>(m55.action)) << m55.reason;
    }
    Bot bot(level(BotLevel::Easy));
    const BotChoice c = bot.decide(gs, Side::A);
    ASSERT_EQ(c.action, (GameAction{action::StartChargeMove{us, {target}}})) << c.reason;
    (void)apply_action(gs, Side::A, c.action);
    (void)play_while(gs, bot, Side::A,
                     [](const GameState& s) { return std::holds_alternative<decision::MoveModels>(s.pending->kind); });
    const std::vector<UnitId> engaged = engaged_enemy_units(gs, *gs.find_unit(us));
    EXPECT_EQ(engaged, std::vector<UnitId>{target});
    EXPECT_EQ(std::count(engaged.begin(), engaged.end(), other), 0);
}

// ---- Normal: activation order ----

TEST(M56_Normal, TheUnitClosestToTheEnemyMovesFirst) {
    // D-066: Intercessor Squad 3 stands 4" ahead of the others; Normal moves it first (M55: Squad 1, id order).
    GameState gs = side_a_move_units();
    shift(gs.units[kSquad3], 0.0f, 4.0f);
    gs.pending = compute_decision(gs);
    const std::vector<GameAction> legal = legal_actions(gs, Side::A);
    EXPECT_EQ(normal::unit_to_move(gs, legal), gs.units[kSquad3].id);
    EXPECT_EQ(normal::unit_to_move(gs, legal, Generation::M55), gs.units[kSquad1].id);
    const BotChoice c = Bot(level(BotLevel::Normal)).decide(gs, Side::A);
    const auto* start = std::get_if<action::StartMove>(&c.action);
    ASSERT_NE(start, nullptr) << c.reason;
    EXPECT_EQ(start->unit, gs.units[kSquad3].id) << c.reason;
    const BotChoice m55 = Bot(level(BotLevel::Normal, Generation::M55)).decide(gs, Side::A);
    ASSERT_TRUE(std::holds_alternative<action::StartMove>(m55.action));
    EXPECT_EQ(std::get<action::StartMove>(m55.action).unit, gs.units[kSquad1].id);
    // With the squads level again, the tie keeps unit order.
    const GameState level_start = side_a_move_units();
    const float g1 = *unit_distance(level_start.units[kSquad1], level_start.units[kGants1]);
    const float g2 = *unit_distance(level_start.units[kSquad2], level_start.units[kGants2]);
    if (std::fabs(g1 - g2) < 1e-3f) {
        EXPECT_EQ(normal::unit_to_move(level_start, legal_actions(level_start, Side::A)),
                  level_start.units[kSquad1].id);
    }
}

// ---- Normal: the charge planner ----

TEST(M56_Charge, TwoTargetsShareTheChargersStrength) {
    // D-066: Intercessor Squad 1 charged into two Termagant units (models of both in base contact). M55 valued each
    // target from the Squad's full strength and summed them: exactly the two single-target values added up. Now the
    // Squad's models are split between the targets and its losses add up, so the two-target value is less than
    // that sum, and no more than the sum of the two single-target values (which each use the full strength).
    GameState gs = two_target_charge_step(1.0f);
    Unit& us = gs.units[kSquad1];
    Unit& a = gs.units[kGants1];
    Unit& b = gs.units[kGants2];
    // Termagants 1 against the Squad's western models, Termagants 2 against its eastern ones, in base contact:
    // each in two rows north of the Squad, Termagants 1 over its west end, Termagants 2 over its east end.
    std::sort(us.models.begin(), us.models.end(),
              [](const Model& x, const Model& y) { return x.position.x < y.position.x; });
    const float r_us = us.models.front().base.diameter_mm * 0.5f;
    const float r_g = a.models.front().base.diameter_mm * 0.5f;
    for (std::size_t i = 0; i < a.models.size(); ++i) {
        a.models[i].position = Position{us.models[0].position.x + 2.0f * r_g * static_cast<float>(i % 5) - in(3.0f),
                                        us.models[0].position.y + r_us + r_g + 0.05f +
                                            2.0f * r_g * static_cast<float>(i / 5),
                                        0.0f};
        b.models[i].position = Position{us.models[4].position.x + 2.0f * r_g * static_cast<float>(i % 5) + in(0.5f),
                                        us.models[4].position.y + r_us + r_g + 0.05f +
                                            2.0f * r_g * static_cast<float>(i / 5),
                                        0.0f};
    }
    ASSERT_TRUE(is_engaged(gs, us));
    const std::vector<UnitId> engaged = engaged_enemy_units(gs, us);
    ASSERT_EQ(engaged.size(), 2u);
    analysis::OddsCache cache;
    const std::vector<UnitId> both{a.id, b.id};
    const std::vector<UnitId> only_a{a.id};
    const std::vector<UnitId> only_b{b.id};
    const double old_both = normal::charged_value(cache, gs, us.id, both, 2, Generation::M55);
    const double old_a = normal::charged_value(cache, gs, us.id, only_a, 2, Generation::M55);
    const double old_b = normal::charged_value(cache, gs, us.id, only_b, 2, Generation::M55);
    EXPECT_DOUBLE_EQ(old_both, old_a + old_b);  // M55: the double count
    const double now_both = normal::charged_value(cache, gs, us.id, both, 2);
    const double now_a = normal::charged_value(cache, gs, us.id, only_a, 2);
    const double now_b = normal::charged_value(cache, gs, us.id, only_b, 2);
    EXPECT_LT(now_both, old_both - 0.05);
    EXPECT_LE(now_both, now_a + now_b + 1e-9);
    // A single target with no other enemy unit free to act is M42's value.
    EXPECT_DOUBLE_EQ(now_a, old_a);
    EXPECT_DOUBLE_EQ(now_b, old_b);
}

TEST(M56_Charge, TheRingedTermagantIsChargedForTheMostValue) {
    // M42's ringed-target fixture (M42_ChargeTargets.ChargesBothUnitsWhenTheTargetAloneCannotBeEngaged): a lone
    // Termagant ringed by six of another unit, which can't be charged alone (11.04). With the Squad's strength
    // shared between targets (D-066) the ring alone is worth slightly more than both units (its models fight the
    // ring either way): the bot charges the ring, and the move stands. Charging both still stands, valued less
    // here, and less than M55's summed value.
    using wh40k::test::Charger;
    GameState gs = wh40k::test::charge_fixture(Charger::Intercessors, wh40k::test::ChargeLayout{4.0f, 0.0f, 0});
    const UnitId ring = wh40k::test::add_unit_copy(gs, wh40k::test::kM42Gants);
    Unit& lone = gs.units[wh40k::test::kM42Gants];
    lone.models.resize(1);
    const Position c{in(12.0f), gs.units[0].models.front().position.y + in(7.0f), 0.0f};
    lone.models.front().position = c;
    Unit& others = *gs.find_unit(ring);
    others.models.resize(6);
    const float d = others.models.front().base.diameter_mm;
    for (std::size_t i = 0; i < 6; ++i) {
        const float a = 2.0f * 3.14159265f * static_cast<float>(i) / 6.0f;
        others.models[i].position = Position{c.x + d * std::cos(a), c.y + d * std::sin(a), 0.0f};
    }
    gs.pending = compute_decision(gs);
    ASSERT_TRUE(wh40k::test::declare_with_roll(gs, Charger::Intercessors, 12));
    const UnitId unit = gs.units[wh40k::test::kM42Marines].id;
    const UnitId lone_id = gs.units[wh40k::test::kM42Gants].id;
    Bot bot(level(BotLevel::Normal));
    const BotChoice choice = bot.decide(gs, Side::A);
    ASSERT_EQ(choice.action, (GameAction{action::StartChargeMove{unit, {ring}}})) << choice.reason;
    analysis::OddsCache cache;
    const int turns = normal::turns_left(gs);
    const std::vector<UnitId> both{lone_id, ring};
    const auto plan_both = normal::plan_charge_move(gs, Side::A, unit, both, decision_seed(gs), {});
    const auto plan_ring = normal::plan_charge_move(gs, Side::A, unit, {ring}, decision_seed(gs), {});
    ASSERT_TRUE(plan_both.has_value());
    ASSERT_TRUE(plan_ring.has_value());
    const double v_both = normal::charged_value(cache, plan_both->after, unit, both, turns);
    const double v_ring = normal::charged_value(cache, plan_ring->after, unit, std::vector<UnitId>{ring}, turns);
    EXPECT_LE(v_both, v_ring + 1e-9);
    EXPECT_GT(normal::charged_value(cache, plan_both->after, unit, both, turns, Generation::M55), v_both);
    (void)apply_action(gs, Side::A, choice.action);
    (void)play_while(gs, bot, Side::A,
                     [](const GameState& s) { return std::holds_alternative<decision::MoveModels>(s.pending->kind); });
    EXPECT_EQ(engaged_enemy_units(gs, *gs.find_unit(unit)), std::vector<UnitId>{ring});
}

// ---- Normal: casualty worth against the attacker ----

TEST(M56_Casualties, WorthIsMeasuredAgainstTheAttacker) {
    // D-066: Intercessor Squad 1 is under attack from Termagants 2 while the closest enemy unit (Termagants 1) is
    // made an Intercessor Squad: the models' worth is measured against the attacker (M55: the closest unit).
    GameState gs = side_a_move_units();
    Unit& closest = gs.units[kGants1];
    {
        // Termagants 1 becomes a copy of Intercessor Squad 2 (side B, new ids) in its place.
        const Position at = centroid(closest);
        Unit copy = gs.units[kSquad2];
        copy.id = closest.id;
        copy.side = Side::B;
        const Position c = centroid(copy);
        for (std::size_t i = 0; i < copy.models.size(); ++i) {
            copy.models[i].id = ModelId{5000 + static_cast<std::uint32_t>(i)};
            copy.models[i].position.x += at.x - c.x;
            copy.models[i].position.y += at.y - c.y - in(2.0f);  // a little ahead of where the Termagants stood
        }
        closest = copy;
    }
    const Unit& us = gs.units[kSquad1];
    ASSERT_EQ(normal::closest_enemy(gs, us)->id, closest.id);
    gs.flow.attack = AttackFlow{};
    gs.flow.attack->attacker = gs.units[kGants2].id;
    analysis::OddsCache cache;
    const auto against_attacker = normal::model_worth(cache, us, gs.units[kGants2]);
    const auto against_closest = normal::model_worth(cache, us, closest);
    ASSERT_NE(against_attacker, against_closest);
    EXPECT_EQ(normal::casualty_worth(cache, gs, us), against_attacker);
    EXPECT_EQ(normal::casualty_worth(cache, gs, us, Generation::M55), against_closest);
    EXPECT_EQ(normal::attacking_enemy(gs, us)->id, gs.units[kGants2].id);
    gs.flow.attack.reset();
    EXPECT_EQ(normal::casualty_worth(cache, gs, us), against_closest);  // no attack: the closest enemy unit
}

// ---- seeded games and the regression switch ----

TEST(M56_StrikeGames, SeededGamesAreClean) {
    // D-054 acceptance on the 44"x60" scenario: whole games finish with no rejected action, no reverted move and no
    // fallback, in every pairing of Easy and Normal.
    const GameState scenario = strike_scenario();
    struct Pairing {
        BotLevel a;
        BotLevel b;
        std::uint64_t seed;
    };
    for (const Pairing& p : {Pairing{BotLevel::Easy, BotLevel::Easy, 1}, Pairing{BotLevel::Easy, BotLevel::Easy, 2},
                             Pairing{BotLevel::Normal, BotLevel::Easy, 3}, Pairing{BotLevel::Easy, BotLevel::Normal, 4},
                             Pairing{BotLevel::Normal, BotLevel::Normal, 5}}) {
        const GameRecord g = play_bot_game(scenario, p.seed, level(p.a), level(p.b));
        EXPECT_TRUE(g.finished) << "seed " << p.seed;
        EXPECT_EQ(g.rejected, 0u) << "seed " << p.seed;
        EXPECT_EQ(g.reverted, 0u) << "seed " << p.seed;
        EXPECT_EQ(g.fallbacks, 0u) << "seed " << p.seed;
    }
}

TEST(M56_StrikeGames, AFreshBotGivesThePlayedAnswer) {
    // D-054: every plan is a pure function of the state it starts from, so at every decision of a Strike game a fresh
    // bot (no memo) answers as the playing bot does: Normal against Normal (seed 6, two battle rounds) and Easy against
    // Easy (seed 7, whole game), through the client's own automatic steps.
    struct Case {
        BotLevel level;
        std::uint64_t seed;
        int last_round;
    };
    for (const Case& k : {Case{BotLevel::Normal, 6, 2}, Case{BotLevel::Easy, 7, 5}}) {
        client::GameSession session = client::GameSession::new_game(strike_scenario(), k.seed);
        std::array<Bot, 2> bots{Bot(level(k.level)), Bot(level(k.level))};
        client::AutoSettings autos;
        autos.bot_controlled = {true, true};
        autos.auto_allocate = false;
        autos.skip_empty_steps = true;
        int decisions = 0;
        while (session.pending() && session.history().size() < 3000) {
            (void)client::run_auto_actions(session, autos);
            if (!session.pending() || session.state().flow.battle_round > k.last_round) break;
            const Side side = session.pending()->side;
            const BotChoice played = bots[side_index(side)].decide(session.state(), side);
            const BotChoice fresh = Bot(level(k.level)).decide(session.state(), side);
            ASSERT_EQ(played.action, fresh.action) << "seed " << k.seed << ", decision " << decisions << ": "
                                                   << played.reason << " / " << fresh.reason;
            ASSERT_TRUE(session.submit(played.action).ok()) << played.reason;
            ++decisions;
        }
        EXPECT_GT(decisions, 50);
    }
}

TEST(M56_Regression, TheM55SwitchPlaysAsM55) {
    // D-066: the arena's hidden levels normal-m55 and easy-m55 reproduce M55 (commit d76e51b) exactly. The lines
    // below are a Release build of that commit's `--per-game 1` report for the same games (result, counts, and
    // hashes of the accepted actions, the final state and the decision trace with every reason). The arena matrix
    // (D-066) compares all 200 seeds of each pairing; these four keep it in ctest.
    const GameState scenario = strike_scenario();
    const std::vector<std::pair<std::pair<BotLevel, BotLevel>, std::string>> expected = {
        {{BotLevel::Normal, BotLevel::Normal},
         "game 1: finished 1 winner A round 4 steps 185 surviving 14/0 wounds lost 2/40 coherency 0/0 fight steps 0/0 "
         "fight moves 0/0 rejected 0 reverted 0 fallbacks 0 actions 77419e4eb340174a state 2d4ff6980bf2a2b0 trace "
         "7a62856c18e099e5"},
        {{BotLevel::Normal, BotLevel::Easy},
         "game 3: finished 1 winner A round 2 steps 140 surviving 14/0 wounds lost 3/40 coherency 0/0 fight steps 2/0 "
         "fight moves 2/0 rejected 0 reverted 0 fallbacks 0 actions a66ccf3450bd3a2e state 83405fdf2658b212 trace "
         "3bb184deed5c9c51"},
        {{BotLevel::Easy, BotLevel::Easy},
         "game 1: finished 1 winner A round 5 steps 258 surviving 14/0 wounds lost 3/40 coherency 0/0 fight steps 0/0 "
         "fight moves 0/0 rejected 0 reverted 0 fallbacks 0 actions f3c71837a2617f03 state 5d8dba3f53944c87 trace "
         "302d529261c9bf5d"},
        {{BotLevel::Easy, BotLevel::Normal},
         "game 4: finished 1 winner none round 5 steps 277 surviving 14/10 wounds lost 2/30 coherency 0/0 fight steps "
         "0/0 fight moves 0/0 rejected 0 reverted 0 fallbacks 0 actions ef7d498398016b04 state 50a2080b654d5f34 trace "
         "847882f6eacca278"},
    };
    ASSERT_FALSE(expected.empty());
    for (const auto& [levels, line] : expected) {
        const std::uint64_t seed = std::stoull(line.substr(5, line.find(':') - 5));
        const PerGameReport r = run_per_game(scenario, 1, seed, level(levels.first, Generation::M55),
                                             level(levels.second, Generation::M55));
        EXPECT_EQ(r.text.substr(0, r.text.find('\n')), line);
    }
}

// ---- Normal: the threat of an enemy unit other than the closest one ----

namespace {

// Side B's first MoveUnits in the strike scenario with only Termagants 2 (centred at `mover_at`) and Intercessor
// Squads 1 and 2 (centred at `squad1_at`, `squad2_at`) left on the battlefield. Turn and phase flags cleared.
GameState threat_fixture(Position mover_at, Position squad1_at, Position squad2_at) {
    GameState gs = strike_until([](const GameState& s) {
        return s.pending && s.pending->side == Side::B && std::holds_alternative<decision::MoveUnits>(s.pending->kind);
    });
    for (const std::size_t i : {kSquad3, kGants1, kGants3, kGants4}) gs.units[i].models.clear();
    const auto place = [&](std::size_t i, Position to) {
        const Position c = centroid(gs.units[i]);
        shift(gs.units[i], core::mm_to_inches(to.x - c.x), core::mm_to_inches(to.y - c.y));
    };
    place(kGants2, mover_at);
    place(kSquad1, squad1_at);
    place(kSquad2, squad2_at);
    for (Unit& u : gs.units) {
        u.turn_flags = {};
        u.phase_flags = {};
    }
    gs.pending = compute_decision(gs);
    return gs;
}

MoveResult first_move_b(GameState gs, BotConfig config) {
    Bot bot(config);
    const BotChoice c = bot.decide(gs, Side::B);
    MoveResult out;
    const auto* start = std::get_if<action::StartMove>(&c.action);
    if (start == nullptr) return out;
    out.unit = start->unit;
    out.before = centroid(*gs.find_unit(start->unit));
    out.reason = c.reason;
    (void)apply_action(gs, Side::B, c.action);
    (void)play_while(gs, bot, Side::B, [](const GameState& s) {
        return std::holds_alternative<decision::MoveModels>(s.pending->kind);
    });
    out.after = centroid(*gs.find_unit(start->unit));
    return out;
}

}  // namespace

TEST(M56_Normal, AThreatFromAnotherEnemyUnitChangesTheMove) {
    // D-066: Termagants 2 (side B's only unit left) stands about 7" from Intercessor Squad 2 (the closest enemy unit,
    // east) and about 14" from Intercessor Squad 1 (south-west), which has no other target. M55 weighed only Squad 2
    // and ended its move within Squad 1's reach (about 17.6" from its centre); weighing Squad 1's shooting and
    // charge threat too, Normal ends it at least 2" further from Squad 1.
    const GameState gs = threat_fixture(at(30, 34), at(24, 18), at(44, 30));
    const Unit& mover = gs.units[kGants2];
    ASSERT_EQ(normal::closest_enemy(gs, mover)->id, gs.units[kSquad2].id);
    const Position other = centroid(gs.units[kSquad1]);
    const MoveResult now = first_move_b(gs, level(BotLevel::Normal));
    const MoveResult old = first_move_b(gs, level(BotLevel::Normal, Generation::M55));
    ASSERT_EQ(now.unit, mover.id);
    ASSERT_EQ(old.unit, mover.id);
    EXPECT_GT(centre_distance(now.after, other), centre_distance(old.after, other) + in(2.0f))
        << now.reason << "\n" << old.reason;
    // The score itself: at M55's end the new context counts Squad 1's reply (a loss), the M55 one doesn't see it.
    analysis::OddsCache cache;
    const GameState& origin = gs;
    normal::MoveScoreContext ctx(cache, origin, mover.id);
    normal::MoveScoreContext ctx55(cache, origin, mover.id, normal::MoveWeights{}, Generation::M55);
    EXPECT_EQ(ctx.other_count(), 1u);
    EXPECT_EQ(ctx55.other_count(), 0u);
    std::vector<Position> ends;
    const float dx = old.after.x - old.before.x;
    const float dy = old.after.y - old.before.y;
    for (const Model& m : mover.models) ends.push_back(Position{m.position.x + dx, m.position.y + dy, 0.0f});
    EXPECT_GT(ctx.other_threat(ends, std::nullopt).unengaged, 0.0);
}
